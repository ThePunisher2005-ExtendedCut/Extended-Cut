#!/usr/bin/env python3
"""
The Punisher (2005, PC) - Costume Manager (early build v0.1)

Pick a costume for each mission. Costumes are folders of texture sets for the Punisher's two main models (trench coat and T-shirt);
the manager rewrites only the level archives you changed. Prison and jungle levels are never touched.

Usage:
  python costume_manager.py "<game folder>"                 interactive (window if available, otherwise a text menu)
  python costume_manager.py "<game folder>" list            missions, archives and the costume each one uses
  python costume_manager.py "<game folder>" library "<folder>"      use your own costume library folder (for example the one that holds MAX)
  python costume_manager.py "<game folder>" costumes        costumes found in the library
  python costume_manager.py "<game folder>" import ["<costume>"]    match your image files to the game's textures and show the result (needs Pillow)
  python costume_manager.py "<game folder>" set "<mission>" "<costume>"      (mission may be 'all'; costume 'Default' = original look)
  python costume_manager.py "<game folder>" set base "<costume>"            a base layer: textures a costume does not have are taken from this costume instead of the game's low-res originals
  python costume_manager.py "<game folder>" set base "<costume>"            base layer: textures a costume does not have come from this costume (e.g. the default upscales) instead of the game's low-res originals
  python costume_manager.py "<game folder>" set skulls "<costume>"          the in-game skulls (HUD, enemy/hotspot icons, mission list, loading screen) - one choice for the whole game
  python costume_manager.py "<game folder>" rename "<costume>" [--dry-run] [--undo]    rename the files in place to the game's names; unused files go to an _unused subfolder
  python costume_manager.py "<game folder>" export "<costume>" "<new folder>" [--dry-run]    copy only the files the game uses, renamed like the game reads them
  python costume_manager.py "<game folder>" use "<costume>" "<part of file name>"    choose which file is used when two files fit the same game texture
  python costume_manager.py "<game folder>" originals "<texture name>" ["<folder>"]   show the ORIGINAL sizes of textures the manager replaced (and save them as PNG into a folder)
  python costume_manager.py "<game folder>" find "<texture name>"      list every archive and bank that holds a texture (e.g. punps2_trench01) and whether the manager handles it
  python costume_manager.py "<game folder>" verify                  after apply: which of each costume's textures are really in the level files, and what stays original
  python costume_manager.py "<game folder>" picker "<costume>" ["<name>"]   v0.9.24: for the Extended Cut in-game picker (scripts/.../Costumes)
  python costume_manager.py "<game folder>" picker used      every costume picked for a level -> the in-game picker
  python costume_manager.py "<game folder>" picker all       v0.9.25: the whole library + the PC Exclusive skins
  python costume_manager.py "<game folder>" picker changes   v0.9.28: only what is new / changed since the last send
  python costume_manager.py "<game folder>" apply [--dry-run]
  (v0.4.25: "restore-all" was removed - it copied whole level files back and could undo other mods. To go back to the
   game's own pictures: set every mission to Default and apply; the originals come back picture by picture.)
Library:  <game folder>\\PunisherCostumes\\costumes\\<Costume name>\\trench\\04.png   and   ...\\tshirt\\01.dds   (numbers 01-10; PNG or DXT5 DDS)
"""
import sys, os, re, json, io, base64, hashlib, shutil, struct, time, threading
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from install_patch import parse_vpp, inner, rebuild, sha, find_archive
import install_patch
from install_textures import bank_dir, tname, replace_in_bank

FOLDER = "PunisherCostumes"
DEFAULT = "Default"
SETS = {"trench": ("punisher_trenchcoat.rxc", "punps2_trench%02d.tga"), "tshirt": ("punisher_tshirt.rxc", "pun_tshirt_%02d.tga")}
KINDS = {"tshirt": "tshirt", "tee": "tshirt", "trench": "trench", "trenchcoat": "trench", "ingameskull": "skull", "skull": "skull"}   # folder names, letters and digits only
OK_NUMBERS = range(1, 14)          # texture numbers a costume may replace; 11-13 carry transparency (hair etc.) and are written with their alpha channel
# v0.4.14 speed: banks that change are recompressed at zlib level 6 instead of 9 (PUNISHER_ZLIB_LEVEL=9 = old way),
# level files are rebuilt 4 at a time (PUNISHER_WORKERS), and a level file whose costume did not change since the
# last apply is skipped without reading it (PunisherCostumes/applied.json; any change to the file itself = redone).
install_patch.MAX_LEVEL = int(os.environ.get("PUNISHER_ZLIB_LEVEL", "6"))
WORKERS = max(1, int(os.environ.get("PUNISHER_WORKERS", "4")))
_LOCK = threading.Lock()
MAX_TEXTURE = int(os.environ.get("PUNISHER_MAX_TEXTURE", "2048"))   # largest side written to the game: 4096 hung the loading screen and crashed in testing, 2048 works (set PUNISHER_MAX_TEXTURE=4096 to experiment)
ALPHA_FILL = {                     # colour the game's own textures hide under their fully transparent pixels (visible where the engine does not blend, e.g. in cutscenes)
    "trench": {11: (14, 14, 11), 12: (38, 33, 28)},
    "tshirt": {11: (14, 14, 11), 12: (8, 8, 8), 13: (0, 0, 0)},
}
MATCH_NUMBERS = range(1, 11)       # numbers used when matching unnamed files by their picture (the transparent ones cannot be matched that way)
MAX_NUMBER = {"trench": 12, "tshirt": 13}
HASH_HINTS = {                     # PS2 texture hashes (start of the file name) that are known to belong to a texture number
    "trench": {"6920281a": 12, "a96752d2": 10},
    "tshirt": {"8a3d9f2b": 12, "236924df": 13, "a96752d2": 10},
}
IMG = (".png", ".dds", ".tga", ".jpg", ".jpeg", ".bmp", ".webp")   # v0.4.25: .webp (the launcher offered it)


def save_json(path, obj, **kw):
    """v0.4.25: write JSON safely - a temporary file first, then swap it in. A power cut / full disk while saving can no
    longer leave a half-written settings, undo or record file (a broken undo file used to stop the next Apply)."""
    d = os.path.dirname(path)
    if d: os.makedirs(d, exist_ok=True)
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f: json.dump(obj, f, **kw); f.flush(); os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            if os.path.exists(tmp): os.remove(tmp)
        except OSError: pass
        raise


def say(s=""):
    print(s); sys.stdout.flush()


