r"""The Punisher: Extended Cut - art for the Extended menu layout "B5" (release part "menuart", Nov 20 2026).

The new Extended menu is drawn with the game's OWN main-menu art. In a level only mini.vpp always_loaded.ceg is
sure to be loaded, so this part copies five pictures from the PLAYER's files into it under our names:
  ec_menu_skull.tga  <- misc.vpp int-mainmenu.ceg overlay_skull.tga (as it is)
  ec_menu_edge.tga   <- misc.vpp int-mainmenu.ceg grung_bottom.tga turned upright (torn edge facing right), its black
                        body in the sidebar's black (10, 10, 12)
  ec_menu_splat.tga  <- misc.vpp int-mainmenu.ceg ink_splat.tga made white (the plugin tints it red)
  ec_menu_bar.tga    <- mini.vpp int-common.ceg main_text_bg.tga (as it is)
  ec_menu_paper.tga  <- misc.vpp int-mainmenu.ceg options_bg.tga (as it is) - v1.38: the notebook paper for the
                        in-level Extended menu (user: paper look like the main menu)
  (solid fills use the middle of ec_menu_bar - the game's blank.tga is fully transparent)
Pure Python (DXT5 decode / encode), like ec_blackcolt.py. No game pixels are shipped.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
sys.path.insert(0, HERE)
import vpppatch as V  # noqa: E402  (first: it adds the dev tools folder for vpp_pack)
import vpp_pack  # noqa: E402
from ec_blackcolt import _pal, _alpha_vals, _encode_block  # noqa: E402

MARK = 'ec_menu_edge.tga'
PAPER = 'ec_menu_paper.tga'                       # v1.38: a mini.vpp built before has the other four but not this one
FMT = bytes.fromhex('0f02050100019600')          # the game's own interface pictures
SIDEBAR = (0, 0, 0)            # v1.07: the plugin's sidebar is drawn pure black now


def decode(data, cw, ch):
    px = [[None] * cw for _ in range(ch)]
    bw = cw // 4
    for by in range(ch // 4):
        for bx in range(bw):
            q = 16 * (by * bw + bx)
            av = _alpha_vals(data[q], data[q + 1])
            abits = int.from_bytes(data[q + 2:q + 8], 'little')
            c0, c1, idx = struct.unpack_from('<HHI', data, q + 8)
            pal = _pal(c0, c1)
            for i in range(16):
                r, g, b = pal[(idx >> (2 * i)) & 3]
                px[by * 4 + i // 4][bx * 4 + i % 4] = (r, g, b, av[(abits >> (3 * i)) & 7])
    return px


def _alpha_block(a):
    lo, hi = min(a), max(a)
    if lo == hi:
        return bytes([hi, lo]) + bytes(6)
    vals = _alpha_vals(hi, lo)
    bits = 0
    for i, v in enumerate(a):
        best = min(range(8), key=lambda j: abs(vals[j] - v))
        bits |= best << (3 * i)
    return bytes([hi, lo]) + bits.to_bytes(6, 'little')


def encode(px, cw, ch):
    out = bytearray(cw * ch)
    bw = cw // 4
    for by in range(ch // 4):
        for bx in range(bw):
            q = 16 * (by * bw + bx)
            blk = [px[by * 4 + j // 4][bx * 4 + j % 4] for j in range(16)]
            out[q:q + 8] = _alpha_block([p[3] for p in blk])
            out[q + 8:q + 16] = _encode_block([p[:3] for p in blk])
    return bytes(out)


def _canvas(e):
    n = len(e['data'])                      # 1 byte per pixel; canvas = smallest power of 2 >= the picture
    cw = 4
    while cw < e['w']:
        cw *= 2
    return cw, n // cw


def edge_from(e):
    cw, ch = _canvas(e)
    px = decode(e['data'], cw, ch)
    w, h = e['w'], e['h']                    # 640 x 130: torn edge on top, black body below
    # turn -90 degrees: new picture h x w, torn edge facing right, body on the left
    nw, nh = h, w
    ncw = 4
    while ncw < nw:
        ncw *= 2
    nch = 4
    while nch < nh:
        nch *= 2
    out = [[(0, 0, 0, 0)] * ncw for _ in range(nch)]
    for y in range(nh):
        for x in range(nw):
            r, g, b, a = px[h - 1 - x][y]
            if (r + g + b) / 3 < 12:
                r, g, b = SIDEBAR
            out[y][x] = (r, g, b, a)
    return nw, nh, encode(out, ncw, nch)


def white_from(e):
    """same alpha, colour halves white (the plugin tints it)"""
    d = bytearray(e['data'])
    for q in range(0, len(d), 16):
        d[q + 8:q + 16] = struct.pack('<HHI', 0xFFFF, 0xFFFF, 0)
    return bytes(d)


def _tex(vp, bank, name):
    b = V.get_entry(vpp_pack.read(vp), bank)
    if not b or b[:4] != b'GEKV':
        return None
    for e in V.bank_entries(b):
        if V.tname(e) == name:
            return e
    return None


def state(mini):
    try:
        b = V.get_entry(vpp_pack.read(mini), 'always_loaded.ceg')
    except Exception as ex:
        return 'mini.vpp could not be read (%s)' % ex
    have = {V.tname(e) for e in V.bank_entries(b)}
    if MARK in have and PAPER in have:
        return 'installed'
    return 'ready'


def build(mini, game):
    misc = os.path.join(game, 'dvd_pc', 'data', 'misc.vpp')
    sk = _tex(misc, 'int-mainmenu.ceg', 'overlay_skull.tga')
    gb = _tex(misc, 'int-mainmenu.ceg', 'grung_bottom.tga')
    sp = _tex(misc, 'int-mainmenu.ceg', 'ink_splat.tga')
    bar = _tex(mini, 'int-common.ceg', 'main_text_bg.tga')
    pa = _tex(misc, 'int-mainmenu.ceg', 'options_bg.tga')
    if not all((sk, gb, sp, bar, pa)):
        raise ValueError('the game\'s menu pictures were not found (misc.vpp int-mainmenu / mini.vpp int-common)')
    ew, eh, ed = edge_from(gb)
    new = [('ec_menu_skull.tga', sk['w'], sk['h'], sk['data']), ('ec_menu_edge.tga', ew, eh, ed),
           ('ec_menu_splat.tga', sp['w'], sp['h'], white_from(sp)), ('ec_menu_bar.tga', bar['w'], bar['h'], bar['data']),
           (PAPER, pa['w'], pa['h'], pa['data'])]
    a = vpp_pack.read(mini)
    b = V.get_entry(a, 'always_loaded.ceg')
    ents = V.bank_entries(b)
    have = {V.tname(e) for e in ents}
    fmts = {PAPER: pa['fmt']}                     # v1.38: the notebook keeps its own format bytes
    add = [{'off': None, 'w': w, 'h': h, 'fmt': fmts.get(n, FMT), 'data': d, 'name': n.encode('latin-1').ljust(32, bytes(1))}
           for n, w, h, d in new if n not in have]
    vpp_pack.put(a, 'always_loaded.ceg', V.bank_build(b, ents + add, len(add)))
    return vpp_pack.build(a), len(add)


if __name__ == '__main__':      # python ec_menuart.py <mini.vpp in> <game folder (for misc.vpp)> <mini.vpp out>
    data, n = build(sys.argv[1], sys.argv[2])
    open(sys.argv[3], 'wb').write(data)
    print('%d picture(s) added -> %s' % (n, sys.argv[3]))
