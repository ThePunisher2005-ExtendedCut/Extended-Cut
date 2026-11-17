"""Extended Cut release - patches for files inside a .vpp archive (misc.vpp font, mini.vpp pictures).

A patch only carries OUR data:
  - add:   whole textures that are our own art, stored in a small texture bank next to the patch (<patch>.ceg);
  - copy:  a texture of the player's OWN bank stored again under a new name (e.g. the PC stick picture as the
           "Xbox" style picture, the first frame of the health bar as the dev HUD's slaughter fill);
  - runs:  byte runs inside a texture or a file (our icon pixels in the font pages, glyph table numbers). Each run
           carries the sha1 of the bytes it replaces, so it is only written over exactly what we expect (or skipped
           when it already holds our bytes).
Nothing is changed unless every step fits.

  python vpppatch.py make  <base.vpp> <target.vpp> <rules.json> <out.json>
  python vpppatch.py check <in.vpp> <patch.json>
  python vpppatch.py apply <in.vpp> <patch.json> <out.vpp>

rules.json: {"<entry>": {"kind": "bank", "add": [names], "copy": [{"dst", "src", "frame", "fmt"}],
                         "runs": [texture names]} | {"kind": "bytes"}}
"""
import base64
import hashlib
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.append(os.path.join(HERE, '..', '5 Cut hunt'))
import vpp_pack  # noqa: E402

FORMAT = 'extended-cut-vpppatch-1'


# ---------------------------------------------------------------- texture bank (GEKV .ceg)
def bank_entries(b):
    n = struct.unpack_from('<I', b, 16)[0]
    out = []
    for i in range(n):
        o = 32 + 48 * i
        off, w, h = struct.unpack_from('<IHH', b, o)
        out.append({'off': off, 'w': w, 'h': h, 'fmt': bytes(b[o + 8:o + 16]), 'name': bytes(b[o + 16:o + 48])})
    order = sorted(range(n), key=lambda i: out[i]['off'])
    for k, i in enumerate(order):
        nxt = out[order[k + 1]]['off'] if k + 1 < n else len(b)
        out[i]['data'] = bytes(b[out[i]['off']:nxt])
    return out


def tname(e):
    return e['name'].split(bytes(1))[0].decode('latin-1').lower()


def bank_build(head, ents, frames_delta=0):
    """ents in directory order; data keeps the order of the original offsets, new ones appended"""
    assert bytes(head[:4]) == b'GEKV', 'not a texture bank header'   # 0.1.1 guard (see patch_bank)
    n = len(ents)
    start = 32 + 48 * n
    body = bytearray()
    for e in sorted(ents, key=lambda e: (e.get('off') is None, e.get('off') or 0)):
        e['newoff'] = start + len(body)
        body += e['data']
    out = bytearray(head[:32])
    struct.pack_into('<I', out, 8, 48 * n)
    struct.pack_into('<I', out, 12, len(body))
    struct.pack_into('<I', out, 16, n)
    struct.pack_into('<I', out, 24, struct.unpack_from('<I', head, 24)[0] + frames_delta)
    for e in ents:
        out += struct.pack('<IHH', e['newoff'], e['w'], e['h']) + e['fmt'] + e['name']
    out += body
    return bytes(out)


def bank_only(head, ents):
    """a small bank holding just these textures (the shipped <patch>.ceg)"""
    for e in ents:
        e['off'] = None
    return bank_build(head, [dict(e) for e in ents], 0)


# ---------------------------------------------------------------- byte runs
def diff_runs(a, b, gap=32):
    assert len(a) == len(b)
    runs, i, n = [], 0, len(a)
    while i < n:
        if a[i] == b[i]:
            i += 1
            continue
        j = i
        last = i
        while j < n and j - last <= gap:
            if a[j] != b[j]:
                last = j
            j += 1
        runs.append((i, last + 1))
        i = last + 1
    return [[s, hashlib.sha1(a[s:e]).hexdigest(), base64.b64encode(zlib.compress(b[s:e], 9)).decode()] for s, e in runs]


def run_bytes(r):
    return zlib.decompress(base64.b64decode(r[2]))


def apply_runs(data, runs, what):
    """-> (new bytes, problems, changed?)"""
    d = bytearray(data)
    bad, changed = [], False
    for off, old_sha, blob in runs:
        new = zlib.decompress(base64.b64decode(blob))
        cur = bytes(d[off:off + len(new)])
        if cur == new:
            continue
        if len(cur) != len(new) or hashlib.sha1(cur).hexdigest() != old_sha:
            bad.append('%s: bytes at %#x are not the original ones' % (what, off))
            continue
        d[off:off + len(new)] = new
        changed = True
    return bytes(d), bad, changed


# ---------------------------------------------------------------- vpp entries
def get_entry(a, name):
    i = vpp_pack.index_of(a, name)
    if i < 0:
        return None
    f = a['files'][i]
    return zlib.decompress(f['stored']) if a['compressed'] else bytes(f['stored'][:f['size']])


