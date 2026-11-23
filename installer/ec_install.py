"""The Punisher: Extended Cut - installer (draft for release 0.1). Works on Windows and the Steam Deck (Python 3).

  python ec_install.py check     "<game folder>"      what is installed, what would change
  python ec_install.py install   "<game folder>" [part ...]
  python ec_install.py uninstall "<game folder>"      puts every original file back

<game folder> = the folder with pun.exe in it. Parts (default: all that are available in this package):
  plugin   ExtendedCut.asi + ExtendedCut_defaults.ini -> scripts\\, SDL3.dll next to pun.exe
  laa      pun.exe may use 4 GB of memory (one header flag) - optional, helps with heavy gore
  tables   tables.vpp text patch (restored kills incl. the tiki impale and blowtorch fixes, gore tables)
  font     misc.vpp: Xbox / PlayStation / Steam Deck button icons for the prompts
  pictures mini.vpp: Xbox / Steam Deck stick pictures, rebuilt M16 launcher + target icons, 2004 lifebar and the
           early development HUD (our recreation)
  blowtorch  the cut blowtorch interrogation in Chop Shop 1 (built from your own Chop1.vpp)
  hooks      the meat-hook quick kill in the Meat Packing Plant (built from your own Meat1.vpp, zoo1.vpp and
             Mansion3.vpp); 0.3: + the drip / pool effects for the zoo1 tiki torch kill (zoo1.vpp)
  blackcolt  mini.vpp: the Punisher's own Colt .45 in black (enemies keep the normal Colt), made from your own
           Colt texture; switch it in game: Extended menu -> Costume -> Weapon Skin -> Colt .45
  menuart  mini.vpp: the Extended menu's pictures (sidebar skull, torn edge, ink splat, brush bar, notebook paper) copied from your
           own game files
  outfits  mini.vpp: both Punisher outfits (trench coat + T-shirt) copied from your own level files, for the outfit
           switch: Extended menu -> Costume -> Outfit
  stumps   mini.vpp: the restored gib stumps + variants for Gore -> "Gib stumps" (built from your own stump models;
           the game's own stumps stay untouched)

Safety rules:
  - every game file is checked before it is changed; a file we do not recognise is left alone (and reported);
  - the first time a file is changed its original is copied to <file>.ec_original (never overwritten later);
  - ExtendedCut_install_state.json (scripts\\) remembers each file's fingerprint after install; uninstall only puts
    an original back if the file still is exactly what we installed (so later changes by other mods are not lost).
"""
import hashlib
import json
import os
import shutil
import struct
import sys

# Install.exe (PyInstaller): the package folder sits next to the .exe, not next to the unpacked script
HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
PKG = os.path.join(HERE, 'package')             # release files sit here (built by make_release.py)
sys.path.insert(0, HERE)
VERSION = '0.3.2'


