import warnings; warnings.simplefilter("ignore")
# 0.15: the menu's track picture from the track itself: the game's own thumbnail (the Challenge.Gbx header's
# <Thumbnail.jpg>, stored upside down), cut to the grid's shape, into <name>.tmt with the medal times of the
# existing .tmt (host/tmthumb writes those). The output is Nadeo's: never in git.
#   python3 tmthumbgbx.py track.Challenge.Gbx in.tmt out.tmt
import sys, io, struct
from PIL import Image
gbx, tin, tout = sys.argv[1:4]
d = open(gbx, 'rb').read()
i = d.find(b'<Thumbnail.jpg>') + len(b'<Thumbnail.jpg>'); j = d.find(b'</Thumbnail.jpg>')
im = Image.open(io.BytesIO(d[i:j])).convert('RGB').transpose(Image.FLIP_TOP_BOTTOM)
hdr = open(tin, 'rb').read(28); w, h = struct.unpack('<ii', hdr[4:12])
W, H = im.size; ch = W * h // w
im = im.crop((0, (H - ch) // 2, W, (H - ch) // 2 + ch)).resize((w, h), Image.LANCZOS)
px = b''.join(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in list(im.getdata()))
open(tout, 'wb').write(hdr + px)
print(tout, w, h)
