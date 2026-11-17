"""Extended Cut release - level parts. Each part rebuilds one level archive of the PLAYER'S OWN game from its own
records; no level data is shipped, only the numbers below (positions, links, names).

blowtorch (Chop Shop 1): the cut blowtorch interrogation. Every animation, camera and the torch prop ship in the
retail Chop1.vpp; only the hotspot and its trigger were never placed. Added here:
  - hotspot "blowtorch_interrogation_chop1_a" = a copy of the retail grinder hotspot (chop1_grinder_kill) with room 0,
    position (8.75, -2.005, 7.5), group "Chop Shop Blow Torch", links = torch camera 0x14000AD, "Blow Torch" prop
    0x200024, death camera 0x140010A;
  - trigger "Blowtorch_Interro_Trigger" = a copy of the retail grinder trigger (Trigger Door 013) centred on
    (8.9, 0, 7.5), linking the new hotspot, NOT starting disabled (flag 0x10 cleared; the grinder's waits for the
    garage door);
  - skull_fish.rxm / skull_fish.ceg, copied from the player's own dclub1.vpp or zoo3.vpp (the blowtorch head gib
    throws that skull prop; without the files Chop Shop hangs on loading).
Same result as recipe chop1_blowtorch_v2.py (tested in game Nov 5, 2026).
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.append(os.path.join(HERE, '..', '5 Cut hunt'))
import vpp_pack  # noqa: E402
from rfl_edit import EditableLevel, Record  # noqa: E402

GRINDER_HOT, GRINDER_TRG = 0x1400060, 0x80002C
BT_NAME, BT_TRG_NAME = 'blowtorch_interrogation_chop1_a', 'Blowtorch_Interro_Trigger'
BT_LINKS = [0x14000AD, 0x200024, 0x140010A]
TRG_FLAGS_OFF = 63                 # Trigger Door body: u32 flags (0x10 = starts disabled)
SKULL_FILES = (('skull_fish.rxm', 'p_weldingmask.rxm'), ('skull_fish.ceg', 'p_weldingmask.ceg'))


def _get(a, name):
    i = vpp_pack.index_of(a, name)
    if i < 0:
        return None
    f = a['files'][i]
    return zlib.decompress(f['stored']) if a['compressed'] else bytes(f['stored'][:f['size']])


def blowtorch_state(chop1_path):
    """'installed' / 'old' (the old restoration's overwritten event) / 'ready' / text of a problem"""
    try:
        a = vpp_pack.read(chop1_path)
        L = EditableLevel(_get(a, 'Chop1.rfl'))
    except Exception as ex:
        return 'Chop1.vpp could not be read (%s)' % ex
    names = {r.name for r in _iter(L)}
    if BT_NAME in names:
        return 'installed' if BT_TRG_NAME in names else 'old'
    try:
        g, t = L.record(GRINDER_HOT), L.record(GRINDER_TRG)
    except Exception:
        return 'the grinder hotspot / trigger is missing (changed by another mod?)'
    if g.name != 'chop1_grinder_kill' or t.cls != 'Trigger Door' or t.links != [GRINDER_HOT]:
        return 'the grinder hotspot / trigger is not the retail one (changed by another mod?)'
    return 'ready'


def _iter(L):
    """every object record of the level"""
    for o in L.objects.values():
        yield Record(L._data[o['off']:o['off'] + o['size']])


def build_blowtorch(chop1_vpp, skull_src_vpps):
    """returns the new Chop1.vpp bytes. Raises ValueError with a readable reason."""
    a = vpp_pack.read(chop1_vpp)
    L = EditableLevel(_get(a, 'Chop1.rfl'))
    g = L.record(GRINDER_HOT)
    t = L.record(GRINDER_TRG)
    if g.cls != 'Environment_Action' or g.name != 'chop1_grinder_kill':
        raise ValueError('grinder hotspot not found')

    hot = Record(bytes(g.raw))
    hot_uid = L.next_uid(0x0140)
    hot.set_uid(hot_uid)
    hot.set_name(BT_NAME)
    hot.room = 0
    hot.pos = (8.75, -2.005000114440918, 7.5)
    hot.replace_string('Chop Grinder', 'Chop Shop Blow Torch')
    hot.set_links(BT_LINKS)
    L.add(hot)

    trg = Record(bytes(t.raw))
    trg_uid = L.next_uid(0x0080)
    trg.set_uid(trg_uid)
    trg.set_name(BT_TRG_NAME)
    trg.pos = (8.899999618530273, 0.0, 7.5)
    trg.set_links([hot_uid])
    trg.raw[trg.body_at + TRG_FLAGS_OFF] &= ~0x10 & 0xFF    # the grinder trigger starts disabled (0x10); this one is on
    L.add(trg)

    data = L.build()
    chk = EditableLevel(data)
    assert chk.record(hot_uid).links == BT_LINKS and chk.record(hot_uid).room == 0
    assert chk.record(trg_uid).links == [hot_uid]
    assert chk.record(GRINDER_TRG).links == [GRINDER_HOT]
    assert not chk.record(trg_uid).raw[chk.record(trg_uid).body_at + TRG_FLAGS_OFF] & 0x10
    vpp_pack.put(a, 'Chop1.rfl', data)

    # the skull prop for the head gib
    if any(vpp_pack.index_of(a, n) < 0 for n, _ in SKULL_FILES):
        src = None
        for p in skull_src_vpps:
            if p and os.path.exists(p):
                s = vpp_pack.read(p)
                if all(vpp_pack.index_of(s, n) >= 0 for n, _ in SKULL_FILES):
                    src = s
                    break
        if src is None:
            raise ValueError('skull_fish files not found in dclub1.vpp / zoo3.vpp')
        for n, after in SKULL_FILES:
            if vpp_pack.index_of(a, n) >= 0:
                continue
            at = vpp_pack.index_of(a, after)
            if at < 0:
                raise ValueError('%s not found in Chop1.vpp (needed to place %s)' % (after, n))
            vpp_pack.put(a, n, _get(src, n), at=at + 1)
    names = [f['raw'].split(bytes(1))[0].decode('latin-1').lower() for f in a['files']]
    assert names.index('skull_fish.rxm') < names.index('chop1.rfl') and names.index('skull_fish.ceg') > names.index('skull_fish.rxm')
    return vpp_pack.build(a)