def sha1(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def find(game, rel):
    """case-insensitive path lookup (the Deck's file system is case sensitive, the game's names are not)"""
    cur = game
    for part in rel.replace('\\', '/').split('/'):
        if not os.path.isdir(cur):
            return None
        hit = next((n for n in os.listdir(cur) if n.lower() == part.lower()), None)
        if hit is None:
            return None
        cur = os.path.join(cur, hit)
    return cur


class State:
    def __init__(self, game):
        # v0.1.1: the plugin lives directly in scripts\ (older ASI Loaders do not look into subfolders - tester report
        # Nov 14); the state file moved with it. An install made before keeps working: its state file is read.
        self.dir = os.path.join(game, 'scripts')
        self.path = os.path.join(self.dir, 'ExtendedCut_install_state.json')
        old = os.path.join(game, 'scripts', 'Extended Cut', 'install_state.json')
        src = self.path if os.path.exists(self.path) else old if os.path.exists(old) else None
        self.d = json.load(open(src)) if src else {'version': VERSION, 'files': {}}

    def save(self):
        os.makedirs(self.dir, exist_ok=True)
        json.dump(self.d, open(self.path, 'w'), indent=1)


def backup(path, st, game):
    rel = os.path.relpath(path, game)
    bak = path + '.ec_original'
    if not os.path.exists(bak):
        shutil.copy2(path, bak)
    st.d['files'].setdefault(rel, {})['original_sha1'] = sha1(bak)
    st.d['files'][rel]['backup'] = os.path.relpath(bak, game)


def installed(path, st, game, new=False):
    rel = os.path.relpath(path, game)
    e = st.d['files'].setdefault(rel, {})
    e['installed_sha1'] = sha1(path)
    if new:
        e['new_file'] = True


# ---------------------------------------------------------------- parts
def move_old_plugin(game, st, dry):
    """v0.1.1: an earlier install (or a hand install) put the plugin in scripts\\Extended Cut\\. A newer ASI Loader
    loads subfolders too -> two copies would load. The old .asi is renamed to .asi.old (not loaded); the player's
    ExtendedCut.ini moves to scripts\\ when there is none there yet (settings kept)."""
    out = []
    sub = find(game, 'scripts/Extended Cut')
    if sub is None:
        return out
    asi = find(game, 'scripts/Extended Cut/ExtendedCut.asi')
    ini = find(game, 'scripts/Extended Cut/ExtendedCut.ini')
    if asi:
        out.append('plugin: old copy in scripts\\Extended Cut\\ %s' %
                   ('would be retired (renamed to ExtendedCut.asi.old)' if dry else 'retired (renamed to ExtendedCut.asi.old)'))
        if not dry:
            old = asi + '.old'
            if os.path.exists(old):
                os.remove(old)
            os.rename(asi, old)
            st.d['files'].pop(os.path.relpath(asi, game), None)
            st.d['files'].pop(os.path.relpath(os.path.join(sub, 'ExtendedCut_defaults.ini'), game), None)
    if ini and find(game, 'scripts/ExtendedCut.ini') is None:
        out.append('plugin: your settings (ExtendedCut.ini) %s to scripts\\' % ('would move' if dry else 'moved'))
        if not dry:
            shutil.copy2(ini, os.path.join(game, 'scripts', 'ExtendedCut.ini'))
    return out


def part_plugin(game, st, dry):
    out = []
    loader = find(game, 'dinput8.dll')
    if loader is None:
        out.append('plugin: WARNING - no dinput8.dll (Ultimate ASI Loader) next to pun.exe: the plugin will not load. '
                   'Install the Ultimate ASI Loader (x86, dinput8.dll) first.')
    out += move_old_plugin(game, st, dry)
    files = [('ExtendedCut.asi', 'scripts/ExtendedCut.asi'),
             ('ExtendedCut_defaults.ini', 'scripts/ExtendedCut_defaults.ini'),
             ('SDL3.dll', 'SDL3.dll')]
    for src, rel in files:
        s = os.path.join(PKG, src)
        if not os.path.exists(s):
            out.append('plugin: %s missing from this package - skipped' % src)
            continue
        dst = find(game, rel) or os.path.join(game, *rel.split('/'))
        same = os.path.exists(dst) and sha1(dst) == sha1(s)
        if same:
            out.append('plugin: %s up to date' % rel)
            continue
        out.append('plugin: %s %s' % (rel, 'would be updated' if dry and os.path.exists(dst) else
                                      'would be added' if dry else 'updated' if os.path.exists(dst) else 'added'))
        if not dry:
            existed = os.path.exists(dst)
            if existed and not rel.lower().endswith('.asi'):
                backup(dst, st, game)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(s, dst)
            installed(dst, st, game, new=not existed)
    # 0.1.6: level memory default 0 -> 128 MB. An ExtendedCut.ini from an older version still says 0 (= the old
    # default) - raise it ONCE; if the player sets 0 again later, it stays 0.
    ini = find(game, 'scripts/ExtendedCut.ini')
    if ini and not st.d.get('levelpool_128'):
        import re
        txt = open(ini, encoding='latin-1', newline='').read()
        m = re.search(r'(?m)^LevelPoolMB=0(?=\s|;|$)', txt)
        if m:
            if dry:
                out.append('plugin: level memory would be raised to 128 MB (the new default) in ExtendedCut.ini')
            else:
                tmp = ini + '.ec_tmp'
                open(tmp, 'w', encoding='latin-1', newline='').write(txt[:m.start()] + 'LevelPoolMB=128' + txt[m.end():])
                os.replace(tmp, ini)
                st.d['levelpool_128'] = True
                out.append('plugin: level memory raised to 128 MB (the new default) - Extended menu -> Settings to change it')
        elif not dry:
            st.d['levelpool_128'] = True
    return out


LAA = 0x20
GORE_HELP = ('Install "The Punisher Gore Patch 3.7" (US version) first - see PCGamingWiki, The Punisher (2005), '
             'Essential improvements. It brings the pun.exe without copy protection and the tables Extended Cut is '
             'built on. Then run this installer again.')
# known pun.exe files (sha1). The disc exe is the same build wrapped in copy protection (extra sections .cyxohx /
# .vdms, 225 call sites redirected); the plugin's addresses are for the unwrapped code (same as the gore patch exe).
EXE_PLAIN = {'29c68e546a21b83e14fc6183eb3822920e2c4afa': 'US 1.0 without copy protection'}
EXE_PROTECTED = {'357f8e68168376ecd4f4ff3b4157f95862ddd0f1': 'US 1.0 disc exe (copy protected)'}
EXE_OTHER = {'aa7f3aee200e4b9b3f5e1902799ce366d80028e0': 'UK / European exe (Gore Patch 3.7 UK)'}
TABLES = {'370752351c71fde6f72ca7f8f0ee9b9c8ddeec0c': ('gore_us', 'Gore Patch 3.7 (US)'),
          'f5f43af1e27fba09da123d6e335116c32cb20cce': ('retail', 'original retail (no gore patch)'),
          '21c5229628a389ee017d84828b7fdd246a71a910': ('gore_uk', 'Gore Patch 3.7 (UK)')}


def exe_info(game):
    """(ok, text). ok = the plugin can run on it. Checks the header, so a 4 GB-flagged exe is still recognised."""
    exe = find(game, 'pun.exe')
    if exe is None:
        return False, 'pun.exe not found'
    h = sha1(exe)
    if h in EXE_PLAIN:
        return True, EXE_PLAIN[h]
    if h in EXE_PROTECTED:
        return False, EXE_PROTECTED[h] + ' - not supported. ' + GORE_HELP
    if h in EXE_OTHER:
        return False, EXE_OTHER[h] + ' - not supported yet (Extended Cut needs the US exe).'
    d = open(exe, 'rb').read(4096)
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    ts, ns = struct.unpack_from('<I', d, pe + 8)[0], struct.unpack_from('<H', d, pe + 6)[0]
    oh = struct.unpack_from('<H', d, pe + 20)[0]
    names = [d[pe + 24 + oh + 40 * i:pe + 32 + oh + 40 * i].rstrip(b'\0').decode('latin-1') for i in range(ns)]
    if ts == 0x41C1F8A3 and os.path.getsize(exe) >= 2605056 and \
            all(n in ('.text', '.rdata', '.data', '.rsrc', '.decals') for n in names):   # same rule as the plugin
        if '.decals' in names:
            return True, 'US 1.0 with the old Extended Cut exe patch (still works; the plugin does it all now)'
        return True, 'US 1.0 without copy protection (header changed, e.g. 4 GB flag)'
    return False, 'unknown pun.exe (sha1 %s) - not supported. Send this line.' % h


def tables_info(game):
    tv = find(game, 'dvd_pc/data/tables.vpp')
    if tv is None:
        return None, 'dvd_pc/data/tables.vpp not found'
    h = sha1(tv)
    if h in TABLES:
        return TABLES[h][0], TABLES[h][1]
    st = State(game)
    rel = os.path.relpath(tv, game)
    if st.d['files'].get(rel, {}).get('installed_sha1') == h:
        return 'ec', 'Extended Cut tables (installed)'
    return 'unknown', 'changed by another mod (sha1 %s)' % h


def setup_report(game):
    ok, et = exe_info(game)
    kind, tt = tables_info(game)
    out = ['setup: pun.exe = ' + et, 'setup: tables.vpp = ' + tt]
    if find(game, 'dinput8.dll') is None:
        out.append('setup: no dinput8.dll (Ultimate ASI Loader) - the plugin will not load. The Widescreen Fix '
                   '(PCGamingWiki) brings it.')
    return ok, kind, out


def part_laa(game, st, dry):
    exe = find(game, 'pun.exe')
    if exe is None:
        return ['laa: pun.exe not found']
    h = sha1(exe)
    if h in EXE_PROTECTED:
        return ['laa: pun.exe is the copy-protected disc exe - left alone. ' + GORE_HELP]
    d = bytearray(open(exe, 'rb').read())
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if d[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', d, pe + 4)[0] != 0x14C:
        return ['laa: pun.exe is not the expected 32-bit program - left alone']
    ch = struct.unpack_from('<H', d, pe + 22)[0]
    if ch & LAA:
        return ['laa: pun.exe already uses 4 GB']
    if h not in EXE_PLAIN:
        return ['laa: pun.exe is not a version we know (sha1 %s) - left alone. Send this line.' % h]
    if dry:
        return ['laa: pun.exe would get the 4 GB flag']
    backup(exe, st, game)
    struct.pack_into('<H', d, pe + 22, ch | LAA)
    open(exe, 'wb').write(d)
    installed(exe, st, game)
    return ['laa: pun.exe now uses 4 GB (original saved as pun.exe.ec_original)']


def part_tables(game, st, dry):
    import tblpatch
    tv = find(game, 'dvd_pc/data/tables.vpp')
    patch = os.path.join(PKG, 'tables.patch.json')
    if tv is None:
        return ['tables: dvd_pc/data/tables.vpp not found']
    if not os.path.exists(patch):
        return ['tables: tables.patch.json missing from this package - skipped']
    kind, text = tables_info(game)
    if kind == 'retail':
        return ['tables: your tables.vpp is the original retail one - NOT installed (nothing changed). ' + GORE_HELP]
    if kind == 'gore_uk':
        return ['tables: your tables.vpp is from the UK gore patch - not supported yet (nothing changed).']
    failed, rep = tblpatch.apply(tv, patch, None, dry=True)
    out = ['tables: ' + r for r in rep]
    # 0.1.4: an update. The tables.vpp an older Extended Cut patched does not take the new patch on top - if the file
    # is still exactly what we installed, build the new version from the saved original instead.
    e = st.d['files'].get(os.path.relpath(tv, game), {})
    bak = os.path.join(game, e['backup']) if e.get('backup') else tv + '.ec_original'
    if failed and os.path.exists(bak) and e.get('installed_sha1') == sha1(tv):
        f2, rep2 = tblpatch.apply(bak, patch, None, dry=True)
        if not f2:
            if dry:
                return ['tables: would be updated to this version (built again from your original tables.vpp)']
            tmp = tv + '.ec_tmp'
            tblpatch.apply(bak, patch, tmp)
            os.replace(tmp, tv)
            installed(tv, st, game)
            return ['tables: updated to this version (built again from your original, tables.vpp.ec_original)']
    if failed:
        out.append('tables: %d changes do not fit this tables.vpp - NOT installed (nothing changed). Send this report.'
                   % failed)
        return out
    if all(r.endswith('already patched') or 'already there' in r for r in rep):
        return out[:0] + ['tables: already installed']
    if dry:
        return out + ['tables: would be patched']
    backup(tv, st, game)
    tmp = tv + '.ec_tmp'
    tblpatch.apply(tv, patch, tmp)
    os.replace(tmp, tv)
    installed(tv, st, game)
    return out + ['tables: patched (original saved as tables.vpp.ec_original)']


def part_blowtorch(game, st, dry):
    """the cut blowtorch interrogation in Chop Shop 1 (level edit built from the player's own Chop1.vpp)"""
    import ec_levels
    cv = find(game, 'dvd_pc/data/shipping/Chop/Chop1.vpp')
    if cv is None:
        return ['blowtorch: dvd_pc/data/shipping/Chop/Chop1.vpp not found']
    state = ec_levels.blowtorch_state(cv)
    if state == 'installed':
        return ['blowtorch: already installed']
    if state == 'refit':                                     # 0.1.6: our older install - torch hand fit
        if dry:
            return ['blowtorch: would move the blowtorch spot a few cm (the torch now sits in the hand)']
        try:
            data = ec_levels.refit_blowtorch(cv)
        except Exception as ex:
            return ['blowtorch: hand fit could not be updated (%s) - left as it was' % ex]
        backup(cv, st, game)
        tmp = cv + '.ec_tmp'
        open(tmp, 'wb').write(data)
        os.replace(tmp, cv)
        installed(cv, st, game)
        return ['blowtorch: spot moved a few cm - the torch now sits in the Punisher\'s hand']
    if state == 'old':
        return ['blowtorch: an older blowtorch restoration is already in Chop1.vpp - left alone']
    if state != 'ready':
        return ['blowtorch: ' + state + ' - NOT installed (nothing changed)']
    srcs = [find(game, 'dvd_pc/data/shipping/dclub/dclub1.vpp'), find(game, 'dvd_pc/data/shipping/zoo/zoo3.vpp')]
    try:
        data = ec_levels.build_blowtorch(cv, srcs)
    except Exception as ex:
        return ['blowtorch: could not be built (%s) - NOT installed (nothing changed)' % ex]
    if dry:
        return ['blowtorch: Chop1.vpp would get the blowtorch interrogation']
    backup(cv, st, game)
    tmp = cv + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, cv)
    installed(cv, st, game)
    return ['blowtorch: Chop1.vpp now has the blowtorch interrogation (original saved as Chop1.vpp.ec_original)']


def part_hooks(game, st, dry):
    """0.2: the meat-hook quick kill on the 23 wall hooks of Meat1 (see ec_hooks.py); 0.3: + the hook kill's blood
    effects for the zoo1 tiki torch kill (zoo1.vpp)"""
    import ec_hooks
    mv = find(game, 'dvd_pc/data/shipping/Meat/Meat1.vpp')
    if mv is None:
        return ['hooks: dvd_pc/data/shipping/Meat/Meat1.vpp not found']
    zv, mz = find(game, 'dvd_pc/data/shipping/zoo/zoo1.vpp'), find(game, 'dvd_pc/data/shipping/Mansion/Mansion3.vpp')
    out = []
    state = ec_hooks.hooks_state(mv)
    if state == 'installed':
        out.append('hooks: already installed')
    elif state != 'ready':
        return ['hooks: ' + state + ' - NOT installed (nothing changed)']
    elif zv is None or mz is None:
        return ['hooks: zoo1.vpp or Mansion3.vpp not found (the kill and its blood pool come from them) - NOT installed']
    else:
        try:
            data = ec_hooks.build_hooks(mv, zv, mz)
        except Exception as ex:
            return ['hooks: could not be built (%s) - NOT installed (nothing changed)' % ex]
        if dry:
            out.append('hooks: Meat1.vpp would get the meat-hook quick kill (23 hooks)')
        else:
            backup(mv, st, game)
            tmp = mv + '.ec_tmp'
            open(tmp, 'wb').write(data)
            os.replace(tmp, mv)
            installed(mv, st, game)
            out.append('hooks: Meat1.vpp now has the meat-hook quick kill (original saved as Meat1.vpp.ec_original)')
    # 0.3: the tiki kill's blood (drip + pool effects and the pool texture, from Meat1 / Mansion3)
    if zv is None or mz is None:
        return out + ['hooks: zoo1.vpp or Mansion3.vpp not found - the tiki kill gets no drip / pool']
    ts = ec_hooks.tiki_state(zv)
    if ts == 'installed':
        return out + ['hooks: zoo1.vpp already has the tiki kill blood']
    if ts != 'ready':
        return out + ['hooks: ' + ts + ' - tiki kill blood NOT installed']
    try:
        data = ec_hooks.build_tiki(zv, mv, mz)
    except Exception as ex:
        return out + ['hooks: tiki kill blood could not be built (%s) - zoo1.vpp not changed' % ex]
    if dry:
        return out + ['hooks: zoo1.vpp would get the tiki kill blood (drip + floor pool)']
    backup(zv, st, game)
    tmp = zv + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, zv)
    installed(zv, st, game)
    return out + ['hooks: zoo1.vpp now has the tiki kill blood (original saved as zoo1.vpp.ec_original)']