def norm(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


VARIANT_LABEL = {}      # costume name -> variant label, for variants made from folders like "Trench Coat (Brown)"


def kind_of(folder):
    """(kind, label) of a costume sub-folder: 'Trench Coat' -> ('trench',''), 'T-Shirt (Black Suit)' -> ('tshirt','Black Suit'), 'Trench Coat - Brown' -> ('trench','Brown')"""
    m = re.match(r"^(.*?)\s*(?:\((.+?)\)|\s-\s(.+?))\s*$", folder)
    base, label = (m.group(1), (m.group(2) or m.group(3)).strip()) if m else (folder, "")
    k = KINDS.get(norm(base))
    return (k, label) if k else (None, "")


def chosen_kind_folders(path, label=""):
    """the T-shirt / trench coat / skull sub-folders a costume (or one of its variants) uses: the ones carrying the variant's label, else the plain ones"""
    cand = {}
    for sub in sorted(os.listdir(path)):
        if os.path.isdir(os.path.join(path, sub)):
            k, lab = kind_of(sub)
            if k: cand.setdefault(k, {}).setdefault(lab.lower(), sub)
    return [c.get(label.lower()) or c.get("") for c in cand.values() if (c.get(label.lower()) or c.get(""))]


def need_pillow():
    try:
        import PIL.Image  # noqa
    except ImportError:
        raise SystemExit("Pillow is needed to read your costume images. Install it with:  python -m pip install pillow")


# ---------------------------------------------------------------- textures
def pow2(n):
    n = max(4, int(n)); lo = 1 << (n.bit_length() - 1); return lo if n - lo < (lo << 1) - n else lo << 1


def to_dxt5(path, kind="opaque", fill=None):
    """(w, h, dxt5 bytes). kind 'opaque' forces alpha 255; kind 'decal' keeps a soft edge (alpha normalised so the solid part is opaque)"""
    raw = open(path, "rb").read()
    if kind == "opaque" and raw[:4] == b"DDS " and raw[84:88] == b"DXT5":
        h, w = struct.unpack_from("<II", raw, 12)
        if w % 4 == 0 and h % 4 == 0 and w == pow2(w) and h == pow2(h) and max(w, h) <= MAX_TEXTURE:
            return w, h, raw[128:128 + w * h]
    need_pillow(); from PIL import Image
    im = Image.open(path).convert("RGBA")
    if kind == "decal":
        S = min(512, pow2(max(im.size))); w = h = S
        a = im.getchannel("A"); mx = max(1, a.getextrema()[1])
        a = a.point(lambda v: min(255, v * 255 // mx))
        px = im.load(); ap = a.load(); tot = [0, 0, 0, 0]
        for y in range(0, im.size[1], 2):
            for x in range(0, im.size[0], 2):
                if ap[x, y] > 128:
                    r, g2, b2, _ = px[x, y]; tot[0] += r; tot[1] += g2; tot[2] += b2; tot[3] += 1
        fill = tuple(t // max(1, tot[3]) for t in tot[:3]) if tot[3] else (255, 255, 255)
        bg = Image.new("RGBA", im.size, fill + (0,)); rgb = im.copy(); rgb.putalpha(255)
        mask = a.point(lambda v: 255 if v > 16 else 0)
        bg.paste(rgb, (0, 0), mask); bg.putalpha(a); im = bg.resize((w, h), Image.LANCZOS)
    elif kind == "alpha":                                     # hair and other transparent textures: keep the alpha channel
        from PIL import ImageStat
        a = im.getchannel("A"); mx = a.getextrema()[1]
        if 0 < mx <= 160: a = a.point(lambda v: min(255, v * 255 // 128))      # PS2 alpha runs 0-128 (128 = opaque; upscaling can overshoot a little): scale it to 0-255
        rgb = im.convert("RGB"); solid = a.point(lambda v: 255 if v > 128 else 0)
        mean = fill or (ImageStat.Stat(rgb, solid).mean if solid.getextrema()[1] else (128, 128, 128))
        bg = Image.new("RGB", im.size, tuple(int(c) for c in mean)); bg.paste(rgb, (0, 0), a.point(lambda v: 255 if v > 16 else 0))   # no dark fringes where it is transparent
        im = bg.convert("RGBA"); im.putalpha(a)
        w, h = pow2(im.size[0]), pow2(im.size[1])
        while max(w, h) > MAX_TEXTURE: w, h = max(4, w // 2), max(4, h // 2)
        if (w, h) != im.size: im = im.resize((w, h), Image.LANCZOS)
    else:
        w, h = pow2(im.size[0]), pow2(im.size[1])
        while max(w, h) > MAX_TEXTURE: w, h = max(4, w // 2), max(4, h // 2)
        if (w, h) != im.size: im = im.resize((w, h), Image.LANCZOS)
        im.putalpha(255)
    buf = io.BytesIO()
    try:
        im.save(buf, format="DDS", pixel_format="DXT5")
    except Exception:
        raise SystemExit("Your Pillow cannot write DXT5. Update it with:  python -m pip install --upgrade pillow")
    return w, h, neutral_darks(buf.getvalue()[128:128 + w * h])

def neutral_darks(data):
    """v0.9.14 (user: boots and hair look off-colour / green): DXT stores colours as 5-6-5 bits, green has twice the steps
    of red and blue, and Pillow's encoder picks block colours that lean GREEN on dark greys (Modern boots 12,12,12 came
    out 9,11,9 - more than half the boot pixels green). Every block colour that is meant to be a dark grey (channels
    within 10 of each other, below 110) is set to the closest true grey 5-6-5 value. Nothing else changes."""
    try:
        import numpy as np
    except Exception:
        return data
    a = np.frombuffer(data, np.uint8).copy()
    if len(a) % 16: return data
    blk = a.reshape(-1, 16)
    for off in (8, 10):                                   # the two colour endpoints of each DXT5 block (after the alpha half)
        c = blk[:, off].astype(np.uint16) | (blk[:, off + 1].astype(np.uint16) << 8)
        r5 = (c >> 11) & 31; g6 = (c >> 5) & 63; b5 = c & 31
        r = (r5 << 3) | (r5 >> 2); g = (g6 << 2) | (g6 >> 4); b = (b5 << 3) | (b5 >> 2)
        mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
        sel = (mx - mn <= 10) & (mx < 110)
        if not sel.any(): continue
        v = (r.astype(np.int32) + g + b + 1) // 3
        nr5 = np.clip((v * 31 + 127) // 255, 0, 31); ve = (nr5 << 3) | (nr5 >> 2)          # grey red / blue level
        ng6 = np.clip((ve * 63 + 127) // 255, 0, 63)                                      # green at that same level
        nc = ((nr5 << 11) | (ng6 << 5) | nr5).astype(np.uint16)
        c = np.where(sel, nc, c)
        blk[:, off] = (c & 255).astype(np.uint8); blk[:, off + 1] = (c >> 8).astype(np.uint8)
    return blk.tobytes()


def decode_dxt(data, w, h):
    from PIL import Image
    def c565(v): return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    img = Image.new("RGB", (w, h)); px = img.load(); p = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            c0, c1, bits = struct.unpack("<HHI", data[p + 8:p + 16]); p += 16
            a, b = c565(c0), c565(c1)
            pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))] if c0 > c1 else \
                  [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0)]
            for j in range(4):
                for i in range(4): px[bx + i, by + j] = pal[(bits >> (2 * (4 * j + i))) & 3]
    return img


def feat(im):
    from PIL import Image, ImageOps
    g = im.convert("L").resize((32, 32), Image.LANCZOS)
    out = []
    for img in (g, ImageOps.equalize(g)):
        v = list(img.tobytes()); m = sum(v) / len(v); v = [x - m for x in v]; n = sum(x * x for x in v) ** 0.5
        out.append([x / n for x in v] if n > 1e-6 else v)
    return out


def corr(f1, f2):
    return max(sum(a * b for a, b in zip(f1[0], f2[0])), sum(a * b for a, b in zip(f1[1], f2[1])))


def numbered(stem):
    m = re.fullmatch(r"(?:[A-Za-z_]*?)(\d{1,2})", stem)
    return int(m.group(1)) if m else None


def texture_number(stem):
    """the texture number a file name asks for, or None. Reads pun_tshirt_04, punps2_trench05, pun_tshirt_04_edited, "pun_tshirt_04 (2)", 04, "04 - old", Trench_05 ...
    A PS2 hash name (6ed5f3b7..., 9f2c33e1...) is never read as a number."""
    m = re.search(r"(?:punps2_trench|pun_tshirt|trench|tshirt|t-shirt|tee)[ _-]?0*(\d{1,2})(?!\d)", stem, re.I)
    if m: return int(m.group(1))
    n = numbered(stem)
    if n is not None: return n
    m = re.match(r"\s*0*(\d{1,2})(?![0-9a-fA-F])", stem)
    return int(m.group(1)) if m else None


def costume_dirs(lib):
    """costume folders: any folder that has a T-shirt / Trench Coat / in-game skull subfolder. Returns {display name: path}.
    Folders like "Trench Coat (Brown)" or "T-Shirt (Black Suit)" make variants named "<costume> / Brown": a variant uses the folders with its label and the plain ones for the rest"""
    out = {}; VARIANT_LABEL.clear()
    def walk(path, rel, depth):
        subs = [d for d in sorted(os.listdir(path)) if os.path.isdir(os.path.join(path, d))]
        kinds = [kind_of(d) for d in subs if kind_of(d)[0]]
        if kinds:
            name = " / ".join(rel)
            if any(not lab for k, lab in kinds): out[name] = path
            for lab in sorted({lab for k, lab in kinds if lab}, key=str.lower):
                out[name + " / " + lab] = path; VARIANT_LABEL[name + " / " + lab] = lab
        if depth < 4:                                         # variant folders can also sit beside the T-shirt / Trench Coat / in-game skull folders
            for d in subs:
                if not kind_of(d)[0] and not d.startswith(("_", ".")): walk(os.path.join(path, d), rel + [d], depth + 1)
    if os.path.isdir(lib):
        for d in sorted(os.listdir(lib)):
            p = os.path.join(lib, d)
            if os.path.isdir(p) and not d.startswith(("_", ".")): walk(p, [d], 1)
    return out


def reference(g, kind):
    """{texture name: PIL image} of the ORIGINAL textures for a model, read from the first archive that carries it"""
    from PIL import Image
    for stem in sorted(g.archives):
        if kind not in g.models(stem):
            continue
        path = g.archives[stem]; d = open(path, "rb").read(); count, total, ents, _ = parse_vpp(d)
        up = os.path.join(g.cdir, "undo", stem + ".json"); undo = json.load(open(up)) if os.path.isfile(up) else {}
        for e in ents:
            if not e["name"].lower().endswith(".ceg"): continue
            b = inner(d, e)
            if b[:4] != b"GEKV": continue
            n, ent = bank_dir(b); out = {}
            for off, w, h, fm, raw in ent:
                nm = tname(raw); pat = SETS[kind][1]
                m = re.fullmatch(re.escape(pat.split("%")[0]) + r"(\d\d)\.tga", nm)
                if not m or int(m.group(1)) not in MATCH_NUMBERS: continue
                o = undo.get(e["name"] + "|" + nm)
                data, ww, hh = (base64.b64decode(o["data"]), o["w"], o["h"]) if o else (b[off:off + w * h], w, h)
                out[nm] = decode_dxt(data, ww, hh)
            if out: return out
    return {}


def mean_diff(a, b):
    from PIL import Image, ImageChops, ImageStat
    x = a.convert("RGB").resize((64, 64), Image.LANCZOS); y = b.convert("RGB").resize((64, 64), Image.LANCZOS)
    return sum(ImageStat.Stat(ImageChops.difference(x, y)).mean) / 3


def match_files(files, refs):
    """files: [(file name, PIL image)], refs: {name: image}.  Returns ({file: (texture, score, note)}, {file: extra info})"""
    rf = {n: feat(i) for n, i in refs.items()}; ims = dict(files); ar = lambda im: im.size[0] / im.size[1]
    ff = {p: feat(im) for p, im in files}; res = {}; extra = {}
    # 1. real copies (same picture at another size or format) are grouped; the largest one represents the group. Similar-looking files whose colours or
    #    details differ are NOT copies - they stay separate candidates.
    reps = []; dup = {}
    for p, im in sorted(files, key=lambda t: -t[1].size[0] * t[1].size[1]):
        for q in reps:
            if abs(ar(ims[q]) - ar(im)) < 0.02 and corr(ff[p], ff[q]) >= 0.88 and mean_diff(im, ims[q]) < 10:
                dup[p] = q; break
        else:
            reps.append(p)
    # 2. strong matches, best first
    pairs = sorted(((round(corr(ff[p], rf[n]), 2), ims[p].size[0] * ims[p].size[1], p, n) for p in reps for n in refs if abs(ar(refs[n]) - ar(ims[p])) < 0.02), key=lambda t: (-t[0], -t[1]))
    used_f = set(); used_t = set(); winner = {}
    for sc, px, p, n in pairs:
        if sc >= 0.80 and p not in used_f and n not in used_t:
            res[p] = (n, sc, ""); used_f.add(p); used_t.add(n); winner[n] = p
    # 3. a file that is the only one left of its shape, and a texture that is the only one left of that shape, belong together
    #    (files that are only rivals of an already matched texture do not count as candidates)
    rivals = {pp for sc, px, pp, n in pairs if sc >= 0.80 and n in used_t and pp not in used_f}
    for sc, px, p, n in pairs:
        if p in used_f or n in used_t: continue
        left_t = [t for t in refs if t not in used_t and abs(ar(refs[t]) - ar(ims[p])) < 0.02]
        left_f = [q for q in reps if q not in used_f and q not in rivals and abs(ar(ims[q]) - ar(ims[p])) < 0.02]
        if len(left_t) == 1 and len(left_f) == 1:
            res[p] = (n, sc, "WEAK match (same shape, the only candidate left) - please check"); used_f.add(p); used_t.add(n); winner[n] = p
    for p in reps:
        if p in used_f: continue
        rival = None
        for sc, px, pp, n in pairs:
            if pp == p and sc >= 0.80 and n in winner: rival = (n, winner[n]); break
        if rival:
            res[p] = (None, 0, "same game texture as %s but DIFFERENT artwork - not used (to use this one: use command)" % rival[1][:24]); extra[p] = {"competes_with": rival[1]}
        else:
            res[p] = (None, 0, "no matching texture found")
    for p, q in dup.items():
        d = mean_diff(ims[p], ims[q])
        res[p] = (None, 0, "%s of %s - not used" % ("identical copy" if d < 1.5 else "near copy (differs slightly)", q[:24])); extra[p] = {"copy_of": q}
    return res, extra


ORANGE_SKULL_HASH = ("1fb660b7",)      # PS2 texture hash of the orange hotspot skull (skull_orng)
WHITE_SKULL_HASH = ("1b2dc518",)       # PS2 texture hash of the white skull (skull01)


def skull_slot(path, fname):
    """'skull_orng.tga' for the orange skull, 'skull.tga' (the white one, skull01) for any other: by the PS2 hash in the file name, else by colour"""
    low = fname.lower()
    if low.startswith(ORANGE_SKULL_HASH): return "skull_orng.tga"
    if low.startswith(WHITE_SKULL_HASH): return "skull.tga"
    try:
        from PIL import Image
        im = Image.open(path).convert("RGBA"); raw = im.resize((64, 64)).tobytes(); px = [raw[i:i + 4] for i in range(0, len(raw), 4) if raw[i + 3] > 128]
        if px:
            r = sum(p[0] for p in px) / len(px); g_ = sum(p[1] for p in px) / len(px); b = sum(p[2] for p in px) / len(px)
            if r > 110 and 0.35 <= g_ / r <= 0.80 and b / r < 0.25: return "skull_orng.tga"      # orange only (a red skull has g/r near 0.2)
    except Exception:
        pass
    return "skull.tga"


def build_mapping(g, name, path, force=False):
    """mapping for one costume folder: {kind: {relative file: {texture, score, note}}}; saved in PunisherCostumes/mappings"""
    mp = os.path.join(g.cdir, "mappings", re.sub(r"[^A-Za-z0-9_.-]+", "_", name) + ".json")
    sig = []; costume_dirs(g.lib); subs = chosen_kind_folders(path, VARIANT_LABEL.get(name, ""))
    for sub in sorted(subs):
        for f in sorted(os.listdir(os.path.join(path, sub))):
            if f.lower().endswith(IMG): sig.append("%s/%s/%d/%d" % (sub, f, os.path.getsize(os.path.join(path, sub, f)), int(os.path.getmtime(os.path.join(path, sub, f)))))
    if not force and os.path.isfile(mp):
        old = json.load(open(mp))
        if old.get("signature") == sig and old.get("version") == 2: return old["sets"]
    need_pillow(); from PIL import Image
    sets = {}; old_sets = json.load(open(mp)).get("sets", {}) if os.path.isfile(mp) else {}
    for sub in sorted(subs):
        kind = kind_of(sub)[0]; d = os.path.join(path, sub)
        if not kind or not os.path.isdir(d): continue
        files = [f for f in sorted(os.listdir(d)) if f.lower().endswith(IMG)]
        if not files: continue
        entry = {}
        if kind == "skull":
            slots = {}
            for f in files:
                slots.setdefault(skull_slot(os.path.join(d, f), f), []).append(f)
            for slot, fl in slots.items():
                best = max(fl, key=lambda f: os.path.getsize(os.path.join(d, f)))
                for f in fl: entry[f] = {"texture": slot if f == best else None, "score": 1.0, "note": "" if f == best else "not used (largest file is used)"}
        else:
            refs = reference(g, kind); pat = SETS[kind][1]; named = {}
            for f in files:
                n = texture_number(os.path.splitext(f)[0])
                if n is None:
                    for hsh, hn in HASH_HINTS.get(kind, {}).items():
                        if f.lower().startswith(hsh): n = hn
                if n is not None: named[f] = n
            by_n = {}
            for f, n in named.items(): by_n.setdefault(n, []).append(f)
            for n, fl in by_n.items():                           # files named for a texture are used as named, whatever the picture looks like
                fl.sort(key=lambda f: -os.path.getmtime(os.path.join(d, f))); win = fl[0]; tex = pat % n
                okn = 1 <= n <= MAX_NUMBER[kind]
                note = "" if okn else "number %d is not a texture of this model" % n
                try:
                    w_, h_ = Image.open(os.path.join(d, win)).size
                    if max(w_, h_) > MAX_TEXTURE: note = (note + "; " if note else "") + "%dx%d is larger than %d - it will be reduced to %d (4096 crashed the game in testing)" % (w_, h_, MAX_TEXTURE, MAX_TEXTURE)
                    if tex in refs and abs(w_ / h_ - refs[tex].size[0] / refs[tex].size[1]) > 0.03:
                        note = (note + "; " if note else "") + "SHAPE %dx%d differs from the game texture (%dx%d) - it will look stretched" % (w_, h_, refs[tex].size[0], refs[tex].size[1])
                except Exception:
                    pass
                entry[win] = {"texture": tex if okn else None, "score": 1.0, "note": note}
                for f in fl[1:]: entry[f] = {"texture": None, "score": 0, "note": "same number as %s - not used (the newest file is used)" % win[:30], "competes_with": win}
            rest = [f for f in files if f not in named]
            if rest:                                             # the others are matched by what the picture looks like
                if not refs:
                    say("  note: no level in the game folder carries the %s model, skipping %d file(s) in %s" % (kind, len(rest), sub))
                else:
                    imgs = []
                    for f in rest:
                        try: im = Image.open(os.path.join(d, f)); im.load(); imgs.append((f, im.convert("RGB")))
                        except Exception as ex: entry[f] = {"texture": None, "score": 0, "note": "could not read (%s)" % ex}
                    taken = {pat % n for n in by_n}; refs2 = {k: v for k, v in refs.items() if k not in taken}
                    if imgs and refs2:
                        res, extra = match_files(imgs, refs2)
                        for f, (n, sc, note) in res.items(): entry[f] = dict({"texture": n, "score": sc, "note": note}, **extra.get(f, {}))
                    else:
                        for f, _ in imgs: entry[f] = {"texture": None, "score": 0, "note": "no matching texture found"}
        sets[sub] = {"kind": kind, "files": entry}
    for sub, s_ in sets.items():                              # re-apply choices made with the 'use' command
        for f, v in old_sets.get(sub, {}).get("files", {}).items():
            if v.get("chosen") and f in s_["files"]:
                for f2, v2 in s_["files"].items():
                    if v2["texture"] == v["texture"] and f2 != f: v2["texture"] = None; v2["note"] = "not used (you chose %s)" % f[:24]
                s_["files"][f].update({"texture": v["texture"], "score": v["score"], "note": "chosen by you", "chosen": True})
    save_json(mp, {"version": 2, "signature": sig, "sets": sets}, indent=1)
    return sets


def print_mapping(name, sets):
    say("Costume: %s" % name)
    for sub, s in sets.items():
        say("  %s  (%s)" % (sub, s["kind"]))
        for f, v in sorted(s["files"].items(), key=lambda kv: (kv[1]["texture"] or "~", kv[0])):
            say("    %-48s -> %-22s %s%s" % (f[:48], v["texture"] or "-", ("%.2f " % v["score"]) if v["texture"] else "", v["note"]))


def load_costume(g, name):
    """{texture name: (kind, w, h, dxt5 data)}"""
    path = costume_dirs(g.lib).get(name)
    if path is None:                                          # old simple layout: <costume>/trench, <costume>/tshirt
        path = os.path.join(g.lib, name)
    sets = build_mapping(g, name, path); out = {}
    for sub, s in sets.items():
        d = os.path.join(path, sub); kind = s["kind"]
        for f, v in s["files"].items():
            if not v["texture"]: continue
            mnum = re.search(r"(\d+)\.tga$", v["texture"]); tkind = "decal" if kind == "skull" else ("alpha" if mnum and int(mnum.group(1)) >= 11 else "opaque")
            fill = ALPHA_FILL.get(kind, {}).get(int(mnum.group(1))) if (mnum and tkind == "alpha") else None
            out[v["texture"]] = (kind,) + to_dxt5(os.path.join(d, f), tkind, fill)
    return out                                                # skull textures have kind 'skull'


# ---------------------------------------------------------------- game data
class Game:
    def __init__(self, root):
        self.root = root; self.cdir = os.path.join(root, FOLDER)
        self.lib = os.path.join(self.cdir, "costumes")
        lp = os.path.join(self.cdir, "library_path.txt")
        self.lib_missing = False
        saved = ""
        if os.path.isfile(lp):
            try: saved = open(lp, encoding="utf-8").read().strip()
            except (OSError, UnicodeDecodeError):
                try: saved = open(lp).read().strip()
                except Exception: saved = ""
        if saved and os.path.isdir(saved):
            self.lib = saved                                    # your own library folder (see the 'library' command)
        elif saved:
            # v0.9.20 (review): the chosen library is not there right now (SD card not mounted, folder moved / renamed).
            # Keep the choice - do NOT switch libraries or rewrite library_path.txt; Apply then leaves missions whose
            # costume cannot be found as they are instead of resetting them.
            self.lib = saved; self.lib_missing = True
            say("Your costume library is not there right now: %s - nothing is changed for missions whose costume is missing." % saved)
        else:
            # v0.9.19: a new install - the zip brings "<launcher folder>/Costume Library" (the templates go there), so
            # use it without asking, unless an older library in the game folder already holds something
            own = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Costume Library")
            def has_content(d):
                for dp, dn, fs in os.walk(d):
                    if any(f.lower() != "readme.txt" for f in fs): return True
                return False
            if os.path.isdir(own) and not (os.path.isdir(self.lib) and has_content(self.lib)):
                self.lib = own
                try:
                    os.makedirs(self.cdir, exist_ok=True)
                    open(lp, "w", encoding="utf-8").write(own)
                except OSError: pass
        if not self.lib_missing:
            os.makedirs(self.lib, exist_ok=True)
        self.state_path = os.path.join(self.cdir, "assignments.json")
        try: self.state = json.load(open(self.state_path)) if os.path.isfile(self.state_path) else {}
        except Exception:                                       # v0.9.20 (review): a damaged file crashed the start
            say("Your mission choices file was damaged - it was set aside as assignments.json.damaged; choices start empty.")
            try: os.replace(self.state_path, self.state_path + ".damaged")
            except OSError: pass
            self.state = {}
        self.archives = {}
        self.duplicates = []
        for dp, dn, fs in os.walk(root):
            rel = os.path.relpath(dp, root).lower()                # v0.4.25: only the part below the game folder - a game
            if FOLDER.lower() in rel or "backup" in rel:           # inside e.g. D:\Backup\Games no longer finds nothing
                continue
            for f in fs:
                if f.lower().endswith(".vpp"):
                    k, p = f[:-4].lower(), os.path.join(dp, f)
                    old = self.archives.get(k)
                    if old:                                         # two copies: keep the one the GAME reads (under dvd_pc/data),
                        rank = lambda x: (0 if os.path.relpath(x, root).lower().replace(os.sep, "/").startswith("dvd_pc/data/") else 1,
                                          os.path.relpath(x, root).count(os.sep))   # then the one closest to the game folder
                        keep, other = (old, p) if rank(old) <= rank(p) else (p, old)
                        self.archives[k] = keep; self.duplicates.append((keep, other))
                    else:
                        self.archives[k] = p
        for keep, other in self.duplicates:
            say("  note: two copies of %s - using %s, ignoring %s" % (os.path.basename(keep), os.path.relpath(keep, root), os.path.relpath(other, root)))
        self.missions = self._missions()

    def _missions(self):
        tp = find_archive(self.root, "tables.vpp"); out = []
        try:
            d = open(tp, "rb").read(); c, t, ents, _ = parse_vpp(d)
            e = [x for x in ents if x["name"].lower() == "levels.tbl"][0]
            cur = None
            for line in inner(d, e).decode("latin1").replace("\r", "").split("\n"):
                m = re.match(r'\s*\$Level:\s*En:\s*"([^"]*)"', line)
                if m:
                    cur = [m.group(1), []]; out.append(cur); continue
                m = re.match(r'\s*\$(?:Section|Cutscene):\s*"([^"]+?)(?:\.rfl)?"', line)
                if m and cur is not None:
                    s = m.group(1).lower()
                    if s in self.archives and s not in cur[1]:
                        cur[1].append(s)
        except Exception:
            out = []
        out = [(a, b) for a, b in out if b]
        if not out:
            out = [("All levels", sorted(self.archives))]
        return out

    def models(self, stem):
        try:
            d = open(self.archives[stem], "rb").read(); c, t, ents, _ = parse_vpp(d)
        except Exception:
            return set()
        names = {e["name"].lower() for e in ents}
        return {k for k, (rxc, _) in SETS.items() if rxc in names}

    def costumes(self):
        found = costume_dirs(self.lib)
        old = [x for x in os.listdir(self.lib) if os.path.isdir(os.path.join(self.lib, x)) and not x.startswith(("_", ".")) and x not in found and
               not any(k.startswith(x + " / ") for k in found) and any(os.path.isdir(os.path.join(self.lib, x, y)) for y in ("trench", "tshirt"))]
        return [DEFAULT] + sorted(list(found) + old)

    def save(self):
        save_json(self.state_path, self.state, indent=1)

    def costume_of(self, mission):
        return self.state.get(mission, DEFAULT)

    def find_mission(self, text):
        if text.lower() in ("skulls", "skull", "global", "hud"):
            return [GLOBAL_KEY]
        if text.lower() in ("base", "base layer", "default hd"):
            return [BASE_KEY]
        if text.lower() == "all":
            return [m for m, _ in self.missions]
        hits = [m for m, _ in self.missions if m.lower() == text.lower()] or [m for m, _ in self.missions if text.lower() in m.lower()]
        return hits

    def find_costume(self, text):
        names = self.costumes()
        hits = [n for n in names if n.lower() == text.lower()] or [n for n in names if text.lower() in n.lower()]
        return hits


# ---------------------------------------------------------------- applying
def stored_bank(b):
    """{texture name: (w, h, stored data including any padding)} for a texture bank"""
    n, ent = bank_dir(b); offs = sorted(x[0] for x in ent); cur = {}
    for off, w, h, fm, raw in ent:
        i = offs.index(off); nxt = offs[i + 1] if i + 1 < len(offs) else len(b)
        cur[tname(raw)] = (w, h, b[off:nxt])
    return cur


def bank_formats(b):
    """{texture name: format byte} (0x0F DXT5, 0x0E DXT3, 0x07 uncompressed 32-bit)"""
    n, ent = bank_dir(b)
    return {tname(raw): bytes(fm)[0] for off, w, h, fm, raw in ent}


def apply_core(g, stem, want_for_bank, dry, d=None):
    """Rewrite one archive so that the textures asked for by want_for_bank(bank, current, undo) are in place and every texture an earlier
    choice changed (but this one does not) goes back to its original. Originals are kept in PunisherCostumes/undo."""
    path = g.archives[stem]
    undo_p = os.path.join(g.cdir, "undo", stem + ".json"); undo = json.load(open(undo_p)) if os.path.isfile(undo_p) else {}
    if d is None: d = open(path, "rb").read()
    count, total, ents, _ = parse_vpp(d)
    new_inner = {}; changed = 0; new_undo = dict(undo)
    for e in ents:
        if not e["name"].lower().endswith(".ceg"):
            continue
        try:
            b = inner(d, e)
        except Exception:
            continue
        if b[:4] != b"GEKV":
            continue
        cur = stored_bank(b); fmts = bank_formats(b)
        want = want_for_bank(e["name"], cur, undo)
        repl = {}
        for nm, val in want.items():
            uk = e["name"] + "|" + nm
            if uk not in new_undo:
                w0, h0, d0 = cur[nm]; new_undo[uk] = {"w": w0, "h": h0, "data": base64.b64encode(d0).decode(), "fmt": fmts.get(nm, 0x0F)}
            elif "fmt" not in new_undo[uk]:                     # undo data from before v0.4.19: the original was compressed
                new_undo[uk] = dict(new_undo[uk], fmt=fmts.get(nm, 0x0F) if fmts.get(nm) != 0x07 else 0x0F)
            repl[nm] = val if len(val) > 3 else tuple(val) + (new_undo[uk]["fmt"],)
        for uk, o in undo.items():                              # textures an earlier choice changed but this one does not
            bn, nm = uk.split("|", 1)
            if bn == e["name"] and nm not in want and nm in cur:
                repl[nm] = (o["w"], o["h"], base64.b64decode(o["data"]), o.get("fmt", 0x0F if fmts.get(nm) == 0x07 else fmts.get(nm)))
        def same(nm, v):
            return cur.get(nm) == tuple(v[:3]) and (len(v) < 4 or v[3] is None or fmts.get(nm) == v[3])
        repl = {nm: v for nm, v in repl.items() if not same(nm, v)}
        if repl:
            new_inner[e["name"]] = replace_in_bank(b, repl); changed += len(repl)
    if not changed or dry:
        return changed
    save_json(undo_p, new_undo)
    bk = os.path.join(g.cdir, "backup", os.path.basename(path))
    if not os.path.isfile(bk):
        # v0.9.20 (review): copy to .part and rename - a full disk / a killed launcher left a cut-off backup forever
        os.makedirs(os.path.dirname(bk), exist_ok=True)
        try:
            shutil.copyfile(path, bk + ".part"); os.replace(bk + ".part", bk)
        except OSError as ex:
            try: os.remove(bk + ".part")
            except OSError: pass
            if getattr(ex, "errno", None) == 28:
                raise SystemExit("Could not back up %s - the disk is full." % os.path.basename(path))
            raise
        with _LOCK:                                             # several level files are rebuilt at the same time
            bm = os.path.join(g.cdir, "backup", "manifest.json"); man = json.load(open(bm)) if os.path.isfile(bm) else {}
            man[os.path.basename(path)] = {"sha256": sha(open(bk, "rb").read()), "target": path}; save_json(bm, man, indent=1)
    new = rebuild(d, count, total, ents, new_inner); tmp = path + ".patching%d" % os.getpid()   # v0.9.20: per process
    try:
        open(tmp, "wb").write(new); c2, t2, e2, _ = parse_vpp(new); assert c2 == count; os.replace(tmp, path)
    except BaseException as ex:                                 # v0.4.25: never leave the big .patching file behind
        try:
            if os.path.exists(tmp): os.remove(tmp)
        except OSError: pass
        if isinstance(ex, PermissionError):
            raise SystemExit("Could not write %s - close the game and try again (or the game folder is write-protected,"
                             " e.g. under C:\\Program Files)." % os.path.basename(path))
        if isinstance(ex, OSError) and getattr(ex, "errno", None) == 28:
            raise SystemExit("Could not write %s - the disk is full." % os.path.basename(path))
        raise
    return changed


def apply_archive(g, stem, costume, tex, dry):
    """one level archive: the trench coat / T-shirt sets, only where the archive carries that model"""
    d = open(g.archives[stem], "rb").read()                   # v0.4.14: read once (models + apply)
    try:
        names = {e["name"].lower() for e in parse_vpp(d)[2]}
        models = {k for k, (rxc, _) in SETS.items() if rxc in names}
    except Exception:
        models = set()
    def want_for_bank(bank, cur, undo):
        return {nm: tuple(v[1:]) for nm, v in tex.items() if nm in cur and v[0] in models}     # (w, h, data[, format])
    return apply_core(g, stem, want_for_bank, dry, d)


def tex_signature(tex):
    """one hash for a costume's texture set (what would be written)"""
    h = hashlib.sha1()
    for nm in sorted(tex):
        v = tex[nm]; kind, w, h_, data = v[:4]; h.update(("%s|%s|%d|%d|%s|" % (nm, kind, w, h_, v[4] if len(v) > 4 else "")).encode()); h.update(hashlib.sha1(data).digest())
    return h.hexdigest()


def _file_stamp(path):
    st = os.stat(path); return [st.st_size, st.st_mtime_ns]


# ---------------------------------------------------------------- global in-game skulls (HUD, enemy and hotspot icons, menus)
GLOBAL_KEY = "@skulls"
GLOBAL_LABEL = "In-game skulls (global)"
BASE_KEY = "@base"
BASE_LABEL = "Base layer (fills in textures a costume lacks)"
LABELS = {GLOBAL_KEY: GLOBAL_LABEL, BASE_KEY: BASE_LABEL}
ORANGE = (184, 112, 15)          # colour of the original orange skull
WORN_SKULLS = True               # v0.4.25: the hotspot skulls (white skull01 in mini.vpp + the orange one) get the generated
                                 # Default-HD-style wear (skull_wear.py v3b) and the orange in the game's exact colours; the
                                 # loading screen, HUD and mission-list skulls keep your clean picture
TINT_ORANGE = False              # False: skull_orng uses your skull as it is (like a pack whose two skull images are the same colour); True: tint it orange
TARGETS = [                       # (archive, bank, texture, original size, how it is made)
    ("mini", "always_loaded.ceg", "skull01.tga", (128, 128), "white"),        # icon over enemies, hotspots, menu headers
    ("mini", "always_loaded.ceg", "skull_orng.tga", (128, 128), "orange"),    # the orange hotspot skull
    ("mini", "always_loaded.ceg", "meters_skull.tga", (28, 41), "hud"),       # HUD skulls (score, health meters)
    ("misc", "int-load.ceg", "skull01.tga", (128, 128), "white"),             # loading screen
    ("misc", "int-mainmenu.ceg", "dot_complete.tga", (32, 32), "dot"),        # completed-mission marker in the War Zone list
]


def decal_rgba(path):
    need_pillow(); from PIL import Image
    im = Image.open(path).convert("RGBA"); a = im.getchannel("A"); mx = max(1, a.getextrema()[1]); a = a.point(lambda v: min(255, v * 255 // mx))
    px = im.load(); ap = a.load(); tot = [0, 0, 0, 0]
    for y in range(0, im.size[1], 2):
        for x in range(0, im.size[0], 2):
            if ap[x, y] > 128:
                r, g2, b2, _ = px[x, y]; tot[0] += r; tot[1] += g2; tot[2] += b2; tot[3] += 1
    fill = tuple(t // max(1, tot[3]) for t in tot[:3]) if tot[3] else (255, 255, 255)
    bg = Image.new("RGBA", im.size, fill + (0,)); rgb = im.copy(); rgb.putalpha(255)
    bg.paste(rgb, (0, 0), a.point(lambda v: 255 if v > 16 else 0)); bg.putalpha(a)
    return bg


def dxt5_bytes(im, w, h):
    from PIL import Image
    buf = io.BytesIO(); im.resize((w, h), Image.LANCZOS).save(buf, format="DDS", pixel_format="DXT5")
    return buf.getvalue()[128:128 + w * h]


def decode_rgba(data, w, h):
    from PIL import Image
    im = decode_dxt(data, w, h).convert("RGBA"); al = Image.new("L", (w, h)); ap = al.load(); p = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            a0, a1 = data[p], data[p + 1]; bits = int.from_bytes(data[p + 2:p + 8], "little"); p += 16
            pal = [a0, a1] + ([(6 - i) * a0 / 7 + (i + 1) * a1 / 7 for i in range(6)] if a0 > a1 else [(4 - i) * a0 / 5 + (i + 1) * a1 / 5 for i in range(4)] + [0, 255])
            for j in range(4):
                for i in range(4): ap[bx + i, by + j] = int(pal[(bits >> (3 * (4 * j + i))) & 7])
    im.putalpha(al); return im


def make_dot(orig, skull):
    """the completed-mission marker: keep the circle, swap the skull inside it for yours (same 32x32 size)"""
    from PIL import Image
    W, H = orig.size; px = orig.load(); cx0, cy0 = (W - 1) / 2, (H - 1) / 2; lum = lambda p: 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]
    mask = [(x, y) for y in range(H) for x in range(W) if px[x, y][3] > 128 and lum(px[x, y]) > 130 and ((x - cx0) ** 2 + (y - cy0) ** 2) ** 0.5 <= W * 0.36]
    if not mask: return None
    xs = [p[0] for p in mask]; ys = [p[1] for p in mask]; x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    ring = sorted((px[x, y] for y in range(H) for x in range(W) if (x, y) not in set(mask) and 0.12 * W <= ((x - cx0) ** 2 + (y - cy0) ** 2) ** 0.5 <= 0.36 * W and lum(px[x, y]) < 130 and px[x, y][3] > 128), key=lum)
    fill = tuple(sorted(c[i] for c in ring)[len(ring) // 2] for i in range(3)) if ring else (40, 50, 60)
    out = orig.copy(); op = out.load(); grow = set(mask)
    for (x, y) in mask:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                xx, yy = x + dx, y + dy
                if 0 <= xx < W and 0 <= yy < H and lum(px[xx, yy]) > 90 and ((xx - cx0) ** 2 + (yy - cy0) ** 2) ** 0.5 <= W * 0.38: grow.add((xx, yy))
    for (x, y) in grow: op[x, y] = fill + (255,)
    bh = y1 - y0 + 2; bw = max(4, round(bh * skull.size[0] / skull.size[1])); sk = skull.resize((bw, bh), Image.LANCZOS)
    left = max(0, min(W - bw, round((x0 + x1) / 2 - bw / 2 + 0.01))); top = max(0, min(H - bh, y0 - 1))
    out.alpha_composite(sk, (left, top)); return out


def skull_sources(g, name):
    if is_kit(name):                                           # v0.4.19: a kit's skull image = the white in-game skull
        r = load_kit(g, name) or {}; p = kit_skull(g, r)
        out = {"skull.tga": p, "skull_orng.tga": p} if p and os.path.isfile(p) else {}
    else:
        out = _skull_sources(g, name)
    # v0.4.21: no orange picture of its own (the same picture for both) -> make the orange one in the game's orange
    if out.get("skull_orng.tga") and out.get("skull_orng.tga") == out.get("skull.tga"):
        try: out["skull_orng.tga"] = orange_skull(g, out["skull.tga"])
        except Exception as ex: say("  orange skull could not be made (%s) - the same picture is used" % ex)
    return out


def _skull_sources(g, name):
    """{'skull.tga': path, 'skull_orng.tga': path} for a costume; a variant without its own skull folder uses the nearest parent costume folder's"""
    if is_mix(name): name = (load_mix(g, name) or {}).get("skull_from", "")
    name = split_name(name)[0]
    dirs = costume_dirs(g.lib); path = dirs.get(name) or os.path.join(g.lib, name); out = {}
    if not os.path.isdir(path): return out
    for sub, s_ in build_mapping(g, name, path).items():
        if s_["kind"] == "skull":
            for f, v in s_["files"].items():
                if v["texture"] in ("skull.tga", "skull_orng.tga"): out[v["texture"]] = os.path.join(path, sub, f)
    lib = os.path.abspath(g.lib); parent = os.path.dirname(os.path.abspath(path))
    while len(parent) > len(lib) and len(out) < 2:
        for n2, p2 in dirs.items():
            if os.path.abspath(p2) == parent and n2 != name:
                for k, v in skull_sources(g, n2).items(): out.setdefault(k, v)
        parent = os.path.dirname(parent)
    return out


def build_skull_texture(how, skull, original):
    from PIL import Image
    if how == "white":
        return 512, 512, dxt5_bytes(skull, 512, 512)
    if how == "orange":
        if not TINT_ORANGE:
            return 512, 512, dxt5_bytes(skull, 512, 512)
        im = skull.copy(); px = im.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]): px[x, y] = ORANGE + (px[x, y][3],)
        return 512, 512, dxt5_bytes(im, 512, 512)
    if how == "hud":                                           # keeps its on-screen size (28x41, stored padded to 32x64)
        small = skull.resize((28, 41), Image.LANCZOS).convert("RGBA"); raw = small.tobytes(); mean = tuple(int(sum(raw[i::4]) / (28 * 41)) for i in range(3))
        canvas = Image.new("RGBA", (32, 64), mean + (0,)); canvas.paste(small, (0, 0))
        buf = io.BytesIO(); canvas.save(buf, format="DDS", pixel_format="DXT5"); return 28, 41, buf.getvalue()[128:128 + 32 * 64]
    if how == "dot":
        base = decode_rgba(original, 32, 32); out = make_dot(base, skull)
        if out is None: return None
        buf = io.BytesIO(); out.save(buf, format="DDS", pixel_format="DXT5"); return 32, 32, buf.getvalue()[128:128 + 32 * 32]


def worn_skulls(g, path):
    """v0.4.25: (worn white path, worn orange path) for a skull picture, cached in mix_cache; None if skull_wear fails"""
    try:
        import skull_wear
        key = hashlib.sha1(("%s|%s|wear3e" % (path, os.path.getmtime(path))).encode()).hexdigest()[:16]
        cdir = os.path.join(g.cdir, "mix_cache"); os.makedirs(cdir, exist_ok=True)
        pw, po = os.path.join(cdir, "worn_%s_w.png" % key), os.path.join(cdir, "worn_%s_o.png" % key)
        if not (os.path.isfile(pw) and os.path.isfile(po)):
            w, o = skull_wear.apply_v3(path, 1.0, ingame=True)   # hotspot glow: stronger wear
            w.save(pw); o.save(po)
        return pw, po
    except Exception as ex:
        say("  worn hotspot skulls could not be made (%s) - your clean skull is used" % ex)
        return None


def apply_global(g, dry=False):
    name = g.costume_of(GLOBAL_KEY); skull_w = skull_o = None; skull_hot = None
    if name != DEFAULT:
        if not (is_kit(name) and load_kit(g, name)) and not (is_mix(name) and load_mix(g, name)) and split_name(name)[0] not in g.costumes():
            say("  warning: costume '%s' for the in-game skulls not found - treated as Default" % name); name = DEFAULT
        else:
            srcs = skull_sources(g, name); base = g.costume_of(BASE_KEY)
            if not srcs and base != DEFAULT and base in g.costumes() and base != name: srcs = skull_sources(g, base)
            if not srcs: say("  note: '%s' has no in-game skull folder - the skulls go back to Default" % name)
            else:
                if "skull_orng.tga" not in srcs: srcs["skull_orng.tga"] = srcs.get("skull.tga")         # a pack with one skull image uses it for the orange skull too
                skull_w = decal_rgba(srcs["skull.tga"]) if srcs.get("skull.tga") else None
                skull_o = decal_rgba(srcs["skull_orng.tga"]) if srcs.get("skull_orng.tga") else None
                # v0.4.25: worn hotspot skulls - only when the orange one is generated from the same picture (a pack
                # that brings its own orange skull keeps it)
                gen_orange = os.path.basename(srcs.get("skull_orng.tga") or "").startswith("orange_") or srcs.get("skull_orng.tga") == srcs.get("skull.tga")
                if WORN_SKULLS and srcs.get("skull.tga") and gen_orange:
                    worn = worn_skulls(g, srcs["skull.tga"])
                    if worn:
                        skull_hot = decal_rgba(worn[0]); skull_o = decal_rgba(worn[1])
                        say("  hotspot skulls: worn (white + exact orange) from %s" % os.path.basename(srcs["skull.tga"])[:34])
                say("  in-game skulls: white skull <- %s ; orange (environmental) skull <- %s" % (os.path.basename(srcs["skull.tga"])[:34] if srcs.get("skull.tga") else "(original)", os.path.basename(srcs["skull_orng.tga"])[:34] if srcs.get("skull_orng.tga") else "(original)"))
    total = files = 0
    for stem in ("mini", "misc"):
        if stem not in g.archives:
            if skull_w is not None or skull_o is not None: say("  note: %s.vpp not found in the game folder - skipped" % stem)
            continue
        def want_for_bank(bank, cur, undo, stem=stem):
            out = {}
            for s_, b_, tex, dims, how in TARGETS:
                skull = skull_o if how == "orange" else skull_w
                if how == "white" and s_ == "mini" and skull_hot is not None: skull = skull_hot    # v0.4.25: hotspot / enemy icon
                if skull is None or s_ != stem or b_.lower() != bank.lower() or tex not in cur: continue
                uk = bank + "|" + tex; o = undo.get(uk)
                odims = (o["w"], o["h"]) if o else cur[tex][:2]
                if odims != dims: continue                    # not the texture I expect: leave it alone
                odata = base64.b64decode(o["data"]) if o else cur[tex][2]
                r = build_skull_texture(how, skull, odata)
                if r: out[tex] = r
            return out
        n = apply_core(g, stem, want_for_bank, dry)
        if n: total += n; files += 1; say("  %s %-12s (%s) -> %s: %d textures" % ("would change" if dry else "changed", stem, GLOBAL_LABEL, name, n))
    return total, files


# v0.4.15 outfit variants: a choice can be "Costume + Variant + Variant" - the variants are folders in the outfit folder
# (Costume Launcher templates/Outfit Variants; PunisherCostumes/variants_path.txt says where). Their files are named for
# their texture (punps2_trench09.png, pun_tshirt_05.png ...) or sit in T-shirt / Trench Coat subfolders; they go on top
# of the costume. Texture 04 (the skull shirt) always stays the costume's own.
VARIANTS_DIR = None

SKULL_TAG = " [skull]"      # v0.4.16: "Costume [skull] + Outfit" = only the costume's skull shirt (texture 04),
                             # everything else from the base layer + the outfits

def split_name(name):
    parts = [p.strip() for p in str(name).split(" + ")]
    return parts[0].replace(SKULL_TAG.strip(), "").strip(), [p for p in parts[1:] if p]

def is_skull_only(name):
    return SKULL_TAG.strip() in str(name).split(" + ")[0]

def join_name(base, variants=(), skull_only=False):
    if base == DEFAULT or not base: return DEFAULT
    return " + ".join([base + (SKULL_TAG if skull_only else "")] + list(variants))

def variants_dir(g):
    if VARIANTS_DIR: return VARIANTS_DIR
    vp = os.path.join(g.cdir, "variants_path.txt")
    return open(vp).read().strip() if os.path.isfile(vp) else None

def variant_names(g):
    d = variants_dir(g)
    return sorted(x for x in os.listdir(d) if os.path.isdir(os.path.join(d, x)) and not x.startswith((".", "_"))) if d and os.path.isdir(d) else []

def load_variant(g, vname, _cache={}):
    d = variants_dir(g); path = os.path.join(d or "", vname)
    if not d or not os.path.isdir(path):
        say("  warning: outfit variant '%s' not found - left out" % vname); return {}
    files = []
    for dp, dn, fs in os.walk(path):
        dn[:] = [x for x in dn if not x.startswith(("_", "."))]
        sub_kind = KINDS.get(re.sub(r"[^a-z0-9]", "", os.path.basename(dp).lower())) if dp != path else None
        for f in sorted(fs):
            if f.lower().endswith(IMG): files.append((os.path.join(dp, f), sub_kind))
    out = {}; unknown = []
    for full, sub_kind in files:
        low = os.path.basename(full).lower(); stem = os.path.splitext(os.path.basename(full))[0]
        kind = sub_kind or ("trench" if "trench" in low else ("tshirt" if re.search(r"t[-_ ]?shirt|tee", low) else None))
        num = texture_number(stem)
        if kind not in ("trench", "tshirt") or not num or num not in OK_NUMBERS:
            unknown.append(os.path.basename(full)); continue
        if num == 4: continue                                     # the skull shirt stays the costume's own
        key = (full, os.path.getsize(full), int(os.path.getmtime(full)))
        if key not in _cache:
            tkind = "alpha" if num >= 11 else "opaque"
            fill = ALPHA_FILL.get(kind, {}).get(num) if tkind == "alpha" else None
            _cache[key] = (kind,) + to_dxt5(full, tkind, fill)
        out[SETS[kind][1] % num] = _cache[key]
    if unknown:
        say("  outfit '%s': %d file(s) not used - name them for their texture (punps2_trench09.png, pun_tshirt_05.png): %s" %
            (vname, len(unknown), ", ".join(unknown[:6]) + (" ..." if len(unknown) > 6 else "")))
    return out

# v0.4.18 MIXES: a choice "mix: <name>" = a recipe in <library>/_Mixes/<name>.json (the _ folder is not a costume):
#   {"skull_from": "616 / Klaus Janson", "shirt": "Modern 2.png", "outfits": ["Brown Trench Coat"], "fit": {...}}
# At Apply the skull shirt (texture 04, T-shirt + trench) is made from the skull costume's skull image on that shirt
# background (Costume Launcher templates/Body Armor) and cached in PunisherCostumes/mix_cache; everything else = base
# layer + outfits. One skull, several shirts = several small recipes - no copied texture folders.
MIX_PREFIX = "mix: "
TEMPLATES_DIR = None

def is_mix(name): return str(name).startswith(MIX_PREFIX)

def mix_file(g, name): return os.path.join(g.lib, "_Mixes", str(name)[len(MIX_PREFIX):] + ".json")

def load_mix(g, name):
    try: return json.load(open(mix_file(g, name), encoding="utf-8"))
    except Exception: return None

def templates_dir(g):
    if TEMPLATES_DIR: return TEMPLATES_DIR
    tp = os.path.join(g.cdir, "templates_path.txt")
    return open(tp).read().strip() if os.path.isfile(tp) else None

def skull_image_of(g, costume):
    """the image a costume's skull shirt is made from: its fit.json skull (Shirt creator), else its in-game skull picture"""
    path = costume_dirs(g.lib).get(costume)
    if not path: return None
    try:
        fit = json.load(open(os.path.join(path, "fit.json")))
        for v in fit.values():
            if isinstance(v, dict) and v.get("skull") and os.path.isfile(v["skull"]): return v["skull"]
    except Exception: pass
    lib = os.path.abspath(g.lib); p = path
    while p and os.path.abspath(p).startswith(lib) and len(os.path.abspath(p)) > len(lib):
        for sub in sorted(os.listdir(p)):
            full = os.path.join(p, sub)
            if os.path.isdir(full) and not sub.startswith("_") and kind_of(sub)[0] == "skull":
                imgs = sorted(f for f in os.listdir(full) if f.lower().endswith(IMG))
                if imgs: return os.path.join(full, imgs[0])
        p = os.path.dirname(p)
    return None

def mix_shirt(g, r):
    """(w, h, dxt5) of the generated skull shirt, cached by everything that goes into it"""
    skull = skull_image_of(g, r.get("skull_from", "")); td = templates_dir(g)
    tpl = os.path.join(td or "", r.get("shirt", ""))
    if not skull or not os.path.isfile(tpl):
        say("  mix: skull image or shirt background missing (%s / %s) - no skull shirt" % (r.get("skull_from"), r.get("shirt"))); return None
    fit = dict(x0=0.13, x1=0.83, top=0.19, aspect=0.55, bottom=0.90, extend=True, fabric=1.0); fit.update(r.get("fit") or {})
    key = hashlib.sha1(json.dumps([skull, os.path.getmtime(skull), tpl, os.path.getmtime(tpl), fit], sort_keys=True).encode()).hexdigest()[:16]
    cdir = os.path.join(g.cdir, "mix_cache"); os.makedirs(cdir, exist_ok=True); out = os.path.join(cdir, key + ".png")
    if not os.path.isfile(out):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import shirt_creator as sc
        tex, _ = sc.make(skull, tpl, 0.0, float(fit.pop("fabric")), 7, straighten=float(fit.get("straighten", 0) or 0),
                         **{k: fit[k] for k in ("x0", "x1", "top", "aspect", "bottom", "extend")})
        tex.save(out)
    return to_dxt5(out)

def apply_variants(g, t, base_tex, variants):
    """outfits on top of t: only what they really change (v0.4.17), later outfit wins a real conflict"""
    by = {}
    for v in variants:
        vt = load_variant(g, v); kept, same = [], 0
        for nm, val in vt.items():
            if nm in (base_tex or {}) and _looks_same(val, base_tex[nm]): same += 1; continue
            if nm in by and by[nm] != v:
                say("  outfits: %s and %s both change %s - %s wins" % (by[nm], v, nm, v))
            t[nm] = val; by[nm] = v; kept.append(nm)
        if vt:
            say("  outfit '%s': %d texture(s)%s" % (v, len(kept), (", %d left out (same as the base layer)" % same) if same else ""))
    return t

# ---------------------------------------------------------------- v0.4.19 PARTS + KITS (each picture stored once)
# <library>/Parts/<any name> (<slots>)/   slots in brackets at the end of the folder name:
#     (05) = texture 05 of BOTH models (pun_tshirt_05 + punps2_trench05)   (t07) = T-shirt only   (c06) = trench coat only
#     (c06-c11) or (t06, t09) = several textures: then each style is a SUBFOLDER whose files are named by number (06.png ...)
#   one style = one picture (any file name; the name is what you pick) or, for several textures, one subfolder.
# <library>/Skulls/<group>/<artist>.png      the skull: printed on the shirt (texture 04) and used as the in-game skull
# <library>/Shirt Backgrounds/<name>.png     shirt backgrounds for the skull shirt (falls back to templates/Body Armor)
# <library>/Kits/<group>/<name>.json         a kit = {"parts": {part folder: style}, "skull": "616/Klaus Janson.png",
#                                            "shirt": "Modern 2.png", "fit": {...}}; <name>.shirt.png beside it = the saved
#                                            skull shirt (kept until you rebuild it). Choice "kit: 616/Klaus Janson".
KIT_PREFIX = "kit: "
UNCOMPRESSED_DARK = True          # parts that are mostly dark are written uncompressed (32-bit) - smooth shading in black
UNCOMPRESSED_MAX = 512            # v0.4.20: largest side of an uncompressed texture (1024 x 1024 = 4 MB each)
PICKER_UNC_MAX = 1024             # v0.9.26: in-game picker pictures never enter the level memory -> no per-outfit budget
                                  # and a bigger cap (2048 x 1024 shirt front / back, 1024 x 1024 square) for smooth blacks
_PICKER = [False]                 # True while export_picker builds a costume (Apply keeps the budget + 512 cap)
UNCOMPRESSED_BUDGET_MB = 6        # v0.4.24: uncompressed textures per OUTFIT (one level loads one outfit). Tested: ~5.5 MB
                                  # loads, ~9.5 MB hangs the loading screen. Over the budget = normal DXT5, full size.
_UNC_PRIORITY = {"tshirt": [4, 7, 8, 5, 3, 12, 6, 9, 1, 2, 10, 11, 13], "trench": [4, 6, 7, 8, 9, 5, 3, 12, 1, 2, 10, 11]}
_SRC = {}                         # (kind, texture) -> (picture, number): to re-encode a texture that is over the budget
_PART_RX = re.compile(r"\(\s*([tc]?\s*\d{1,2}(?:\s*[-,]\s*[tc]?\s*\d{1,2})*)\s*\)\s*$", re.I)

def is_kit(name): return str(name).startswith(KIT_PREFIX)

def part_slots(folder):
    """folder name -> [(model, number)], model 'both' / 'tshirt' / 'trench'; [] = not a part folder"""
    m = _PART_RX.search(folder)
    if not m: return []
    out = []
    for tok in re.split(r"\s*,\s*", m.group(1).strip()):
        mm = re.match(r"([tc]?)\s*(\d{1,2})(?:\s*-\s*([tc]?)\s*(\d{1,2}))?$", tok, re.I)
        if not mm: continue
        model = {"t": "tshirt", "c": "trench", "": "both"}[mm.group(1).lower()]
        a = int(mm.group(2)); b = int(mm.group(4)) if mm.group(4) else a
        out += [(model, n) for n in range(min(a, b), max(a, b) + 1) if n in OK_NUMBERS]
    return out

def slot_textures(model, n):
    names = []
    if model in ("both", "tshirt"): names.append(("tshirt", SETS["tshirt"][1] % n))
    if model in ("both", "trench"): names.append(("trench", SETS["trench"][1] % n))
    return names

# v0.8.9: the PC version's three exclusive T-shirt outfits (unlocked by a main-menu code in the retail game) live in
# misc_pc.vpp as pun_tshirt_eb / _gs / _wm.ceg: 13 DXT5 textures each (pun_<xx>tshirt_NN_x.tga). 02, 10-13 are the same
# in all three; 01, 03-09 differ. "Add the PC-exclusive T-shirts" saves those 8 per set as T-shirt-only part styles,
# straight from the player's own game files.
PC_EXCLUSIVE_SETS = (("eb", "PC Exclusive - EB"), ("gs", "PC Exclusive - GS"), ("wm", "PC Exclusive - WM"))
PC_EXCLUSIVE_SLOTS = {1: "Face, Belt & Boots", 3: "Neck & Collar", 4: "T-shirt Front", 5: "Pants & Gloves",
                      6: "Strap", 7: "T-shirt Back", 8: "Sleeves", 9: "Thigh Holsters"}

def _dds_dxt5(w, h, data):
    hdr = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x1007 | 0x80000, h, w, len(data), 0, 1, b"\0" * 44,
                      32, 4, 0x35545844, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    return hdr + data

def extract_pc_exclusives(g):
    """save the PC-exclusive T-shirt pieces into <library>/Parts (T-shirt-only folders); never overwrites a file"""
    from PIL import Image
    import io
    src = g.archives.get("misc_pc")
    if not src or not os.path.isfile(src):
        say("misc_pc.vpp not found in the game folder - the PC-exclusive T-shirts come from it (the PC version only)."); return 0
    d = open(src, "rb").read(); _, _, ents, _ = parse_vpp(d)
    by = {e["name"].lower(): e for e in ents}
    parts = list_parts(g); made = skipped = 0
    for code, style in PC_EXCLUSIVE_SETS:
        e = by.get("pun_tshirt_%s.ceg" % code)
        if not e: say("  %s: pun_tshirt_%s.ceg is not in misc_pc.vpp - skipped" % (style, code)); continue
        b = inner(d, e); n, ent = bank_dir(b); order = sorted(range(n), key=lambda i: ent[i][0])
        for k, i in enumerate(order):
            m = re.search(r"_(\d\d)_x\.tga$", tname(ent[i][4]))
            num = int(m.group(1)) if m else 0
            if num not in PC_EXCLUSIVE_SLOTS: continue
            off, w, h = ent[i][0], ent[i][1], ent[i][2]
            end = ent[order[k + 1]][0] if k + 1 < n else len(b)
            if ent[i][3][0] != 0x0F: say("  %s %02d: not DXT5 - skipped" % (style, num)); continue
            im = Image.open(io.BytesIO(_dds_dxt5(w, h, b[off:off + max(16, w * h)]))).convert("RGBA")
            im.putalpha(255)                                   # 01-09 are solid textures
            folder = next((f for f, inf in parts.items() if inf["slots"] == [("tshirt", num)]), None) \
                or "%s (t%02d)" % (PC_EXCLUSIVE_SLOTS[num], num)
            dst = os.path.join(g.lib, "Parts", folder, style + ".png")
            if os.path.exists(dst): skipped += 1; continue
            os.makedirs(os.path.dirname(dst), exist_ok=True); im.save(dst); made += 1
        say("  %s: done" % style)
    say("PC-exclusive T-shirts: %d pictures added to Parts%s. They are T-shirt-only styles named 'PC Exclusive - EB / GS / WM'"
        " (128-256 px, the game's own size)." % (made, (", %d already there (not changed)" % skipped) if skipped else ""))
    return made

def list_parts(g):
    """{part folder: {"slots": [...], "styles": {style: path}}}  (path = file, or folder for several textures)"""
    root = os.path.join(g.lib, "Parts"); out = {}
    if not os.path.isdir(root): return out
    for f in sorted(os.listdir(root), key=str.lower):
        full = os.path.join(root, f); slots = part_slots(f)
        if not os.path.isdir(full) or f.startswith(("_", ".")) or not slots: continue
        styles = {}
        for e in sorted(os.listdir(full), key=str.lower):
            p = os.path.join(full, e)
            if e.startswith(("_", ".")): continue
            if len(slots) == 1 and os.path.isfile(p) and e.lower().endswith(IMG): styles[os.path.splitext(e)[0]] = p
            elif os.path.isdir(p): styles[e] = p
        out[f] = {"slots": slots, "styles": styles}
    return out

def _encode(path, n, _cache={}):
    """a picture -> (w, h, data, format): uncompressed 32-bit for mostly dark pictures (if on), else DXT5"""
    cap = PICKER_UNC_MAX if _PICKER[0] else UNCOMPRESSED_MAX          # v0.9.26
    key = (path, os.path.getsize(path), int(os.path.getmtime(path)), UNCOMPRESSED_DARK, cap)
    if key in _cache: return _cache[key]
    from PIL import Image
    tkind = "alpha" if n >= 11 else "opaque"
    r = None
    if UNCOMPRESSED_DARK:
        im = Image.open(path).convert("RGBA")
        lum = list(im.convert("L").resize((128, 128)).getdata())
        # v0.4.23: "dark" = the FABRIC is near-black, judged by the share of near-black pixels - a white skull print or white
        # wraps no longer push a navy shirt front / sleeve over the line (the old test used the whole picture's average)
        dark_share = sum(1 for v in lum if v < 40) / float(len(lum))
        if dark_share >= 0.45 or sum(lum) / len(lum) < 48:             # near-black fabric: DXT5 would band it
            w, h = im.size
            # memory cap by PIXELS: the setting's square (512 x 512) or a tall / wide piece of the same width
            # (512 x 1024 shirt front / back stay full size); larger pictures are halved until they fit
            p2 = lambda v: 1 << max(2, int(v).bit_length() - 1)          # v0.4.25: round DOWN to a power of two (1000 -> 512),
            w, h = p2(w), p2(h)                                           # as the DXT5 path does; the old engine wants them
            while w * h > 2 * cap * cap or max(w, h) > 2 * cap: w, h = w // 2, h // 2
            if (w, h) != im.size: im = im.resize((w, h), Image.LANCZOS)
            if tkind == "opaque": im.putalpha(255)
            r = (w, h, im.tobytes("raw", "BGRA"), 0x07)
    if r is None:
        fill = ALPHA_FILL.get("tshirt", {}).get(n) if tkind == "alpha" else None
        w, h, data = to_dxt5(path, tkind, fill); r = (w, h, data, 0x0F)
    _cache[key] = r; return r

def part_textures(g, part, style, parts=None):
    """the textures one part style writes: {texture name: (kind, w, h, data, format)}"""
    parts = parts or list_parts(g); p = parts.get(part)
    if not p or style not in p["styles"]:
        say("  warning: part '%s' / '%s' not found - left out" % (part, style)); return {}
    src = p["styles"][style]; out = {}
    for model, n in p["slots"]:
        if os.path.isfile(src): f = src
        else:
            f = next((os.path.join(src, x) for x in sorted(os.listdir(src)) if x.lower().endswith(IMG) and texture_number(os.path.splitext(x)[0]) == n), None)
        if not f:
            say("  note: %s / %s has no picture for texture %02d" % (part, style, n)); continue
        enc = _encode(f, n)
        for kind, tex in slot_textures(model, n):
            out[tex] = (kind,) + enc; _SRC[(kind, tex)] = (f, n)
    return out

def choice_exists(g, name):
    if name == DEFAULT: return True
    if is_kit(name): return load_kit(g, name) is not None
    if is_mix(name): return load_mix(g, name) is not None
    return split_name(name)[0] in g.costumes()

def orange_skull(g, path):
    """v0.4.21: the orange (environmental kill) skull made from a skull picture, in the game's own orange: the picture's
    light / dark becomes the original's shades (dark spots ~83,45,6 - body ~158,89,12 - light ~187,115,15)"""
    from PIL import Image
    import numpy as np
    key = hashlib.sha1(("%s|%s|v2" % (path, os.path.getmtime(path))).encode()).hexdigest()[:16]
    cdir = os.path.join(g.cdir, "mix_cache"); os.makedirs(cdir, exist_ok=True); out = os.path.join(cdir, "orange_%s.png" % key)
    if os.path.isfile(out): return out
    im = Image.open(path).convert("RGBA"); a = np.asarray(im).astype(np.float32)
    lum = a[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    xs = [0, 60, 110, 170, 230, 255]
    o = np.zeros_like(a)
    for c, ys in enumerate(([30, 70, 110, 150, 170, 180], [15, 38, 60, 85, 98, 106], [2, 5, 8, 11, 13, 14])):
        o[..., c] = np.interp(lum, xs, ys)
    o[..., 3] = a[..., 3]
    Image.fromarray(o.clip(0, 255).astype(np.uint8), "RGBA").save(out); return out

# ---------------------------------------------------------------- v0.4.22: old costume folder -> parts + skull + kit
SLOT_NAMES = {   # (model, number) -> body part name for new part folders (from the in-game colour test, Nov 3)
    ("both", 1): "Face, Belt & Boots", ("both", 2): "Part 02", ("both", 3): "Neck & Collar", ("both", 4): "Shirt Front",
    ("both", 5): "Pants & Gloves", ("both", 12): "Hair", ("both", 10): "Detail 10", ("both", 11): "Detail 11", ("both", 13): "Detail 13",
    ("tshirt", 6): "T-shirt 06", ("tshirt", 7): "T-shirt Back", ("tshirt", 8): "T-shirt Sleeves", ("tshirt", 9): "Thigh Holsters",
    ("trench", 6): "Coat Back", ("trench", 7): "Coat Sleeves & Front", ("trench", 8): "Coat Skirt", ("trench", 9): "Coat Shoulders",
}

def slot_code(model, n):
    return {"both": "", "tshirt": "t", "trench": "c"}[model] + "%02d" % n

def slot_label(model, n):
    base = SLOT_NAMES.get((model, n)) or SLOT_NAMES.get(("both", n)) or "Texture %02d" % n
    if model == "tshirt" and ("both", n) in SLOT_NAMES: base = "T-shirt " + base
    if model == "trench" and ("both", n) in SLOT_NAMES: base = "Coat " + base
    return base

def part_folder_for(g, model, n, create=True):
    """the part folder that holds exactly this one slot (an existing one, whatever its name), else a new one"""
    root = os.path.join(g.lib, "Parts")
    for f, info in list_parts(g).items():
        if info["slots"] == [(model, n)]: return os.path.join(root, f)
    p = os.path.join(root, "%s (%s)" % (slot_label(model, n), slot_code(model, n)))
    if create: os.makedirs(p, exist_ok=True)
    return p

def _same_picture(a, b):
    from PIL import Image
    import numpy as np
    try:
        A, B = Image.open(a).convert("RGBA"), Image.open(b).convert("RGBA")
        if A.size != B.size: return False
        return float(np.abs(np.asarray(A, np.int16) - np.asarray(B, np.int16)).mean()) < 1.0
    except Exception:
        return False

def convert_costume(g, name, dry=False):
    """an old costume folder -> one picture per part (shared slots stored once), its skull, a kit with its own shirt
    front saved as the kit's skull shirt. Nothing of the old costume is changed or removed. Returns the kit name."""
    from PIL import Image
    path = costume_dirs(g.lib).get(name)
    if not path: say("  %s: folder not found" % name); return None
    sets = build_mapping(g, name, path)
    files = {"tshirt": {}, "trench": {}, "skull": {}}
    for sub, st in sets.items():
        for f, v in st["files"].items():
            if not v["texture"]: continue
            full = os.path.join(path, sub, f)
            if st["kind"] == "skull": files["skull"][v["texture"]] = full; continue
            m = re.search(r"(\d+)\.tga$", v["texture"])
            if m: files[st["kind"]][int(m.group(1))] = full
    parts_ = name.split(" / "); group = parts_[0] if len(parts_) > 1 else "Converted"
    style = " - ".join(parts_[1:]) if len(parts_) > 1 else name
    kit = {"parts": {}, "skull": "", "shirt": "", "fit": {}}
    def put(model, n, src):
        d = part_folder_for(g, model, n, create=not dry); ext = os.path.splitext(src)[1].lower()
        dst = os.path.join(d, style + ext); k = 2
        while os.path.isfile(dst) and not _same_picture(dst, src): dst = os.path.join(d, "%s (%d)%s" % (style, k, ext)); k += 1
        if not dry and not os.path.isfile(dst): shutil.copy2(src, dst)
        kit["parts"][os.path.basename(d)] = os.path.splitext(os.path.basename(dst))[0]
    nums = sorted(set(files["tshirt"]) | set(files["trench"]))
    for n in nums:
        if n == 4: continue                                   # the shirt front becomes the kit's saved skull shirt
        t, c = files["tshirt"].get(n), files["trench"].get(n)
        if t and c and _same_picture(t, c): put("both", n, t)
        else:
            if t: put("tshirt", n, t)
            if c: put("trench", n, c)
    sk = files["skull"].get("skull.tga") or next(iter(files["skull"].values()), None)
    if sk:
        rel = "%s/%s.png" % (group, style); dst = os.path.join(g.lib, "Skulls", group, style + ".png")
        if not dry:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.isfile(dst): Image.open(sk).convert("RGBA").save(dst)
        kit["skull"] = rel
    kname = "%s/%s" % (group, style); kp = kit_file(g, KIT_PREFIX + kname)
    front = files["tshirt"].get(4) or files["trench"].get(4)
    if not dry:
        os.makedirs(os.path.dirname(kp), exist_ok=True)
        save_json(kp, kit, indent=1)
        if front: Image.open(front).convert("RGBA").save(os.path.splitext(kp)[0] + ".shirt.png")
    say("  %s -> kit %s (%d parts%s%s)" % (name, kname, len(kit["parts"]), ", skull" if sk else "", ", its shirt front" if front else ""))
    return KIT_PREFIX + kname

def _apply_budget(t):
    """keep at most UNCOMPRESSED_BUDGET_MB of uncompressed textures per outfit, the most visible ones first;
    the rest go back to DXT5 at full size"""
    if _PICKER[0]: return t                                      # v0.9.26: the in-game picker has no level-memory budget
    budget = UNCOMPRESSED_BUDGET_MB * 1048576; over = []
    for kind, order in _UNC_PRIORITY.items():
        unc = [nm for nm, v in t.items() if v[0] == kind and len(v) > 4 and v[4] == 0x07]
        rank = lambda nm: order.index(texture_number(os.path.splitext(nm)[0])) if texture_number(os.path.splitext(nm)[0]) in order else 99
        used = 0
        for nm in sorted(unc, key=rank):
            size = len(t[nm][3])
            if used + size <= budget: used += size; continue
            src = _SRC.get((kind, nm))
            if not src: continue
            f, n = src; tkind = "alpha" if n >= 11 else "opaque"
            fill = ALPHA_FILL.get("tshirt", {}).get(n) if tkind == "alpha" else None
            w, h, data = to_dxt5(f, tkind, fill); t[nm] = (kind, w, h, data, 0x0F); over.append(nm.replace(".tga", ""))
    if over: say("  smooth blacks: over the %d MB budget per outfit, written normally: %s" % (UNCOMPRESSED_BUDGET_MB, ", ".join(sorted(over))))
    return t

def kit_file(g, name): return os.path.join(g.lib, "Kits", *str(name)[len(KIT_PREFIX):].split("/")) + ".json"

def load_kit(g, name):
    try: return json.load(open(kit_file(g, name), encoding="utf-8"))
    except Exception: return None

def shirt_backgrounds_dir(g):
    d = os.path.join(g.lib, "Shirt Backgrounds")
    return d if os.path.isdir(d) and any(f.lower().endswith(".png") for f in os.listdir(d)) else templates_dir(g)

def make_shirt(g, skull, shirt, fit, out=None, warp="tshirt"):
    """render the skull shirt (PNG path). out = where to keep it (a kit's saved shirt), else the cache"""
    tpl = os.path.join(shirt_backgrounds_dir(g) or "", shirt or "")
    if not skull or not os.path.isfile(skull) or not os.path.isfile(tpl):
        say("  skull shirt: skull image or shirt background missing (%s / %s)" % (skull, shirt)); return None
    f = dict(x0=0.13, x1=0.83, top=0.19, aspect=0.55, bottom=0.90, extend=True, fabric=1.0); f.update(fit or {})
    if not out:
        wk = [] if warp == "tshirt" else [warp, os.path.getmtime(os.path.join(os.path.dirname(os.path.abspath(__file__)), warp + "_warp.json"))
                                          if os.path.isfile(os.path.join(os.path.dirname(os.path.abspath(__file__)), warp + "_warp.json")) else 0]
        key = hashlib.sha1(json.dumps([skull, os.path.getmtime(skull), tpl, os.path.getmtime(tpl), f] + wk, sort_keys=True).encode()).hexdigest()[:16]
        cdir = os.path.join(g.cdir, "mix_cache"); os.makedirs(cdir, exist_ok=True); out = os.path.join(cdir, key + ".png")
        if os.path.isfile(out): return out
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import shirt_creator as sc
    tex, _ = sc.make(skull, tpl, 0.0, float(f["fabric"]), 7, straighten=float(f.get("straighten", 0) or 0), warp=warp,
                     **{k: f[k] for k in ("x0", "x1", "top", "aspect", "bottom", "extend")})
    tex.save(out); return out

# v0.9.4 (user: the trench coat's skull has no fabric look): the coat model lights its shirt front much brighter than
# the T-shirt model - Deck screen: 99 % of the coat's skull pixels at 255, the same print on the T-shirt ~117 with the
# fabric shading visible. So the shading was there but blown out to flat white. The coat gets its own copy with the
# bright parts toned down (dark fabric < 110 unchanged): v' = 110 + (v - 110) * COAT_TONE. 1.0 = as before.
COAT_TONE = 0.7                    # white 224 -> 190; retail coat print peaks at 238, so this is a little darker than retail

def coat_tone(g, png):
    if COAT_TONE >= 0.999: return png
    try:
        from PIL import Image
        import numpy as np
        cdir = os.path.join(g.cdir, "mix_cache"); os.makedirs(cdir, exist_ok=True)
        key = hashlib.sha1(json.dumps([png, os.path.getmtime(png), COAT_TONE]).encode()).hexdigest()[:16]
        out = os.path.join(cdir, "coat_" + key + ".png")
        if not os.path.isfile(out):
            a = np.asarray(Image.open(png).convert("RGB"), np.float32)
            a = np.where(a > 110, 110 + (a - 110) * COAT_TONE, a)
            Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), "RGB").save(out)
        return out
    except Exception as ex:
        say("  coat print tone: %s - the coat gets the T-shirt picture" % ex); return png

def kit_skull(g, r):
    return os.path.join(g.lib, "Skulls", *r["skull"].split("/")) if r.get("skull") else None

def kit_textures(g, name, base_tex):
    r = load_kit(g, name)
    if not r: say("  warning: kit '%s' not found - treated as Default" % name); return {}
    t = dict(base_tex or {}); parts = list_parts(g); covered = set()
    for part, style in (r.get("parts") or {}).items():
        if not style: continue
        pt = part_textures(g, part, style, parts); t.update(pt); covered |= set(pt)
    free = [(k, nm) for k, nm in (("tshirt", "pun_tshirt_04.tga"), ("trench", "punps2_trench04.tga")) if nm not in covered]
    if free and r.get("skull"):                                    # a chosen shirt-front part wins over the skull shirt
        saved = os.path.splitext(kit_file(g, name))[0] + ".shirt.png"
        try:
            png = saved if os.path.isfile(saved) else make_shirt(g, kit_skull(g, r), r.get("shirt"), r.get("fit"))
        except Exception as ex:                                  # v0.9.20 (review): one bad kit must not stop Apply
            say("  warning: kit '%s': the skull shirt could not be made (%s) - its shirt front stays as it was" % (name[len(KIT_PREFIX):], ex)); png = None
        if png:
            enc = _encode(png, 4)
            coat_png = png
            if any(k == "trench" for k, _ in free) and float((r.get("fit") or {}).get("straighten", 0) or 0) > 0:
                # v0.9.6: the coat bends differently (its own measured map) - render its copy with coat_warp.json
                try: coat_png = make_shirt(g, kit_skull(g, r), r.get("shirt"), r.get("fit"), warp="coat") or png
                except Exception as ex: say("  warning: coat skull shirt: %s - the coat uses the T-shirt picture" % ex); coat_png = png
            for k, nm in free:
                use = coat_tone(g, coat_png) if k == "trench" else png         # v0.9.4
                e = enc if use == png else _encode(use, 4)
                t[nm] = (k,) + e; _SRC[(k, nm)] = (use, 4)
    t = _apply_budget(t)
    unc = [(nm, v[1], v[2]) for nm, v in t.items() if len(v) > 4 and v[4] == 0x07]
    allmb = sum(len(v[3]) for v in t.values()) / 1048576.0
    say("  kit '%s': %d textures, %.1f MB%s" % (name[len(KIT_PREFIX):], len(t), allmb,
        ("; uncompressed: " + ", ".join("%s %dx%d" % (n.replace(".tga", ""), w, h) for n, w, h in sorted(unc))) if unc else ""))
    return t

def costume_textures(g, name, base_tex=None):
    """textures to write for a costume: the base layer (if any), the costume's own textures, then its outfit variants"""
    if name == DEFAULT: return {}
    if is_kit(name): return kit_textures(g, name, base_tex)
    if is_mix(name):
        r = load_mix(g, name)
        if not r: say("  warning: mix '%s' not found - treated as Default" % name); return {}
        t = dict(base_tex or {}); sh = mix_shirt(g, r)
        if sh: t["pun_tshirt_04.tga"] = ("tshirt",) + sh; t["punps2_trench04.tga"] = ("trench",) + sh
        return apply_variants(g, t, base_tex, r.get("outfits", []))
    base, variants = split_name(name)
    own = load_costume(g, base)
    if is_skull_only(name):
        own = {k: v for k, v in own.items() if texture_number(os.path.splitext(k)[0]) == 4}
    t = dict(base_tex or {}); t.update(own)
    return apply_variants(g, t, base_tex, variants)

def _looks_same(a, b, _cache={}):
    """two prepared textures (kind, w, h, dxt5 data) show the same picture (small DXT differences allowed)"""
    if a[1:3] != b[1:3]: return False
    if a[3] == b[3]: return True
    key = (hash(a[3][:4096]), hash(b[3][:4096]), a[1], a[2])
    if key in _cache: return _cache[key]
    try:
        import numpy as np
        from PIL import Image
        w, h = a[1], a[2]; n = max(1, w // 4) * max(1, h // 4) * 16
        ia = np.asarray(Image.frombytes("RGBA", (w, h), bytes(a[3][:n]), "bcn", 3), np.int16)
        ib = np.asarray(Image.frombytes("RGBA", (w, h), bytes(b[3][:n]), "bcn", 3), np.int16)
        r = float(np.abs(ia[..., :3] - ib[..., :3]).mean()) < 4.0
    except Exception:
        r = False
    _cache[key] = r; return r


def load_base(g):
    b = g.costume_of(BASE_KEY)
    if b == DEFAULT: return {}
    if b not in g.costumes():
        say("  warning: base layer '%s' not found - ignored" % b); return {}
    return load_costume(g, b)


def verify(g):
    """for every mission with a costume: which of the costume's textures are really in the level files, and which are not and why"""
    any_ = False; sets = {"trench": "trench coat", "tshirt": "T-shirt"}; counts = {"trench": 12, "tshirt": 13}; base_tex = load_base(g)
    for mission, stems in g.missions:
        name = g.costume_of(mission)
        if name == DEFAULT: continue
        if not choice_exists(g, name):                        # v0.4.21: a removed costume / kit no longer crashes Check
            say("%s -> %s: NOT FOUND in the library (pick another costume for it)" % (mission, name)); continue
        any_ = True; tex = costume_textures(g, name, base_tex); kinds = set()
        say("%s -> %s" % (mission, name))
        for stem in stems:
            models = g.models(stem)
            if not models: continue
            kinds |= models
            d = open(g.archives[stem], "rb").read(); count, total, ents, _ = parse_vpp(d); found = {}
            for e in ents:
                if not e["name"].lower().endswith(".ceg"): continue
                try: b = inner(d, e)
                except Exception: continue
                if b[:4] != b"GEKV": continue
                for nm, val in stored_bank(b).items(): found.setdefault(nm, val)
            ok, bad, absent = [], [], []
            for nm, v in sorted(tex.items()):
                kind, w, h, data = v[:4]
                if kind not in models: continue
                if nm not in found: absent.append(nm)
                elif found[nm] == (w, h, data): ok.append(nm)
                else: bad.append(nm)
            say("  %-12s %d of %d costume textures in place (%s)" % (stem, len(ok), len(ok) + len(bad) + len(absent), " + ".join(sets[m] for m in sorted(models))))
            if bad: say("      NOT applied yet: " + ", ".join(n.replace(".tga", "") for n in bad) + "   -> run apply")
            if absent: say("      not in this level file: " + ", ".join(n.replace(".tga", "") for n in absent))
        for kind in sorted(kinds):
            have = {n for n, v in tex.items() if v[0] == kind}
            if not have: say("  this costume has no %s textures (they stay original)" % sets[kind]); continue
            left = [SETS[kind][1] % i for i in range(1, counts[kind] + 1) if (SETS[kind][1] % i) not in have]
            if left: say("  %s textures this costume does not cover (they stay original): %s" % (sets[kind], ", ".join(n.replace(".tga", "").replace("punps2_", "").replace("pun_", "") for n in left)))
    if not any_: say("No mission uses a costume yet - nothing to verify. (set \"<mission>\" \"<costume>\", then apply)")


# ---------------------------------------------------------------- v0.9.24: in-game picker (Extended Cut plugin v1.12+)
# The plugin reads <folder of ExtendedCut.asi>\Costumes\<costume>\pun_tshirt_NN.dds / punps2_trenchNN.dds and swaps them
# in while the game runs (Extended menu -> Gameplay -> Costume). These write the same pictures Apply would write.
def _dds(w, h, data, fmt):
    if fmt == 0x07:                                            # uncompressed 32-bit, BGRA bytes = A8R8G8B8
        hdr = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x100F, h, w, w * 4, 0, 1, b"\0" * 44,
                          32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x1000, 0, 0, 0, 0)
        return hdr + data
    return _dds_dxt5(w, h, data)


def picker_dir(g):
    """the Costumes folder next to ExtendedCut.asi (scripts or scripts/Extended Cut)"""
    for sub in (os.path.join("scripts", "Extended Cut"), "scripts"):
        if os.path.isfile(os.path.join(g.root, sub, "ExtendedCut.asi")): return os.path.join(g.root, sub, "Costumes")
    return os.path.join(g.root, "scripts", "Extended Cut", "Costumes")


def picker_name(name):
    n = str(name)
    for pre in (KIT_PREFIX, MIX_PREFIX):
        if n.startswith(pre): n = n[len(pre):]
    n = n.replace("/", " - ").replace("\\", " - ")
    n = re.sub(r'[<>:"|?*]', "", n).strip(" .")
    return n or "Costume"


def picker_skulls(g, name):
    """v0.9.26: a choice's in-game skulls for the picker (plugin v1.19 'In-game skulls: Follow costume'):
    skull01.dds = clean (menus, loading screen, score), skull01_worn.dds = worn (skull over enemies, interrogation /
    special-kill hotspots), skull_orng.dds = the orange hotspot skull (worn), meters_skull.dds = HUD meter skull.
    Same pictures Apply makes; [] when the choice has no skull of its own."""
    return skull_files(g, skull_sources(g, name))


def skull_files(g, srcs):
    """{'skull.tga': path[, 'skull_orng.tga': path]} -> the picker's skull .dds files (see picker_skulls)"""
    srcs = dict(srcs or {})
    if not srcs.get("skull.tga"): return []
    from PIL import Image
    if "skull_orng.tga" not in srcs: srcs["skull_orng.tga"] = srcs["skull.tga"]
    clean = decal_rgba(srcs["skull.tga"]); hot = clean; orng = decal_rgba(srcs["skull_orng.tga"])
    gen_orange = os.path.basename(srcs.get("skull_orng.tga") or "").startswith("orange_") or srcs.get("skull_orng.tga") == srcs.get("skull.tga")
    if WORN_SKULLS and gen_orange:
        worn = worn_skulls(g, srcs["skull.tga"])
        if worn: hot = decal_rgba(worn[0]); orng = decal_rgba(worn[1])
    out = [("skull01.dds", _dds(512, 512, dxt5_bytes(clean, 512, 512), 0x0F)),
           ("skull01_worn.dds", _dds(512, 512, dxt5_bytes(hot, 512, 512), 0x0F)),
           ("skull_orng.dds", _dds(512, 512, dxt5_bytes(orng, 512, 512), 0x0F))]
    # HUD meter skull: the game shows 28 x 41; the plugin draws the whole picture, so it fills the 32 x 64 texture
    out.append(("meters_skull.dds", _dds(32, 64, dxt5_bytes(clean.resize((28, 41), Image.LANCZOS), 32, 64), 0x0F)))
    return out


def export_picker(g, name, folder=None, dry=False):
    """one library choice (costume, kit, mix, variant) -> Costumes/<name>/*.dds for the in-game picker"""
    _PICKER[0] = True                                          # v0.9.26: full smooth blacks (no budget, bigger cap)
    try: tex = costume_textures(g, name, load_base(g))
    finally: _PICKER[0] = False
    pick = {nm: v for nm, v in tex.items() if nm.lower().startswith(("pun_tshirt_", "punps2_trench"))}
    if not pick:
        say("  %s: no T-shirt / coat pictures - skipped" % name); return 0
    out = os.path.join(picker_dir(g), folder or picker_name(name))
    if dry:
        say("  would write %d pictures to %s" % (len(pick), out)); return len(pick)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):                                  # the folder holds exactly this costume
        if f.lower().endswith(".dds"):
            try: os.remove(os.path.join(out, f))
            except OSError: pass
    mb = 0
    for nm, v in sorted(pick.items()):
        w, h, data = v[1], v[2], v[3]; fmt = v[4] if len(v) > 4 and v[4] is not None else 0x0F
        b = _dds(w, h, data, fmt); mb += len(b)
        with open(os.path.join(out, os.path.splitext(nm)[0].lower() + ".dds"), "wb") as fh: fh.write(b)
    tee = sum(1 for nm in pick if nm.lower().startswith("pun_tshirt_")); coat = len(pick) - tee
    try: sk = picker_skulls(g, name)                           # v0.9.26: its in-game skulls too
    except Exception as ex: sk = []; say("  %s: in-game skulls could not be made (%s)" % (name, ex))
    for f, b in sk:
        mb += len(b)
        with open(os.path.join(out, f), "wb") as fh: fh.write(b)
    say("  %s -> Costumes/%s: %d T-shirt + %d coat pictures%s, %.1f MB" % (name, os.path.basename(out), tee, coat, ", in-game skulls" if sk else "", mb / 1048576.0))
    return len(pick)


def export_picker_used(g, dry=False):
    """every choice picked for a level -> the in-game picker"""
    used = []
    for m, _ in g.missions:
        c = g.costume_of(m)
        if c != DEFAULT and c not in used: used.append(c)
    if not used:
        say("No level has a costume picked yet - pick one (or 'Use for the whole game') first.")
        return export_pc_exclusive(g, dry)                     # v0.9.25: the PC Exclusive skins still go across
    say("In-game picker: %d costume(s) -> %s" % (len(used), picker_dir(g)))
    n = 0
    for c in used:
        try:
            if export_picker(g, c, dry=dry): n += 1
        except Exception as ex:
            say("  %s: could not be written (%s)" % (c, ex))
    n += export_pc_exclusive(g, dry)                         # v0.9.25
    say("Done: %d costume(s). In the game: Extended menu -> Costume tab ('Reload costume files' if the game is running)." % n)
    return n


def library_choices(g):
    """v0.9.25: every choice in the library: kits, costumes (not the game's own), mixes"""
    kits = []
    root = os.path.join(g.lib, "Kits")
    for dp, dn, fs in os.walk(root):
        dn[:] = [d for d in dn if not d.startswith(("_", "."))]
        for f in fs:
            if f.endswith(".json"): kits.append(os.path.relpath(os.path.join(dp, f[:-5]), root).replace(os.sep, "/"))
    md = os.path.join(g.lib, "_Mixes")
    mixes = sorted(os.path.splitext(f)[0] for f in os.listdir(md) if f.endswith(".json")) if os.path.isdir(md) else []
    return ([KIT_PREFIX + k for k in sorted(kits, key=str.lower)] + [c for c in g.costumes() if c != DEFAULT] +
            [MIX_PREFIX + m for m in sorted(mixes, key=str.lower)])


PC_EXCLUSIVE = (("gs", "PC Exclusive - 1980"), ("eb", "PC Exclusive - 1990"), ("wm", "PC Exclusive - 2000"))


def export_pc_exclusive(g, dry=False):
    """v0.9.25: the game's three PC-exclusive T-shirt skins (misc_pc.vpp, pun_tshirt_gs / eb / wm) -> in-game picker
    folders. Made from the player's own game files, never shipped."""
    p = g.archives.get("misc_pc")
    if not p:
        say("  PC Exclusive skins: misc_pc.vpp not found - skipped"); return 0
    d = open(p, "rb").read(); c, t, ents, _ = parse_vpp(d); by = {e["name"].lower(): e for e in ents}
    n = 0
    for code, folder in PC_EXCLUSIVE:
        e = by.get("pun_tshirt_%s.ceg" % code)
        if not e: continue
        bank = inner(d, e); cnt, ent = bank_dir(bank)
        out = os.path.join(picker_dir(g), folder)
        pics = []
        for off, w, h, fm, raw in ent:
            m = re.match(r"pun_%stshirt_(\d\d)_x\.tga$" % code, tname(raw).lower())
            if m and (fm[0] if isinstance(fm, (bytes, bytearray)) else fm) == 0x0F:
                pics.append(("pun_tshirt_%s.dds" % m.group(1), _dds(w, h, bank[off:off + w * h], 0x0F)))
        if dry:
            say("  would write %d pictures to %s" % (len(pics), out)); n += 1; continue
        sk = pc_exclusive_skull(g, folder)                     # v0.9.27: its own skull from the library, if there is one
        if sk:
            try: pics += skull_files(g, {"skull.tga": sk})
            except Exception as ex: say("  %s: skull could not be made (%s)" % (folder, ex)); sk = None
        os.makedirs(out, exist_ok=True)
        for f, b in pics:
            with open(os.path.join(out, f), "wb") as fh: fh.write(b)
        say("  %s: 13 T-shirt pictures (from your misc_pc.vpp)%s" % (folder, ", skulls from " + os.path.basename(sk) if sk else "")); n += 1
    return n


def pc_exclusive_skull(g, folder):
    """v0.9.27: the library skull picture for a PC Exclusive skin: any .png under <library>/Skulls whose name has the
    year and "PC Exclusive" (e.g. "Mike Zeck - 1980 PC Exclusive.png"); None = keep Apply's skulls"""
    year = folder.split(" - ")[-1]
    root = os.path.join(g.lib, "Skulls"); hits = []
    for dp, dn, fs in os.walk(root):
        dn[:] = [d for d in dn if not d.startswith(("_", "."))]
        for f in fs:
            lo = f.lower()
            if lo.endswith(".png") and year in lo and "pc exclusive" in lo: hits.append(os.path.join(dp, f))
    return sorted(hits)[0] if hits else None


# ---------------------------------------------------------------- v0.9.28: send only what changed
# Every library choice gets a fingerprint of everything it is built from (its recipe / folder, the part pictures, the
# skull, the shirt background, the base layer, the picker settings). Costumes\_sent.json keeps the fingerprint of the
# last send; "Send changes to the game" rebuilds only choices whose fingerprint differs (or whose folder is missing),
# and removes the folders of choices that are gone from the library.
SENT_FILE = "_sent.json"


def _sig_paths(paths):
    h = hashlib.sha1()
    for p in paths:
        if not p: continue
        if os.path.isfile(p):
            st = os.stat(p); h.update(("%s|%d|%d\n" % (os.path.basename(p).lower(), st.st_size, int(st.st_mtime))).encode())
        elif os.path.isdir(p):
            for dp, dn, fs in os.walk(p):
                dn.sort()
                for f in sorted(fs):
                    q = os.path.join(dp, f); st = os.stat(q)
                    h.update(("%s|%d|%d\n" % (os.path.relpath(q, p).lower(), st.st_size, int(st.st_mtime))).encode())
        else:
            h.update(("missing:%s\n" % p).encode())
    return h.hexdigest()


def choice_sig(g, name):
    """fingerprint of everything a library choice is built from"""
    paths = []
    if is_kit(name):
        kf = kit_file(g, name); paths += [kf, os.path.splitext(kf)[0] + ".shirt.png"]
        r = load_kit(g, name) or {}; parts = list_parts(g)
        for part, style in sorted((r.get("parts") or {}).items()):
            if style and part in parts: paths.append(parts[part]["styles"].get(style))
        paths.append(kit_skull(g, r))
        if r.get("shirt"): paths.append(os.path.join(shirt_backgrounds_dir(g) or "", r["shirt"]))
    elif is_mix(name):
        mf = mix_file(g, name); paths.append(mf)
        r = load_mix(g, name) or {}; dirs = costume_dirs(g.lib)
        for v in [r.get("skull_from")] + list(r.get("outfits") or []):
            if v: paths.append(dirs.get(split_name(v)[0]) or os.path.join(g.lib, str(v)))
        if r.get("shirt"): paths.append(os.path.join(shirt_backgrounds_dir(g) or "", r["shirt"]))
    else:
        dirs = costume_dirs(g.lib); paths.append(dirs.get(split_name(name)[0]) or os.path.join(g.lib, name))
    b = g.costume_of(BASE_KEY)                                   # the base layer fills what a choice lacks
    if b != DEFAULT: paths.append(costume_dirs(g.lib).get(b))
    extra = "%s|%s|%s|%s" % (PICKER_UNC_MAX, UNCOMPRESSED_DARK, WORN_SKULLS, b)
    return hashlib.sha1((_sig_paths(paths) + extra).encode()).hexdigest()


def _sent_load(g):
    try: return json.load(open(os.path.join(picker_dir(g), SENT_FILE), encoding="utf-8"))
    except Exception: return {}


def _sent_save(g, st):
    try:
        os.makedirs(picker_dir(g), exist_ok=True)
        save_json(os.path.join(picker_dir(g), SENT_FILE), st, indent=1)
    except Exception as ex: say("  note: could not save %s (%s)" % (SENT_FILE, ex))


def _sent_mark(g, st, name):
    try: st[name] = {"folder": picker_name(name), "sig": choice_sig(g, name)}
    except Exception: pass


def _pcx_sig(g):
    p = g.archives.get("misc_pc")
    return _sig_paths([p] + [pc_exclusive_skull(g, f) for c, f in PC_EXCLUSIVE]) if p else ""


def export_picker_changes(g, dry=False):
    """v0.9.28: only the kits / costumes / mixes that are new or changed since the last send; removed ones are deleted"""
    st = _sent_load(g); choices = library_choices(g); todo = []
    for c in choices:
        folder = os.path.join(picker_dir(g), picker_name(c))
        has = os.path.isdir(folder) and any(f.lower().endswith(".dds") for f in os.listdir(folder))
        try: sig = choice_sig(g, c)
        except Exception: sig = None
        if not has or not sig or (st.get(c) or {}).get("sig") != sig: todo.append(c)
    gone = [c for c in list(st) if not c.startswith("@") and c not in choices]
    pcx = _pcx_sig(g); pcx_todo = pcx and (st.get("@pc_exclusive") or {}).get("sig") != pcx or \
        any(not os.path.isdir(os.path.join(picker_dir(g), f)) for c, f in PC_EXCLUSIVE)
    say("Send changes to the game: %d of %d choice(s) new or changed%s%s" % (len(todo), len(choices),
        ", %d removed" % len(gone) if gone else "", ", PC Exclusive skins" if pcx_todo else ""))
    for c in todo: say("  changed: %s" % c)
    if dry: return len(todo)
    n = 0
    for c in todo:
        try:
            if export_picker(g, c): n += 1; _sent_mark(g, st, c)
        except Exception as ex:
            say("  %s: could not be written (%s)" % (c, ex))
    for c in gone:
        f = (st.get(c) or {}).get("folder")
        d = os.path.join(picker_dir(g), f) if f else None
        if d and os.path.isdir(d) and f not in [picker_name(x) for x in choices]:
            try: shutil.rmtree(d); say("  removed from the game: %s (no longer in your library)" % f)
            except OSError as ex: say("  %s could not be removed (%s)" % (f, ex))
        st.pop(c, None)
    if pcx_todo:
        export_pc_exclusive(g); st["@pc_exclusive"] = {"sig": pcx}
    _sent_save(g, st)
    if not todo and not gone and not pcx_todo: say("Nothing changed since the last send - the game is up to date.")
    else: say("Done: %d costume(s) updated. In the game: Costume tab ('Reload costume files' if the game is running)." % n)
    return n


def export_picker_all(g, dry=False):
    """v0.9.25: the WHOLE library (every kit, costume and mix) + the PC Exclusive skins -> the in-game picker"""
    choices = library_choices(g)
    say("In-game picker: whole library, %d choice(s) -> %s" % (len(choices), picker_dir(g)))
    n = 0
    for c in choices:
        try:
            if export_picker(g, c, dry=dry): n += 1
        except Exception as ex:
            say("  %s: could not be written (%s)" % (c, ex))
    n += export_pc_exclusive(g, dry)
    if not dry:                                                # v0.9.28: remember what was sent
        st = _sent_load(g)
        for c in choices: _sent_mark(g, st, c)
        st["@pc_exclusive"] = {"sig": _pcx_sig(g)}; _sent_save(g, st)
    say("Done: %d costume(s). In the game: Extended menu -> Costume tab ('Reload costume files' if the game is running)." % n)
    return n


def export_costume(g, name, outdir, dry=False):
    """copy the files a costume really uses into a new folder, named the way the game reads them (pun_tshirt_01.png, punps2_trench04.dds ...)"""
    path = costume_dirs(g.lib).get(name) or os.path.join(g.lib, name)
    if not os.path.isdir(path): say("Costume folder not found: %s" % name); return
    sets = build_mapping(g, name, path); base = os.path.join(outdir, *name.split(" / ")); total = 0
    for sub, s in sets.items():
        used = {f: v for f, v in s["files"].items() if v["texture"]}
        skipped = len(s["files"]) - len(used)
        for f, v in sorted(used.items(), key=lambda kv: kv[1]["texture"]):
            new = v["texture"].replace(".tga", "") + os.path.splitext(f)[1].lower()
            dst = os.path.join(base, sub, new); total += 1
            say("  %s%-14s %-40s -> %s" % ("" if not dry else "(dry) ", sub, f[:40], new))
            if not dry:
                os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(os.path.join(path, sub, f), dst)
        if skipped: say("  %-14s %d file(s) not used by the game were left out" % (sub, skipped))
    say("%s %d files %s %s" % ("Would copy" if dry else "Copied", total, "to", base))
    if not dry: say("Point the manager at that folder with the library command to use the clean copy. Your original files were not touched.")


def rename_costume(g, name, dry=False, undo=False):
    """rename a costume's files in place to the names the game reads (pun_tshirt_01.png ...); files the game does not use go into an _unused subfolder (nothing is deleted)"""
    path = costume_dirs(g.lib).get(name) or os.path.join(g.lib, name)
    if not os.path.isdir(path): say("Costume folder not found: %s" % name); return
    logp = os.path.join(g.cdir, "rename_logs", re.sub(r"[^A-Za-z0-9_.-]+", "_", name) + ".json")
    if undo:
        if not os.path.isfile(logp): say("No rename log for '%s' - nothing to undo." % name); return
        ops = json.load(open(logp)); n = 0
        for o in reversed(ops):
            if os.path.exists(o["to"]) and not os.path.exists(o["from"]):
                if not dry: shutil.move(o["to"], o["from"])
                n += 1
        if not dry:
            os.remove(logp)
            for dp, dn, fs in os.walk(path, topdown=False):
                if os.path.basename(dp) == "_unused" and not os.listdir(dp): os.rmdir(dp)
            mp = os.path.join(g.cdir, "mappings", re.sub(r"[^A-Za-z0-9_.-]+", "_", name) + ".json")
            if os.path.isfile(mp): os.remove(mp)
        say("%s %d files back to their old names." % ("Would restore" if dry else "Restored", n)); return
    sets = build_mapping(g, name, path); ops = []
    for sub, s in sets.items():
        d = os.path.join(path, sub); taken = set(os.listdir(d))
        for f, v in sorted(s["files"].items(), key=lambda kv: (kv[1]["texture"] or "~", kv[0])):
            ext = os.path.splitext(f)[1].lower()
            if v["texture"]:
                new = v["texture"].replace(".tga", "") + ext
                if new == f: continue
                if new in taken and new != f: say("  skipped %s: %s already exists" % (f[:40], new)); continue
                ops.append({"from": os.path.join(d, f), "to": os.path.join(d, new)}); taken.add(new)
                say("  %s%-14s %-44s -> %s" % ("(dry) " if dry else "", sub, f[:44], new))
            else:
                ops.append({"from": os.path.join(d, f), "to": os.path.join(d, "_unused", f)})
                say("  %s%-14s %-44s -> _unused/   (%s)" % ("(dry) " if dry else "", sub, f[:44], v["note"][:40]))
    if not ops: say("Nothing to rename - the files already use the game's names."); return
    if dry: say("Would rename or move %d files. Nothing changed." % len(ops)); return
    for o in ops:
        os.makedirs(os.path.dirname(o["to"]), exist_ok=True); shutil.move(o["from"], o["to"])
    save_json(logp, ops, indent=1)
    say("Renamed or moved %d files. Undo any time with:  rename \"%s\" --undo" % (len(ops), name))


def use_file(g, name, part):
    """pick a different file for a game texture when two files compete for it (or one is a copy of the other)"""
    path = costume_dirs(g.lib).get(name) or os.path.join(g.lib, name); sets = build_mapping(g, name, path)
    hits = [(sub, f) for sub, s_ in sets.items() for f in s_["files"] if part.lower() in f.lower()]
    if len(hits) != 1: say("%s '%s' - give a longer part of the file name." % ("No file matches" if not hits else "Several files match", part)); return
    sub, f = hits[0]; e = sets[sub]["files"][f]
    if e["texture"]: say("%s is already used for %s." % (f, e["texture"])); return
    other = e.get("copy_of") or e.get("competes_with")
    if not other or other not in sets[sub]["files"] or not sets[sub]["files"][other]["texture"]:
        say("%s does not compete with a used file, so there is nothing to swap. (Textures 11-13 and files with no match cannot be assigned yet.)" % f); return
    o = sets[sub]["files"][other]; tex = o["texture"]
    e.update({"texture": tex, "score": o["score"], "note": "chosen by you", "chosen": True}); o.update({"texture": None, "note": "not used (you chose %s)" % f[:24]}); o.pop("chosen", None)
    mp = os.path.join(g.cdir, "mappings", re.sub(r"[^A-Za-z0-9_.-]+", "_", name) + ".json"); data = json.load(open(mp)); data["sets"] = sets; save_json(mp, data, indent=1)
    say("Now using %s for %s (instead of %s). Run apply to write it." % (f, tex, other))


def find_texture(g, text):
    """list every place in the game folder where a texture with this name lives (archive, bank, size) and whether the manager handles that archive"""
    key = re.sub(r"[^a-z0-9]", "", text.lower()); hits = 0           # ignores _ - and spaces: trench_12, trench-12 and trench12 all work
    say("Looking for '%s' in %d archives ..." % (text, len(g.archives)))
    for stem in sorted(g.archives):
        try:
            d = open(g.archives[stem], "rb").read(); count, total, ents, _ = parse_vpp(d)
        except Exception:
            continue
        have = {e["name"].lower() for e in ents}; models = {k for k, (rxc, _) in SETS.items() if rxc in have}
        for e in ents:
            if not e["name"].lower().endswith(".ceg"): continue
            try: b = inner(d, e)
            except Exception: continue
            if b[:4] != b"GEKV": continue
            n, ent = bank_dir(b)
            for off, w, h, fm, raw in ent:
                nm = tname(raw)
                if key in re.sub(r"[^a-z0-9]", "", nm.lower()):
                    say("  %-16s %-26s %-26s %4dx%-4d %s" % (stem, e["name"][:26], nm[:26], w, h, "handled" if models else "NOT HANDLED (no Punisher model file in this archive)")); hits += 1
    say("%d place(s) found." % hits)


def show_originals(g, text, outdir=None):
    """list (and optionally save as PNG) the ORIGINAL textures the manager replaced, taken from its undo data: sizes show what the game really shipped"""
    key = re.sub(r"[^a-z0-9]", "", text.lower()); udir = os.path.join(g.cdir, "undo"); sizes = {}; n = 0
    if not os.path.isdir(udir): say("Nothing has been replaced yet, so there are no originals saved."); return
    if outdir: os.makedirs(outdir, exist_ok=True)
    for f in sorted(os.listdir(udir)):
        if not f.endswith(".json"): continue
        stem = f[:-5]
        try: undo = json.load(open(os.path.join(udir, f)))
        except Exception: continue
        for uk, o in sorted(undo.items()):
            bank, nm = uk.split("|", 1)
            if key not in re.sub(r"[^a-z0-9]", "", nm.lower()): continue
            n += 1; sizes[(o["w"], o["h"])] = sizes.get((o["w"], o["h"]), 0) + 1
            line = "  %-14s %-24s original %4dx%-4d" % (stem, nm, o["w"], o["h"])
            if outdir:
                dst = os.path.join(outdir, "%s_%s.png" % (stem, nm[:-4])); data = base64.b64decode(o["data"])
                try: cm_img = decode_rgba(data[:o["w"] * o["h"]], o["w"], o["h"]); cm_img.save(dst); line += "  -> " + os.path.basename(dst)
                except Exception as ex: line += "  (could not save: %s)" % ex
            say(line)
    say("%d original(s) found. Sizes: %s" % (n, ", ".join("%dx%d in %d archive(s)" % (w, h, c) for (w, h), c in sorted(sizes.items())) or "-"))


def apply_all(g, dry=False):
    """v0.9.20 (review): one Apply at a time per game - two launchers at once could corrupt a level file"""
    if dry:
        return _apply_all(g, dry)
    lk = os.path.join(g.cdir, "apply.lock")
    os.makedirs(g.cdir, exist_ok=True)
    try:
        if os.path.isfile(lk) and time.time() - os.path.getmtime(lk) > 3600: os.remove(lk)   # left by a crash
    except OSError: pass
    try:
        fd = os.open(lk, os.O_CREAT | os.O_EXCL | os.O_WRONLY); os.write(fd, str(os.getpid()).encode()); os.close(fd)
    except FileExistsError:
        raise SystemExit("Another Costume Launcher is applying costumes to this game right now - wait for it to finish."
                         " (If none is running: delete PunisherCostumes/apply.lock in the game folder.)")
    try:
        return _apply_all(g, dry)
    finally:
        try: os.remove(lk)
        except OSError: pass


def _apply_all(g, dry=False):
    t0 = time.time(); cache = {}; sigs = {}; total_c = total_f = 0; base_tex = load_base(g)
    ap = os.path.join(g.cdir, "applied.json")
    try: applied = json.load(open(ap)) if os.path.isfile(ap) else {}
    except Exception: applied = {}
    jobs = []
    for mission, stems in g.missions:
        name = g.costume_of(mission)
        if name != DEFAULT and not choice_exists(g, name):
            # v0.9.20 (review): left as it is (was: reset to Default - an unmounted library wiped every costume on PLAY)
            say("  warning: costume '%s' for %s not found in the library - that mission is left as it is" % (name, mission)); continue
        if name not in cache:
            cache[name] = costume_textures(g, name, base_tex); sigs[name] = tex_signature(cache[name])
        for stem in stems: jobs.append((mission, stem, name))
    last = {}                                                   # a level file listed under two missions: the later one wins
    for job in jobs: last[job[1]] = job                         # (as before), and it is never rebuilt by two threads at once
    jobs = [j for j in jobs if last[j[1]] is j]
    t1 = time.time()
    todo = []; skipped = 0
    for mission, stem, name in jobs:
        rec = applied.get(stem)
        try: stamp = _file_stamp(g.archives[stem])
        except OSError: stamp = None
        if rec and stamp and rec.get("sig") == sigs[name] and rec.get("stamp") == stamp:
            skipped += 1; continue                              # this level already has exactly this costume
        todo.append((mission, stem, name))
    def work(job):
        mission, stem, name = job
        n = apply_archive(g, stem, name, cache[name], dry)
        return job, n
    new_rec = {}
    with ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for (mission, stem, name), n in pool.map(work, todo):
            if n:
                total_c += n; total_f += 1; say("  %s %-12s (%s) -> %s: %d textures" % ("would change" if dry else "changed", stem, mission, name, n))
            if not dry:
                try: new_rec[stem] = {"sig": sigs[name], "stamp": _file_stamp(g.archives[stem]), "costume": name}
                except OSError: pass
    t2 = time.time()
    gt, gf = apply_global(g, dry); total_c += gt; total_f += gf
    if not dry and new_rec:
        applied.update(new_rec); save_json(ap, applied, indent=1)
    say("%s %d files, %d textures." % ("Would change" if dry else "Changed", total_f, total_c) if total_f else "Nothing to change - everything already matches your choices.")
    say("  time: %.1f s (costumes %.1f s, %d level files checked + %d unchanged skipped %.1f s, skulls %.1f s)" %
        (time.time() - t0, t1 - t0, len(todo), skipped, t2 - t1, time.time() - t2))


def restore_all(g):
    say("restore-all was removed (v0.4.25): it copied whole level files back and could undo other mods' changes."
        " To go back to the game's own pictures, set every mission to Default and apply.")


# ---------------------------------------------------------------- interfaces
def show_list(g):
    say(" 0. %-28s %-26s HUD, enemy and hotspot icons, menu and loading skulls" % (GLOBAL_LABEL, "[" + g.costume_of(GLOBAL_KEY) + "]"))
    say(" B. %-28s %-26s e.g. the default T-shirt/trench coat upscales" % ("Base layer", "[" + g.costume_of(BASE_KEY) + "]"))
    for i, (m, stems) in enumerate(g.missions, 1):
        info = {"trench": 0, "tshirt": 0}
        for s in stems:
            for k in g.models(s): info[k] += 1
        say("%2d. %-28s %-26s trench coat in %d, T-shirt in %d of %d files" % (i, m, "[" + g.costume_of(m) + "]", info["trench"], info["tshirt"], len(stems)))


def text_menu(g):
    while True:
        say(); show_list(g)
        say("\nType a mission number to change its costume, A to apply, S to set every mission, Q to quit.")
        c = input("> ").strip().lower()
        if c == "q": return
        if c == "a": apply_all(g); continue
        names = g.costumes()
        if c == "s": targets = [m for m, _ in g.missions]
        elif c == "0": targets = [GLOBAL_KEY]
        elif c == "b": targets = [BASE_KEY]
        elif c.isdigit() and 1 <= int(c) <= len(g.missions): targets = [g.missions[int(c) - 1][0]]
        else: continue
        say("Costumes: " + "  ".join("%d=%s" % (i, n) for i, n in enumerate(names, 1)))
        k = input("costume number> ").strip()
        if k.isdigit() and 1 <= int(k) <= len(names):
            for t in targets: g.state[t] = names[int(k) - 1]
            g.save()


def gui(g):
    import tkinter as tk
    from tkinter import ttk
    root = tk.Tk(); root.title("The Punisher - Costume Manager"); names = g.costumes(); vars_ = {}
    outer = ttk.Frame(root, padding=10); outer.pack(fill="both", expand=True)
    canvas = tk.Canvas(outer, width=520, height=420); sb = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
    inner_f = ttk.Frame(canvas); inner_f.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
    canvas.create_window((0, 0), window=inner_f, anchor="nw"); canvas.configure(yscrollcommand=sb.set); canvas.pack(side="left", fill="both", expand=True); sb.pack(side="right", fill="y")
    rows = [(BASE_KEY, BASE_LABEL), (GLOBAL_KEY, GLOBAL_LABEL)] + [(m, m) for m, _ in g.missions]
    for r, (m, label) in enumerate(rows):
        ttk.Label(inner_f, text=label, width=28).grid(row=r, column=0, sticky="w", pady=2)
        v = tk.StringVar(value=g.costume_of(m)); vars_[m] = v
        ttk.Combobox(inner_f, textvariable=v, values=names, state="readonly", width=28).grid(row=r, column=1, pady=2)
    bar = ttk.Frame(root, padding=10); bar.pack(fill="x"); status = tk.StringVar()
    def do_apply():
        for m, v in vars_.items(): g.state[m] = v.get()
        g.save(); status.set("Applying - please wait..."); root.update_idletasks()
        buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
        try: apply_all(g)
        finally: sys.stdout = old
        status.set(buf.getvalue().strip().split("\n")[-1])
    def set_all():
        keys = [k for k in vars_ if k not in (GLOBAL_KEY, BASE_KEY)]; first = vars_[keys[0]].get() if keys else DEFAULT
        for k in keys: vars_[k].set(first)
    ttk.Button(bar, text="Apply", command=do_apply).pack(side="left"); ttk.Button(bar, text="Set all like the first mission", command=set_all).pack(side="left", padx=8)
    ttk.Label(bar, textvariable=status).pack(side="left", padx=8); root.mainloop()


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    root = os.path.abspath((args[0] if args else os.getcwd()).strip('"'))
    if not any(os.path.exists(os.path.join(root, x)) for x in ("pun.exe", "punisher.exe")):
        say("Could not find the game folder (the one with pun.exe): %s" % root); return 2
    g = Game(root); cmd = args[1].lower() if len(args) > 1 else ""
    if cmd == "list": show_list(g)
    elif cmd == "library" and len(args) >= 3:
        p = os.path.abspath(args[2].strip('"'))
        if not os.path.isdir(p): say("Folder not found: %s" % p); return 1
        os.makedirs(g.cdir, exist_ok=True); open(os.path.join(g.cdir, "library_path.txt"), "w", encoding="utf-8").write(p)
        say("Costume library is now: %s" % p); say("Costumes found: " + ", ".join(Game(root).costumes()[1:]))
    elif cmd == "costumes": say("\n".join(g.costumes()) + "\n\nLibrary folder: " + g.lib)
    elif cmd == "set" and len(args) >= 4:
        ms = g.find_mission(args[2]); cs = g.find_costume(args[3])
        if not ms: say("No mission matches '%s'. Use the list command." % args[2]); return 1
        if len(cs) != 1: say("Costume '%s' not found or ambiguous: %s" % (args[3], cs or g.costumes())); return 1
        for m in ms: g.state[m] = cs[0]
        note = ""
        if args[2].lower() == "all":                          # "all" also sets the in-game skulls when the costume has skulls (or Default)
            if cs[0] == DEFAULT or skull_sources(g, cs[0]): g.state[GLOBAL_KEY] = cs[0]; note = " The in-game skulls were set to it too."
            else: note = " The in-game skulls were NOT changed: this costume has no in-game skull folder (use: set skulls ...)."
        g.save(); say("Set %s -> %s.%s Run 'apply' to write it." % (", ".join(LABELS.get(m, m) for m in ms) if len(ms) < 4 else "%d missions" % len(ms), cs[0], note))
    elif cmd in ("import", "report"):
        names = g.costumes()[1:] if len(args) < 3 else g.find_costume(args[2])
        if not names: say("No costume found. Library folder: " + g.lib); return 1
        for nme in names:
            if nme == DEFAULT: continue
            path = costume_dirs(g.lib).get(nme) or os.path.join(g.lib, nme)
            print_mapping(nme, build_mapping(g, nme, path, force=(cmd == "import")))
        say("\nThe matches are saved in %s and used by apply. Check any line marked WEAK or '-'." % os.path.join(g.cdir, "mappings"))
    elif cmd == "verify": verify(g)
    elif cmd == "find" and len(args) >= 3: find_texture(g, args[2])
    elif cmd == "originals" and len(args) >= 3: show_originals(g, args[2], os.path.abspath(args[3].strip('"')) if len(args) >= 4 else None)
    elif cmd == "use" and len(args) >= 4:
        cs = g.find_costume(args[2])
        if len(cs) != 1 or cs[0] == DEFAULT: say("Costume '%s' not found or ambiguous: %s" % (args[2], cs or g.costumes())); return 1
        use_file(g, cs[0], args[3])
    elif cmd == "rename" and len(args) >= 3:
        cs = g.find_costume(args[2])
        if len(cs) != 1 or cs[0] == DEFAULT: say("Costume '%s' not found or ambiguous: %s" % (args[2], cs or g.costumes())); return 1
        rename_costume(g, cs[0], "--dry-run" in argv, "--undo" in argv)
    elif cmd == "export" and len(args) >= 4:
        cs = g.find_costume(args[2])
        if len(cs) != 1 or cs[0] == DEFAULT: say("Costume '%s' not found or ambiguous: %s" % (args[2], cs or g.costumes())); return 1
        export_costume(g, cs[0], os.path.abspath(args[3].strip('"')), "--dry-run" in argv)
    elif cmd == "picker" and len(args) >= 3:                 # v0.9.24: in-game picker
        if args[2].lower() == "used": export_picker_used(g, "--dry-run" in argv)
        elif args[2].lower() == "all": export_picker_all(g, "--dry-run" in argv)            # v0.9.25
        elif args[2].lower() == "changes": export_picker_changes(g, "--dry-run" in argv)    # v0.9.28
        else:
            allc = library_choices(g)                          # v0.9.26: kits and mixes too
            cs = [c for c in allc if c.lower() == args[2].lower()] or [c for c in allc if args[2].lower() in c.lower()] or g.find_costume(args[2])
            if len(cs) != 1 or cs[0] == DEFAULT: say("Costume '%s' not found or ambiguous: %s" % (args[2], cs or g.costumes())); return 1
            export_picker(g, cs[0], args[3] if len(args) >= 4 else None, "--dry-run" in argv)
    elif cmd == "apply": apply_all(g, "--dry-run" in argv)
    elif cmd == "restore-all": restore_all(g)
    elif cmd in ("", "menu", "gui"):
        if cmd != "menu":
            try: gui(g); return 0
            except Exception as ex:
                if cmd == "gui": say("Window not available (%s)" % ex); return 1
        text_menu(g)
    else: say(__doc__)
    return 0


if __name__ == "__main__":
    try: code = main(sys.argv[1:])
    except SystemExit as ex: print(ex); code = 1
    except Exception as ex: print("Unexpected error: %s" % ex); code = 9
    sys.exit(code)
