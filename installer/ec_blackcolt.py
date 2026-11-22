r"""The Punisher: Extended Cut - Punisher-only black Colt (release part "blackcolt", Nov 20 2026).

The plugin (v0.95+) draws the PUNISHER's own Colt .45 with a texture named 3rd-colt_pun.tga and the Colt HUD /
pickup picture with ws_colt_pun.tga, when they are loaded (Extended menu -> Weapons -> Black Colt). Enemies keep the
game's Colt. This part makes both textures FROM THE PLAYER'S OWN FILES and adds them to mini.vpp always_loaded.ceg
(loaded in every level):
  3rd-colt_pun : the player's 3rd-colt.tga (from any level's wep_colt.ceg, must be the retail texture) repainted with
                 OUR paint data package\blackcolt_paint.bin (a per-pixel brightness change + our painted parts:
                 silver details, gold medallion, compensator ports). No game pixels are shipped.
  ws_colt_pun  : the player's misc.vpp wep_icons.ceg ws_colt.tga, metal darkened, silver muzzle kept.
Pure Python (DXT5 decoded / re-encoded here), like ec_stumps.py.
"""
import hashlib
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
sys.path.insert(0, HERE)
import vpppatch as V  # noqa: E402  (first: it adds the dev tools folder for vpp_pack)
import vpp_pack  # noqa: E402

COLT_SHA1 = 'eff6235aea5a646683d3f22acfdf4c963a69e558'     # retail 3rd-colt.tga (256 x 256 DXT5)
WS_SHA1 = 'cd5afa73806af2b065948e02ad4a083ee4cd4035'       # retail ws_colt.tga (95 x 39 on a 128 x 64 DXT5 canvas)
MARK = '3rd-colt_pun.tga'
PAINT = os.path.join(HERE, 'package', 'blackcolt_paint.bin')
LEVEL_ORDER = ('Chop/Chop1.vpp', 'Chop/Chop2.vpp', 'Chop/Chop3.vpp', 'bar/bar1.vpp', 'mort/mort1.vpp')


# ---------------------------------------------------------------- DXT5 (colour half: 4-colour mode)
def _565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def _to565(r, g, b):
    r, g, b = (max(0, min(255, int(round(x)))) for x in (r, g, b))
    if r == g == b:          # 0.3 (Deck: green spots on the black Colt): a grey stays grey - green has 6 bits, red / blue
        q = (r * 31 + 127) // 255           # 5, so rounding each on its own tinted dark greys green
        return q << 11 | (2 * q) << 5 | q
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


GREY_SPREAD = 28   # 0.3: pixels whose channels differ by less than this become exact greys (the dark metal had a
                   # green-teal cast, R 14 / G 18 / B 17, that the game's lighting turned into green spots)


def _neutral(px):
    """the low-colour pixels (black metal, silver) become exact greys; the gold medallion etc. keep their colour"""
    for row in px:
        for x, p in enumerate(row):
            c = p[:3]
            if max(c) - min(c) < GREY_SPREAD:
                v = round(sum(c) / 3)
                row[x] = (v, v, v) + tuple(p[3:])
    return px


def _pal(c0, c1):
    a, b = _565(c0), _565(c1)
    if c0 > c1:
        return [a, b, tuple((2 * a[k] + b[k]) / 3 for k in range(3)), tuple((a[k] + 2 * b[k]) / 3 for k in range(3))]
    return [a, b, tuple((a[k] + b[k]) / 2 for k in range(3)), (0, 0, 0)]


def _encode_block(pix):
    mean = [sum(p[k] for p in pix) / 16 for k in range(3)]
    ax = [max(p[k] for p in pix) - min(p[k] for p in pix) for k in range(3)]
    ln = math.sqrt(sum(x * x for x in ax)) or 1.0
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
        return struct.pack('<HHI', c0, c1, 0)
    pal = _pal(c0, c1)
    idx = 0
    for i, p in enumerate(pix):
        best = min(range(4), key=lambda j: sum((p[k] - pal[j][k]) ** 2 for k in range(3)))
        idx |= best << (2 * i)
    return struct.pack('<HHI', c0, c1, idx)