def part_stumps(game, st, dry):
    """0.1.5: restored gib stumps + variants, ADDED to the player's own mini.vpp (see ec_stumps.py)"""
    import ec_stumps
    vp = find(game, 'dvd_pc/data/mini.vpp')
    if vp is None:
        return ['stumps: dvd_pc/data/mini.vpp not found']
    state = ec_stumps.state(vp)
    if state == 'installed':
        return ['stumps: already installed']
    if state != 'ready':
        return ['stumps: ' + state + ' - NOT installed (nothing changed)']
    try:
        data, added = ec_stumps.build(vp)
    except Exception as ex:
        return ['stumps: could not be built (%s) - NOT installed (nothing changed)' % ex]
    if dry:
        return ['stumps: mini.vpp would get %d restored / variant stump models (the game\'s own stay)' % len(added)]
    backup(vp, st, game)
    tmp = vp + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, vp)
    installed(vp, st, game)
    return ['stumps: mini.vpp now has %d restored / variant stump models (Gore -> Gib stumps; original saved as '
            'mini.vpp.ec_original)' % len(added)]


def broken_banks(vp):
    """texture banks (.ceg) in a vpp whose header is not GEKV"""
    try:
        import vpp_pack
        import vpppatch
        a = vpp_pack.read(vp)
        out = []
        for f in a['files']:
            n = f['raw'].split(bytes(1))[0].decode('latin-1')
            if n.lower().endswith('.ceg') and vpppatch.get_entry(a, n)[:4] != b'GEKV':
                out.append(n)
        return out
    except Exception:
        return []


