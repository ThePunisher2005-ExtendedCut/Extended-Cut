#!/usr/bin/env python3
"""
shirt_creator.py - prototype of the costume launcher's shirt creator (The Punisher 2005, PC).

Placement = fit_skull.py "Fit A" (same numbers), plus two new steps:
  fabric : the shirt's own folds and shading run through the skull (looks printed, not pasted on)
  wear   : 0 = new print ... 1 = battle-worn (cracks, faded patches, weave, grime, rough edges)
The wear pattern is made at the skull's natural shape and then squeezed like the skull, so on the chest
(where the model stretches the texture back) cracks look natural, not stretched sideways.

    python shirt_creator.py SKULL.png SHIRT.png OUT.png [--wear 0.5] [--fabric 1.0] [--seed 7]
                            [--x0 0.13 --x1 0.83 --top 0.19 --aspect 0.55 --bottom 0.90 --no-extend]
                            [--chest CHEST_PREVIEW.png]
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# ---------------------------------------------------------------- placement (from fit_skull.py)
def _run_counts(sk, thr=128, minw=3):
    """number of solid runs (at least minw pixels wide) in every row - numpy, fast enough for live sliders"""
    a = np.asarray(sk.getchannel("A")) > thr
    out = []
    for row in a:
        if not row.any(): out.append(0); continue
        d = np.diff(np.concatenate(([0], row.astype(np.int8), [0])))
        starts, ends = np.where(d == 1)[0], np.where(d == -1)[0]
        out.append(int(((ends - starts) >= minw).sum()))
    return out

def bars_start(sk):
    h = sk.height; counts = _run_counts(sk)
    last = max((y for y in range(h) if counts[y] > 0), default=None)
    if last is None: return None, 0
    tail = [counts[y] for y in range(max(0, last - int(h * 0.10)), last + 1) if counts[y] > 0]
    n = max(set(tail), key=tail.count)
    if n < 2: return None, n
    y = last
    while y > 0 and counts[y] != n: y -= 1
    bottom = y
    while y > 0 and counts[y] == n: y -= 1
    return (y + 1, n) if (bottom - y) > h * 0.06 else (None, n)

def extend_teeth(sk, extra):
    w, h = sk.size; y_b, n = bars_start(sk)
    start = int(h * 0.75) if (y_b is None or extra <= 0) else y_b + int(0.15 * (h - y_b))
    top = sk.crop((0, 0, w, start)); bot = sk.crop((0, start, w, h)).resize((w, max(1, h - start + extra)), Image.LANCZOS)
    out = Image.new("RGBA", (w, h + extra), (0, 0, 0, 0)); out.paste(top, (0, 0)); out.paste(bot, (0, start)); return out

def place(skull, W, H, x0=0.13, x1=0.83, top=0.19, aspect=0.55, bottom=0.90, extend=True):
    """skull squeezed + teeth extended, and where it goes: (image, x, y, natural_aspect)"""
    box = skull.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox(); skull = skull.crop(box)
    natural = skull.width / skull.height
    X0, X1, T = int(x0 * W), int(x1 * W), int(top * H); w = X1 - X0
    h = round(w / aspect); sk = skull.resize((w, h), Image.LANCZOS)
    extra = int(bottom * H) - (T + h)
    if extend and extra > 0: sk = extend_teeth(sk, extra)
    # v0.9.20 (review): a narrow squeeze or a low top made the skull taller than the shirt -> fabric_shade crashed and
    # Apply failed for every level. Keep the skull on the canvas (the part below the shirt's edge is never seen).
    X0 = max(0, X0); T = max(0, min(T, H - 1))
    if sk.width > W - X0 or sk.height > H - T:
        sk = sk.crop((0, 0, min(sk.width, W - X0), min(sk.height, H - T)))
    return sk, X0, T, natural

_WARP = {}
def _tshirt_warp(name="tshirt"):
    """the measured bend of a model's shirt front (<name>_warp.json next to this file: tshirt / coat): du / dv every
    32 px of a 512x1024 front. v0.9.6: the trench coat has its own (coat_warp.json)"""
    if name not in _WARP:
        import json, os
        try:
            j = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), name + "_warp.json")))
            _WARP[name] = (float(j["step"]), np.asarray(j["du"], np.float32), np.asarray(j["dv"], np.float32))
        except Exception:
            _WARP[name] = False
    return _WARP[name]

def straighten(sk, x, y, W, H, amount, warp="tshirt"):
    """'Straighten teeth' (v0.9.0): counter-warp the skull by the T-shirt model's measured bend, so it comes out
    straight on the chest. Measured from the author's grid shirt (Nov 17, T-shirt, straight on): every grid line found,
    a smooth map fitted, and the inverse stored in tshirt_warp.json (texture px of a 512x1024 front; the design is
    sampled at (u + du, v + dv)). v0.8.9 only lifted the middle of the teeth; the test showed they also lean sideways
    (the right side of the shirt rises and turns in toward the bottom), which this map covers too.
    1.00 = the full T-shirt correction; the trench coat bends less (~0.4). Trusted area: the grid's red lines
    (u 128-384) +- 64 px and v 224-928; it fades to nothing outside. Returns (layer, x, y) - the layer gets a margin."""
    wp = _tshirt_warp(warp)
    if amount <= 0 or not wp: return sk, x, y
    step, DU, DV = wp
    sx, sy = W / 512.0, H / 1024.0
    m = int(np.ceil(max(abs(DU).max() * sx, abs(DV).max() * sy) * amount)) + 2
    a = np.zeros((sk.height + 2 * m, sk.width + 2 * m, 4), np.float32)
    a[m:m + sk.height, m:m + sk.width] = np.asarray(sk, np.float32)
    h, w = a.shape[:2]; x -= m; y -= m
    jj, ii = np.mgrid[0:h, 0:w].astype(np.float32)
    gu = (ii + x) / sx / step; gv = (jj + y) / sy / step                       # canvas -> warp grid cells
    r0 = np.clip(np.floor(gv).astype(np.int32), 0, DU.shape[0] - 2); c0 = np.clip(np.floor(gu).astype(np.int32), 0, DU.shape[1] - 2)
    fy = np.clip(gv - r0, 0, 1); fx = np.clip(gu - c0, 0, 1)
    def bil(g): return (g[r0, c0] * (1 - fy) + g[r0 + 1, c0] * fy) * (1 - fx) + (g[r0, c0 + 1] * (1 - fy) + g[r0 + 1, c0 + 1] * fy) * fx
    src_x = ii + bil(DU) * sx * amount; src_y = jj + bil(DV) * sy * amount
    x0 = np.floor(src_x).astype(np.int32); y0 = np.floor(src_y).astype(np.int32)
    fx = (src_x - x0)[..., None]; fy = (src_y - y0)[..., None]
    def px(yy, xx):
        ok = ((xx >= 0) & (xx < w) & (yy >= 0) & (yy < h))[..., None]
        return a[np.clip(yy, 0, h - 1), np.clip(xx, 0, w - 1)] * ok
    out = (px(y0, x0) * (1 - fx) + px(y0, x0 + 1) * fx) * (1 - fy) + (px(y0 + 1, x0) * (1 - fx) + px(y0 + 1, x0 + 1) * fx) * fy
    out = out[max(0, -y):h - max(0, y + h - H), max(0, -x):w - max(0, x + w - W)]   # keep it on the canvas
    return Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8), "RGBA"), max(0, x), max(0, y)

# ---------------------------------------------------------------- noise helpers
def value_noise(shape, cell, rng):
    """smooth noise 0..1, features about `cell` pixels big"""
    h, w = shape
    gh, gw = max(2, int(h / cell) + 2), max(2, int(w / cell) + 2)
    g = Image.fromarray((rng.random((gh, gw)) * 255).astype(np.uint8))
    return np.asarray(g.resize((w, h), Image.BICUBIC), dtype=np.float32) / 255.0

def fractal(shape, cell, rng, octaves=4):
    out = np.zeros(shape, np.float32); amp = 1.0; tot = 0.0
    for _ in range(octaves):
        out += amp * value_noise(shape, cell, rng); tot += amp; amp *= 0.5; cell = max(2, cell / 2)
    return out / tot

def crack_map(shape, rng, amount):
    """thin crack lines (1 = crack), random walks that branch, like washed-out screen-print ink"""
    h, w = shape
    img = Image.new("L", (w, h), 0); d = ImageDraw.Draw(img)
    n = int(amount * 110 * (w * h) / (900 * 1000)) + 1
    lw = max(1, round(min(w, h) / 300))
    for _ in range(n):
        x, y = rng.random() * w, rng.random() * h
        ang = rng.random() * 2 * np.pi
        for _seg in range(int(rng.integers(6, 22))):
            step = min(w, h) * (0.012 + rng.random() * 0.02)
            ang += rng.normal(0, 0.55)
            nx, ny = x + np.cos(ang) * step, y + np.sin(ang) * step
            d.line([(x, y), (nx, ny)], fill=255, width=lw)
            if rng.random() < 0.18:                                   # side branch
                ba = ang + rng.choice([-1, 1]) * (0.8 + rng.random())
                bx, by = x, y
                for _b in range(int(rng.integers(2, 6))):
                    bs = step * 0.7; ba += rng.normal(0, 0.5)
                    ex, ey = bx + np.cos(ba) * bs, by + np.sin(ba) * bs
                    d.line([(bx, by), (ex, ey)], fill=200, width=max(1, lw - 1)); bx, by = ex, ey
            x, y = nx, ny
    return np.asarray(img.filter(ImageFilter.GaussianBlur(0.6)), np.float32) / 255.0

# ---------------------------------------------------------------- the two new steps
def wear_layer(sk, natural, aspect, wear, seed):
    """returns the skull (RGBA) with wear applied. Maps are built at the chest shape, then squeezed."""
    if wear <= 0: return sk
    rng = np.random.default_rng(seed)
    w, h = sk.size
    cw = int(round(w * (natural / aspect)))          # the width the model stretches the skull to on the chest
    shp = (h, cw)
    def squeeze(m): return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).resize((w, h), Image.LANCZOS), np.float32) / 255.0

    a = np.asarray(sk.getchannel("A"), np.float32) / 255.0
    rgb = np.asarray(sk.convert("RGB"), np.float32)

    # 1. faded, patchy ink: big soft patches, stronger near the edges of the print (rubbing)
    patches = squeeze(fractal(shp, max(8, min(shp) / 5), rng))
    fade = np.clip((patches - (0.62 - 0.35 * wear)) * 3.0, 0, 1) * (0.25 + 0.55 * wear)
    # 2. cracks through the ink
    cracks = squeeze(crack_map(shp, rng, wear))
    # 3. fabric weave showing through: fine grain
    grain = squeeze(fractal(shp, 3.0, rng, 2))
    weave = (grain - 0.5) * 0.30 * wear
    # 4. rough edges: eat into the outline with noise
    edge_noise = squeeze(fractal(shp, max(4, min(shp) / 60), rng, 3))
    soft = np.asarray(sk.getchannel("A").filter(ImageFilter.GaussianBlur(max(1.0, w / 260))), np.float32) / 255.0
    rough = np.clip((soft - 0.5 + (edge_noise - 0.5) * 0.9 * wear) * 6 + 0.5, 0, 1)

    keep = np.clip((1 - fade) * (1 - cracks * (0.35 + 0.65 * min(1, wear * 1.4))) + weave, 0, 1)
    a2 = a * keep * np.where(a > 0, rough, 0)
    # 5. grime: off-white, slightly warm, darker in the patches
    tint = np.array([1.0, 0.97, 0.90], np.float32) * (1 - 0.12 * wear) + 0.0
    grime = 1 - 0.18 * wear * patches[..., None]
    rgb2 = np.clip(rgb * (1 - 0.5 * wear + 0.5 * wear * tint) * grime, 0, 255)
    out = np.dstack([rgb2, a2 * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")

def fabric_shade(shirt_rgb, sk, x, y, strength):
    """multiply the skull by the shirt's light and folds under it (1 = full, 0 = flat white print)"""
    if strength <= 0: return sk
    L = np.asarray(shirt_rgb.convert("L"), np.float32)
    big = np.asarray(Image.fromarray(L.astype(np.uint8)).filter(ImageFilter.GaussianBlur(40)), np.float32)
    w, h = sk.size
    reg = L[y:y + h, x:x + w]; regb = big[y:y + h, x:x + w]
    # folds: local detail relative to the surroundings (works on the flat Modern and the shaded Classic shirt)
    eps = 8.0
    folds = (reg + eps) / (regb + eps)
    # overall light: brighter / darker areas of the shirt (muscle shading), relative to the skull area's average
    m = np.median(regb)
    light = np.clip((regb + eps) / (m + eps), 0.35, 1.6)
    shade = 0.88 * (folds ** 1.6) * (light ** 1.1)      # ink a little under pure white, so light can lift it
    shade = 1 + (shade - 1) * strength
    shade = np.clip(shade, 0.15, 1.25)[..., None]
    arr = np.asarray(sk, np.float32)
    arr[..., :3] = np.clip(arr[..., :3] * shade, 0, 255)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")

