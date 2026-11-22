"""The Punisher (PC, 2005) .rfa skeletal animation reader / writer (VMVF version 16, Red Faction II era).

Format decoded from the data + pun.exe (decoders FUN_005311c0 rotation, FUN_00530dd0 position,
FUN_00532f20 bone position, FUN_00532cf0 root motion, table init FUN_005328d0). See
cut_content\\scans\\overnight2\\rfa_format.md.

  python rfa_tool.py dump  <file.rfa> [out.json]       parsed structure + decoded keys (JSON)
  python rfa_tool.py text  <file.rfa>                  short text summary
  python rfa_tool.py selftest <file.rfa | folder ...>  parse -> rebuild, must be byte-identical
  python rfa_tool.py frames <file.rfa> <skeleton.rxc> [bone]   per-frame model-space position of a bone (FK)
  python rfa_tool.py offset <in.rfa> <out.rfa> <bone> dx dy dz   add a constant (m) to a bone position track

Layout (little endian):
  header 0x58: 'VMVF', u32 16, f pos_reduction, f rot_reduction, i start, i end (1/4800 s; 160 per frame),
    i n_bones, i root_rot_keys, i root_pos_keys, i ramp_in, i ramp_out, quat total_rot (x,y,z,w),
    vec3 total_trans, u32 flags (bit1 = 16-bit key times), i root_rot_bases, u32 root_rot_ofs,
    u32 root_pos_ofs, then u32 bone_ofs[n_bones] (0xffffffff = bone not animated).
  bone: s16 n_rot, s16 n_pos, s16 n_bases, u8 flags, u8 weight (100),
    rot:  n_bases * 4 s8 base quats, n_rot * 4 u8 key bytes, n_rot times (u8, or u16 if header flag bit1), pad 2
    pos:  u16 n_seg, n_seg * (u16 count, s16 x, y, z), n_pos * 3 s8, pad 2, n_pos times, pad 2
    flags bit0: 16 more bytes after (3 floats = bone scale + u32 0). Other flag bits: kept as raw bytes.
  times are in 1/60 s (= 80 / 4800 s, half a frame).
  rotation component c of key k: s16(s8 base[b][c] * 128 + T[key[k][c]]) / 16383, b = k * n_bases // n_rot,
    T[127 + x] = int(46x - 46x*x/150 + 0.0032154340x^3), T[127 - x] = -T[127 + x].
  position of key k: seg_base(s16) / 64 + delta(s8) / 1000 m, added to the skeleton's rest offset.
  root motion: root rot track at root_rot_ofs (count, bases from the header), root pos track at root_pos_ofs.
"""
import json, os, struct, sys
import numpy as np

HDR = struct.Struct('<4sIffiiiiiii4f3fIiII')


def _table():
    t = [0] * 256
    for i in range(127, 256):
        x = float(i - 127)
        a = np.float32(46.0) * np.float32(x)
        v = a - a * np.float32(x) * np.float32(0.006666666828095913) + np.float32(x) ** 3 * np.float32(0.003215434029698372)
        t[i] = int(v)
    for j in range(128):
        t[127 - j] = -t[127 + j]
    return t


T = _table()
# inverse lookup: for each high byte, best key byte for a wanted s16 value
_TS = np.array(T)


def a2(o):
    return (o + 1) & ~1


def dec_quat(base, key):
    return [(((base[c] * 128 + T[key[c]] + 32768) & 0xffff) - 32768) / 16383.0 for c in range(4)]


def enc_quat(q, base=None):
    """quaternion (x,y,z,w floats) -> (base s8 x4, key u8 x4). If base is None the best per-component base is used."""
    q = np.asarray(q, float)
    if np.dot(q, q) > 0:
        q = q / np.linalg.norm(q)
    tgt = np.round(q * 16383).astype(int)
    if base is None:
        base = [int(np.clip(np.round(v / 128), -127, 127)) for v in tgt]
    key = []
    for c in range(4):
        k = int(np.argmin(np.abs(base[c] * 128 + _TS - tgt[c])))
        key.append(k)
    return list(base), key