def repair_bank_headers(vp, bak, names):
    """0.1.1: the 0.1 bug only destroyed the 32-byte bank header (directory, count and data are fine). Take the header
    from the same bank in the player's saved original; frame count = original + the textures added since."""
    import struct
    import vpp_pack
    import vpppatch
    try:
        a, o = vpp_pack.read(vp), vpp_pack.read(bak)
        for n in names:
            b = bytearray(vpppatch.get_entry(a, n))
            ob = vpppatch.get_entry(o, n)
            if ob is None or ob[:4] != b'GEKV':
                return False, 'could not be repaired (%s has no original) - use Uninstall, then Install' % n
            cnt, ocnt = struct.unpack_from('<I', b, 16)[0], struct.unpack_from('<I', ob, 16)[0]
            if not (ocnt <= cnt <= ocnt + 200) or struct.unpack_from('<I', b, 8)[0] != 48 * cnt:
                return False, 'could not be repaired (%s) - use Uninstall, then Install' % n
            frames = struct.unpack_from('<I', ob, 24)[0] + (cnt - ocnt)
            dsize, dirsize = struct.unpack_from('<I', b, 12)[0], 48 * cnt
            b[0:32] = ob[0:32]
            struct.pack_into('<I', b, 8, dirsize)
            struct.pack_into('<I', b, 12, dsize)
            struct.pack_into('<I', b, 16, cnt)
            struct.pack_into('<I', b, 24, frames)
            vpppatch.bank_entries(bytes(b))              # still readable
            vpp_pack.put(a, n, bytes(b))
        tmp = vp + '.ec_tmp'
        open(tmp, 'wb').write(vpp_pack.build(a))
        os.replace(tmp, vp)
        return True, 'repaired (your other changes kept)'
    except Exception as ex:
        return False, 'could not be repaired (%s) - use Uninstall, then Install' % ex


