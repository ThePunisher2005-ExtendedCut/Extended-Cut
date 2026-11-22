#!/usr/bin/env python3
"""
fit_skull.py - put a skull image on a shirt texture so it looks right on the Punisher's chest (The Punisher 2005, PC).

    python fit_skull.py SKULL.png SHIRT.png OUT.png [--bottom 0.90] [--preview PREVIEW.png]

SKULL.png  your skull with a transparent background (any size)
SHIRT.png  the shirt template without a skull (the 512x1024 skull-shirt texture, number 04 in the T-shirt and Trench Coat sets)
OUT.png    the finished shirt texture (save it as 04.png in your T-shirt and Trench Coat folders)

How it fits (measured from the game's own skull and your in-game test, "Fit A"):
  - the skull is as wide as the chest panel (13% to 83% of the texture width) and starts at 19% of the height;
  - its shape in the texture is tall and narrow (width : height = 0.55), which the model stretches back to normal on the chest;
  - then the teeth bars are lengthened (only the straight bottom part is stretched, the skull's head is not) so they reach the belt:
    --bottom is where the bars end, as a fraction of the texture height (default 0.90, chosen after an in-game test; 0.855 just touches the belt).
Needs Pillow.  Options: --no-extend, --aspect 0.55, --x0 0.13 --x1 0.83 --top 0.19
"""
import sys
from PIL import Image, ImageDraw


def runs(img, y, thr=128, minw=3):
    px = img.getchannel("A").load(); n = 0; w = 0
    for x in range(img.width):
        if px[x, y] > thr: w += 1
        else:
            if w >= minw: n += 1
            w = 0
    return n + (1 if w >= minw else 0)


def bars_start(sk):
    """first row of the straight teeth bars at the bottom of the skull, or None"""
    h = sk.height; counts = [runs(sk, y) for y in range(h)]
    last = max((y for y in range(h) if counts[y] > 0), default=None)          # ignore transparent rows below the skull
    if last is None: return None, 0
    tail = [counts[y] for y in range(max(0, last - int(h * 0.10)), last + 1) if counts[y] > 0]
    n = max(set(tail), key=tail.count)
    if n < 2: return None, n
    y = last
    while y > 0 and counts[y] != n: y -= 1                                    # skip the faded last rows of the bars
    bottom = y
    while y > 0 and counts[y] == n: y -= 1
    return (y + 1, n) if (bottom - y) > h * 0.06 else (None, n)


def extend_teeth(sk, extra):
    w, h = sk.size; y_b, n = bars_start(sk)
    if y_b is None or extra <= 0:
        start = int(h * 0.75); note = "no separate bars found - stretching the bottom quarter"
    else:
        start = y_b + int(0.15 * (h - y_b)); note = "%d bars start at row %d of %d" % (n, y_b, h)
    top = sk.crop((0, 0, w, start)); bot = sk.crop((0, start, w, h)).resize((w, max(1, h - start + extra)), Image.LANCZOS)
    out = Image.new("RGBA", (w, h + extra), (0, 0, 0, 0)); out.paste(top, (0, 0)); out.paste(bot, (0, start)); return out, note


def main(argv):
    pos = [a for a in argv if not a.startswith("--")]
    def opt(name, default):
        return float(argv[argv.index(name) + 1]) if name in argv else default
    if len(pos) < 3: raise SystemExit(__doc__)
    skull = Image.open(pos[0]).convert("RGBA"); box = skull.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox(); skull = skull.crop(box)
    shirt = Image.open(pos[1]).convert("RGB"); W, H = shirt.size
    x0, x1, top = int(opt("--x0", 0.13) * W), int(opt("--x1", 0.83) * W), int(opt("--top", 0.19) * H); w = x1 - x0; aspect = opt("--aspect", 0.55)
    h = round(w / aspect); sk = skull.resize((w, h), Image.LANCZOS)
    bottom = int(opt("--bottom", 0.90) * H); extra = bottom - (top + h); note = "bars not extended"
    if "--no-extend" not in argv and extra > 0: sk, note = extend_teeth(sk, extra)
    out = shirt.convert("RGBA"); out.alpha_composite(sk, (x0, top)); out = out.convert("RGB"); out.save(pos[2])
    print("skull placed at x %d-%d, y %d-%d of %dx%d (%s)" % (x0, x1, top, top + sk.height, W, H, note))
    if "--preview" in argv:
        p = out.copy(); d = ImageDraw.Draw(p); d.rectangle([x0, top, x1, top + sk.height], outline=(60, 200, 255), width=2); p.save(argv[argv.index("--preview") + 1])


if __name__ == "__main__":
    main(sys.argv[1:])