def enc_track_quats(qs, n_bases):
    """encode a list of quaternions with n_bases shared bases (key k uses base k*n_bases//n)."""
    n = len(qs)
    qs = [np.asarray(q, float) / np.linalg.norm(q) for q in qs]
    bases, keys = [], []
    cand = np.arange(-127, 128)
    grid = cand[:, None] * 128 + _TS[None, :]           # (255 bases, 256 key bytes) -> s16 value
    for b in range(n_bases):
        ks = [k for k in range(n) if k * n_bases // n == b]
        tg = np.round(np.array([qs[k] for k in ks]) * 16383)  # (keys, 4)
        base = []
        for c in range(4):
            err = np.abs(grid[:, :, None] - tg[None, None, :, c]).min(1).max(1)   # per base: worst key error
            base.append(int(cand[int(np.argmin(err))]))
        bases.append(base)
    for k in range(n):
        keys.append(enc_quat(qs[k], bases[k * n_bases // n])[1])
    return bases, keys


def enc_err(qs, n_bases):
    bases, keys = enc_track_quats(qs, n_bases)
    n = len(qs)
    e = 0
    for k in range(n):
        d = dec_quat(bases[k * n_bases // n], keys[k])
        e = max(e, float(np.max(np.abs(np.array(d) - qs[k] / np.linalg.norm(qs[k])))))
    return e


# ---------------------------------------------------------------- parse
def parse_rot(d, o, n, nb, tw):
    bases = [list(struct.unpack_from('<4b', d, o + 4 * i)) for i in range(nb)]
    o += 4 * nb
    keys = [list(d[o + 4 * i:o + 4 * i + 4]) for i in range(n)]
    o += 4 * n
    times = list(struct.unpack_from('<%d%s' % (n, 'H' if tw == 2 else 'B'), d, o))
    o += tw * n
    return {'bases': bases, 'keys': keys, 'times': times}, a2(o)


def parse_pos(d, o, n, tw, final_pad=True):
    ns = struct.unpack_from('<H', d, o)[0]
    segs = [list(struct.unpack_from('<H3h', d, o + 2 + 8 * i)) for i in range(ns)]
    o += 2 + 8 * ns
    deltas = [list(struct.unpack_from('<3b', d, o + 3 * i)) for i in range(n)]
    o = a2(o + 3 * n)
    times = list(struct.unpack_from('<%d%s' % (n, 'H' if tw == 2 else 'B'), d, o))
    o = o + tw * n
    return {'segs': segs, 'deltas': deltas, 'times': times}, (a2(o) if final_pad else o)


def parse(d):
    h = HDR.unpack_from(d, 0)
    if h[0] != b'VMVF' or h[1] != 16:
        raise ValueError('not a VMVF v16 file')
    hd = dict(pos_red=h[2], rot_red=h[3], start=h[4], end=h[5], n_bones=h[6], root_rot_n=h[7], root_pos_n=h[8],
              ramp_in=h[9], ramp_out=h[10], total_rot=list(h[11:15]), total_trans=list(h[15:18]), flags=h[18],
              root_rot_bases=h[19], root_rot_ofs=h[20], root_pos_ofs=h[21])
    tw = 2 if hd['flags'] & 2 else 1
    nb = hd['n_bones']
    offs = list(struct.unpack_from('<%dI' % nb, d, 0x58))
    a = {'header': hd, 'order': [], 'bones': [None] * nb, 'raw_header': d[:0x58].hex()}
    # floats are kept as raw bits for exact rebuild
    a['header_f'] = d[8:16].hex() + '|' + d[0x2c:0x48].hex()
    pos = 0x58 + 4 * nb
    gaps = {}
    order = sorted(range(nb), key=lambda i: offs[i])
    order = [i for i in order if offs[i] != 0xffffffff]
    a['order'] = order
    ends = sorted([offs[i] for i in order] + [hd['root_rot_ofs'], hd['root_pos_ofs']])
    for i in order:
        o = offs[i]
        if o != pos:
            gaps[str(i)] = d[pos:o].hex()
        nr, npz, nbs, fl, w = struct.unpack_from('<hhhBB', d, o)
        b = {'n_rot': nr, 'n_pos': npz, 'n_bases': nbs, 'flags': fl, 'weight': w}
        q = o + 8
        if nr:
            b['rot'], q = parse_rot(d, q, nr, nbs, tw)
        else:
            q = a2(q + 4 * nbs) if nbs else q
            if nbs:
                b['rot_bases_only'] = [list(struct.unpack_from('<4b', d, o + 8 + 4 * k)) for k in range(nbs)]
        if npz:
            b['pos'], q = parse_pos(d, q, npz, tw)
        nxt = min([e for e in ends if e > o] + [len(d)])
        if fl & 1 and q + 16 <= nxt:
            b['scale'] = list(struct.unpack_from('<3f', d, q))
            b['scale_raw'] = d[q:q + 16].hex()
            q += 16
        b['_end'] = q
        a['bones'][i] = b
        pos = q
    a['gaps'] = gaps
    # root tracks
    tail = {}
    if hd['root_rot_n'] > 0:
        o = hd['root_rot_ofs']
        if o != pos:
            tail['gap_rot'] = d[pos:o].hex()
        tail['rot'], pos = parse_rot(d, o, hd['root_rot_n'], hd['root_rot_bases'], tw)
    if hd['root_pos_n'] > 0:
        o = hd['root_pos_ofs']
        if o != pos:
            tail['gap_pos'] = d[pos:o].hex()
        tail['pos'], pos = parse_pos(d, o, hd['root_pos_n'], tw, final_pad=False)
    tail['trailer'] = d[pos:].hex()
    a['root'] = tail
    a['bone_align'] = 4 if all(offs[i] % 4 == 0 for i in order) else 2
    return a


# ---------------------------------------------------------------- build
def _pad_to(out, al):
    while len(out) % al:
        out.append(0)


def build_rot(r, tw):
    out = bytearray()
    for b in r['bases']:
        out += struct.pack('<4b', *b)
    for k in r['keys']:
        out += bytes(k)
    out += struct.pack('<%d%s' % (len(r['times']), 'H' if tw == 2 else 'B'), *r['times'])
    return out


def build_pos(p, tw):
    out = bytearray(struct.pack('<H', len(p['segs'])))
    for s in p['segs']:
        out += struct.pack('<H3h', *s)
    for dl in p['deltas']:
        out += struct.pack('<3b', *dl)
    return out, p['times']


def build(a, keep_gaps=True):
    hd = a['header']
    tw = 2 if hd['flags'] & 2 else 1
    nb = len(a['bones'])
    hf = a.get('header_f')
    out = bytearray(0x58 + 4 * nb)
    offs = [0xffffffff] * nb
    al = a.get('bone_align', 4)
    for i in a['order']:
        g = a['gaps'].get(str(i)) if keep_gaps else None
        if g is not None:
            out += bytes.fromhex(g)
        else:
            _pad_to(out, al)
        b = a['bones'][i]
        offs[i] = len(out)
        out += struct.pack('<hhhBB', b['n_rot'], b['n_pos'], b['n_bases'], b['flags'], b['weight'])
        if b['n_rot']:
            out += build_rot(b['rot'], tw)
            _pad_to(out, 2)
        elif b.get('rot_bases_only'):
            for x in b['rot_bases_only']:
                out += struct.pack('<4b', *x)
            _pad_to(out, 2)
        if b['n_pos']:
            body, times = build_pos(b['pos'], tw)
            out += body
            _pad_to(out, 2)
            out += struct.pack('<%d%s' % (len(times), 'H' if tw == 2 else 'B'), *times)
            _pad_to(out, 2)
        if 'scale_raw' in b:
            out += bytes.fromhex(b['scale_raw'])
        elif 'scale' in b:
            out += struct.pack('<3fI', *b['scale'], 0)
    rt = a['root']
    rro = rpo = None
    if 'rot' in rt:
        if 'gap_rot' in rt and keep_gaps:
            out += bytes.fromhex(rt['gap_rot'])
        else:
            _pad_to(out, al)
        rro = len(out)
        out += build_rot(rt['rot'], tw)
        _pad_to(out, 2)
    if 'pos' in rt:
        if 'gap_pos' in rt and keep_gaps:
            out += bytes.fromhex(rt['gap_pos'])
        rpo = len(out)
        body, times = build_pos(rt['pos'], tw)
        out += body
        _pad_to(out, 2)
        out += struct.pack('<%d%s' % (len(times), 'H' if tw == 2 else 'B'), *times)
    if rpo is None:
        rpo = len(out)
    if rro is None:
        rro = rpo
    if keep_gaps:
        out += bytes.fromhex(rt.get('trailer', ''))
    rr = hd['root_rot_ofs'] if (keep_gaps and 'rot' not in rt) else rro
    rp = hd['root_pos_ofs'] if (keep_gaps and 'pos' not in rt) else rpo
    struct.pack_into('<4sIffiiiiiii4f3fIiII', out, 0, b'VMVF', 16, hd['pos_red'], hd['rot_red'], hd['start'],
                     hd['end'], nb, len(rt['rot']['times']) if 'rot' in rt else hd['root_rot_n'],
                     len(rt['pos']['times']) if 'pos' in rt else hd['root_pos_n'], hd['ramp_in'], hd['ramp_out'],
                     *hd['total_rot'], *hd['total_trans'], hd['flags'],
                     len(rt['rot']['bases']) if 'rot' in rt else hd['root_rot_bases'], rr, rp)
    if hf and keep_gaps:
        f1, f2 = hf.split('|')
        out[8:16] = bytes.fromhex(f1)
        out[0x2c:0x48] = bytes.fromhex(f2)
    struct.pack_into('<%dI' % nb, out, 0x58, *offs)
    return bytes(out)


# ---------------------------------------------------------------- decode helpers
def rot_track_quats(r):
    n, nb = len(r['keys']), len(r['bases'])
    return [dec_quat(r['bases'][k * nb // n], r['keys'][k]) for k in range(n)]


def pos_track_values(p):
    out, k = [], 0
    for cnt, x, y, z in p['segs']:
        for _ in range(cnt):
            dl = p['deltas'][k]
            out.append([x / 64 + dl[0] / 1000, y / 64 + dl[1] / 1000, z / 64 + dl[2] / 1000])
            k += 1
    return out


def enc_pos_track(vals, times):
    """values (metres) -> one segment per run of keys that fit in s8 mm deltas around an s16/64 base."""
    segs, deltas = [], []
    i = 0
    while i < len(vals):
        j = i
        best = None
        while j < len(vals):
            chunk = np.array(vals[i:j + 1])
            base = np.round((chunk.min(0) + chunk.max(0)) / 2 * 64).astype(int)
            dl = np.round((chunk - base / 64) * 1000).astype(int)
            if np.abs(dl).max() > 127:
                break
            best = (j, base, dl)
            j += 1
        j, base, dl = best
        segs.append([j - i + 1] + [int(v) for v in base])
        deltas += [[int(v) for v in r] for r in dl]
        i = j + 1
    return {'segs': segs, 'deltas': deltas, 'times': list(times)}


def slerp(a, b, t):
    a, b = np.asarray(a, float), np.asarray(b, float)
    if np.dot(a, b) < 0:
        b = -b
    dt = np.clip(np.dot(a, b), -1, 1)
    if dt > 0.9995:
        r = a + t * (b - a)
        return r / np.linalg.norm(r)
    th = np.arccos(dt)
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


def sample(times, vals, t, quat):
    """t in 1/60 s units (key-time units)"""
    if not vals:
        return None
    if t <= times[0] or len(vals) == 1:
        return np.asarray(vals[0], float)
    if t >= times[-1]:
        return np.asarray(vals[-1], float)
    for k in range(len(times) - 1):
        if times[k] <= t <= times[k + 1]:
            f = (t - times[k]) / max(1, times[k + 1] - times[k])
            if quat:
                return slerp(vals[k], vals[k + 1], f)
            return (1 - f) * np.asarray(vals[k], float) + f * np.asarray(vals[k + 1], float)


def bone_pose(a, i, t):
    """(quat or None, pos offset or None) of bone i at time t (1/60 s)"""
    b = a['bones'][i] if i < len(a['bones']) else None
    if b is None:
        return None, None
    q = p = None
    if b['n_rot']:
        q = sample(b['rot']['times'], rot_track_quats(b['rot']), t, True)
    if b['n_pos']:
        p = sample(b['pos']['times'], pos_track_values(b['pos']), t, False)
    return q, p


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


# ---------------------------------------------------------------- skeleton (.rxc)
def load_skeleton(path):
    """bone records in a character .rxc: 136 bytes = 2 x 3x4 float matrices, rest offset (3 f, parent space),
    name[24], s16 parent, s16 index. Found by the first '<x>-root' name."""
    import re
    d = open(path, 'rb').read()
    m = re.search(rb'[a-z]+-root\0', d)
    o = m.start()
    bones = []
    while o + 28 <= len(d):
        nm = d[o:o + 24].split(b'\0')[0]
        if not nm or not re.fullmatch(rb'[\x20-\x7e]+', nm):
            break
        par, idx = struct.unpack_from('<hh', d, o + 24)
        if idx != len(bones):
            break
        bones.append({'name': nm.decode(), 'parent': par, 'rest': list(struct.unpack_from('<3f', d, o - 12))})
        o += 0x88
    return bones


def fk(a, skel, t):
    """world (model space) positions of every bone at time t. Assumes key rotations are full local rotations
    and positions = rest offset + pos track (as pun.exe FUN_00532f20 does)."""
    n = len(skel)
    R = [None] * n
    P = [None] * n
    for i in range(n):
        q, p = bone_pose(a, i, t) if i < len(a['bones']) else (None, None)
        lr = qmat(q) if q is not None else np.eye(3)
        lp = np.array(skel[i]['rest']) + (p if p is not None else 0)
        par = skel[i]['parent']
        if par < 0:
            R[i], P[i] = lr, lp
        else:
            R[i] = R[par] @ lr
            P[i] = P[par] + R[par] @ lp
    return P, R


# ---------------------------------------------------------------- cli
def selftest(paths):
    import glob
    files = []
    for p in paths:
        files += sorted(glob.glob(os.path.join(p, '*.rfa'))) if os.path.isdir(p) else [p]
    ok = bad = err = 0
    for f in files:
        d = open(f, 'rb').read()
        try:
            a = parse(d)
            r = build(json.loads(json.dumps(a)))
        except Exception as e:  # noqa
            err += 1
            print('ERR', f, e)
            continue
        if r == d:
            ok += 1
        else:
            bad += 1
            k = next((i for i in range(min(len(r), len(d))) if r[i] != d[i]), min(len(r), len(d)))
            print('DIFF', f, 'at', hex(k), len(d), len(r))
    print('identical %d / %d  (diff %d, error %d)' % (ok, len(files), bad, err))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd, f = sys.argv[1], sys.argv[2]
    if cmd == 'selftest':
        selftest(sys.argv[2:])
        return
    if cmd == 'offset':   # offset <in> <out> <bone> dx dy dz : add a constant (metres) to a bone's position track
        a = parse(open(f, 'rb').read())
        bi = int(sys.argv[4])
        dv = np.array([float(x) for x in sys.argv[5:8]])
        b = a['bones'][bi]
        if b['n_pos']:
            vals = [list(np.array(v) + dv) for v in pos_track_values(b['pos'])]
            b['pos'] = enc_pos_track(vals, b['pos']['times'])
        else:
            b['pos'] = enc_pos_track([list(dv), list(dv)], [0, a['header']['end'] // 80])
            b['n_pos'] = 2
        open(sys.argv[3], 'wb').write(build(a, keep_gaps=False))
        print('written', sys.argv[3])
        return
    a = parse(open(f, 'rb').read())
    if cmd == 'dump':
        for b in a['bones']:
            if b and 'rot' in b:
                b['rot']['quats'] = [[round(v, 5) for v in q] for q in rot_track_quats(b['rot'])]
            if b and 'pos' in b:
                b['pos']['metres'] = [[round(v, 4) for v in p] for p in pos_track_values(b['pos'])]
        if 'pos' in a['root']:
            a['root']['pos']['metres'] = pos_track_values(a['root']['pos'])
        if 'rot' in a['root']:
            a['root']['rot']['quats'] = rot_track_quats(a['root']['rot'])
        s = json.dumps(a, indent=1)
        if len(sys.argv) > 3:
            open(sys.argv[3], 'w').write(s)
        else:
            print(s)
    elif cmd == 'text':
        h = a['header']
        print('frames %.1f (end %d), bones %d, flags %d, root rot keys %d, root pos keys %d' %
              (h['end'] / 160, h['end'], h['n_bones'], h['flags'], h['root_rot_n'], h['root_pos_n']))
        for i, b in enumerate(a['bones']):
            if b:
                print(' bone %2d rot %3d (bases %2d) pos %3d flags %d' % (i, b['n_rot'], b['n_bases'], b['n_pos'], b['flags']))
    elif cmd == 'frames':
        skel = load_skeleton(sys.argv[3])
        bi = sys.argv[4] if len(sys.argv) > 4 else None
        names = [s['name'] for s in skel]
        bi = names.index(bi) if bi in names else (int(bi) if bi else names.index(next(n for n in names if n.endswith('-head'))))
        h = a['header']
        for fr in range(h['end'] // 160 + 1):
            P, _ = fk(a, skel, fr * 2)
            print(fr, ' '.join('%.3f' % v for v in P[bi]))


if __name__ == '__main__':
    main()
