"""Skull wear + exact orange (Costume Launcher helper, mock-up stage).
- WEAR: the scratches / pits / chips of the Default HD in-game skull, taken as a detail layer (the skull's brightness
  minus its smooth shading), fitted to the bounding box of any clean skull picture and added to it.
- ORANGE: the environmental-kill skull's exact colours, measured pixel by pixel from the Default HD white / orange
  pair (white brightness -> orange RGB), applied to the worn white skull.
  python skull_wear.py <clean skull png> <out folder> [strength 0..2, default 1]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
HD = os.path.join(HERE, 'Costume Library', 'Default HD', 'in-game skull')
WHITE_HD = os.path.join(HD, 'skull.dds')
ORANGE_HD = os.path.join(HD, '1fb660b7804b360-ab152a26bf656236-00005dd3.png')
# measured: Default HD white brightness (bin centre) -> Default HD orange RGB
LUT_X = [8, 24, 40, 56, 72, 88, 104, 120, 136, 152, 168, 184, 200, 216, 232, 255]
LUT_RGB = [(17, 10, 2), (36, 20, 3), (49, 27, 4), (61, 33, 5), (74, 40, 6), (87, 48, 7), (95, 52, 8), (106, 58, 9),
           (115, 64, 10), (125, 70, 10), (136, 78, 11), (149, 86, 12), (162, 95, 13), (174, 101, 13), (181, 100, 13),
           (184, 104, 14)]


def lum(rgb):
    return rgb[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)


def opaque(a):
    """alpha 0-255, with PS2-style 0-128 pictures stretched to 0-255"""
    al = a[..., 3].copy()
    if al.max() <= 140:
        al = np.clip(al * 255.0 / 128.0, 0, 255)
    return al


def bbox(al):
    ys, xs = np.where(al > 128)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def wear_layer():
    """(fine detail, large-scale stains / shading as a ratio, target surface brightness) of the Default HD skull,
    cropped to its bounding box"""
    w = np.asarray(Image.open(WHITE_HD).convert('RGBA'), np.float32)
    al = opaque(w)
    L = lum(w)
    inside = al > 128
    Lf = np.where(inside, L, 0); wgt = inside.astype(np.float32)
    def blur(x, r):
        return np.asarray(Image.fromarray(np.clip(x, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32)
    big_dark = blur((L < 70) * inside * 255.0, 7) > 60        # eyes, nose, teeth gaps: the skull's shapes, not wear
    surf = inside & ~big_dark
    Ls = np.where(surf, L, 0); ws = surf.astype(np.float32) * 255
    sh = blur(Ls, 18) / np.maximum(blur(ws, 18) / 255, 1e-3)    # stains + shading (surface only)
    det = (L - sh) * surf
    target = np.percentile(L[surf], 70)
    ratio = np.where(surf, sh / max(target, 1), 1.0)
    x0, y0, x1, y1 = bbox(al)
    det, ratio, surf = det[y0:y1, x0:x1], ratio[y0:y1, x0:x1], surf[y0:y1, x0:x1]
    # where the default skull has no surface (its eye / nose holes, outside its outline) a wider or differently shaped
    # skull would get flat patches: fill those with wear tiled from the default skull's forehead
    H, W = det.shape
    fy0, fy1, fx0, fx1 = int(H * 0.10), int(H * 0.30), int(W * 0.25), int(W * 0.75)
    pd, pr = det[fy0:fy1, fx0:fx1], ratio[fy0:fy1, fx0:fx1]
    ph, pw = pd.shape
    yy, xx = np.mgrid[0:H, 0:W]
    hole = ~surf
    det = np.where(hole, pd[(yy + 7) % ph, (xx + 13) % pw], det)
    ratio = np.where(hole, pr[(yy + 7) % ph, (xx + 13) % pw], ratio)
    return det, ratio, np.percentile(L[inside & ~big_dark], 50)


def apply(clean_path, strength=1.0):
    src = Image.open(clean_path).convert('RGBA')
    a = np.asarray(src, np.float32)
    al = opaque(a)
    x0, y0, x1, y1 = bbox(al)
    det, ratio, target = wear_layer()
    W, H = x1 - x0, y1 - y0
    d = np.asarray(Image.fromarray(((det + 128).clip(0, 255)).astype(np.uint8)).resize((W, H), Image.LANCZOS), np.float32) - 128
    r = np.asarray(Image.fromarray((ratio * 128).clip(0, 255).astype(np.uint8)).resize((W, H), Image.LANCZOS), np.float32) / 128
    rgb = a[..., :3].copy()
    region = rgb[y0:y1, x0:x1]
    L = lum(region)
    inside = al[y0:y1, x0:x1] > 128
    # tone: the skull's light surface down to the Default HD skull's level (a pure white skull becomes its stone grey)
    light = L[inside & (L > 60)]
    p70 = np.percentile(light, 70) if light.size else 255
    tone = min(1.0, target / max(p70, 1))
    vis = np.clip((L - 40) / 120, 0, 1)[..., None]            # wear shows on the light surface, not on dark parts
    region *= (1 + (tone * r - 1) * vis * min(strength, 1.0))[..., None] if False else (1 + (tone * r[..., None] - 1) * vis * min(strength, 1.0))
    region += d[..., None] * strength * vis
    rgb[y0:y1, x0:x1] = region
    rgb = rgb.clip(0, 255)
    white = np.dstack([rgb, a[..., 3]])
    Lw = lum(rgb)
    orange = np.zeros_like(white)
    for c in range(3):
        orange[..., c] = np.interp(Lw, LUT_X, [v[c] for v in LUT_RGB])
    orange[..., 3] = a[..., 3]
    return (Image.fromarray(white.clip(0, 255).astype(np.uint8), 'RGBA'),
            Image.fromarray(orange.clip(0, 255).astype(np.uint8), 'RGBA'))


def launcher_orange_now(clean_path):
    """the orange the launcher makes today (costume_manager.orange_skull v0.4.21), for comparison"""
    a = np.asarray(Image.open(clean_path).convert('RGBA'), np.float32)
    L = lum(a); xs = [0, 60, 110, 170, 230, 255]
    o = np.zeros_like(a)
    for c, ys in enumerate(([30, 70, 110, 150, 170, 180], [15, 38, 60, 85, 98, 106], [2, 5, 8, 11, 13, 14])):
        o[..., c] = np.interp(L, xs, ys)
    o[..., 3] = a[..., 3]
    return Image.fromarray(o.clip(0, 255).astype(np.uint8), 'RGBA')


if __name__ == '__main__':
    clean, out = sys.argv[1], sys.argv[2]
    k = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    os.makedirs(out, exist_ok=True)
    w, o = apply(clean, k)
    base = os.path.splitext(os.path.basename(clean))[0]
    w.save(os.path.join(out, base + ' - worn white.png')); o.save(os.path.join(out, base + ' - worn orange.png'))
    print('->', out)


# ---------------------------------------------------------------- v2 (user feedback): aligned fine wear + regional orange
def _landmarks(a):
    """(bbox, [0, eye top, eye bottom, teeth top, 1]) - rows as fractions of the skull height; holes = transparent"""
    al = opaque(a); x0, y0, x1, y1 = bbox(al); inside = al > 128; H = y1 - y0
    frac = []
    for y in range(y0, y1):
        xs = np.where(inside[y])[0]
        frac.append((~inside[y, xs.min():xs.max() + 1]).mean() if len(xs) > 1 else 0.0)
    frac = np.array(frac)
    eyes = np.where(frac[:int(H * 0.6)] > 0.12)[0]
    et, eb = (eyes.min() / H, eyes.max() / H) if len(eyes) else (0.3, 0.45)
    low = np.where(frac[int(H * 0.5):] > 0.04)[0]          # gaps between the teeth
    tt = (low.min() + int(H * 0.5)) / H if len(low) else 0.6
    tt = max(tt, eb + 0.02)
    return (x0, y0, x1, y1), [0.0, et, eb, tt, 1.0]


def apply_v2(clean_path, strength=0.8):
    w = np.asarray(Image.open(WHITE_HD).convert('RGBA'), np.float32)
    o = np.asarray(Image.open(ORANGE_HD).convert('RGBA'), np.float32)
    (hx0, hy0, hx1, hy1), hk = _landmarks(w)
    walp = opaque(w); Lw = lum(w); inside = walp > 128
    blur = lambda x, r: np.asarray(Image.fromarray(np.clip(x, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32)
    big_dark = blur((Lw < 70) * inside * 255.0, 7) > 60
    surf = inside & ~big_dark
    Ls = np.where(surf, Lw, 0)
    sh = blur(Ls, 4) / np.maximum(blur(surf * 255.0, 4) / 255, 1e-3)
    fine = (Lw - sh) * surf                                    # only scratches, pits, chips (no stains / shading)
    # orange: per-row colour shift of the real orange skull vs the brightness mapping (dome yellower, teeth redder)
    exp = np.stack([np.interp(Lw, LUT_X, [v[c] for v in LUT_RGB]) for c in range(3)], -1)
    ok = surf & (opaque(o) > 128) & (Lw > 90)
    tint_rows = np.ones((hy1 - hy0, 3), np.float32)
    for y in range(hy0, hy1):
        s = ok[y]
        if s.sum() > 20: tint_rows[y - hy0] = (o[y, s, :3] / np.maximum(exp[y, s], 1)).mean(0)
    k = 15
    tint_rows = np.stack([np.convolve(np.pad(tint_rows[:, c], k, mode='edge'), np.ones(2 * k + 1) / (2 * k + 1), 'valid') for c in range(3)], -1)

    src = np.asarray(Image.open(clean_path).convert('RGBA'), np.float32)
    (x0, y0, x1, y1), ck = _landmarks(src)
    W, H = x1 - x0, y1 - y0
    HH, HW = hy1 - hy0, hx1 - hx0
    vy = np.arange(H) / H
    uy = np.interp(vy, ck, hk)                                 # input row -> HD row (eyes to eyes, teeth to teeth)
    rows = np.clip((uy * HH).astype(int) + hy0, hy0, hy1 - 1)
    cols = np.clip((np.arange(W) / W * HW).astype(int) + hx0, hx0, hx1 - 1)
    d = fine[rows][:, cols]
    # fill places the default skull has no surface with fine wear from its forehead
    srf = surf[rows][:, cols]
    fy0, fy1, fx0, fx1 = hy0 + int(HH * .1), hy0 + int(HH * .3), hx0 + int(HW * .25), hx0 + int(HW * .75)
    pd = fine[fy0:fy1, fx0:fx1]; ph, pw = pd.shape
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.where(srf, d, pd[(yy + 7) % ph, (xx + 13) % pw])
    rgb = src[..., :3].copy(); reg = rgb[y0:y1, x0:x1]
    Lr = lum(reg)
    inside_c = opaque(src)[y0:y1, x0:x1] > 128
    light = Lr[inside_c & (Lr > 60)]
    target = np.percentile(Lw[surf], 50)
    tone = min(1.0, target / max(np.percentile(light, 70) if light.size else 255, 1))
    vis = np.clip((Lr - 40) / 120, 0, 1)[..., None]
    reg *= 1 + (tone - 1) * vis
    reg += d[..., None] * strength * vis
    rgb[y0:y1, x0:x1] = reg
    rgb = rgb.clip(0, 255)
    white = np.dstack([rgb, src[..., 3]])
    Lo = lum(rgb)
    orange = np.zeros_like(white)
    for c in range(3):
        orange[..., c] = np.interp(Lo, LUT_X, [v[c] for v in LUT_RGB])
    t = np.ones(rgb.shape, np.float32)
    t[y0:y1, x0:x1] = tint_rows[np.clip((uy * HH).astype(int), 0, HH - 1)][:, None, :]
    orange[..., :3] *= t
    orange[..., 3] = src[..., 3]
    return (Image.fromarray(white.clip(0, 255).astype(np.uint8), 'RGBA'),
            Image.fromarray(orange.clip(0, 255).astype(np.uint8), 'RGBA'))


# ---------------------------------------------------------------- v3: wear GENERATED for each skull's own shape
# measured on the Default HD skull: 1 px dark outline (~95), surface mean 197 / std 28 (mottled), 72 marks per 10k px
# of surface, mark sizes median 3 px / 90 % <= 28 px / 99 % <= 145 px, ~8 % long thin scratches, marks ~92 darker
def _dist_to_edge(inside, maxd=8):
    m = Image.fromarray((inside * 255).astype(np.uint8)); d = np.zeros(inside.shape, np.int32); cur = inside.copy()
    for k in range(1, maxd + 1):
        m = m.filter(ImageFilter.MinFilter(3)); e = np.asarray(m) > 128; d[cur & ~e] = k; cur = e
    d[cur] = maxd + 1
    return d


def _row_profile(a, nb=24):
    """mean luminance of the brighter 60% of the skull, in nb bands from top to bottom (relative height)"""
    al = a[..., 3]; m = al > al.max() * 0.5
    ys = np.where(m.any(1))[0]; y0, y1 = ys.min(), ys.max()
    prof = np.zeros(nb, np.float32)
    for i in range(nb):
        ya = int(y0 + i / nb * (y1 - y0)); yb = max(ya + 1, int(y0 + (i + 1) / nb * (y1 - y0)))
        L = lum(a[ya:yb, :, :3][m[ya:yb]])
        prof[i] = L[L >= np.percentile(L, 40)].mean() if L.size > 10 else np.nan
    prof = np.where(np.isnan(prof), np.nanmean(prof), prof)
    return prof, y0, y1


_HD_PROF = None


def _row_gain(orange):
    """per-row gain that brings this orange skull's brightness profile onto the Default HD orange's (smoothed, 0.85-1.35)"""
    global _HD_PROF
    if _HD_PROF is None:
        _HD_PROF = _row_profile(np.asarray(Image.open(ORANGE_HD).convert('RGBA'), np.float32))[0]
    p, y0, y1 = _row_profile(orange)
    g = np.convolve(np.pad(_HD_PROF / np.maximum(p, 1), 2, mode='edge'), np.ones(5) / 5, 'valid').clip(0.85, 1.35)
    H = orange.shape[0]; rel = (np.arange(H) - y0) / max(1, y1 - y0)
    return np.interp(rel, (np.arange(len(g)) + 0.5) / len(g), g).astype(np.float32)


def apply_v3(clean_path, strength=1.0, seed=7, ingame=False):
    """ingame=True: the hotspot skulls are drawn as a glowing see-through sprite (dark = see-through), so the wear is
    stronger: bigger fully black chips, thicker darker scratches, horizontal stroke bundles across the top of the dome
    like the base skull, extra chips along the worn edge"""
    src = np.asarray(Image.open(clean_path).convert('RGBA'), np.float32)
    al = opaque(src); inside = al > 128
    H, W = inside.shape
    rng = np.random.default_rng(seed)
    blur = lambda x, r: np.asarray(Image.fromarray(np.clip(x, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32)
    def noise(r):
        n = rng.standard_normal((H, W)).astype(np.float32)
        b = blur((n - n.min()) / (n.max() - n.min()) * 255, r)
        return (b - b.mean()) / (b.std() + 1e-6)
    rgb = src[..., :3].copy(); L = lum(rgb)
    light = inside & (L > 60)
    vis = np.clip((L - 40) / 120, 0, 1)
    # 1) tone + mottling: the light surface becomes the reference's stone grey
    p70 = np.percentile(L[light], 70) if light.any() else 255
    tone = min(1.0, 197.0 / max(p70, 1))
    mott = noise(6) * 13 + noise(1.2) * 7
    Lnew = L * (1 + (tone - 1) * vis) + mott * vis * strength
    # 2) marks: pits / chips / scratches drawn at 4x and shrunk (crisp, slightly soft edges like the reference)
    S = 4
    mk = Image.new('L', (W * S, H * S), 0); dr = ImageDraw.Draw(mk)
    area = light.sum()
    n = int(area / 1e4 * 72 * strength)
    ys, xs = np.where(light)
    yt0, yt1 = ys.min(), ys.max()
    for _ in range(n):
        i = rng.integers(len(ys)); cy, cx = ys[i], xs[i]
        if (cy - yt0) / max(1, yt1 - yt0) > 0.62 and rng.random() < 0.75:   # teeth: keep only a quarter of the marks
            continue
        if rng.random() < 0.09:                               # scratch - mix like the Default HD skull
            kind = rng.random()
            if kind < 0.35:                                   # longer straight strokes, mostly horizontal (dome)
                ln = rng.uniform(30, 90); ang = rng.normal(0, 0.12); bend = rng.uniform(-0.05, 0.05)
            elif kind < 0.55:                                 # vertical streaks (sides, teeth)
                ln = rng.uniform(20, 70); ang = np.pi / 2 + rng.normal(0, 0.08); bend = rng.uniform(-0.05, 0.05)
            else:                                             # short random ones
                ln = rng.uniform(8, 30); ang = rng.uniform(0, np.pi); bend = rng.uniform(-0.25, 0.25)
            pts = []
            for t in np.linspace(-0.5, 0.5, 8):
                a = ang + bend * t
                pts.append(((cx + np.cos(a) * ln * t) * S, (cy + np.sin(a) * ln * t) * S))
            dr.line(pts, fill=int(rng.uniform(215, 255) if ingame else rng.uniform(150, 235)),
                    width=int(rng.choice([4, 5, 6]) if ingame else rng.choice([3, 4, 5])))
            if kind < 0.35 and rng.random() < 0.5:            # the base often has a fainter twin line beside a stroke
                off = rng.uniform(3, 6) * S
                dr.line([(x, y + off) for x, y in pts], fill=int(rng.uniform(90, 160)), width=3)
            continue
        size = min(500.0, float(np.exp(rng.normal(0.9, 1.45))))     # px; mostly tiny specks, a few big chips
        r = max(0.6, np.sqrt(size / np.pi)) * (1.1 if ingame else 1.0)
        k = int(rng.integers(7, 12) + min(14, r * 1.5))          # bigger chips get more ragged outlines
        angs = np.sort(rng.uniform(0, 2 * np.pi, k))
        rad = r * rng.uniform(0.45, 1.3, k) * (1 + 0.15 * np.sin(angs * rng.integers(2, 5) + rng.uniform(0, 6)))
        poly = [((cx + np.cos(a) * q) * S, (cy + np.sin(a) * q) * S) for a, q in zip(angs, rad)]
        dr.polygon(poly, fill=255 if ingame else int(rng.uniform(190, 255)))
        if size > 25 and rng.random() < 0.6:                  # little satellite specks around bigger chips
            for _ in range(rng.integers(1, 4)):
                sx, sy = cx + rng.normal(0, r * 1.5), cy + rng.normal(0, r * 1.5); sr = rng.uniform(0.5, 1.2) * S
                dr.ellipse((sx * S - sr, sy * S - sr, sx * S + sr, sy * S + sr), fill=int(rng.uniform(150, 230)))
    if ingame:
        ys0, xs0 = np.where(inside)
        top, bot, left, right = ys0.min(), ys0.max(), xs0.min(), xs0.max()
        Hs, Ws = bot - top, right - left
        # horizontal stroke bundles across the top of the dome (like the base skull: long, parallel, some broken)
        for _ in range(int(rng.integers(2, 4))):
            by = top + Hs * rng.uniform(0.04, 0.26); bx = left + Ws * rng.uniform(0.1, 0.55)
            ln = Ws * rng.uniform(0.25, 0.5)
            for k in range(int(rng.integers(2, 4))):
                yk = by + k * rng.uniform(3, 7) + rng.normal(0, 0.6)
                x = bx + rng.uniform(-6, 6); xe = x + ln * rng.uniform(0.6, 1.0)
                while x < xe:                                    # broken into a few dashes
                    seg = rng.uniform(12, 45); x2 = min(xe, x + seg)
                    tilt = rng.normal(0, 0.02)
                    dr.line([(x * S, yk * S), (x2 * S, (yk + (x2 - x) * tilt) * S)], fill=int(rng.uniform(200, 255)),
                            width=int(rng.choice([4, 5, 6])))
                    x = x2 + rng.uniform(2, 10)
        # extra chips along the worn edge
        dd = _dist_to_edge(inside)
        ey, ex = np.where(inside & (dd >= 2) & (dd <= 6))
        for _ in range(int(area / 1e4 * 5)):
            i = rng.integers(len(ey)); cy, cx = ey[i], ex[i]; r = rng.uniform(1.2, 4.0)
            k = int(rng.integers(7, 12)); angs = np.sort(rng.uniform(0, 2 * np.pi, k)); rad = r * rng.uniform(0.5, 1.3, k)
            dr.polygon([((cx + np.cos(a) * q) * S, (cy + np.sin(a) * q) * S) for a, q in zip(angs, rad)], fill=255)
    marks = np.asarray(mk.resize((W, H), Image.LANCZOS), np.float32) / 255
    Lnew = Lnew * (1 - marks * (0.95 if ingame else 0.88) * vis)   # chips go near-black like the reference
    # 3) the worn dark outline along this skull's own edge (and its eye / nose holes)
    d = _dist_to_edge(inside)
    rim = np.where(d == 1, 1.0, np.where(d == 2, 0.25, 0.0)) * inside
    Lnew = Lnew * (1 - rim) + 95 * rim
    # new white: keep the picture's colour, change its brightness
    scale = np.where(L > 1, Lnew / np.maximum(L, 1), 1)[..., None]
    out = np.where(light[..., None], rgb * scale, rgb)
    out = np.where((rim > 0)[..., None], np.repeat(Lnew[..., None], 3, 2), out).clip(0, 255)
    white = np.dstack([out, src[..., 3]])
    # orange: exact colours + the regional shift (dome yellower, teeth redder) from the Default HD pair
    _, o2 = apply_v2(clean_path, 0.0)
    w0 = lum(out)
    orange = np.zeros_like(white)
    for c in range(3):
        orange[..., c] = np.interp(w0, LUT_X, [v[c] for v in LUT_RGB])
    o2a = np.asarray(o2, np.float32)
    base0 = np.stack([np.interp(lum(np.asarray(apply_v2(clean_path, 0.0)[0], np.float32)), LUT_X, [v[c] for v in LUT_RGB]) for c in range(3)], -1)
    tint = np.where(base0 > 1, o2a[..., :3] / np.maximum(base0, 1), 1)
    orange[..., :3] *= tint
    orange[..., 3] = src[..., 3]
    orange[..., :3] *= _row_gain(orange)[:, None, None]      # same brightness top -> bottom as the Default HD orange
    return (Image.fromarray(white.clip(0, 255).astype(np.uint8), 'RGBA'),
            Image.fromarray(orange.clip(0, 255).astype(np.uint8), 'RGBA'))