def make(base, target, rules, out):
    A, B = vpp_pack.read(base), vpp_pack.read(target)
    rules = json.load(open(rules, encoding='utf-8')) if isinstance(rules, str) else rules
    patch = {'format': FORMAT, 'entries': {}}
    add_ents, head = [], None
    for ent, r in rules.items():
        old, new = get_entry(A, ent), get_entry(B, ent)
        if r['kind'] == 'bytes':
            patch['entries'][ent] = {'kind': 'bytes', 'runs': diff_runs(old, new)}
            continue
        oe = {tname(e): e for e in bank_entries(old)}
        ne = {tname(e): e for e in bank_entries(new)}
        head = head or new[:32]
        p = {'kind': 'bank', 'add': [], 'copy': r.get('copy', []), 'runs': {}}
        for n in r.get('add', []):
            add_ents.append(ne[n])
            p['add'].append(n)
        for n in r.get('runs', []):
            assert oe[n]['w'] == ne[n]['w'] and len(oe[n]['data']) == len(ne[n]['data']), n
            p['runs'][n] = diff_runs(oe[n]['data'], ne[n]['data'])
        patch['entries'][ent] = p
    if add_ents:
        open(os.path.splitext(out)[0] + '.ceg', 'wb').write(bank_only(head, add_ents))
        patch['add_bank'] = os.path.basename(os.path.splitext(out)[0] + '.ceg')
    json.dump(patch, open(out, 'w', encoding='utf-8'), indent=0)
    return patch


def patch_bank(data, p, addbank, what):
    """-> (new bank bytes, problems, changed?)"""
    ents = bank_entries(data)
    by = {tname(e): e for e in ents}
    bad, changed, frames = [], False, 0
    # byte runs inside existing textures
    for n, runs in p.get('runs', {}).items():
        if n not in by:
            bad.append('%s: texture %s missing' % (what, n))
            continue
        nd, b2, ch = apply_runs(by[n]['data'], runs, '%s %s' % (what, n))
        bad += b2
        if ch:
            by[n]['data'] = nd
            changed = True
    # copies of the player's own textures under a new name
    for c in p.get('copy', []):
        src = by.get(c['src'].lower())
        if src is None:
            bad.append('%s: texture %s missing' % (what, c['src']))
            continue
        fsz = c.get('frame_size') or len(src['data'])
        pix = src['data'][c.get('frame', 0) * fsz:(c.get('frame', 0) + 1) * fsz]   # 0.1.1: was "data" = overwrote
        # the bank itself, so bank_build took its header from these pixels (broken HUD bank, tester report Nov 14)
        fmt = bytes.fromhex(c['fmt']) if c.get('fmt') else src['fmt']
        name = c['dst'].encode('latin-1').ljust(32, bytes(1))
        e = by.get(c['dst'].lower())
        if e is not None:
            if e['data'] == pix and e['fmt'] == fmt:
                continue
            e.update(data=pix, fmt=fmt, w=src['w'], h=src['h'])
        else:
            e = {'off': None, 'w': src['w'], 'h': src['h'], 'fmt': fmt, 'name': name, 'data': pix}
            ents.append(e)
            by[c['dst'].lower()] = e
            frames += 1
        changed = True
    # our own textures
    for n in p.get('add', []):
        src = addbank[n]
        e = by.get(n)
        if e is not None:
            if e['data'] == src['data'] and e['fmt'] == src['fmt'] and (e['w'], e['h']) == (src['w'], src['h']):
                continue
            e.update(data=src['data'], fmt=src['fmt'], w=src['w'], h=src['h'])     # an older version of ours
        else:
            e = dict(src)
            e['off'] = None
            ents.append(e)
            by[n] = e
            frames += 1
        changed = True
    if bad or not changed:
        return data, bad, changed
    return bank_build(data, ents, frames), bad, True


def apply(in_vpp, patch_path, out_vpp=None, dry=False):
    """-> (problems, report lines). Writes out_vpp only when there are no problems and something changed."""
    patch = json.load(open(patch_path, encoding='utf-8'))
    if patch.get('format') != FORMAT:
        return ['unknown patch format'], []
    addbank = {}
    if patch.get('add_bank'):
        ab = open(os.path.join(os.path.dirname(patch_path), patch['add_bank']), 'rb').read()
        addbank = {tname(e): e for e in bank_entries(ab)}
    a = vpp_pack.read(in_vpp)
    bad, rep, any_change = [], [], False
    for ent, p in patch['entries'].items():
        data = get_entry(a, ent)
        if data is None:
            bad.append('%s missing' % ent)
            continue
        if p['kind'] == 'bytes':
            nd, b2, ch = apply_runs(data, p['runs'], ent)
        else:
            nd, b2, ch = patch_bank(data, p, addbank, ent)
        bad += b2
        rep.append('%s: %s' % (ent, 'problem' if b2 else 'changed' if ch else 'already there'))
        if ch and not b2:
            vpp_pack.put(a, ent, nd)
            any_change = True
    if not bad and any_change and not dry and out_vpp:
        open(out_vpp, 'wb').write(vpp_pack.build(a))
    return bad, rep + (['nothing to change'] if not any_change else [])


if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'make':
        p = make(*sys.argv[2:6])
        print({k: (v['kind'], len(v.get('add', [])), len(v.get('runs', []))) for k, v in p['entries'].items()})
    elif c in ('check', 'apply'):
        bad, rep = apply(sys.argv[2], sys.argv[3], sys.argv[4] if c == 'apply' else None, dry=c == 'check')
        print('\n'.join(rep + bad))
        sys.exit(1 if bad else 0)
