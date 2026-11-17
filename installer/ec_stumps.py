"""Extended Cut installer part "stumps" (0.1.5): the restored gib stumps, built from the player's OWN mini.vpp.

The game's five stump models (mini.vpp stub-*.rxm + .ceg) ship turned INWARD - only a flat cone shows. The plugin's
Gore option "Gib stumps" (Original / Restored / Random variation) picks a stump file per gib by name. This part ADDS
these files next to the untouched originals (nothing of the game's own files is changed or shipped):
  <stub>_u   restored: the developers' stump turned outward (mirrored along its long axis), single-sided
  <stub>_b   restored + bloodier texture (own texture name *stmb.tga, blood blended into the texture blocks)
  hips _s    slim legs (x / y 0.72, z 0.85) + bloodier texture
  arms _t    tucked arms: shorter, pushed back into the sleeve (meat + bone show)
  hips _c    cone + piece: the original cone kept, its own flesh disc + bone turned out onto the cone's tip
Pure Python (no Pillow): geometry = point positions / normals only; the bloodier texture is made in the compressed
(DXT3) data in pure Python: decoded, blood blended per pixel (pale bone keeps a light wash), re-encoded.
The plugin only uses these files when they are present (else "Original").
"""
import hashlib
import math
import os
import random
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
sys.path.insert(0, HERE)
import vpp_pack  # noqa: E402
from rxm_tool import Model  # noqa: E402
import stump_conepiece  # noqa: E402

STUBS = ['stub-neck', 'stub-l_shldr', 'stub-r_shoulder', 'stub-l_hip', 'stub-r_hip']
TEX = {'stub-neck': 'nckstmp', 'stub-l_shldr': 'uprarmstmp', 'stub-r_shoulder': 'uprarmstmp',
       'stub-l_hip': 'uprlegstmp', 'stub-r_hip': 'uprlegstmp'}
RETAIL = {   # sha1 of the retail PC files (rxm, ceg) - a stump that is not retail is left alone
    'stub-neck': ('67833731d8acb71f1ffbcf0d648df2f236aa2e23', 'caaed60e18dc2121d3bd9e8d2c41d419c1e632b6'),
    'stub-l_shldr': ('9c457d4d91100c2ea7522712221c7d50e73b9450', 'ebd93e7a1715054f3029244e04124dac93a13796'),
    'stub-r_shoulder': ('0364cb3a098ac9e59351c1d8a2a77db66b497858', 'ebd93e7a1715054f3029244e04124dac93a13796'),
    'stub-l_hip': ('0c695dfc265a86b7638bbcaa6a7cc22a2469882d', 'eab9e7d4f34eac412341c1bbb24ee3ec223e3f6f'),
    'stub-r_hip': ('a13e6ddb9d5371fa1713d0364808122fae4d796d', 'eab9e7d4f34eac412341c1bbb24ee3ec223e3f6f'),
}
SLIM = (0.72, 0.72, 0.85)
TUCK_ARM = (0.75, 0.03, 0.92)            # length kept, pushed in (m), width - user-approved Nov 16
CONE_V2 = (-0.01, 0.8, 0.9, 0.5)         # stump_conepiece.build args - user-picked Nov 16
BLOOD = (95, 6, 8)
MARK = 'stub-neck_u.rxm'                 # present = installed


def _get(a, name):
    import zlib
    f = a['files'][vpp_pack.index_of(a, name)]
    return zlib.decompress(f['stored']) if a['compressed'] else bytes(f['stored'][:f['size']])


def _has(a, name):
    return any(f['raw'].split(b'\0')[0].decode('latin-1').lower() == name.lower() for f in a['files'])


