r"""The Punisher: Extended Cut - outfit switch data (release part "outfits", plugin v1.10+).

The plugin's Extended menu -> Costume -> Outfit puts the Punisher in the trench coat or the T-shirt in ANY level
(the level file only says which one it gives him). The other outfit's files are not in that level, so this part
copies them FROM THE PLAYER'S OWN LEVEL FILES into mini.vpp (loaded by every level):
  punisher_tshirt.rxc + punisher_tee_faces.rmx + punisher_tee_speech.rmx       (a T-shirt level, e.g. Apart1.vpp)
  punisher_trenchcoat.rxc + punisher_faces1.rmx + punisher_speech1.rmx        (a coat level, e.g. bar1.vpp)
  ec_outfit_tshirt.ceg  = pun_tshirt_01..13 textures                           (the plugin loads the bank of the
  ec_outfit_trench.ceg  = punps2_trench01..12 + pun_tshirt_13 textures          outfit a level does not carry)
Every piece is checked against the retail fingerprints below; level files the Costume Launcher changed are skipped
and the launcher's backup copies / the installer's .ec_original copies are used instead. Nothing from the game is
shipped with the release.
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
sys.path.insert(0, HERE)
import vpppatch as V  # noqa: E402
import vpp_pack  # noqa: E402

MARK = 'ec_outfit_tshirt.ceg'
FILES = [('punisher_tshirt.rxc', 'tee'), ('punisher_tee_faces.rmx', 'tee'), ('punisher_tee_speech.rmx', 'tee'),
         ('punisher_trenchcoat.rxc', 'coat'), ('punisher_faces1.rmx', 'coat'), ('punisher_speech1.rmx', 'coat')]
TEE_TEX = ['pun_tshirt_%02d.tga' % k for k in range(1, 14)]
COAT_TEX = ['punps2_trench%02d.tga' % k for k in range(1, 13)] + ['pun_tshirt_13.tga']
PREFER = {'tee': ('Apart/Apart1.vpp', 'Apart/ApartA.vpp', 'Apart/ApartB.vpp'),
          'coat': ('bar/bar1.vpp', 'bar/bara.vpp', 'nycrack/nycrack1.vpp', 'zoo/zoo1.vpp')}
RETAIL = {   # retail SHA-1 of every piece (texture: its pixel data, DXT5 = w*h bytes) - any of them is accepted
    'pun_tshirt_01.tga': {'0f38a5069dd44c3dec5d09ab0a49deb97e26baf9', '17ba08ca900c28761585d6a75f52ba355f5968e5', '18cb977ca5cc13a200534553bf5766e2a3bcaf49', '225f24ac2f10b7646fd2064ec37dcdb2d0a05ef1', '38a1a5b0bc4a7c8199c1fd5610c30d4dab81a3ed', '70cc3df5e69f99377903c653bcb07f08e33dab17', '746389e5291be966a2d8af3ac38e780cbfec3fa2', 'b56a38715536e431698b3ba3ae3cc691c234b200', 'd35938139f1b787c35e683900305a879e79b07da', 'd594b6b9f8cfecec0bf8c1ba5d94a5a1d889a79c', 'db08bac638ebf6b35d77a97206c18c851496fc72'},
    'pun_tshirt_02.tga': {'d9ef784c2f4335ba2c14e42ae3e6fbda465038ba'},
    'pun_tshirt_03.tga': {'8f6ca5541ee218d9e84200dca6c65a80d1935e49'},
    'pun_tshirt_04.tga': {'f6bb56e5c11b809266f3af330803b24deab58110'},
    'pun_tshirt_05.tga': {'8b59128252288d25a35a92f20f26d4ff57ea27d2'},
    'pun_tshirt_06.tga': {'1822f6bef4386a967eb74089ac4e4f1fa298b8fe'},
    'pun_tshirt_07.tga': {'72d99a7773680ae575ab1233087ec326ffa69a84'},
    'pun_tshirt_08.tga': {'cf2e1005a77b4e3fa3642e297654c46254b9f045'},
    'pun_tshirt_09.tga': {'7ebd5933294ca9527a6d8767e554ca1db7f1251f'},
    'pun_tshirt_10.tga': {'b034c4b9cd2ac2b970e5c6f8e6e9bae4f3bc3fb0'},
    'pun_tshirt_11.tga': {'f4ae0b0da3c453d956ae41648218f9631b83d519'},
    'pun_tshirt_12.tga': {'d03c2ca3f4d25f433f2a57f53741000a36aa341f'},
    'pun_tshirt_13.tga': {'2594f47fd5997a6ad4558c168b92a4dea8747de5', '95000f78e70df7644be552c3c61808c53e76db83', 'de5fb0b5d721fcf9b0c27f4d6eac7aae04c13142'},
    'punisher_faces1.rmx': {'fdf1468b8a4ac5e7da21c2dcfa6296383cdff5eb'},
    'punisher_speech1.rmx': {'474e45328057132271c75e2829e27b376dd4b862'},
    'punisher_tee_faces.rmx': {'b2d55bca3c9e8f7b9cf315399b8e45115def83a1'},
    'punisher_tee_speech.rmx': {'723cf0b5e328e84725c835717f6de691066f167d'},
    'punisher_trenchcoat.rxc': {'b71d8f193c400ea12bed4ed4c21ff1d1d130a71c'},
    'punisher_tshirt.rxc': {'66b00f667e9073717726b808fad943cddf4f9367'},
    'punps2_trench01.tga': {'007a0a84c10a0e8a9e977fc76d238d42896aba20', '03b396f25f9cf9eefc08aceb54ded08f494328c0', '0423bf0407c5646cec45e21e4722323ee8bd7337', '0780c4ff526d82d9f10a98e740df407b84f47272', '0ca8aa7bf33d9fe8c46742ec21187bf13dc8ea6d', '0f38a5069dd44c3dec5d09ab0a49deb97e26baf9', '225f24ac2f10b7646fd2064ec37dcdb2d0a05ef1', '37f07ddd186ad448c672cdd5c5665d1a1031b18a', '38a1a5b0bc4a7c8199c1fd5610c30d4dab81a3ed', '3cd0b48c53fd76101571309cd7599ad3a3157590', '3d4b7e86558e75040aadf23a8bcc9df41b48d70a', '4cfc65f3f99a2690cf036d796e914e657495787b', '55847c92c6e1d4486897d71c61e4ff11365aea51', '5ae319af32ece2c1a84900eaabea0be5f7c40d34', '70cc3df5e69f99377903c653bcb07f08e33dab17', '746389e5291be966a2d8af3ac38e780cbfec3fa2', '7ca9da9aa6112a679f22b3ef458e4379249e0599', '870248cacb3ebd83068564ce674d684eb7836b6d', '9c23eebe1fe34bc8df8beab54c67e621e7ec1839', 'aa5736cfd5821a14e468f1d5438c82278b2bb623', 'af6a096ae4938f3e7485175249274782667a42bd', 'b56a38715536e431698b3ba3ae3cc691c234b200', 'bef0d3be5e78eacf588e67b86aa7e5081a7458d5', 'cd4d7f061df73617a46abe6f8aabd530bf6906c1', 'ce8739194e79458dac92c923f8a4ee8cfa731f5e', 'db08bac638ebf6b35d77a97206c18c851496fc72', 'e9c9b3eeac1e3b64e7d6d2b48457bccab2ef4407', 'eb849eef0ff9b44c65186aa7ee492915a05d0f6b'},
    'punps2_trench02.tga': {'9fc3d839a2ba6f4cadc9552164f6bc5c1f26d552'},
    'punps2_trench03.tga': {'8f6ca5541ee218d9e84200dca6c65a80d1935e49'},
    'punps2_trench04.tga': {'a1607a93e11daf9e85e2d173e9fed0df02cb557a'},
    'punps2_trench05.tga': {'e7f8a6f7c752320c7b420ce75c0bf9d7e7458578'},
    'punps2_trench06.tga': {'2edc11f8ef0d821b078741c5e6e5bab3c48e2319'},
    'punps2_trench07.tga': {'61bf4cabd2bb71b948827da8f4024dd90f1c034e'},
    'punps2_trench08.tga': {'8586b20a89bbc2603f888dfb7eb4ba85140fc318'},
    'punps2_trench09.tga': {'e86c290f0aabfe4df383b4d08fc4addc5418735e'},
    'punps2_trench10.tga': {'b034c4b9cd2ac2b970e5c6f8e6e9bae4f3bc3fb0'},
    'punps2_trench11.tga': {'209a863aa1c94fe27b3d4d42a4294d084e600c47', 'bbda3e7cca7828ef4170ebf2e905b75cefcbcc6e'},
    'punps2_trench12.tga': {'39026851934532c5b7009bb12a31ac964973b1b7', '9d05cdff132cef2dc2a698efb87643e12a61ae9b', 'e3ec629cefb61790397ca572fd77513fd9a40269', 'e52e79332a6d220b6bd91b46b5d54fdbb5e1954e'},
}


def _sha(b):
    return hashlib.sha1(b).hexdigest()


def _tex_sha(e):
    f = e['fmt'][0]
    n = e['w'] * e['h'] * (4 if f in (6, 7) else 1)
    return _sha(e['data'][:n])


def _candidates(game, kind):
    """level files to look in: the preferred ones first, then every level file, each also as the launcher's backup
    copy and as the installer's .ec_original"""
    ship = os.path.join(game, 'dvd_pc', 'data', 'shipping')
    out = []
    for rel in PREFER[kind]:
        out.append(os.path.join(ship, *rel.split('/')))
    for dp, dn, fn in os.walk(ship):
        out += [os.path.join(dp, f) for f in sorted(fn) if f.lower().endswith('.vpp')]
    more = []
    bk = os.path.join(game, 'PunisherCostumes', 'backup')
    for p in out:
        more.append(p)
        more.append(p + '.ec_original')
        more.append(os.path.join(bk, os.path.basename(p)))
    seen, res = set(), []
    for p in more:
        k = os.path.normcase(p)
        if k not in seen and os.path.isfile(p):
            seen.add(k); res.append(p)
    return res


