#!/usr/bin/env python3
"""
The Punisher (2005, PC) - Restoration Project - early build installer.

Needs only Python 3.8+ (nothing to install). It edits the game's own archives in place:
it checks that the files inside them are the untouched retail ones, makes a backup
copy, then applies the changes. It never downloads anything and touches no other file.

Usage:
    python install_patch.py "C:\\Path\\To\\The Punisher"     install (or just run it inside the game folder)
    python install_patch.py "C:\\Path\\To\\The Punisher" --check    only report what it finds
    python install_patch.py "C:\\Path\\To\\The Punisher" --restore  put your original files back
"""
import sys, os, struct, zlib, lzma, json, base64, hashlib, shutil

VERSION = "0.1"
BACKUP_DIR = "PunisherPatch_backup"
ARCHIVES = ["tables.vpp", "zoo1.vpp", "Chop1.vpp"]
HERE = os.path.dirname(os.path.abspath(__file__))


def align(n, a=2048):
    return (n + a - 1) // a * a


def sha(b):
    return hashlib.sha256(b).hexdigest()


def parse_vpp(d):
    sig, ver, count, total = struct.unpack("<4I", d[:16])
    if sig != 0x51890ACE:
        raise ValueError("not a Volition VPP archive")
    ents = []
    pos = 0x800 + align(count * 32)
    for i in range(count):
        o = 0x800 + i * 32
        name = d[o:o + 24].split(b"\0")[0].decode("latin1")
        sz, csz = struct.unpack("<2I", d[o + 24:o + 32])
        ents.append({"name": name, "sz": sz, "csz": csz, "pos": pos, "i": i})
        pos += align(csz)
    return count, total, ents, pos


def inner(d, e):
    blob = d[e["pos"]:e["pos"] + e["csz"]]
    if e["csz"] != e["sz"]:
        try:
            blob = zlib.decompress(blob)
        except zlib.error:
            blob = zlib.decompress(blob, -15)
    return blob


def apply_ops(old, ops):
    out = bytearray()
    for op in ops:
        if op[0] == "c":
            out += old[op[1]:op[1] + op[2]]
        else:
            out += base64.b64decode(op[1])
    return bytes(out)


