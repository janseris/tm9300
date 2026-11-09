# a font atlas for the PC preview (the phone draws its own fonts into the same form): "TMFT" h asc stripW x[95] w[95] mask
import sys, struct
from PIL import Image, ImageDraw, ImageFont
path, size, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
f = ImageFont.truetype(path, size)
asc, desc = f.getmetrics(); h = asc + desc
ws = [max(1, round(f.getlength(chr(c)))) for c in range(32, 127)]
xs = []; x = 0
for w in ws: xs.append(x); x += w + 1
im = Image.new('L', (x, h), 0); d = ImageDraw.Draw(im)
for i, c in enumerate(range(32, 127)): d.text((xs[i], 0), chr(c), font=f, fill=255)
open(out, 'wb').write(b'TMFT' + struct.pack('<iii', h, asc, x) + struct.pack('<95i', *xs) + struct.pack('<95i', *ws) + im.tobytes())