def _vpp_part(tag, rel, patch_name, what, game, st, dry):
    import vpppatch
    vp = find(game, rel)
    patch = os.path.join(PKG, patch_name)
    if vp is None:
        return ['%s: %s not found' % (tag, rel)]
    if not os.path.exists(patch):
        return ['%s: %s missing from this package - skipped' % (tag, patch_name)]
    pre = []
    # 0.1.1: installer 0.1 wrote a broken texture bank into mini.vpp (all HUD pictures drawn as squares). If the file is
    # still exactly what we installed, start again from the saved original.
    rel_vp = os.path.relpath(vp, game)
    e = st.d['files'].get(rel_vp, {})
    bak = os.path.join(game, e['backup']) if e.get('backup') else vp + '.ec_original'
    broken = broken_banks(vp) if os.path.exists(bak) else []
    if broken and e.get('installed_sha1') == sha1(vp):
        if dry:
            return ['%s: %s has a broken picture bank from installer 0.1 - would be rebuilt from your original'
                    % (tag, os.path.basename(vp))]
        shutil.copy2(bak, vp)
        pre.append('%s: broken picture bank from installer 0.1 found - original put back, building again' % tag)
    elif broken:
        # changed since (e.g. the Costume Launcher applied a costume): keep its pictures, only mend the bank header
        if dry:
            return ['%s: %s has a broken picture bank from installer 0.1 - would be repaired' % (tag, os.path.basename(vp))]
        ok, msg = repair_bank_headers(vp, bak, broken)
        pre.append('%s: broken picture bank from installer 0.1 %s' % (tag, msg))
        if not ok:
            return pre
    bad, rep = vpppatch.apply(vp, patch, None, dry=True)
    if bad:
        return pre + ['%s: %s does not fit (%s) - NOT installed (nothing changed). Send this report.'
                % (tag, os.path.basename(vp), '; '.join(bad[:3]))]
    if 'nothing to change' in rep:
        return pre + ['%s: already installed' % tag]
    if dry:
        return pre + ['%s: %s would get %s' % (tag, os.path.basename(vp), what)]
    backup(vp, st, game)
    tmp = vp + '.ec_tmp'
    bad, rep = vpppatch.apply(vp, patch, tmp)
    if bad or not os.path.exists(tmp):
        return pre + ['%s: failed (%s) - nothing changed' % (tag, '; '.join(bad[:3]))]
    os.replace(tmp, vp)
    installed(vp, st, game)
    return pre + ['%s: %s now has %s (original saved as %s.ec_original)' % (tag, os.path.basename(vp), what, os.path.basename(vp))]