def _fix_bounds(m):
    if struct.unpack_from('<I', m.d, 0x1c8)[0] != 1:
        raise ValueError('stump model bounds not where expected')
    vs = m.vertices()
    lo = [min(v[k] for v in vs) for k in range(3)]
    hi = [max(v[k] for v in vs) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = max(math.dist(c, v[:3]) for v in vs)
    struct.pack_into('<10f', m.d, 0x1cc, *c, r, *lo, *hi)


def _flipped(rxm, tmp, name, scale=(1, 1, 1), bloody_tex=None, tuck=None):
    """same as tools\\5 Cut hunt\\stumps_restored.py flipped() (single-sided)"""
    p = os.path.join(tmp, name + '_in.rxm'); open(p, 'wb').write(rxm)
    m = Model(p)
    vs = m.vertices()
    ext = [max(v[k] for v in vs) - min(v[k] for v in vs) for k in range(3)]
    ax = ext.index(max(ext))
    out_dir = -1 if sum(v[ax] for v in vs) > 0 else 1
    if tuck:
        scale = [tuck[2] if k != ax else tuck[0] for k in range(3)]
    for i, v in enumerate(vs):
        q = [v[0] * scale[0], v[1] * scale[1], v[2] * scale[2]]
        n = list(v[3:6])
        q[ax] = -q[ax]; n[ax] = -n[ax]
        if tuck:
            q[ax] -= out_dir * tuck[1]
        m.set_vertex(i, [*q, *n, v[6], v[7]])
    _fix_bounds(m)
    if bloody_tex:
        k = bytes(m.d).lower().find((bloody_tex + '.tga').encode())
        if k < 0:
            raise ValueError('texture name not in %s' % name)
        m.d[k + len(bloody_tex) - 1] = ord('b')                    # ...Stmp.tga -> ...Stmb.tga, case kept
    return bytes(m.d)


def _565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def _to565(r, g, b):
    r, g, b = (max(0, min(255, int(round(x)))) for x in (r, g, b))
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def _dxt_block_colours(c0, c1):
    a, b = _565(c0), _565(c1)
    return [a, b, tuple((2 * a[k] + b[k]) / 3 for k in range(3)), tuple((a[k] + 2 * b[k]) / 3 for k in range(3))]


def _decode_dxt3(b, off, w, h):
    """-> rows of (r, g, b) and the untouched alpha halves per block"""
    px = [[None] * w for _ in range(h)]
    alpha = {}
    bw = w // 4
    for by in range(h // 4):
        for bx in range(bw):
            q = off + 16 * (by * bw + bx)
            alpha[(bx, by)] = bytes(b[q:q + 8])
            c0, c1, idx = struct.unpack_from('<HHI', b, q + 8)
            pal = _dxt_block_colours(c0, c1)
            for i in range(16):
                px[by * 4 + i // 4][bx * 4 + i % 4] = pal[(idx >> (2 * i)) & 3]
    return px, alpha


def _encode_block(pix):
    """16 (r, g, b) -> DXT colour half (c0 >= c1, 4-colour mode): endpoints along the block's main colour axis"""
    mean = [sum(p[k] for p in pix) / 16 for k in range(3)]
    ax = [max(p[k] for p in pix) - min(p[k] for p in pix) for k in range(3)]
    ln = math.sqrt(sum(x * x for x in ax)) or 1.0
    # signed axis: follow how the strongest channel moves with the others
    big = ax.index(max(ax))
    sgn = []
    for k in range(3):
        cov = sum((p[k] - mean[k]) * (p[big] - mean[big]) for p in pix)
        sgn.append(ax[k] if cov >= 0 else -ax[k])
    proj = [sum((p[k] - mean[k]) * sgn[k] for k in range(3)) / ln for p in pix]
    lo, hi = pix[proj.index(min(proj))], pix[proj.index(max(proj))]
    c0, c1 = _to565(*hi), _to565(*lo)
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        pal = [_565(c0)] * 4
    else:
        pal = _dxt_block_colours(c0, c1)
    idx = 0
    for i, p in enumerate(pix):
        best = min(range(4), key=lambda j: sum((p[k] - pal[j][k]) ** 2 for k in range(3)))
        idx |= best << (2 * i)
    return struct.pack('<HHI', c0, c1, idx)


def _bloody_bank(ceg, tex, seed):
    """the stub's texture bank with its texture made bloodier and renamed *stmb.tga - the same per-pixel blend as
    the Deck-tested version (tools\\5 Cut hunt\\stumps_restored.py blood_texture: blotchy wet blood over the flesh,
    pale bone / fat keeps a light wash), done on the DXT3 data in pure Python; the alpha half stays as it was"""
    b = bytearray(ceg)
    n = struct.unpack_from('<I', b, 16)[0]
    for i in range(n):
        o = 32 + 48 * i
        nm = bytes(b[o + 16:o + 48]).split(b'\0')[0].decode('latin-1')
        if nm.lower() != tex + '.tga':
            continue
        off, w, h = struct.unpack_from('<IHH', b, o)
        if b[o + 8] != 0x0E:
            raise ValueError('stump texture is not DXT3')
        px, alpha = _decode_dxt3(b, off, w, h)
        rnd = random.Random(seed)
        noise = [[rnd.random() for _ in range(w)] for _ in range(h)]
        out = [[None] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                s = sum(noise[(y + dy) % h][(x + dx) % w] for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9
                r, g, bb = px[y][x]
                lum = (r + g + bb) / 3; sat = max(r, g, bb) - min(r, g, bb)
                bone = lum > 115 and sat < 95
                a = 0.12 if bone else (0.25 + 0.6 * s * s if s > 0.35 else 0.18)
                kf = 0.8 + 0.4 * s
                out[y][x] = tuple(min(255.0, (c * (1 - a) + bc * a) * kf) for c, bc in zip((r, g, bb), BLOOD))
        bw = w // 4
        for by in range(h // 4):
            for bx in range(bw):
                q = off + 16 * (by * bw + bx)
                blk = [out[by * 4 + j // 4][bx * 4 + j % 4] for j in range(16)]
                b[q:q + 8] = alpha[(bx, by)]
                b[q + 8:q + 16] = _encode_block(blk)
        newname = (tex[:-1] + 'b.tga').encode()
        b[o + 16:o + 16 + len(newname)] = newname
        return bytes(b)
    raise ValueError('texture %s not in its bank' % tex)


def state(vp):
    """'installed' / 'ready' / text why not"""
    try:
        a = vpp_pack.read(vp)
    except Exception as ex:
        return 'mini.vpp could not be read (%s)' % ex
    if _has(a, MARK):
        return 'installed'
    for s in STUBS:
        if not (_has(a, s + '.rxm') and _has(a, s + '.ceg')):
            return '%s is missing from mini.vpp' % s
        r, c = RETAIL[s]
        if hashlib.sha1(_get(a, s + '.rxm')).hexdigest() != r or hashlib.sha1(_get(a, s + '.ceg')).hexdigest() != c:
            return '%s in mini.vpp is not the game\'s own (changed by another mod?)' % s
    return 'ready'


def build(vp):
    """the new mini.vpp bytes (the player's file + the stump files) and a short list of what was added"""
    a = vpp_pack.read(vp)
    tmp = tempfile.mkdtemp()
    added = []
    for i, s in enumerate(STUBS):
        rxm, ceg = _get(a, s + '.rxm'), _get(a, s + '.ceg')
        t = TEX[s]
        bank = _bloody_bank(ceg, t, i + 1)
        files = {'_u': (_flipped(rxm, tmp, s + '_u'), ceg),
                 '_b': (_flipped(rxm, tmp, s + '_b', bloody_tex=t), bank)}
        if 'hip' in s:
            files['_s'] = (_flipped(rxm, tmp, s + '_s', SLIM, bloody_tex=t), bank)
            pi = os.path.join(tmp, s + '_cin.rxm'); po = os.path.join(tmp, s + '_c.rxm'); open(pi, 'wb').write(rxm)
            stump_conepiece.build(pi, po, *CONE_V2)
            files['_c'] = (open(po, 'rb').read(), ceg)
        else:
            if s != 'stub-neck':
                files['_t'] = (_flipped(rxm, tmp, s + '_t', tuck=TUCK_ARM), ceg)
        for suf, (r_, c_) in files.items():
            vpp_pack.put(a, s + suf + '.rxm', r_); vpp_pack.put(a, s + suf + '.ceg', c_)
            added.append(s + suf)
    return vpp_pack.build(a), added


if __name__ == '__main__':
    st = state(sys.argv[1]); print(st)
    if st == 'ready' and len(sys.argv) > 2:
        data, added = build(sys.argv[1]); open(sys.argv[2], 'wb').write(data); print(len(added), 'stump files ->', sys.argv[2])
