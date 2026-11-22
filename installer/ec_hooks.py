"""Extended Cut release - the MEAT HOOK quick kill (Meat Packing Plant 1). Rebuilds the player's own Meat1.vpp from
records and files of the player's own game; only the numbers below ship (hook positions, offsets, names).

  - 23 hotspots "Hook_Impale_NN" (copies of zoo1's "Tiki_Torch_Impale" hotspot, group renamed "Meat Hook Impale")
    + their triggers "Hook_Impale_NN_Trigger" (copies of zoo1's "Tiki Kill Trigger 2", use limit 1), one per wall hook
    of the level's own racks (Meat1.dbr, prototypes 41 / 42 - positions measured from the mesh, Nov 18 2026);
  - the kill's files from the player's zoo1.vpp: pnsh_ek_tikikill.rfa, male_ek_tikikill.rfa, env_kill_tiki.rxe;
  - male_ek_hookkill.rfa = male_ek_tikikill.rfa with the body eased 6 cm further onto the hook during the lift
    (normal thugs; fat thugs keep the zoo anim - tables group "Meat Hook Impale", see make_release_tables.py);
  - env_kill_bear_trp.rxe from the player's Mansion3.vpp, reduced to its floor pool (the plugin adds the drip).
Same result as recipe tools\\5 Cut hunt\\recipes\\meat1_hook_impale.py "all" v10 (Deck tests 7-13, Nov 2026).
"""
import struct
import zlib

import vpppatch as V  # noqa: E402  (first: it adds the dev tools folder for vpp_pack)
import vpp_pack
from rfl_edit import EditableLevel, Record, TRIGGER_BRACKET_OFF

ZOO_HOT, ZOO_TRG_NAME = 0x14000C0, 'Tiki Kill Trigger 2'
GROUP_OLD, GROUP_NEW = 'Zoo Tiki Impale', 'Meat Hook Impale'
CHEST_OUT_FROM_MESH_TIP = 0.222        # the author-approved fit (test 5 + 10 cm)
# v11 (Nov 21, Deck v1.35 + v1.36: both kills on hook 17 hung 10 cm deeper than on every other hook - the back in the
# rack bar; same spot geometry as its neighbours): per-hook extra distance out from the tip for the chest
HOOK_EXTRA_OUT = {}   # test 14 had {17: 0.10} - undone: the deep kills are random (~1 in 5), not that hook
CHEST_MODEL = (-0.65, 0.33)            # male-spine1 (x, z) in the kill's model space at the end
FLOOR_Y = 0.01
HOOK_DEPTH, HOOK_T0, HOOK_T1 = 0.06, 132, 156
ZOO_FILES = ('pnsh_ek_tikikill.rfa', 'male_ek_tikikill.rfa', 'env_kill_tiki.rxe')
HOOKS = [   # id, mesh tip (x, z), direction out of the wall (x, z), room
    (0, -68.750, 48.500, -1, 0, 5), (1, -68.750, 50.500, -1, 0, 5), (2, -68.750, 52.500, -1, 0, 5),
    (3, -68.750, 38.500, -1, 0, 5), (4, -68.750, 40.500, -1, 0, 5), (5, -68.750, 42.500, -1, 0, 5),
    (6, -68.750, 0.500, -1, 0, 20), (7, -68.750, 2.500, -1, 0, 20), (8, -68.750, 4.500, -1, 0, 20),
    (9, -68.750, -9.500, -1, 0, 20), (10, -68.750, -7.500, -1, 0, 20), (11, -68.750, -5.500, -1, 0, 20),
    (12, -88.625, 12.250, 0, -1, 20), (13, -86.625, 12.250, 0, -1, 20), (14, -84.625, 12.250, 0, -1, 20),
    (15, -88.625, 7.250, 0, -1, 20), (16, -86.625, 7.250, 0, -1, 20), (17, -84.625, 7.250, 0, -1, 20),
    (18, -82.625, 7.250, 0, -1, 20),
    (19, -88.625, 8.750, 0, 1, 20), (20, -86.625, 8.750, 0, 1, 20), (21, -84.625, 8.750, 0, 1, 20),
    (22, -82.625, 8.750, 0, 1, 20),
]


def _get(a, name):
    i = vpp_pack.index_of(a, name)
    if i < 0:
        return None
    f = a['files'][i]
    return zlib.decompress(f['stored']) if a['compressed'] else bytes(f['stored'][:f['size']])


def _names(a):
    return [f['raw'].split(b'\0')[0].decode('latin-1') for f in a['files']]


def _level(a):
    rfl = [n for n in _names(a) if n.lower().endswith('.rfl')][0]
    return rfl, EditableLevel(_get(a, rfl))


def _m3(r):
    return [list(r[0:3]), list(r[3:6]), list(r[6:9])]


