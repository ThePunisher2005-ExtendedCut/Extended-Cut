"""Replace / add files in a VPP v3 archive (raw or zlib-compressed, any alignment), keeping every
untouched file's stored bytes and padding byte-identical.

  python vpp_pack.py <in.vpp> <out.vpp> name=path [name=path ...]
  python vpp_pack.py --selftest <in.vpp | folder>     rebuild with no changes, must be byte-identical

A replaced / added file is stored the way the archive stores files: zlib level 9 ("78 da") when the
archive is compressed, raw otherwise; then padded to the archive's alignment.
"""
import os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vpp_extract import parse, layout  # noqa: E402


def read(path):
    d, ver, total, ents, start, _, _ = parse(path)
    align, offs = layout(d, ents, start)
    if align is None:
        raise ValueError('unknown layout')
    compressed = any(e['size'] != e['csize'] for e in ents)
    files = []
    for i, (e, o) in enumerate(zip(ents, offs)):
        end = offs[i + 1] if i + 1 < len(offs) else len(d)
        raw_name = d[0x800 + 32 * i:0x800 + 32 * i + 24]
        files.append({'raw': raw_name, 'size': e['size'], 'csize': e['csize'],
                      'stored': d[o:o + e['csize']], 'gap': d[o + e['csize']:end]})
    return {'head': d[:0x800], 'dir_gap': d[0x800 + 32 * len(ents):start], 'files': files,
            'align': align, 'compressed': compressed}


def build(a):
    out = bytearray(a['head'])
    struct.pack_into('<I', out, 8, len(a['files']))
    for f in a['files']:
        out += f['raw'] + struct.pack('<II', f['size'], f['csize'])
    n_dir = 0x800 + 32 * len(a['files'])
    if len(a['dir_gap']) and n_dir + len(a['dir_gap']) == ((n_dir + 0x7ff) & ~0x7ff):
        out += a['dir_gap']
    else:
        out += b'\0' * (((len(out) + 0x7ff) & ~0x7ff) - len(out))
    al = a['align']
    for f in a['files']:
        out += f['stored']
        if f['gap'] is not None:
            out += f['gap']
        else:
            out += b'\0' * (((len(out) + al - 1) & ~(al - 1)) - len(out))
    struct.pack_into('<I', out, 12, len(out))
    return bytes(out)


def index_of(a, name):
    for i, f in enumerate(a['files']):
        if f['raw'].split(b'\0')[0].decode('latin-1').lower() == name.lower():
            return i
    return -1


def first_with_ext(a, *exts):
    for i, f in enumerate(a['files']):
        if f['raw'].split(b'\0')[0].decode('latin-1').lower().endswith(exts):
            return i
    return -1


def put(a, name, data, at=None):
    """Replace <name>, or add it. New files go at position <at> (default: end).
    LOAD ORDER MATTERS: the game loads an archive's files in directory order and builds the level when it
    reaches the .rfl - meshes / animations / effects must come BEFORE the level's .tbl / .rfl block."""
    stored = zlib.compress(data, 9) if a['compressed'] else data
    hit = [f for f in a['files'] if f['raw'].split(b'\0')[0].decode('latin-1').lower() == name.lower()]
    if hit:
        f = hit[0]
    else:
        if len(name) > 23:
            raise ValueError('name too long (max 23 chars)')
        f = {'raw': name.encode('latin-1').ljust(24, b'\0')}
        if at is None or at >= len(a['files']):
            if a['files']:
                a['files'][-1]['gap'] = None     # the old last file may carry extra end-of-archive bytes: re-pad it
            a['files'].append(f)
        else:
            a['files'].insert(at, f)
    f.update(size=len(data), csize=len(stored), stored=stored, gap=None)
    # the file after a changed one keeps its own stored bytes; only padding is recomputed
    return f


def selftest(path):
    a = read(path)
    return build(a) == open(path, 'rb').read()


def main():
    if sys.argv[1] == '--selftest':
        tgt = sys.argv[2]
        paths = [tgt] if os.path.isfile(tgt) else [os.path.join(r, f) for r, _, fs in os.walk(tgt)
                                                     for f in fs if f.lower().endswith('.vpp')]
        bad = [p for p in paths if not selftest(p)]
        print(f'{len(paths)} archives, {len(bad)} not identical')
        for p in bad[:20]:
            print('  ', p)
        return 1 if bad else 0
    src, dst, reps = sys.argv[1], sys.argv[2], sys.argv[3:]
    if os.path.abspath(src) == os.path.abspath(dst):
        raise SystemExit('refusing to overwrite the input')
    a = read(src)
    for r in reps:
        name, path = r.split('=', 1)
        put(a, name, open(path, 'rb').read())
    open(dst, 'wb').write(build(a))
    print(f'{dst}: {len(a["files"])} files')


if __name__ == '__main__':
    sys.exit(main())
