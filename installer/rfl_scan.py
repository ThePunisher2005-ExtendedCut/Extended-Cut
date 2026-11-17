"""Heuristic object scanner for Punisher (RF2-engine) .rfl level files.

Every placed object starts with:  u32 uid, u16 len, class/name string, u32 room, 3 floats position,
9 floats rotation, u16 len, script name.  This scanner finds those records anywhere in the file
(no need to know the section layout), then reads the optional link list (u32 count + uids) that
ends each event record, and keeps every readable string inside the record as its "params".

  python rfl_scan.py <level.rfl>                  print the objects
  import rfl_scan; rfl_scan.scan(bytes) -> list of dicts
"""
import math, re, struct, sys

NAME = re.compile(rb'[A-Za-z0-9_][\x20-\x7e]{1,62}')


def _str(d, p):
    if p + 2 > len(d):
        return None, p
    n = struct.unpack_from('<H', d, p)[0]
    if not 2 <= n <= 64 or p + 2 + n > len(d):
        return None, p
    s = d[p + 2:p + 2 + n]
    if not NAME.fullmatch(s):
        return None, p
    return s.decode('latin-1'), p + 2 + n


def _sane(f):
    return not math.isnan(f) and abs(f) < 1e5


def scan(d):
    objs, p, N = [], 4, len(d)
    while p < N - 70:
        cls, q = _str(d, p + 4)
        if cls is None:
            p += 1
            continue
        try:
            room = struct.unpack_from('<I', d, q)[0]
            fl = struct.unpack_from('<12f', d, q + 4)
        except struct.error:
            break
        rot = fl[3:]
        ok = (room < 4096 or room == 0xFFFFFFFF) and all(_sane(x) for x in fl) and all(abs(x) <= 1.001 for x in rot)
        if ok:
            # each rotation row should be ~unit length
            ok = all(0.98 < math.sqrt(rot[i] ** 2 + rot[i + 1] ** 2 + rot[i + 2] ** 2) < 1.02 for i in (0, 3, 6))
        if not ok:
            p += 1
            continue
        name, r = _str(d, q + 52)
        uid = struct.unpack_from('<I', d, p)[0]
        objs.append({'off': p, 'uid': uid, 'cls': cls, 'room': room, 'pos': fl[:3],
                     'name': name, 'body': r if name else q + 52})
        p = (r if name else q + 52)
    # record bodies run to the next record; pull params + trailing link list
    for i, o in enumerate(objs):
        end = objs[i + 1]['off'] if i + 1 < len(objs) else min(N, o['body'] + 4096)
        body = d[o['body']:end]
        o['size'] = end - o['off']
        o['params'] = [m.group().decode('latin-1') for m in re.finditer(rb'[\x20-\x7e]{3,}', body)]
        o['links'] = []
        # link list = u32 count followed by count uids, ending exactly at the next record
        for k in range(1, 65):
            s = len(body) - 4 - 4 * k
            if s < 0:
                break
            if struct.unpack_from('<I', body, s)[0] == k:
                uids = list(struct.unpack_from('<%dI' % k, body, s + 4))
                if all((u >> 16) and u < 0x10000000 for u in uids):
                    o['links'] = uids
                    break
    return objs


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    for o in scan(d):
        x, y, z = o['pos']
        print(f"{o['off']:08x} uid={o['uid']:08x} room={o['room']:<4} ({x:8.2f},{y:8.2f},{z:8.2f}) "
              f"{o['cls']:<28} {o['name'] or '':<34} links={[hex(u) for u in o['links']]} {o['params'][:4]}")
