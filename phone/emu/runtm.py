#!/usr/bin/env python3
# frames of a packed track in the ARM emulator: instructions, an estimate in ms (3 cycles an instruction at 150 MHz, as Hrimfell's phone), the picture
import sys, struct; sys.path.insert(0, '/root/ref/bt/native9300/emu')
from armemu import Emu
exe, sym, tmb = sys.argv[1:4]; samples = [int(x) for x in sys.argv[4].split(',')] if len(sys.argv) > 4 else [0, 40, 100, 120, 180]
cache = '-c' in sys.argv
W, H = 320, 200
e = Emu(exe, sym, cache=cache)
e.call('emu_init')
data = open(tmb, 'rb').read(); e.write(0x2000000, data)
n, _ = e.call('emu_load', 0x2000000, len(data), W, H); print('path samples', n)
FRAME, STATS = 0x3000000, 0x3800000
tot = 0
for s in samples:
    r, ins = e.call('emu_frame', FRAME, s, STATS)
    st = struct.unpack('<8i', e.read(STATS, 32))
    ms = ins * 3 / 150000.0
    tot += ins
    print(f'sample {s:3d}: {ins:9d} instr = ~{ms:5.1f} ms  visible {st[1]}/{st[0]} polys {st[2]} drawn {st[3]} culled {st[4]} hidden {st[5]} pixels {st[6]}{" FULL" if st[7] else ""}')
    if '-p' in sys.argv: e.profile(top=12)
    if cache: print('   reads, misses, by function:', e.misses())
    if '-img' in sys.argv:
        from PIL import Image
        px = e.read(FRAME, W * H * 2); im = Image.new('RGB', (W, H))
        im.putdata([(((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3) for c in struct.unpack('<%dH' % (W * H), px)])
        im.save(f'/root/ref/frames/emu_{s:03d}.png')
print(f'average {tot / len(samples):.0f} instructions = ~{tot / len(samples) * 3 / 150000:.1f} ms a frame -> ~{150000 / (tot / len(samples) * 3 / 1000):.1f} fps (drawing only)')
