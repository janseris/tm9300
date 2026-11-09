#!/usr/bin/env python3
# the physics in the ARM emulator: the same scripted drive as /tmp/claude-0/pcdrive.cpp, the state every 100 steps (must match the PC bit for bit), the cost in instructions
import sys, struct; sys.path.insert(0, '/root/ref/bt/native9300/emu')
from armemu import Emu
exe, sym, tmb = sys.argv[1:4]; n = int(sys.argv[4]) if len(sys.argv) > 4 else 500
e = Emu(exe, sym)
e.call('emu_init')
data = open(tmb, 'rb').read(); e.write(0x2000000, data)
e.call('emu_load', 0x2000000, len(data), 320, 200)
r, ins = e.call('emu_drive_reset'); print('collision instances', r, 'reset', ins, 'instr')
OUT, FRAME = 0x3800000, 0x3000000
tot = 0
for t in range(0, n, 100):
    r, ins = e.call('emu_drive', 100, t, OUT, 0)
    s = struct.unpack('<12i', e.read(OUT, 48)); tot += ins
    print(f'step {t+100}: {s[0]} {s[1]} {s[2]} v {s[3]} {s[4]} {s[5]} yaw {s[6]} g {s[7]} surf {s[8]} speed {s[9]} probes {s[10]} tests {s[11]}   ({ins/100:.0f} instr a step)')
    if '-p' in sys.argv: e.profile(top=8)
print(f'{tot/n:.0f} instructions a step = {tot/n*50/150000*2:.2f} ms a second of play at 2 cycles an instruction')
r, ins = e.call('emu_drive', 1, n, OUT, FRAME); print('a step with the frame:', ins, 'instr')