def make(skull_path, shirt_path, wear=0.0, fabric=1.0, seed=7, **pl):
    straight = float(pl.pop("straighten", 0.0) or 0.0)                         # v0.8.9
    warp = pl.pop("warp", "tshirt") or "tshirt"                                # v0.9.6: "coat" = the trench coat's map
    skull = Image.open(skull_path).convert("RGBA")
    shirt = Image.open(shirt_path).convert("RGB"); W, H = shirt.size
    sk, x, y, natural = place(skull, W, H, **pl)
    sk, x, y = straighten(sk, x, y, W, H, straight, warp)
    sk = wear_layer(sk, natural, pl.get("aspect", 0.55), wear, seed)
    sk = fabric_shade(shirt, sk, x, y, fabric)
    out = shirt.convert("RGBA"); out.alpha_composite(sk, (x, y))
    return out.convert("RGB"), (x, y, sk.width, sk.height, natural)

def chest_view(tex, info, aspect=0.55):
    """rough 'on the chest' view: the texture stretched sideways the way the model stretches the skull"""
    x, y, w, h, natural = info
    W, H = tex.size
    return tex.resize((int(W * natural / aspect * 1.0), H), Image.LANCZOS)

def main(argv):
    pos = [a for a in argv if not a.startswith("--")]
    def opt(name, default):
        return float(argv[argv.index(name) + 1]) if name in argv else default
    if len(pos) < 3: raise SystemExit(__doc__)
    pl = dict(x0=opt("--x0", 0.13), x1=opt("--x1", 0.83), top=opt("--top", 0.19), aspect=opt("--aspect", 0.55),
              bottom=opt("--bottom", 0.90), extend="--no-extend" not in argv)
    tex, info = make(pos[0], pos[1], opt("--wear", 0.0), opt("--fabric", 1.0), int(opt("--seed", 7)), **pl)
    tex.save(pos[2])
    if "--chest" in argv: chest_view(tex, info, pl["aspect"]).save(argv[argv.index("--chest") + 1])

if __name__ == "__main__":
    main(sys.argv[1:])
