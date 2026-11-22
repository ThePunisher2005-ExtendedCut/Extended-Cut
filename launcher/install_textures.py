#!/usr/bin/env python3
"""
The Punisher (2005, PC) - texture pack installer (early build).
Replaces the Punisher's trench coat textures and the interrogation-hotspot skull in EVERY level archive it finds
(each level carries its own copy of them). Needs Python 3.8+ and nothing else. Companion to install_patch.py (restoration build).

Usage:  python install_textures.py "C:\\Path\\To\\The Punisher"             install the trench coat set
        python install_textures.py "<game folder>" --groups trench,skull    choose groups (trench is the default; skull is experimental)
        python install_textures.py "<game folder>" --check                  report only
        python install_textures.py "<game folder>" --restore                put the original level files back
Backups of every file it changes go to PunisherTexturePack_backup (about 1 GB in total for a full install)."""
import sys, os, struct, lzma, json, base64, hashlib, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from install_patch import parse_vpp, inner, rebuild, sha, align

BACKUP = "PunisherTexturePack_backup"
HERE = os.path.dirname(os.path.abspath(__file__))


def say(s=""):
    print(s); sys.stdout.flush()


def bank_dir(b):
    n = struct.unpack_from('<I', b, 16)[0]; ents = []
    for i in range(n):
        o = 32 + 48 * i; off, w, h = struct.unpack_from('<IHH', b, o)
        ents.append([off, w, h, b[o + 8:o + 16], b[o + 16:o + 48]])
    return n, ents


def tname(raw):
    return raw.split(b'\0')[0].decode('latin1').lower()


def replace_in_bank(b, repl):
    n, ent = bank_dir(b); start = 32 + 48 * n
    order = sorted(range(n), key=lambda i: ent[i][0]); body = bytearray(); newoff = {}
    for k, i in enumerate(order):
        off = ent[i][0]; nxt = ent[order[k + 1]][0] if k + 1 < n else len(b)
        nm = tname(ent[i][4])
        if nm in repl:
            v = repl[nm]; w, h, data = v[0], v[1], v[2]; ent[i][1], ent[i][2] = w, h
            if len(v) > 3 and v[3] is not None:              # v0.4.19: a new texture format (0x07 = uncompressed 32-bit)
                ent[i][3] = bytes([v[3]]) + bytes(ent[i][3])[1:]
        else:
            data = b[off:nxt]
        newoff[i] = start + len(body); body += data
    out = bytearray(b[:32])
    for i in range(n):
        out += struct.pack('<IHH', newoff[i], ent[i][1], ent[i][2]) + bytes(ent[i][3]) + ent[i][4]
    out += body
    struct.pack_into('<I', out, 12, len(body))
    return bytes(out)


def load_pack(groups):
    pk = json.loads(lzma.decompress(open(os.path.join(HERE, "data", "textures.pack"), "rb").read()).decode())
    tex = {}
    for g, items in pk["groups"].items():
        if groups and g not in groups:
            continue
        for name, t in items.items():
            tex[name.lower()] = (g, t["orig"], t["w"], t["h"], base64.b64decode(t["data"]))
    return tex


def plan_bank(b, tex):
    """textures to replace in this bank: {name: (w, h, data)}"""
    n, ent = bank_dir(b); todo = {}
    for off, w, h, fm, raw in ent:
        nm = tname(raw)
        if nm in tex:
            g, orig, nw, nh, data = tex[nm]
            if [w, h] != orig:
                continue                                  # a different texture with the same name: leave alone
            cur = b[off:off + w * h]
            if (w, h) == (nw, nh) and cur == data:
                continue                                  # already applied
            todo[nm] = (nw, nh, data)
    return todo


def scan(root):
    out = []
    for dp, dn, fs in os.walk(root):
        if BACKUP.lower() in dp.lower() or "PunisherPatch_backup".lower() in dp.lower():
            continue
        for f in fs:
            if f.lower().endswith(".vpp"):
                out.append(os.path.join(dp, f))
    return sorted(out)


