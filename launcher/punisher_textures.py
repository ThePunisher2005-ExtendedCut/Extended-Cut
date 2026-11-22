#!/usr/bin/env python3
"""
punisher_textures.py - which textures make up the Punisher (trench coat and T-shirt model, plus any other
Punisher model in the game files), read straight from the model files themselves.

    $PY punisher_textures.py "$GAME"            (writes "Punisher textures" next to this script)
    $PY punisher_textures.py "$GAME" OUTFOLDER

For every Punisher model (punisher_trenchcoat.rxc, punisher_tshirt.rxc, punisher_*.rxc ...):
  - every texture name the model file refers to,
  - its size and format, and whether it is one the costume manager already handles (trench 01-12 / T-shirt 01-13)
    or one you did not know about (hair, eyes, teeth, holster ...),
  - a PNG of each texture (the ORIGINAL picture, also when a costume already replaced it in that level).
Nothing in the game is changed.
"""
import os, sys, re, json, base64, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from install_patch import parse_vpp, inner
from install_textures import bank_dir, tname
import costume_manager as cm


def texture_bytes(fmt, w, h):
    return {0x0F: w * h, 0x0E: w * h, 0x0C: w * h // 2, 0x07: w * h * 4}.get(fmt)


def to_png(data, fmt, w, h, path):
    from PIL import Image
    try:
        if fmt in (0x0F, 0x0E): im = Image.frombytes("RGBA", (w, h), bytes(data[:w * h]), "bcn", 3 if fmt == 0x0F else 2)
        elif fmt == 0x0C: im = Image.frombytes("RGBA", (w, h), bytes(data[:w * h // 2]), "bcn", 1)
        elif fmt == 0x07: im = Image.frombytes("RGBA", (w, h), bytes(data[:w * h * 4]), "raw", "BGRA")
        else: return False
        im.save(path); return True
    except Exception:
        return False


def banks_of(d, ents):
    """{texture name: (bank, w, h, fmt, data)} for all texture banks of an archive"""
    out = {}
    for e in ents:
        if not e["name"].lower().endswith((".ceg", ".peg")): continue
        b = inner(d, e)
        if b[:4] != b"GEKV": continue
        n, ent = bank_dir(b)
        for off, w, h, fm, raw in ent:
            nm = tname(raw); fmt = fm[0]
            size = texture_bytes(fmt, w, h)
            out.setdefault(nm, (e["name"], w, h, fmt, b[off:off + size] if size else b""))
    return out


def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    game = sys.argv[1]; out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "Punisher textures")
    g = cm.Game(game); os.makedirs(out_dir, exist_ok=True)
    known = {}
    for kind, (rxc, pat) in cm.SETS.items():
        for n in cm.OK_NUMBERS: known[(pat % n).lower()] = "%s %02d (the costume manager handles it)" % ("trench coat" if kind == "trench" else "T-shirt", n)
    found_models = {}                              # model file name -> (level, archive bytes, entries)
    for stem in sorted(g.archives):
        try:
            d = open(g.archives[stem], "rb").read(); c, t, ents, _ = parse_vpp(d)
        except Exception:
            continue
        for e in ents:
            nm = e["name"].lower()
            if nm.endswith(".rxc") and "punisher" in nm and nm not in found_models:
                found_models[nm] = (stem, d, ents, e)
    if not found_models: raise SystemExit("No Punisher model found in %s" % game)
    # global banks (misc / mini) for textures a level does not carry itself
    glob = {}
    for nm in ("misc", "mini"):
        p = g.archives.get(nm)
        if p:
            try: d = open(p, "rb").read(); c, t, ents, _ = parse_vpp(d); glob.update(banks_of(d, ents))
            except Exception: pass
    report = []
    for model, (stem, d, ents, e) in sorted(found_models.items()):
        body = inner(d, e).lower()
        banks = banks_of(d, ents)
        up = os.path.join(g.cdir, "undo", stem + ".json"); undo = json.load(open(up)) if os.path.isfile(up) else {}
        names = set(re.findall(rb"[a-z0-9_\-]+\.(?:tga|vbm|dds)", body))
        names = sorted(n.decode() for n in names)
        # plus any bank texture whose name (without .tga) appears in the model
        for nm in banks:
            if nm not in names and nm.rsplit(".", 1)[0].encode() in body: names.append(nm)
        mdir = os.path.join(out_dir, os.path.splitext(model)[0]); os.makedirs(mdir, exist_ok=True)
        report.append("=" * 78); report.append("%s   (read from level file %s)   %d textures" % (model, stem, len(names))); report.append("=" * 78)
        new = 0
        for nm in names:
            src = banks.get(nm); where = "level"
            if not src and nm in glob: src = glob[nm]; where = "misc/mini (always loaded)"
            note = known.get(nm, "NOT handled yet")
            if note.startswith("NOT"): new += 1
            if not src:
                report.append("  %-26s  (not in this level's banks)   %s" % (nm, note)); continue
            bank, w, h, fmt, data = src
            o = undo.get(bank + "|" + nm)
            if o: data = base64.b64decode(o["data"]); w, h = o["w"], o["h"]           # the original, before any costume
            ok = to_png(data, fmt, w, h, os.path.join(mdir, os.path.splitext(nm)[0] + ".png"))
            report.append("  %-26s %4dx%-4d fmt %02X  %-14s %s%s%s" % (nm, w, h, fmt, bank, note, "" if ok else "  (no preview)",
                                                                       "" if where == "level" else "  [" + where + "]"))
        report.append("  -> %d of them are not handled by the costume manager yet" % new); report.append("")
    txt = "\n".join(report); print(txt)
    open(os.path.join(out_dir, "Punisher textures.txt"), "w", encoding="utf-8").write(txt + "\n")
    print("\nPictures and this list are in: %s" % out_dir)


if __name__ == "__main__":
    main()