def part_font(game, st, dry):
    return _vpp_part('font', 'dvd_pc/data/misc.vpp', 'misc.patch.json', 'the Xbox / PlayStation / Steam Deck button icons',
                     game, st, dry)


def part_pictures(game, st, dry):
    return _vpp_part('pictures', 'dvd_pc/data/mini.vpp', 'mini.patch.json',
                     'the stick pictures, rebuilt icons and the early development HUD', game, st, dry)


def part_blackcolt(game, st, dry):
    """0.2: the Punisher-only black Colt - 3rd-colt_pun + ws_colt_pun ADDED to the player's own mini.vpp, made from his
    own Colt texture + our paint data (see ec_blackcolt.py); the plugin switches it on / off (Weapons page)"""
    import ec_blackcolt
    vp = find(game, 'dvd_pc/data/mini.vpp')
    if vp is None:
        return ['blackcolt: dvd_pc/data/mini.vpp not found']
    state = ec_blackcolt.state(vp, game)
    if state == 'installed':
        return ['blackcolt: already installed']
    if state == 'tinted':                                   # 0.3: the 0.2 black Colt had green spots in the game
        if dry:
            return ['blackcolt: the black Colt in mini.vpp would get true black (no green spots)']
        data = ec_blackcolt.regrey(vp)
        backup(vp, st, game)
        tmp = vp + '.ec_tmp'
        open(tmp, 'wb').write(data)
        os.replace(tmp, vp)
        installed(vp, st, game)
        return ['blackcolt: the black Colt is now true black (no green spots)']
    if state != 'ready':
        return ['blackcolt: ' + state + ' - NOT installed (nothing changed)']
    try:
        data, src, n = ec_blackcolt.build(vp, game)
    except Exception as ex:
        return ['blackcolt: could not be built (%s) - NOT installed (nothing changed)' % ex]
    if dry:
        return ['blackcolt: mini.vpp would get the black Colt for the Punisher (made from %s)' % src]
    backup(vp, st, game)
    tmp = vp + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, vp)
    installed(vp, st, game)
    return ['blackcolt: mini.vpp now has the black Colt (%d texture(s), made from your %s; Extended menu -> Costume -> Weapon Skin -> '
            'Colt .45: 04 Movie)' % (n, src)]