def collect(game):
    """-> ({file name: bytes}, {texture name: bank entry}, {piece: where it came from}); raises if a piece is missing"""
    files, tex, src = {}, {}, {}
    need_f = {n: k for n, k in FILES}
    need_t = {n: 'tee' for n in TEE_TEX}
    for n in COAT_TEX:
        need_t.setdefault(n, 'coat')
    for kind in ('tee', 'coat'):
        for vp in _candidates(game, kind):
            wf = [n for n, k in need_f.items() if k == kind and n not in files]
            wt = [n for n, k in need_t.items() if n not in tex]
            if not wf and not wt:
                break
            try:
                a = vpp_pack.read(vp)
            except Exception:
                continue
            for n in wf:
                b = V.get_entry(a, n)
                if b is not None and _sha(b) in RETAIL[n]:
                    files[n] = b; src[n] = vp
            if wt:
                for f in a['files']:
                    nm = f['raw'].split(bytes(1))[0].decode('latin-1').lower()
                    if not nm.endswith('.ceg'):
                        continue
                    b = V.get_entry(a, nm)
                    if not b or b[:4] != b'GEKV':
                        continue
                    for e in V.bank_entries(b):
                        t = V.tname(e)
                        if t in wt and t not in tex and _tex_sha(e) in RETAIL[t]:
                            tex[t] = e; src[t] = vp
    miss = [n for n, _ in FILES if n not in files] + [n for n in TEE_TEX + COAT_TEX if n not in tex]
    if miss:
        raise ValueError('the game\'s own %s not found in any level file (or only changed copies)' % ', '.join(sorted(set(miss))[:4]))
    return files, tex, src