def _mul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _tr(A):
    return [[A[j][i] for j in range(3)] for i in range(3)]


def hook_anim(tiki_rfa):
    import rfa_tool as R
    a = R.parse(tiki_rfa); b = a['bones'][0]
    vals = R.pos_track_values(b['pos']); times = b['pos']['times']
    new = [[v[0], v[1], v[2] + HOOK_DEPTH * min(1.0, max(0.0, (t - HOOK_T0) / (HOOK_T1 - HOOK_T0)))] for v, t in zip(vals, times)]
    b['pos'] = R.enc_pos_track(new, times)
    return R.build(a, keep_gaps=False)


def pool_only(rxe):
    d = bytearray(rxe)
    if d[4:8] != b'FEXR' or struct.unpack_from('<I', d, 0x48)[0] != 15:
        raise ValueError('unexpected env_kill_bear_trp.rxe in Mansion3.vpp')
    tex = [bytes(d[0x80 + i * 0x18:0x80 + i * 0x18 + 0x18]).split(b'\0')[0].decode('latin-1') for i in range(6)]
    kept = []
    for k in range(15):
        o = 0x8B0 + k * 0x78
        t = struct.unpack_from('<I', d, o + 8)[0]
        if t < 6 and tex[t] == 'ds_bloodpool.tga':
            kept.append(k); continue
        struct.pack_into('<I', d, o + 0xC, 0)
    if kept != [7]:
        raise ValueError('env_kill_bear_trp.rxe: pool emitter not where expected')
    return bytes(d)


def hooks_state(meat1_path):
    """'installed' / 'ready' / text of a problem"""
    try:
        a = vpp_pack.read(meat1_path); _, L = _level(a)
    except Exception as ex:
        return 'Meat1.vpp could not be read (%s)' % ex
    for o in L.objects.values():
        if Record(L._data[o['off']:o['off'] + o['size']]).name == 'Hook_Impale_00':
            return 'installed'
    return 'ready'


def build_hooks(meat1_vpp, zoo1_vpp, mansion3_vpp):
    """returns the new Meat1.vpp bytes. Raises ValueError with a readable reason."""
    za = vpp_pack.read(zoo1_vpp); _, Z = _level(za)
    zh = Z.record(ZOO_HOT)
    if zh is None or GROUP_OLD.encode() not in bytes(zh.raw):
        raise ValueError('zoo1.vpp: the tiki impale spot was not found')
    zt = next((Record(Z._data[o['off']:o['off'] + o['size']]) for o in Z.objects.values()
               if Record(Z._data[o['off']:o['off'] + o['size']]).name == ZOO_TRG_NAME), None)
    if zt is None or zt.links != [ZOO_HOT]:
        raise ValueError('zoo1.vpp: the tiki kill trigger was not found')
    Rh, Rt = _m3(zh.rot), _m3(zt.rot)
    Mt = _mul(Rt, _tr(Rh))
    dz = [zt.pos[k] - zh.pos[k] for k in range(3)]
    trg_local = [sum(Rh[i][k] * dz[k] for k in range(3)) for i in range(3)]

    a = vpp_pack.read(meat1_vpp); rfl, L = _level(a)
    for hid, tx, tz, dx, dzz, room in HOOKS:
        d = [float(dx), 0.0, float(dzz)]
        r0 = [-d[0], 0.0, -d[2]]; r2 = [0.0, 1.0, 0.0]
        r1 = [r2[1] * r0[2] - r2[2] * r0[1], r2[2] * r0[0] - r2[0] * r0[2], r2[0] * r0[1] - r2[1] * r0[0]]
        Rx = [r0, r1, r2]
        out_ = CHEST_OUT_FROM_MESH_TIP + HOOK_EXTRA_OUT.get(int(hid), 0.0)   # v11
        chest = (tx + d[0] * out_, tz + d[2] * out_)
        off = [CHEST_MODEL[0] * Rx[1][k] + CHEST_MODEL[1] * Rx[0][k] for k in range(3)]
        hp = (chest[0] - off[0], FLOOR_Y, chest[1] - off[2])
        name = 'Hook_Impale_%02d' % hid
        hot = Record(bytes(zh.raw)); uid = L.next_uid(0x0140); hot.set_uid(uid); hot.set_name(name)
        hot.room = room; hot.pos = hp; hot.rot = sum(Rx, [])
        hot.replace_string(GROUP_OLD, GROUP_NEW)
        L.add(hot)
        trg = Record(bytes(zt.raw)); tuid = L.next_uid(0x0080); trg.set_uid(tuid); trg.set_name(name + '_Trigger')
        trg.room = room
        trg.pos = tuple(hp[k] + sum(Rx[i][k] * trg_local[i] for i in range(3)) for k in range(3))
        trg.rot = sum(_mul(Mt, Rx), [])
        trg.set_links([uid])
        trg.set_body_u32(16, 1)                                   # use limit 1 (zoo1's are 0 = used up)
        if trg.body_u32(TRIGGER_BRACKET_OFF) != 0xFFFFFFFF:
            trg.set_body_u32(TRIGGER_BRACKET_OFF, 0xFFFFFFFF)
        L.add(trg)
    data = L.build()
    EditableLevel(data)                                           # must read back
    vpp_pack.put(a, rfl, data)

    def add(fn, body, ext):
        names = [n.lower() for n in _names(a)]
        if fn.lower() in names:
            return
        last = max(i for i, n in enumerate(names) if n.endswith('.' + ext))
        vpp_pack.put(a, fn, body, at=last + 1)
    for fn in ZOO_FILES:
        body = _get(za, fn)
        if body is None:
            raise ValueError('zoo1.vpp has no %s' % fn)
        add(fn, body, fn.rsplit('.', 1)[1])
    add('male_ek_hookkill.rfa', hook_anim(_get(za, 'male_ek_tikikill.rfa')), 'rfa')
    ma = vpp_pack.read(mansion3_vpp)
    bt = _get(ma, 'env_kill_bear_trp.rxe')
    if bt is None:
        raise ValueError('Mansion3.vpp has no env_kill_bear_trp.rxe')
    add('env_kill_bear_trp.rxe', pool_only(bt), 'rxe')
    return vpp_pack.build(a)


