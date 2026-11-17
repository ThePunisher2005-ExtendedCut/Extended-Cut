"""Punisher .rfl section layer (byte-exact).

File = u32 magic 0xD4BADA55, u32 version, then sections until EOF:
    u32 type, u32 section_version (0x221), u32 size, <size bytes payload>
Object sections start their payload with u32 count.

  python rfl_sections.py <level.rfl>            list sections
  python rfl_sections.py --check <root>         parse + rebuild every .rfl under root, must be byte-identical
"""
import os, struct, sys

MAGIC = 0xD4BADA55


class Level:
    def __init__(self, data):
        self.magic, self.version = struct.unpack_from('<II', data, 0)
        if self.magic != MAGIC:
            raise ValueError('not an rfl')
        self.sections = []          # [type, sver, payload(bytearray)]
        p = 8
        while p < len(data):
            if p + 12 > len(data):
                raise ValueError(f'truncated section header at {p:#x}')
            t, v, s = struct.unpack_from('<III', data, p)
            if p + 12 + s > len(data):
                raise ValueError(f'section {t:#x} at {p:#x} runs past EOF')
            self.sections.append([t, v, bytearray(data[p + 12:p + 12 + s])])
            p += 12 + s

    def build(self):
        out = bytearray(struct.pack('<II', self.magic, self.version))
        for t, v, pl in self.sections:
            out += struct.pack('<III', t, v, len(pl)) + pl
        return bytes(out)

    def offsets(self):
        p, res = 8, []
        for t, v, pl in self.sections:
            res.append((p, t, v, len(pl)))
            p += 12 + len(pl)
        return res


def main():
    if sys.argv[1] == '--check':
        ok = bad = 0
        for r, _, fs in os.walk(sys.argv[2]):
            for f in fs:
                if f.lower().endswith('.rfl'):
                    p = os.path.join(r, f)
                    d = open(p, 'rb').read()
                    try:
                        same = Level(d).build() == d
                    except ValueError as e:
                        same = False
                        print('FAIL', p, e)
                    if same:
                        ok += 1
                    else:
                        bad += 1
        print(f'{ok} identical, {bad} failed')
        return
    d = open(sys.argv[1], 'rb').read()
    L = Level(d)
    for p, t, v, s in L.offsets():
        cnt = struct.unpack_from('<I', d, p + 12)[0] if s >= 4 else 0
        print(f'{p:08x} type {t:#08x} ver {v:#x} size {s:9d}  first-u32 {cnt}')


if __name__ == '__main__':
    main()
