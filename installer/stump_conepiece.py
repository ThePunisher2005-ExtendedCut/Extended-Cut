"""CONE + PIECE leg stumps (Nov 16, user idea): keep the original cone (it fits the pelvis), add the torn meat + bone.
No merge needed - the retail hip stub (stub-l_hip / stub-r_hip.rxm) is already THREE separate pieces in one model:
  A  24 points  meat tube (texture top half)            -> seen from outside = the "cone" of the final game
  C  12 points  round flesh cross-section (lower left)  -> caps the tube's deep end, hidden inside the body
  B  22 points  the BONE (lower right, pale)            -> sticks out past C, deepest part, hidden inside the body
The developers' outward stub = A + C + B turned out. Here A stays exactly as shipped; only C and B are mirrored along
the leg (so they face out) and moved to the cone's tip (centred on the leg - the tip, not the off-centre tube), the
flesh disc sitting DISC_DEPTH into the cone and the bone sticking out past it (BONE_LEN of its length kept).
Same points / strips / draw data; only positions + normals move (single-sided like the original). Bounds rewritten.
v2 (user: lots of meat model left inside the body): the cone tube's HIDDEN part (below INNER_FROM along the leg)
is pulled toward the joint by INNER_SCALE (0.5 = half as deep); the visible tip, flesh disc and bone are unchanged.
  python stump_conepiece.py <in stub.rxm> <out stub.rxm> [disc_depth 0.02] [bone_len 0.6] [disc_scale 0.9] [inner 1] [sink 0]
v3 (user: more of the restored stump, less cone): SINK pulls the whole cone tube this far into the body; the disc +
bone stay placed from the ORIGINAL tip, so they cover it.
"""
import math
import struct
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rxm_tool import Model  # noqa: E402

DISC_DEPTH, BONE_LEN, DISC_SCALE = 0.02, 0.6, 0.9
INNER_FROM, INNER_SCALE = 0.0, 1.0


def pieces(m):
    par = list(range(m.n))

    def f(x):
        while par[x] != x:
            par[x] = par[par[x]]; x = par[x]
        return x
    for t in m.triangles():
        par[f(t[0])] = f(t[1]); par[f(t[1])] = f(t[2])
    comps = {}
    for i in range(m.n):
        comps.setdefault(f(i), []).append(i)
    return list(comps.values())


def fix_bounds(m):
    if struct.unpack_from('<I', m.d, 0x1c8)[0] != 1:
        raise SystemExit('bounds block not where expected')
    vs = m.vertices()
    lo = [min(v[k] for v in vs) for k in range(3)]
    hi = [max(v[k] for v in vs) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = max(math.dist(c, v[:3]) for v in vs)
    struct.pack_into('<10f', m.d, 0x1cc, *c, r, *lo, *hi)


def build(src, dst, disc_depth=DISC_DEPTH, bone_len=BONE_LEN, disc_scale=DISC_SCALE, inner=INNER_SCALE, sink=0.0):
    m = Model(src)
    vs = [list(v) for v in m.vertices()]
    ps = pieces(m)
    if len(ps) != 3:
        raise SystemExit('expected 3 pieces, found %d' % len(ps))
    tube = max(ps, key=len)                                           # A
    rest = [p for p in ps if p is not tube]
    disc = min(rest, key=len)                                         # C (12 points)
    bone = max(rest, key=len)                                         # B (22 points)
    if min(vs[i][7] for i in tube) > 0.1 or max(vs[i][6] for i in disc) > 0.5 or min(vs[i][6] for i in bone) < 0.45:
        raise SystemExit('pieces do not match the expected texture areas')
    Z = 2
    tip = max(tube, key=lambda i: vs[i][Z])                           # the cone's tip on the leg axis
    tx, ty, tz = vs[tip][0], vs[tip][1], vs[tip][Z]
    dcx = sum(vs[i][0] for i in disc) / len(disc)
    dcy = sum(vs[i][1] for i in disc) / len(disc)
    dz_near = max(vs[i][Z] for i in disc)                              # disc face toward the tube (inside)
    # mirrored: z' = -z ; the disc's original inner face (dz_near) becomes its outer-most... place the disc so its
    # mirrored range ends disc_depth below the tip, centred on the tip in x / y
    dmin = min(vs[i][Z] for i in disc)
    for i in disc + bone:
        v = vs[i]
        x = (v[0] - dcx) * disc_scale + tx
        y = (v[1] - dcy) * disc_scale + ty
        zm = -v[Z]                                                    # mirror along the leg
        zm = zm - (-dmin)                                             # disc's far face -> 0
        if i in bone:
            zm = zm * bone_len if zm > 0 else zm
        z = zm + tz - disc_depth
        vs[i] = [x, y, z, v[3], v[4], -v[5], v[6], v[7]]
    for i in tube:
        if vs[i][Z] < INNER_FROM:
            vs[i][Z] = INNER_FROM + (vs[i][Z] - INNER_FROM) * inner
        vs[i][Z] -= sink
    for i, v in enumerate(vs):
        m.set_vertex(i, v)
    fix_bounds(m)
    m.save(dst)
    out = [vs[i][Z] for i in bone]
    return 'tube from %.3f, tip z %.3f, disc %.3f..%.3f, bone to %.3f' % (min(vs[i][Z] for i in tube), max(vs[i][Z] for i in tube), min(vs[i][Z] for i in disc), max(vs[i][Z] for i in disc), max(out))


if __name__ == '__main__':
    a = sys.argv[1:]
    print(build(a[0], a[1], *[float(x) for x in a[2:7]]))
