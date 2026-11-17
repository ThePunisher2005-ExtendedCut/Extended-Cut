"""Text patches for the game's .tbl files inside tables.vpp (Extended Cut release patcher).

A patch holds, per changed .tbl file, hunks of (context before, removed lines, added lines, context after). Applying
looks for each hunk in the PLAYER's own file by its removed lines + context (exact first, then without the context,
nearest to the expected line), so it still works when other mods moved things around. Nothing from the game is
shipped except the few changed / context lines.

  python tblpatch.py make  <base tables.vpp> <target tables.vpp> <out patch.json> [context lines, default 3]
  python tblpatch.py apply <tables.vpp in> <patch.json> <tables.vpp out>
  python tblpatch.py check <tables.vpp in> <patch.json>          (dry run: says which hunks would apply)
"""
import difflib
import hashlib
import json
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)                                    # release: vpp_pack.py sits next to this file
sys.path.append(os.path.join(HERE, '..', '5 Cut hunt'))      # work folder
import vpp_pack  # noqa: E402

ENC = 'latin-1'


def read_tbls(path):
    a = vpp_pack.read(path)
    out = {}
    for f in a['files']:
        n = f['raw'].split(b'\0')[0].decode(ENC)
        data = zlib.decompress(f['stored']) if a['compressed'] else bytes(f['stored'][:f['size']])
        out[n] = data
    return a, out


def split(data):
    t = data.decode(ENC)
    nl = '\r\n' if '\r\n' in t else '\n'
    lines = t.split(nl)
    return lines, nl


def make(base, target, out, ctx=3):
    _, A = read_tbls(base)
    _, B = read_tbls(target)
    la = {k.lower(): k for k in A}
    patch = {'format': 'extended-cut-tblpatch-1', 'files': {}, 'new_files': {}, 'base_sha1': {}, 'target_sha1': {}}
    for name, data in sorted(B.items()):
        key = name.lower()
        if key not in la:
            patch['new_files'][name] = data.decode(ENC)
            continue
        old = A[la[key]]
        if old == data:
            continue
        a, _ = split(old)
        b, _ = split(data)
        hunks = []
        for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
            if tag == 'equal':
                continue
            hunks.append({'at': i1, 'before': a[max(0, i1 - ctx):i1], 'remove': a[i1:i2], 'add': b[j1:j2],
                          'after': a[i2:i2 + ctx]})
        patch['files'][name] = hunks
        patch['base_sha1'][name] = hashlib.sha1(old).hexdigest()
        patch['target_sha1'][name] = hashlib.sha1(data).hexdigest()
    json.dump(patch, open(out, 'w', encoding='utf-8'), indent=0, ensure_ascii=False)
    n = sum(len(h) for h in patch['files'].values())
    print('patch: %d files, %d hunks, %d new files -> %s' % (len(patch['files']), n, len(patch['new_files']), out))


def _find(lines, h, hint, used):
    """index where h['remove'] starts in lines, or -1. Tries: full context, before-context only, no context."""
    rem, bef, aft = h['remove'], h['before'], h['after']
    tries = [(bef, rem, aft), (bef, rem, []), ([], rem, aft)]
    if rem:
        tries.append(([], rem, []))
    for b, r, a in tries:
        block = b + r + a
        if not block:
            continue
        n = len(block)
        cands = []
        first = block[0]
        for i in range(0, len(lines) - n + 1):
            if lines[i] == first and lines[i:i + n] == block:
                s = i + len(b)
                if s not in used:
                    cands.append(s)
        if cands:
            return min(cands, key=lambda s: abs(s - hint))
    return -1


def apply_lines(lines, hunks):
    """returns (new lines, list of failed hunk indexes). Hunks are applied from the bottom up so indexes stay valid."""
    places = []
    failed = []
    shift = 0
    used = set()
    for k, h in enumerate(hunks):
        s = _find(lines, h, h['at'] + shift, used)
        if s < 0:
            failed.append(k)
            continue
        used.add(s)
        places.append((s, k))
        shift = s - h['at']
    for s, k in sorted(places, reverse=True):
        h = hunks[k]
        lines[s:s + len(h['remove'])] = h['add']
    return lines, failed


def apply(src, patch_path, out=None, dry=False):
    patch = json.load(open(patch_path, encoding='utf-8'))
    a, T = read_tbls(src)
    lower = {k.lower(): k for k in T}
    report, total_fail = [], 0
    for name, hunks in patch['files'].items():
        real = lower.get(name.lower())
        if real is None:
            report.append('%s: missing in this tables.vpp - %d changes skipped' % (name, len(hunks)))
            total_fail += len(hunks)
            continue
        lines, nl = split(T[real])
        sha = hashlib.sha1(T[real]).hexdigest()
        if sha == patch.get('target_sha1', {}).get(name) or (not patch.get('target_sha1') and _already(lines, hunks)):
            report.append('%s: already patched' % name)
            continue
        exact = sha == patch.get('base_sha1', {}).get(name)
        new, failed = apply_lines(lines, hunks)
        total_fail += len(failed)
        report.append('%s: %d of %d changes applied%s%s' % (name, len(hunks) - len(failed), len(hunks), ' (exact base)' if exact else '',
                      '' if not failed else ' - NOT applied: ' + ', '.join(str(i) for i in failed)))
        if not dry:
            vpp_pack.put(a, real, nl.join(new).encode(ENC))
    for name, text in patch.get('new_files', {}).items():
        report.append('%s: new file %s' % (name, 'already there' if name.lower() in lower else 'added'))
        if not dry and name.lower() not in lower:
            vpp_pack.put(a, name, text.encode(ENC))
    if not dry and out:
        open(out, 'wb').write(vpp_pack.build(a))
    return total_fail, report


def _already(lines, hunks):
    """True if every hunk's ADDED block (with its context) is already in the file"""
    text = '\n'.join(lines)
    for h in hunks:
        blk = '\n'.join(h['before'] + h['add'] + h['after'])
        if blk and blk not in text:
            return False
    return True


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'make':
        make(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]) if len(sys.argv) > 5 else 3)
    elif cmd in ('apply', 'check'):
        nf, rep = apply(sys.argv[2], sys.argv[3], None if cmd == 'check' else sys.argv[4], dry=cmd == 'check')
        print('\n'.join(rep))
        print('FAILED CHANGES: %d' % nf if nf else 'ALL CHANGES APPLY')
        sys.exit(1 if nf else 0)