def make_bank(tex, names):
    n = len(names); start = 32 + 48 * n; body = bytearray(); dirs = bytearray()
    for nm in names:
        e = tex[nm]
        dirs += struct.pack('<IHH', start + len(body), e['w'], e['h']) + e['fmt'] + nm.encode('latin-1').ljust(32, bytes(1))
        body += e['data']
    return b'GEKV' + struct.pack('<IIIIIII', 1, 48 * n, len(body), n, 0, n, 0x80) + bytes(dirs) + bytes(body)


def state(mini, game):
    try:
        a = vpp_pack.read(mini)
    except Exception as ex:
        return 'mini.vpp could not be read (%s)' % ex
    if vpp_pack.index_of(a, MARK) >= 0:
        return 'installed'
    return 'ready'


def build(mini, game):
    """-> (new mini.vpp bytes, short source note)"""
    files, tex, src = collect(game)
    a = vpp_pack.read(mini)
    for n, _ in FILES:
        vpp_pack.put(a, n, files[n])
    vpp_pack.put(a, 'ec_outfit_tshirt.ceg', make_bank(tex, TEE_TEX))
    vpp_pack.put(a, 'ec_outfit_trench.ceg', make_bank(tex, COAT_TEX))
    used = sorted(set(os.path.basename(p) for p in src.values()))
    return vpp_pack.build(a), ', '.join(used[:4]) + (' ...' if len(used) > 4 else '')