def part_menuart(game, st, dry):
    """1.0: the Extended menu's art (layout B5) - 4 of the game's own menu pictures copied into the player's mini.vpp
    always_loaded.ceg (see ec_menuart.py). Without it the menu falls back to the plain text layout."""
    import ec_menuart
    vp = find(game, 'dvd_pc/data/mini.vpp')
    if vp is None:
        return ['menuart: dvd_pc/data/mini.vpp not found']
    state = ec_menuart.state(vp)
    if state == 'installed':
        return ['menuart: already installed']
    if state != 'ready':
        return ['menuart: ' + state + ' - NOT installed (nothing changed)']
    try:
        data, n = ec_menuart.build(vp, game)
    except Exception as ex:
        return ['menuart: could not be built (%s) - NOT installed (nothing changed)' % ex]
    if dry:
        return ['menuart: mini.vpp would get the Extended menu pictures (from your own misc.vpp / mini.vpp)']
    backup(vp, st, game)
    tmp = vp + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, vp)
    installed(vp, st, game)
    return ['menuart: mini.vpp now has the Extended menu pictures (%d)' % n]


def part_outfits(game, st, dry):
    """0.3: the outfit switch (Extended menu -> Costume -> Outfit) - both Punisher outfits' model, face animations and
    textures ADDED to the player's own mini.vpp, copied from his own level files (see ec_outfits.py)"""
    import ec_outfits
    vp = find(game, 'dvd_pc/data/mini.vpp')
    if vp is None:
        return ['outfits: dvd_pc/data/mini.vpp not found']
    state = ec_outfits.state(vp, game)
    if state == 'installed':
        return ['outfits: already installed']
    if state != 'ready':
        return ['outfits: ' + state + ' - NOT installed (nothing changed)']
    try:
        data, src = ec_outfits.build(vp, game)
    except Exception as ex:
        return ['outfits: could not be built (%s) - NOT installed (nothing changed)' % ex]
    if dry:
        return ['outfits: mini.vpp would get the trench coat + T-shirt outfits (from your %s)' % src]
    backup(vp, st, game)
    tmp = vp + '.ec_tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, vp)
    installed(vp, st, game)
    return ['outfits: mini.vpp now has both outfits (from your %s; Extended menu -> Costume -> Outfit)' % src]