def restore(root):
    bdir = os.path.join(root, BACKUP); mp = os.path.join(bdir, "backup_manifest.json")
    if not os.path.isfile(mp):
        say("No texture backup found - nothing to restore."); return 1
    man = json.load(open(mp)); bad = [k for k, v in man.items() if not os.path.isfile(os.path.join(bdir, k)) or sha(open(os.path.join(bdir, k), "rb").read()) != v["sha256"]]
    if bad:
        say("Backup damaged or missing for: " + ", ".join(bad[:5])); return 1
    for k, v in man.items():
        shutil.copyfile(os.path.join(bdir, k), v["target"])
    say("Restored %d level files. (The backup folder was left in place.)" % len(man)); return 0


def main(argv):
    args = [a for a in argv if not a.startswith("--")]; flags = [a for a in argv if a.startswith("--")]
    groups = None
    if "--groups" in argv:
        i = argv.index("--groups"); groups = argv[i + 1].split(","); args = [a for a in args if a != argv[i + 1]]
    root = os.path.abspath((args[0] if args else os.getcwd()).strip('"'))
    if not os.path.isdir(root) or not any(os.path.exists(os.path.join(root, x)) for x in ("pun.exe", "punisher.exe")):
        say("Could not find the game folder (the one with pun.exe): %s" % root); return 2
    say("Texture pack installer - game folder: %s" % root)
    if "--restore" in flags:
        return restore(root)
    if groups is None:
        groups = ["trench"]                                  # 'skull' (the per-level skull.tga) is experimental: what it draws is not known yet
    tex = load_pack(groups)
    say("Texture groups: %s (%d textures)" % (", ".join(sorted({v[0] for v in tex.values()})), len(tex)))
    bdir = os.path.join(root, BACKUP); mp = os.path.join(bdir, "backup_manifest.json")
    man = json.load(open(mp)) if os.path.isfile(mp) else {}
    changed = skipped = nothing = 0; total_tex = 0; check = "--check" in flags
    for path in scan(root):
        rel = os.path.relpath(path, root)
        try:
            d = open(path, "rb").read(); count, total, ents, _ = parse_vpp(d)
        except Exception:
            continue
        new_inner = {}; ntex = 0
        for e in ents:
            if not e["name"].lower().endswith(".ceg"):
                continue
            try:
                b = inner(d, e)
            except Exception:
                continue
            if b[:4] != b"GEKV":
                continue
            todo = plan_bank(b, tex)
            if todo:
                new_inner[e["name"]] = replace_in_bank(b, todo); ntex += len(todo)
        if not new_inner:
            nothing += 1; continue
        if check:
            say("  would patch %-34s %d textures" % (rel, ntex)); changed += 1; total_tex += ntex; continue
        key = rel.replace("\\", "/")
        dst = os.path.join(bdir, key)
        if key not in man or not os.path.isfile(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(path, dst)
            man[key] = {"sha256": sha(open(dst, "rb").read()), "target": path}
            json.dump(man, open(mp, "w"), indent=1)
        new = rebuild(d, count, total, ents, new_inner); tmp = path + ".patching"
        try:
            open(tmp, "wb").write(new)
            c2, t2, e2, _ = parse_vpp(new); assert c2 == count
            os.replace(tmp, path)
        except PermissionError:
            say("Could not write %s - close the game and run again." % rel)
            if os.path.exists(tmp): os.remove(tmp)
            return 4
        except Exception as ex:
            say("Failed on %s (%s) - that file was not changed." % (rel, ex)); skipped += 1
            if os.path.exists(tmp): os.remove(tmp)
            continue
        changed += 1; total_tex += ntex; say("  patched %-34s %d textures" % (rel, ntex))
    say()
    say("%s: %d level files, %d textures. %d files had nothing to replace (or were already done)%s." %
        ("Would change" if check else "Changed", changed, total_tex, nothing, (", %d failed" % skipped) if skipped else ""))
    if not check and changed:
        say("Undo any time with: python install_textures.py \"<game folder>\" --restore")
    return 0


if __name__ == "__main__":
    try:
        code = main(sys.argv[1:])
    except Exception as ex:
        print("Unexpected error: %s" % ex); code = 9
    if os.name == "nt" and sys.stdin.isatty() and "--no-pause" not in sys.argv:
        try: input("\nPress Enter to close...")
        except EOFError: pass
    sys.exit(code)