def apply_bytes(old, edits):
    b = bytearray(old)
    for off, oldhex, newhex in edits:
        if off + len(bytes.fromhex(oldhex)) > len(b) or bytes(b[off:off + len(oldhex) // 2]) != bytes.fromhex(oldhex):
            raise ValueError("expected original bytes not found at offset %d" % off)
        b[off:off + len(newhex) // 2] = bytes.fromhex(newhex)
    return bytes(b)


MAX_LEVEL = None      # costume manager v0.4.14: cap the zlib level of rebuilt entries (6 = ~8x faster than 9, ~10 % bigger)


def rebuild(d, count, total, ents, new_inner):
    """Rebuild an archive with some entries replaced. Other entries are copied byte for byte."""
    directory = bytearray(d[0x800:0x800 + align(count * 32)])
    body = bytearray()
    for e in ents:
        if e["name"] in new_inner:
            data = new_inner[e["name"]]
            if e["csz"] != e["sz"]:
                hdr = d[e["pos"] + 1]
                level = {0x01: 1, 0x5E: 2, 0x9C: 6, 0xDA: 9}.get(hdr, 9)
                if MAX_LEVEL: level = min(level, MAX_LEVEL)
                blob = zlib.compress(data, level)
            else:
                blob = data
            struct.pack_into("<2I", directory, e["i"] * 32 + 24, len(data), len(blob))
        else:
            blob = d[e["pos"]:e["pos"] + e["csz"]]
        body += blob + b"\0" * (align(len(blob)) - len(blob))
    end_old = 0x800 + align(count * 32) + sum(align(e["csz"]) for e in ents)
    trailing = len(d) - end_old
    header = bytearray(d[:0x800])
    new = bytes(header) + bytes(directory) + bytes(body) + b"\0" * trailing
    new_total = total + (len(new) - len(d))
    return new[:12] + struct.pack("<I", new_total) + new[16:]


def find_archive(root, name):
    want = name.lower()
    best = None
    for dirpath, dirs, files in os.walk(root):
        depth = dirpath[len(root):].count(os.sep)
        if depth >= 3:
            dirs[:] = []
        if BACKUP_DIR.lower() in dirpath.lower():
            continue
        for f in files:
            if f.lower() == want:
                p = os.path.join(dirpath, f)
                if best is None or len(p) < len(best):
                    best = p
    return best


def load_patch(name):
    p = os.path.join(HERE, "data", name.replace(".vpp", ".pnp"))
    with open(p, "rb") as f:
        return json.loads(lzma.decompress(f.read()).decode("utf-8"))


def plan(path, patch):
    """Return (status, detail, new_inner) for one archive."""
    d = open(path, "rb").read()
    try:
        count, total, ents, end = parse_vpp(d)
    except Exception as ex:
        return "bad", ["not a valid archive (%s)" % ex], None, None
    by = {e["name"]: e for e in ents}
    new_inner, done, problems = {}, 0, []
    for pe in patch["entries"]:
        e = by.get(pe["name"])
        if e is None:
            problems.append("%s: file not found inside the archive" % pe["name"])
            continue
        cur = inner(d, e)
        h = sha(cur)
        if h == pe["new_sha256"]:
            done += 1
            continue
        try:
            if pe["kind"] == "delta":
                if h != pe["old_sha256"]:
                    problems.append("%s: not the retail version (sha256 %s)" % (pe["name"], h[:16]))
                    continue
                res = apply_ops(cur, pe["ops"])
            else:
                res = apply_bytes(cur, pe["ops"])
        except Exception as ex:
            problems.append("%s: %s" % (pe["name"], ex))
            continue
        if sha(res) != pe["new_sha256"]:
            problems.append("%s: result check failed (your file differs from the retail one)" % pe["name"])
            continue
        new_inner[pe["name"]] = res
    if problems:
        return "mismatch", problems, None, None
    if not new_inner:
        return "done", [], None, None
    return "ok", [], (d, count, total, ents), new_inner


def say(s=""):
    print(s)
    sys.stdout.flush()


def restore(root):
    bdir = os.path.join(root, BACKUP_DIR)
    mpath = os.path.join(bdir, "backup_manifest.json")
    if not os.path.isfile(mpath):
        say("No backup found in %s - nothing to restore." % bdir)
        return 1
    man = json.load(open(mpath))
    for name, info in man.items():
        src = os.path.join(bdir, name)
        if not os.path.isfile(src) or sha(open(src, "rb").read()) != info["sha256"]:
            say("Backup of %s is missing or damaged - stopping." % name)
            return 1
    for name, info in man.items():
        shutil.copyfile(os.path.join(bdir, name), info["target"])
        say("Restored %s" % name)
    say("Done. Your original files are back. (The backup folder was left in place.)")
    return 0


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    flags = [a for a in argv if a.startswith("--")]
    root = args[0] if args else os.getcwd()
    root = os.path.abspath(root.strip('"'))
    if not args and not any(os.path.exists(os.path.join(root, x)) for x in ("pun.exe", "punisher.exe")):
        up = os.path.dirname(root)
        if any(os.path.exists(os.path.join(up, x)) for x in ("pun.exe", "punisher.exe")):
            root = up
    if not os.path.isdir(root) or not any(os.path.exists(os.path.join(root, x)) for x in ("pun.exe", "punisher.exe")):
        say("Could not find the game here: %s" % root)
        say("Run this again with the game folder (the one that contains pun.exe), for example:")
        say('    python install_patch.py "C:\\Program Files (x86)\\THQ\\Marvel\\The Punisher"')
        return 2
    say("The Punisher restoration project - early build v%s" % VERSION)
    say("Game folder: %s" % root)
    if "--restore" in flags:
        return restore(root)

    found, results = {}, {}
    for name in ARCHIVES:
        p = find_archive(root, name)
        if not p:
            say("Missing: %s was not found under the game folder." % name)
            return 2
        found[name] = p
        results[name] = plan(p, load_patch(name))
    bad = False
    for name in ARCHIVES:
        st, detail = results[name][0], results[name][1]
        label = {"ok": "ready to patch", "done": "already patched", "mismatch": "CANNOT PATCH", "bad": "CANNOT PATCH"}[st]
        say("  %-11s %s   (%s)" % (name, label, os.path.relpath(found[name], root)))
        for line in detail:
            say("      - " + line)
        bad = bad or st in ("mismatch", "bad")
    if bad:
        say()
        say("Nothing was changed. The files above are not the untouched retail PC versions")
        say("(another mod, a different release, or a damaged copy). Restore the original files")
        say("or tell me which release you have and include the lines above.")
        return 3
    if "--check" in flags:
        say("Check only - nothing changed.")
        return 0
    todo = [n for n in ARCHIVES if results[n][0] == "ok"]
    if not todo:
        say("Everything is already patched.")
        return 0

    bdir = os.path.join(root, BACKUP_DIR)
    os.makedirs(bdir, exist_ok=True)
    mpath = os.path.join(bdir, "backup_manifest.json")
    man = json.load(open(mpath)) if os.path.isfile(mpath) else {}
    for name in todo:
        dst = os.path.join(bdir, name)
        if name not in man or not os.path.isfile(dst):
            shutil.copyfile(found[name], dst)
            man[name] = {"sha256": sha(open(dst, "rb").read()), "target": found[name]}
    json.dump(man, open(mpath, "w"), indent=1)
    say("Backup of your originals saved in: %s" % bdir)

    for name in todo:
        (d, count, total, ents), new_inner = results[name][2], results[name][3]
        new = rebuild(d, count, total, ents, new_inner)
        tmp = found[name] + ".patching"
        try:
            with open(tmp, "wb") as f:
                f.write(new)
            c2, t2, e2, _ = parse_vpp(new)
            by2 = {e["name"]: e for e in e2}
            for pe in load_patch(name)["entries"]:
                if sha(inner(new, by2[pe["name"]])) != pe["new_sha256"]:
                    raise ValueError("self-check failed for " + pe["name"])
            os.replace(tmp, found[name])
        except PermissionError:
            say("Could not write %s - close the game and any launcher, then run this again." % name)
            if os.path.exists(tmp):
                os.remove(tmp)
            return 4
        except Exception as ex:
            say("Failed on %s (%s). That file was not changed." % (name, ex))
            if os.path.exists(tmp):
                os.remove(tmp)
            return 4
        say("Patched %s (%d file(s) changed)" % (name, len(new_inner)))
    say()
    say("All done. Start the game as usual. To undo: run this again with --restore.")
    return 0


if __name__ == "__main__":
    try:
        code = main(sys.argv[1:])
    except Exception as ex:
        print("Unexpected error: %s" % ex)
        code = 9
    if os.name == "nt" and sys.stdin.isatty() and "--no-pause" not in sys.argv:
        try:
            input("\nPress Enter to close...")
        except EOFError:
            pass
    sys.exit(code)