PARTS = {'plugin': part_plugin, 'laa': part_laa, 'tables': part_tables, 'font': part_font, 'pictures': part_pictures,
         'blowtorch': part_blowtorch, 'hooks': part_hooks, 'stumps': part_stumps, 'blackcolt': part_blackcolt,
         'menuart': part_menuart, 'outfits': part_outfits}


def uninstall(game):
    st = State(game)
    out = []
    for rel, e in sorted(st.d['files'].items()):
        p = os.path.join(game, rel)
        if not os.path.exists(p):
            out.append('%s: not there any more' % rel)
            continue
        if sha1(p) != e.get('installed_sha1'):
            out.append('%s: changed since install (another mod?) - left alone' % rel)
            continue
        if e.get('new_file'):
            os.remove(p)
            out.append('%s: removed' % rel)
        elif e.get('backup') and os.path.exists(os.path.join(game, e['backup'])):
            shutil.copy2(os.path.join(game, e['backup']), p)
            out.append('%s: original put back' % rel)
        else:
            out.append('%s: no original saved - left alone' % rel)
    st.d['files'] = {}
    st.save()
    return out


def _main(argv):
    if len(argv) < 2 or argv[0] not in ('check', 'install', 'uninstall'):
        print(__doc__)
        return 2
    cmd, game = argv[0], argv[1]
    if find(game, 'pun.exe') is None:
        print('No pun.exe in "%s" - give the game folder (the one with pun.exe).' % game)
        return 2
    if cmd == 'uninstall':
        print('\n'.join(uninstall(game)))
        return 0
    parts = argv[2:] or list(PARTS)
    bad = [p for p in parts if p not in PARTS]
    if bad:
        print('Unknown part(s): %s. Parts: %s' % (', '.join(bad), ', '.join(PARTS)))
        return 2
    ok, kind, out = setup_report(game)
    if not ok:
        print('\n'.join(out + ['Nothing was changed: Extended Cut needs the US pun.exe without copy protection.']))
        return 1
    st = State(game)
    for p in parts:
        out += PARTS[p](game, st, dry=cmd == 'check')
    if cmd == 'install':
        st.d['version'] = VERSION
        st.save()
    print('\n'.join(out))
    return 0


def main(argv):
    """0.1.1: everything printed also goes to <game>/scripts/ExtendedCut_install.log (testers send it with the plugin
    logs). The newest run is added at the end."""
    import io
    import contextlib
    import datetime
    buf = io.StringIO()

    class Tee(io.TextIOBase):
        def write(self, s):
            buf.write(s)
            return sys.__stdout__.write(s) if sys.__stdout__ else len(s)

        def flush(self):
            if sys.__stdout__:
                sys.__stdout__.flush()

    with contextlib.redirect_stdout(Tee()):
        code = _main(argv)
    try:
        game = argv[1] if len(argv) > 1 else None
        if game and find(game, 'pun.exe') is not None:
            d = os.path.join(game, 'scripts')
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, 'ExtendedCut_install.log'), 'a', encoding='utf-8') as f:
                f.write('=== %s  installer %s  %s\n%s\n' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), VERSION,
                                                              argv[0], buf.getvalue().rstrip()))
    except OSError:
        pass
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