# 0.3 (Nov 21 2026, user): the zoo1 tiki torch impale gets the hook kill's added blood (plugin v1.41). The drip and the
# floor pool need three effects + one texture that zoo1.vpp lacks - copied from the player's Meat1.vpp (blood_drip01,
# blood_hit01, ds_bloodpool) and Mansion3.vpp (env_kill_bear_trp, pool-only). Same as the recipe zoo1_tiki_blood.py (tools, 5 Cut hunt, recipes).
TIKI_MEAT_FILES = ('blood_drip01.rxe', 'blood_hit01.rxe')
TIKI_POOL_TEX, TIKI_NEAR_TEX = 'ds_bloodpool.tga', 'ds_bloodmist02.tga'


def _lnames(a):
    return [n.lower() for n in _names(a)]


def _banks(a):
    for n in _lnames(a):
        if n.endswith(('.ceg', '.peg')):
            b = V.get_entry(a, n)
            if b and b[:4] == b'GEKV':
                yield n, b


def tiki_state(zoo1_path):
    """'installed' / 'ready' / text of a problem"""
    try:
        names = _lnames(vpp_pack.read(zoo1_path))
    except Exception as ex:
        return 'zoo1.vpp could not be read (%s)' % ex
    return 'installed' if 'blood_drip01.rxe' in names and 'env_kill_bear_trp.rxe' in names else 'ready'


def build_tiki(zoo1_vpp, meat1_vpp, mansion3_vpp):
    """returns the new zoo1.vpp bytes. Raises ValueError with a readable reason."""
    a, m, ma = vpp_pack.read(zoo1_vpp), vpp_pack.read(meat1_vpp), vpp_pack.read(mansion3_vpp)
    bt = _get(ma, 'env_kill_bear_trp.rxe')
    if bt is None:
        raise ValueError('Mansion3.vpp has no env_kill_bear_trp.rxe')
    adds = [('env_kill_bear_trp.rxe', pool_only(bt))]
    for fn in TIKI_MEAT_FILES:
        body = _get(m, fn)
        if body is None:
            raise ValueError('Meat1.vpp has no %s' % fn)
        adds.append((fn, body))
    for fn, body in adds:
        names = _lnames(a)
        if fn in names:
            continue
        last = max(i for i, n in enumerate(names) if n.endswith('.rxe'))
        vpp_pack.put(a, fn, body, at=last + 1)
    src = next((e for n, b in _banks(m) for e in V.bank_entries(b) if V.tname(e).lower() == TIKI_POOL_TEX), None)
    if src is None:
        raise ValueError('Meat1.vpp has no %s' % TIKI_POOL_TEX)
    if not any(V.tname(e).lower() == TIKI_POOL_TEX for n, b in _banks(a) for e in V.bank_entries(b)):
        tgt = next((n for n, b in _banks(a) if any(V.tname(e).lower() == TIKI_NEAR_TEX for e in V.bank_entries(b))), None)
        if tgt is None:
            raise ValueError('zoo1.vpp: no texture bank with %s' % TIKI_NEAR_TEX)
        b = V.get_entry(a, tgt); ents = V.bank_entries(b)
        ents.append({'off': None, 'w': src['w'], 'h': src['h'], 'fmt': src['fmt'], 'data': src['data'], 'name': src['name']})
        vpp_pack.put(a, tgt, V.bank_build(b, ents, 1))
    return vpp_pack.build(a)
