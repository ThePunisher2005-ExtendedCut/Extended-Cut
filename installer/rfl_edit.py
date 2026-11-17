"""Edit Punisher .rfl levels: copy / add objects, change uid, name, room, position, links, strings.

Object sections (payload starts with u32 count):
  0x000600 events (Environment_Action, Environment_Action_Cam, Play_Animation, Delay, ...)
  0x060000 triggers (Trigger Door / Trigger Auto)      0x030000 entities + animated props
  0x050000 clutter      0x040000 items      0x020000 nav points      0x001000 decals
Adding = append the record to the end of its section and count + 1 (no other index is touched).

Record layout (all object kinds):
  u32 uid | u16 len + class | u32 room (0xFFFFFFFF = none) | 3f pos | 9f rotation | u16 len + name | body
  events end with: u32 link_count + link_count * u32 uid
"""
import struct

from rfl_sections import Level
import rfl_scan

SECTION_OF_PREFIX = {0x0140: 0x000600, 0x0080: 0x060000, 0x0020: 0x030000, 0x0004: 0x050000,
                     0x0400: 0x004001, 0x0300: 0x007779}
TRIGGER_BRACKET_OFF = 43      # Trigger Door body (measured on 5 working USE triggers): u32 uid of the hud_bracket_placeholder (USE prompt), -1 = none


class Record:
    def __init__(self, raw):
        self.raw = bytearray(raw)
        self.uid = struct.unpack_from('<I', raw, 0)[0]
        n = struct.unpack_from('<H', raw, 4)[0]
        self.cls = raw[6:6 + n].decode('latin-1')
        self._room_at = 6 + n
        self._name_at = self._room_at + 4 + 48
        m = struct.unpack_from('<H', raw, self._name_at)[0]
        self.name = raw[self._name_at + 2:self._name_at + 2 + m].decode('latin-1')

    # --- header fields -------------------------------------------------
    @property
    def room(self):
        return struct.unpack_from('<I', self.raw, self._room_at)[0]

    @room.setter
    def room(self, v):
        struct.pack_into('<I', self.raw, self._room_at, v)

    @property
    def pos(self):
        return struct.unpack_from('<3f', self.raw, self._room_at + 4)

    @pos.setter
    def pos(self, xyz):
        struct.pack_into('<3f', self.raw, self._room_at + 4, *xyz)

    @property
    def rot(self):
        return struct.unpack_from('<9f', self.raw, self._room_at + 16)

    @rot.setter
    def rot(self, m):
        struct.pack_into('<9f', self.raw, self._room_at + 16, *m)

    @property
    def body_at(self):
        return self._name_at + 2 + len(self.name.encode('latin-1'))

    def body_u32(self, off):
        return struct.unpack_from('<I', self.raw, self.body_at + off)[0]

    def set_body_u32(self, off, v):
        struct.pack_into('<I', self.raw, self.body_at + off, v)

    def set_body_f32(self, off, v):
        struct.pack_into('<f', self.raw, self.body_at + off, v)

    def set_uid(self, uid):
        self.uid = uid
        struct.pack_into('<I', self.raw, 0, uid)

    def set_cls(self, cls):
        """Change the record's class string (e.g. turn a copied 67-byte event into another event type)."""
        n = struct.unpack_from('<H', self.raw, 4)[0]
        b = cls.encode('latin-1')
        self.raw[4:6 + n] = struct.pack('<H', len(b)) + b
        self.__init__(bytes(self.raw))

    def set_name(self, name):
        old = struct.unpack_from('<H', self.raw, self._name_at)[0]
        b = name.encode('latin-1')
        self.raw[self._name_at:self._name_at + 2 + old] = struct.pack('<H', len(b)) + b
        self.name = name

    # --- body ----------------------------------------------------------
    def replace_string(self, old, new):
        """Replace a u16-length-prefixed string in the body (first match)."""
        ob, nb = old.encode('latin-1'), new.encode('latin-1')
        pat = struct.pack('<H', len(ob)) + ob
        i = self.raw.find(pat, self._name_at)
        if i < 0:
            raise KeyError(f'string {old!r} not in record {self.name}')
        self.raw[i:i + len(pat)] = struct.pack('<H', len(nb)) + nb

    def links_at(self):
        for k in range(0, 65):
            s = len(self.raw) - 4 - 4 * k
            if s < self._name_at:
                break
            if struct.unpack_from('<I', self.raw, s)[0] == k:
                uids = struct.unpack_from('<%dI' % k, self.raw, s + 4)
                if all((u >> 16) and u < 0x10000000 for u in uids):
                    return s, list(uids)
        raise ValueError(f'no link list found in {self.cls} {self.name}')

    @property
    def links(self):
        return self.links_at()[1]

    def set_links(self, uids):
        s, _ = self.links_at()
        self.raw[s:] = struct.pack('<I', len(uids)) + b''.join(struct.pack('<I', u) for u in uids)


