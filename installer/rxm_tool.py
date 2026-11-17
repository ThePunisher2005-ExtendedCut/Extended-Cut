"""Simple static model (.rxm, PC / Xbox) reader / editor - found on the gib stump models (stub-*.rxm).

Layout found so far (single mesh models):
  vertices: 32 bytes each = position (3 floats), normal (3 floats, unit length), texture coordinate (2 floats);
            the block ends where the long run of floats ends.
  indices:  u16 triangle STRIP (repeated indices join strips), max index = vertex count - 1.
  python rxm_tool.py obj <model.rxm> <out.obj>          export (Blender can open it)
  python rxm_tool.py info <model.rxm>
"""
import sys, struct, math


def float_runs(d, min_len=48):
    runs, start = [], None
    for o in range(0, len(d) - 3, 4):
        f = struct.unpack_from('<f', d, o)[0]
        ok = math.isfinite(f) and (f == 0.0 or 1e-5 < abs(f) < 1e4)
        if ok and start is None: start = o
        if not ok and start is not None:
            if o - start >= min_len: runs.append((start, o))
            start = None
    if start is not None and len(d) - start >= min_len: runs.append((start, len(d) - len(d) % 4))
    return runs


def u16_runs(d, limit=1000, min_len=24):
    runs, start = [], None
    for o in range(0, len(d) - 1, 2):
        ok = struct.unpack_from('<H', d, o)[0] < limit
        if ok and start is None: start = o
        if not ok and start is not None:
            if o - start >= min_len: runs.append((start, o))
            start = None
    return runs


def is_vertex(d, o):
    nx, ny, nz = struct.unpack_from('<3f', d, o + 12)
    return abs(nx * nx + ny * ny + nz * nz - 1) < 0.02


def find_layout(d):
    """returns (vertex offset, vertex count, index offset, index count)"""
    best = None
    for fa, fb in float_runs(d):
        for ia, ib in u16_runs(d):
            if ia >= fa and ia < fb:
                continue
            n_idx = (ib - ia) // 2
            idx = struct.unpack_from(f'<{n_idx}H', d, ia)
            count = max(idx) + 1                      # the strip uses every vertex
            if count < 3:
                continue
            vo = fb - count * 32
            if vo < 0 or vo % 4 or not all(is_vertex(d, vo + 32 * i) for i in range(count)):
                continue
            if set(idx) != set(range(count)):
                continue
            if best is None or n_idx > best[3]:
                best = (vo, count, ia, n_idx)
    return best


class Model:
    def __init__(self, path):
        self.path = path
        self.d = bytearray(open(path, 'rb').read())
        self.vo, self.n, self.io, self.ni = find_layout(self.d)

    def vertices(self):
        return [struct.unpack_from('<8f', self.d, self.vo + 32 * i) for i in range(self.n)]

    def set_vertex(self, i, v):
        struct.pack_into('<8f', self.d, self.vo + 32 * i, *v)

    def strip(self):
        return struct.unpack_from(f'<{self.ni}H', self.d, self.io)

    def batches(self):
        """The strip is drawn as separate short strips: batch table after the vertices = (strip start, count,
        pushbuffer offset) x N, chaining from 0. The strip data ends where find_layout's run ends; the first u16s of
        that run are a header. Returns [(start, count)] or None."""
        d = self.d
        for o in range(self.vo + 32 * self.n, len(d) - 24, 4):
            if struct.unpack_from('<I', d, o)[0] != 0:
                continue
            bt, p = [], o
            while p + 12 <= len(d):
                st, cnt, pb = struct.unpack_from('<3I', d, p)
                if st != (bt[-1][0] + bt[-1][1] if bt else 0) or not 3 <= cnt <= self.ni:
                    break
                bt.append((st, cnt)); p += 12
            if len(bt) >= 1 and bt[-1][0] + bt[-1][1] <= self.ni and \
                    self.ni - (bt[-1][0] + bt[-1][1]) <= 8 and (len(bt) > 1 or bt[0][1] >= self.ni - 8):
                return bt
        return None

    def triangles(self, with_batch=False):
        s, tris = self.strip(), []
        bt = self.batches()
        if bt is None:
            bt, skip = [(0, len(s))], 0
        else:
            skip = len(s) - (bt[-1][0] + bt[-1][1])   # header u16s in front of the strip data
        for k, (st, cnt) in enumerate(bt):
            seg = s[skip + st: skip + st + cnt]
            for i in range(len(seg) - 2):
                a, b, c = seg[i], seg[i + 1], seg[i + 2]
                if a == b or b == c or a == c:
                    continue
                t = (a, b, c) if i % 2 == 0 else (b, a, c)
                tris.append((t, k) if with_batch else t)
        return tris

    def save(self, path):
        open(path, 'wb').write(self.d)

    def obj(self, out):
        with open(out, 'w') as o:
            for v in self.vertices():
                o.write(f'v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n')
            for v in self.vertices():
                o.write(f'vt {v[6]:.6f} {1 - v[7]:.6f}\n')
            for v in self.vertices():
                o.write(f'vn {v[3]:.6f} {v[4]:.6f} {v[5]:.6f}\n')
            for a, b, c in self.triangles():
                o.write(f'f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}\n')


if __name__ == '__main__':
    m = Model(sys.argv[2])
    if sys.argv[1] == 'info':
        vs = m.vertices()
        print(f'{sys.argv[2]}: {m.n} vertices at {m.vo:#x}, {m.ni} strip indices at {m.io:#x}, {len(m.triangles())} triangles')
        for k in range(3):
            lo, hi = min(v[k] for v in vs), max(v[k] for v in vs)
            print(f'  axis {"xyz"[k]}: {lo:.4f} .. {hi:.4f}')
        print(f'  uv: u {min(v[6] for v in vs):.3f}..{max(v[6] for v in vs):.3f}  v {min(v[7] for v in vs):.3f}..{max(v[7] for v in vs):.3f}')
    elif sys.argv[1] == 'obj':
        m.obj(sys.argv[3]); print('->', sys.argv[3])
