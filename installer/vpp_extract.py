"""Extract Volition VPP v3 archives (The Punisher 2005: PC, Xbox, PS2, demos).

Usage:
  python vpp_extract.py <file.vpp | folder> <out_folder> [--list]

For every archive:  <out>/<archive name>/<files>  +  <out>/<archive name>.manifest.json
Handles raw archives and per-file zlib-compressed archives (Xbox). Every file is size-checked.
Leftover bytes (header slack, junk after the NUL in name fields) are saved in the manifest:
they are uninitialised memory from the build machine and can hold dev paths / old names.
Read-only on the input. Standard library only.
"""
import hashlib, json, os, re, struct, sys, zlib

MAGIC = 0x51890ACE


def strings_in(b, minlen=4):
    return [m.decode('latin-1') for m in re.findall(rb'[\x20-\x7e]{%d,}' % minlen, b)]


def parse(path):
    with open(path, 'rb') as f:
        d = f.read()
    magic, ver, n, total = struct.unpack_from('<IIII', d, 0)
    if magic != MAGIC:
        raise ValueError('not a VPP')
    ents, off, name_junk = [], 0x800, []
    for _ in range(n):
        raw = d[off:off + 24]
        name = raw.split(b'\0')[0].decode('latin-1')
        tail = raw[len(name) + 1:]
        if tail.strip(b'\0\xcd'):
            name_junk.append(tail)
        size, csize = struct.unpack_from('<II', d, off + 24)
        ents.append({'name': name, 'size': size, 'csize': csize})
        off += 32
    hdr_slack = d[0x10:0x800]
    return d, ver, total, ents, (off + 0x7ff) & ~0x7ff, hdr_slack, name_junk


def layout(d, ents, start):
    """Pick the per-entry alignment: the largest that keeps every entry inside the file
    and puts every compressed entry on a zlib header (0x78)."""
    for a in (2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2, 1):
        offs, p, ok = [], start, True
        for e in ents:
            offs.append(p)
            if e['size'] != e['csize'] and d[p:p + 1] != b'\x78':
                ok = False
                break
            p = (p + e['csize'] + a - 1) & ~(a - 1)
        if ok and p <= ((len(d) + 0x7ff) & ~0x7ff):
            return a, offs
    return None, None


def extract(path, outroot, list_only=False):
    d, ver, total, ents, pos, slack, name_junk = parse(path)
    base = os.path.basename(path)
    outdir = os.path.join(outroot, base)
    compressed = any(e['size'] != e['csize'] for e in ents)
    problems = []
    align, offs = layout(d, ents, pos)
    if align is None:
        problems.append('no alignment fits - archive truncated or unknown layout')
        offs, p = [], pos
        for e in ents:
            offs.append(p)
            p = (p + e['csize'] + 0x7ff) & ~0x7ff
    if not list_only:
        os.makedirs(outdir, exist_ok=True)
    seen = {}
    for e, pos in zip(ents, offs):
        e['offset'] = pos
        if e['size'] != e['csize']:
            z = zlib.decompressobj()
            try:
                data = z.decompress(d[pos:pos + e['csize'] + 64]) + z.flush()
            except zlib.error as ex:
                data = b''
                problems.append(f"{e['name']}: zlib {ex}")
        else:
            data = d[pos:pos + e['size']]
        if len(data) != e['size']:
            problems.append(f"{e['name']}: got {len(data)} want {e['size']}")
        e['sha1'] = hashlib.sha1(data).hexdigest()
        if not list_only:
            nm = e['name'] or f'_noname_{len(seen)}'
            nm = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', nm)
            k = nm.lower()
            if k in seen:          # duplicate name inside one archive: keep both
                seen[k] += 1
                nm = f'{nm}.dup{seen[k]}'
                e['dup_saved_as'] = nm
            else:
                seen[k] = 0
            with open(os.path.join(outdir, nm), 'wb') as f:
                f.write(data)
    man = {
        'archive': os.path.abspath(path), 'version': ver, 'count': len(ents), 'alignment': align,
        'header_total': total, 'file_size': len(d), 'compressed': compressed,
        'header_flags_0x14': struct.unpack_from('<I', d, 0x14)[0],
        'leftover_strings_header': strings_in(slack),
        'leftover_strings_names': sorted({s for j in name_junk for s in strings_in(j, 3)}),
        'problems': problems, 'files': ents,
    }
    if not list_only:
        with open(os.path.join(outroot, base + '.manifest.json'), 'w', encoding='utf-8') as f:
            json.dump(man, f, indent=1)
    return man


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, out = sys.argv[1], sys.argv[2]
    list_only = '--list' in sys.argv
    paths = []
    if os.path.isdir(src):
        for r, _, fs in os.walk(src):
            paths += [os.path.join(r, f) for f in fs if f.lower().endswith('.vpp')]
    else:
        paths = [src]
    bad = 0
    for p in sorted(paths):
        try:
            sub = os.path.dirname(os.path.relpath(p, src)) if os.path.isdir(src) else ''
            m = extract(p, os.path.join(out, sub), list_only)
            flag = ' COMPRESSED' if m['compressed'] else ''
            pr = f"  PROBLEMS {len(m['problems'])}" if m['problems'] else ''
            print(f"{os.path.basename(p):28s} {m['count']:5d} files{flag}{pr}")
            bad += bool(m['problems'])
        except Exception as ex:
            print(f'{p}: ERROR {ex}')
            bad += 1
    print(f'{len(paths)} archives, {bad} with problems')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