class EditableLevel(Level):
    def __init__(self, data):
        super().__init__(data)
        self._index()

    def _index(self):
        data = self.build()
        self.objects = {}
        offs = self.offsets()
        objs = rfl_scan.scan(data)
        for o in objs:
            for si, (p, t, v, s) in enumerate(offs):
                if p + 12 <= o['off'] < p + 12 + s:
                    o['section'] = si
                    o['size'] = min(o['size'], p + 12 + s - o['off'])   # never run into the next section
                    break
            self.objects[o['uid']] = o
        self._data = data

    def record(self, uid):
        o = self.objects[uid]
        return Record(self._data[o['off']:o['off'] + o['size']])

    def section_index(self, stype):
        hits = [i for i, s in enumerate(self.sections) if s[0] == stype]
        if len(hits) != 1:
            raise ValueError(f'section {stype:#x} found {len(hits)} times')
        return hits[0]

    def next_uid(self, prefix):
        used = [u for u in self.objects if (u >> 16) == prefix]
        return max(used) + 1 if used else (prefix << 16) | 1

    def replace(self, rec):
        """Write an edited copy of an existing record back (its size may change; count unchanged)."""
        o = self.objects[rec.uid]
        sec = self.sections[o['section']]
        start = o['off'] - (self.offsets()[o['section']][0] + 12)
        sec[2][start:start + o['size']] = rec.raw
        self._index()
        if self.objects[rec.uid]['cls'] != rec.cls:
            raise RuntimeError('replaced record not readable back')

    def preload_pairs(self):
        """Section 0x7006 = animation-set preload list: u32 count, then count x (str skeleton, str set)."""
        out = []
        for t, v, pl in self.sections:
            if t != 0x7006:
                continue
            n = struct.unpack_from('<I', pl, 0)[0]
            p, pairs = 4, []
            for _ in range(n):
                a = []
                for _k in range(2):
                    m = struct.unpack_from('<H', pl, p)[0]
                    a.append(bytes(pl[p + 2:p + 2 + m]).decode('latin-1'))
                    p += 2 + m
                pairs.append(tuple(a))
            out.append(pairs)
        return out

    def add_preload(self, skeleton, aset='default'):
        """Add (skeleton, set) to every 0x7006 preload list (needed for any entity type the level never had)."""
        for sec in self.sections:
            if sec[0] != 0x7006:
                continue
            pl = sec[2]
            n = struct.unpack_from('<I', pl, 0)[0]
            body = bytes(pl[4:])
            entry = b''.join(struct.pack('<H', len(s)) + s.encode('latin-1') for s in (skeleton, aset))
            if entry in body:
                continue
            sec[2] = bytearray(struct.pack('<I', n + 1) + entry + body)
        self._index()

    def add(self, rec, stype=None):
        stype = stype or SECTION_OF_PREFIX[rec.uid >> 16]
        if rec.uid in self.objects:
            raise ValueError(f'uid {rec.uid:#x} already used')
        sec = self.sections[self.section_index(stype)]
        pl = sec[2]
        cnt = struct.unpack_from('<I', pl, 0)[0]
        struct.pack_into('<I', pl, 0, cnt + 1)
        pl += rec.raw
        self._index()
        if rec.uid not in self.objects:
            raise RuntimeError('added record is not readable back')
        return rec.uid