def _alpha_vals(a0, a1):
    if a0 > a1:
        return [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    return [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]


def _decode(data, w, h):
    """-> pixels[y][x] = (r, g, b, a)"""
    px = [[None] * w for _ in range(h)]
    bw = w // 4
    for by in range(h // 4):
        for bx in range(bw):
            q = 16 * (by * bw + bx)
            a0, a1 = data[q], data[q + 1]
            abits = int.from_bytes(data[q + 2:q + 8], 'little')
            av = _alpha_vals(a0, a1)
            c0, c1, idx = struct.unpack_from('<HHI', data, q + 8)
            pal = _pal(c0, c1)
            for i in range(16):
                r, g, b = pal[(idx >> (2 * i)) & 3]
                px[by * 4 + i // 4][bx * 4 + i % 4] = (r, g, b, av[(abits >> (3 * i)) & 7])
    return px


def _encode(data, px, w, h):
    """new DXT5 data: the colour halves from px, the alpha halves kept from data"""
    out = bytearray(data)
    bw = w // 4
    for by in range(h // 4):
        for bx in range(bw):
            q = 16 * (by * bw + bx)
            blk = [px[by * 4 + j // 4][bx * 4 + j % 4][:3] for j in range(16)]
            out[q + 8:q + 16] = _encode_block(blk)
    return bytes(out)


# ---------------------------------------------------------------- the two textures
def paint_colt(data):
    raw = zlib.decompress(open(PAINT, 'rb').read())
    n = 256 * 256
    gain, mask, paint = raw[:n], raw[n:2 * n], raw[2 * n:2 * n + 3 * n]
    px = _decode(data, 256, 256)
    for y in range(256):
        for x in range(256):
            i = y * 256 + x
            if mask[i]:
                px[y][x] = (paint[3 * i], paint[3 * i + 1], paint[3 * i + 2], 255)
            else:
                g = gain[i] / 100.0
                r, gg, b, a = px[y][x]
                px[y][x] = (min(255, r * g), min(255, gg * g), min(255, b * g), a)
    return _encode(data, _neutral(px), 256, 256)


def paint_ws(data):
    w, h = 128, 64
    px = _decode(data, w, h)
    metal = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[y][x]
            l = (r + g + b) / 3; s = max(r, g, b) - min(r, g, b)
            if a > 128 and l > 70 and s < 45 and not (b - r > 40):
                metal.append((x, y, l))
    if not metal:
        raise ValueError('ws_colt has no metal pixels')
    x0 = min(m[0] for m in metal); x1 = max(m[0] for m in metal)
    tipx = x0 + (x1 - x0) * 0.18
    body = [m for m in metal if m[0] >= tipx]
    mu = sum(m[2] for m in body) / max(1, len(body))
    for x, y, l in body:
        nl = 40 + (l - mu) * 0.55
        px[y][x] = (max(0, nl * 0.97), max(0, nl * 0.98), max(0, nl * 1.03), px[y][x][3])
    return _encode(data, px, w, h)


# ---------------------------------------------------------------- find the player's own textures
def _bank_tex(vp, bank, name):
    try:
        a = vpp_pack.read(vp)
        b = V.get_entry(a, bank)
        if not b or b[:4] != b'GEKV':
            return None
        for e in V.bank_entries(b):
            if V.tname(e) == name:
                return e
    except Exception:
        return None
    return None


def find_colt(game):
    ship = os.path.join(game, 'dvd_pc', 'data', 'shipping')
    cands = [os.path.join(ship, *p.split('/')) for p in LEVEL_ORDER]
    for dp, dn, fn in os.walk(ship):
        cands += [os.path.join(dp, f) for f in sorted(fn) if f.lower().endswith('.vpp')]
    seen = set()
    for vp in cands:
        k = os.path.normcase(vp)
        if k in seen or not os.path.isfile(vp):
            continue
        seen.add(k)
        e = _bank_tex(vp, 'wep_colt.ceg', '3rd-colt.tga')
        if e and hashlib.sha1(e['data']).hexdigest() == COLT_SHA1:
            return e, vp
    return None, None


def state(mini, game):
    try:
        a = vpp_pack.read(mini)
        b = V.get_entry(a, 'always_loaded.ceg')
    except Exception as ex:
        return 'mini.vpp could not be read (%s)' % ex
    if not b or b[:4] != b'GEKV':
        return 'mini.vpp has no always_loaded.ceg'
    hit = [e for e in V.bank_entries(b) if V.tname(e) == MARK]
    if hit:
        return 'tinted' if _green_cast(hit[0]) else 'installed'     # 0.3: a 0.2 black Colt gets the grey fix
    if not os.path.isfile(PAINT):
        return 'package\\blackcolt_paint.bin is missing'
    return 'ready'


def _blocks(e):
    """(data, block size, colour offset) of a DXT1 / DXT5 bank texture"""
    w, h = e['w'], e['h']
    bs = len(e['data']) // max(1, (w // 4) * (h // 4))
    return bytearray(e['data']), bs, bs - 8


def _green_cast(e):
    """0.3: True if the dark metal leans green (the 0.2 black Colt: R 12 / G 15.6 / B 13.4)"""
    d, bs, off = _blocks(e)
    if bs not in (8, 16):
        return False
    tot, n = 0.0, 0
    for q in range(off, len(d) - 7, bs * 7):           # every 7th block is plenty
        c0, c1, idx = struct.unpack_from('<HHI', d, q)
        for p in _pal(c0, c1):
            if sum(p) / 3 < 90:
                tot += p[1] - (p[0] + p[2]) / 2; n += 1
    return n > 0 and tot / n > 1.0


def regrey(mini):
    """0.3: re-encode an installed black Colt with exact greys (no green spots) -> new mini.vpp bytes"""
    a = vpp_pack.read(mini)
    b = V.get_entry(a, 'always_loaded.ceg')
    ents = V.bank_entries(b)
    e = [x for x in ents if V.tname(x) == MARK][0]
    d, bs, off = _blocks(e)
    for q in range(off, len(d) - 7, bs):
        c0, c1, idx = struct.unpack_from('<HHI', d, q)
        if bs == 8 and c0 <= c1:
            continue                                     # DXT1 block with transparency - leave it
        pal = _pal(c0, c1)
        row = [list(pal[(idx >> (2 * i)) & 3]) for i in range(16)]
        _neutral([row])
        d[q:q + 8] = _encode_block([tuple(p) for p in row])
    e['data'] = bytes(d)
    vpp_pack.put(a, 'always_loaded.ceg', V.bank_build(b, ents, 0))
    return vpp_pack.build(a)


def build(mini, game):
    """-> (new mini.vpp bytes, where the Colt texture came from)"""
    colt, src = find_colt(game)
    if colt is None:
        raise ValueError('no level file has the game\'s own Colt texture (3rd-colt.tga)')
    misc = os.path.join(game, 'dvd_pc', 'data', 'misc.vpp')
    ws = _bank_tex(misc, 'wep_icons.ceg', 'ws_colt.tga')
    a = vpp_pack.read(mini)
    b = V.get_entry(a, 'always_loaded.ceg')
    ents = V.bank_entries(b)
    add = [{'off': None, 'w': colt['w'], 'h': colt['h'], 'fmt': colt['fmt'], 'data': paint_colt(colt['data']),
            'name': MARK.encode('latin-1').ljust(32, bytes(1))}]
    if ws and hashlib.sha1(ws['data']).hexdigest() == WS_SHA1:
        add.append({'off': None, 'w': ws['w'], 'h': ws['h'], 'fmt': ws['fmt'], 'data': paint_ws(ws['data']),
                    'name': b'ws_colt_pun.tga'.ljust(32, bytes(1))})
    vpp_pack.put(a, 'always_loaded.ceg', V.bank_build(b, ents + add, len(add)))
    return vpp_pack.build(a), os.path.relpath(src, game), len(add)


if __name__ == '__main__':      # dev test: python ec_blackcolt.py <game folder> <mini.vpp out>
    g = sys.argv[1]
    data, src, n = build(os.path.join(g, 'dvd_pc', 'data', 'mini.vpp'), g)
    open(sys.argv[2], 'wb').write(data)
    print('built from %s, %d texture(s) added -> %s' % (src, n, sys.argv[2]))
