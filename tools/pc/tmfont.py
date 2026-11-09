# the game's menu font (GameData/Menu/Media/Font/United_Textures/United_00.dds: Latin-1 on a 1024 x 256 sheet) as a
# glyph strip for the phone (0.14): ASCII 32..126 cut out of the sheet, each glyph in a cell with its bearings, all
# on the sheet's own scale ("TMFT" h asc stripW x[95] w[95] mask, as mkfont.py); the phone scales it to its fonts'
# heights. The output is Nadeo's: never in git.
#   python3 tmfont.py United_00.dds out.fnt [preview.png]
import sys, struct
from PIL import Image
import numpy as np
src, out = sys.argv[1], sys.argv[2]
A = np.array(Image.open(src).split()[3]).astype(np.int32)
W = A.shape[1]
def segs(y0, y1):
    col = A[y0:y1].max(0) > 0
    r = []; x = 0
    while x < W:
        if col[x]:
            x0 = x
            while x < W and col[x]: x += 1
            r.append([x0, x])
        x += 1
    return r
def bottom(x0, x1, y0, y1):
    rows = np.where(A[y0:y1, x0:x1].max(1) > 0)[0]
    return y0 + rows.max() + 1, y0 + rows.min()
# the first line: a box (the missing glyph), then '!'..'S' ('"' is two strokes)
L1 = segs(0, 35)
g1 = L1[1:]
i = 1; g1 = [L1[1], [L1[2][0], L1[3][1]]] + L1[4:]
chars1 = [chr(c) for c in range(33, 84)]
assert len(g1) == len(chars1), (len(g1), len(chars1))
base1, capTop = bottom(*g1[chars1.index('A')], 0, 35)
# the second line: its baseline from 'T', the same height around it
tx = None
L2a = segs(36, 69)
base2, _ = bottom(L2a[0][0], L2a[0][1], 36, 69)
pitch = base2 - base1
# the line: from the sheet's top to the first line's deepest descender (its band ends at the gap before the second)
glyphs = {}
for (x0, x1), c in zip(g1, chars1): glyphs[c] = (x0, x1, 1)
band = np.where(A[:36].max(1) > 0)[0]
asc = base1 - max(0, band.min() - 1)
desc = band.max() + 1 - base1
L2 = segs(base2 - asc, base2 + desc)
chars2 = [chr(c) for c in range(84, 127)]
L2 = L2[:len(chars2)]
for (x0, x1), c in zip(L2, chars2): glyphs[c] = (x0, x1, 2)
H = asc + desc
pad = 2                     # a side bearing (the sheet's scale)
cells = []
for code in range(32, 127):
    c = chr(code)
    if c == ' ': cells.append(np.zeros((H, 9), np.int32)); continue
    x0, x1, l = glyphs[c]
    b = base1 if l == 1 else base2
    g = A[b - asc:b - asc + H, x0:x1]
    if g.shape[0] < H: g = np.vstack([g, np.zeros((H - g.shape[0], x1 - x0), np.int32)])
    cell = np.zeros((H, x1 - x0 + 2 * pad), np.int32); cell[:, pad:pad + x1 - x0] = g
    cells.append(cell)
xs, ws = [], []; x = 0
for c in cells: xs.append(x); ws.append(c.shape[1]); x += c.shape[1]
strip = np.hstack(cells).astype(np.uint8)
open(out, 'wb').write(b'TMFT' + struct.pack('<iii', H, asc, x) + struct.pack('<95i', *xs) + struct.pack('<95i', *ws) + strip.tobytes())
print('h', H, 'asc', asc, 'strip', x, 'pitch', pitch)
if len(sys.argv) > 3: Image.fromarray(255 - strip).save(sys.argv[3])
