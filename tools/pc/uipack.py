# the menu's pictures from the game's own files (GameData), cut and scaled for the 640 x 200 screen, into one file:
# "TMUI", count, then per picture: name (16 bytes), w, h (u16), offset (u32); the data: w*h RGB565, then w*h alpha
import sys, struct, os
from PIL import Image
G = sys.argv[1]; out = sys.argv[2]
def img(rel): return Image.open(os.path.join(G, rel)).convert('RGBA')
icons = img('MenuForever/Media/Texture/Image/Icons128x128.dds')
def icon(r, c, size):
    im = icons.crop((c * 128, r * 128, c * 128 + 128, r * 128 + 128))
    bb = im.getchannel('A').getbbox() or (0, 0, 128, 128)
    im = im.crop(bb); s = size / max(im.width, im.height)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    pad = Image.new('RGBA', (size, size), (0, 0, 0, 0)); pad.paste(im, ((size - im.width) // 2, (size - im.height) // 2)); return pad
pics = {}
IC = 24
for name, r, c in [('solo', 7, 0), ('party', 7, 1), ('online', 7, 2), ('lan', 7, 3), ('car', 7, 4), ('back', 5, 0), ('quit', 6, 3),
                   ('editors', 6, 1), ('profile', 6, 2), ('flagw', 2, 2), ('flagg', 2, 3), ('flagb', 2, 4), ('flagr', 2, 5), ('flagk', 2, 6),
                   ('races', 2, 7), ('browse', 6, 6), ('united', 1, 0), ('medals', 0, 0), ('nations', 1, 1), ('star', 1, 5), ('mycamp', 1, 2), ('platforms', 3, 0), ('stunts', 3, 1), ('puzzle', 3, 2)]:
    pics['i_' + name] = icon(r, c, IC)
pics['i_carbig'] = icon(7, 4, 30)
pics['i_lock'] = icon(1, 3, 30)
# the medals (the big coins): bronze, silver, gold, Nadeo on the lower row; the empty one top left
mb = img('MenuForever/Media/Texture/Image/MedalsBig.dds')
def coin(box, size):
    im = mb.crop(box); bb = im.getchannel('A').getbbox(); im = im.crop(bb)
    return im.resize((size, size), Image.LANCZOS)
for size in (20, 13):
    for k, name in enumerate(['bronze', 'silver', 'gold', 'nadeo']):
        pics['m%d_%s' % (size, name)] = coin((k * 512, 512, k * 512 + 512, 1024), size)
    pics['m%d_none' % size] = coin((0, 0, 512, 512), size)
# the timer's LED digits ("0123456789:.-+"), 22 pixels high, and their backing plate
led = img('Interface/Media/Font/Led_Textures/Led_00.dds')
# the glyphs' boxes in the LED sheet (its rows: A-M, N-Z 0 1, 2-9 : . , ; ! ? ( ) - +)
R2, R3 = (69, 123), (132, 184)
boxes = {'0': (447, 473, R2), '1': (474, 500, R2), '2': (4, 30, R3), '3': (36, 63, R3), '4': (70, 96, R3), '5': (103, 129, R3),
         '6': (136, 162, R3), '7': (167, 193, R3), '8': (202, 228, R3), '9': (235, 261, R3), ':': (266, 278, R3), '.': (281, 293, R3),
         '-': (408, 431, R3), '+': (432, 464, R3)}
LH = 18
for ch, (x0, x1, (y0, y1)) in boxes.items():
    g = led.crop((x0, y0, x1, y0 + 54)); sc = LH / 54
    pics['led_' + {':': 'colon', '.': 'dot', '-': 'minus', '+': 'plus'}.get(ch, ch)] = g.resize((max(1, round(g.width * sc)), LH), Image.LANCZOS)
tb = img('Interface/Media/Texture/Image/UiBgBottomCenterRace.dds')
tb = tb.crop(tb.getchannel('A').getbbox()); pics['timerbg'] = tb.resize((118, 26), Image.LANCZOS)
go = img('MenuForever/Media/Texture/Image/321Go.dds'); ga = go.getchannel('A')
cols = []; inside = False
for x in range(go.width):
    on = any(ga.getpixel((x, y)) > 30 for y in range(0, go.height, 2))
    if on and not inside: x0 = x; inside = True
    if not on and inside: cols.append((x0, x)); inside = False
# merge the "Go!" pieces (G, o, !) into one
parts = cols[:3] + [(cols[3][0], cols[-1][1])]
for k, (x0, x1) in enumerate(parts):
    g = go.crop((x0, 0, x1, go.height)); g = g.crop(g.getchannel('A').getbbox()); sc = 58 / g.height
    pics['go%d' % k] = g.resize((round(g.width * sc), 58), Image.LANCZOS)
lg = img('Menu/Media/Texture/Image/LogoUnited.DDS'); lg = lg.crop(lg.getchannel('A').getbbox())
pics['logo'] = lg.resize((320, round(lg.height * 320 / lg.width)), Image.LANCZOS)
# 0.11: the menu's background and the title from the user's own screenshots of the game (1920 x 1080): the globe of the
# Party Mode screen (the left panel cut off, the blue widened to 442 pixels), the rally jump of the loading screen
A = sys.argv[3] if len(sys.argv) > 3 else None
if A:
    from PIL import ImageFilter
    shot = Image.open(A + '/7a9b32d6-image.png').convert('RGB')
    g = shot.crop((450, 0, 1920, 1080)).resize((round(1470 * 200 / 1080), 200), Image.LANCZOS)
    bg = Image.new('RGB', (640, 200))
    left = g.crop((0, 0, 1, 200)).resize((198 + (442 - g.width) // 2, 200)); right = g.crop((g.width - 1, 0, g.width, 200)).resize((640 - (198 + (442 - g.width) // 2) - g.width, 200))
    bg.paste(left, (0, 0)); bg.paste(g, (left.width, 0)); bg.paste(right, (left.width + g.width, 0))
    pics['bg'] = bg.convert('RGBA')
    t = Image.open(A + '/73965dea-image.png').convert('RGB').crop((0, 170, 1920, 770)).resize((640, 200), Image.LANCZOS)
    pics['title'] = t.convert('RGBA')
# a preview of what is kept
prev = Image.new('RGBA', (640, 120), (20, 40, 60, 255)); x = 2; y = 2
for k, v in pics.items():
    if v.width > 64: continue
    if x + v.width > 636: x = 2; y += 34
    prev.alpha_composite(v, (x, y)); x += v.width + 4
prev.convert('RGB').save('/tmp/claude-0/assets/v_pack.png')
# the file
names = list(pics); data = b''; table = b''
off = 8 + len(names) * 24
for n in names:
    im = pics[n]; w, h = im.size
    px = im.load(); rgb = bytearray(); al = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a: r, g, b = min(255, r * 255 // a) if False else r, g, b
            rgb += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); al.append(a)
    opaque = all(a == 255 for a in al)
    blob = bytes(rgb) + (b'' if opaque else bytes(al))
    while len(blob) & 3: blob += b'\0'
    table += struct.pack('<16sHHI', n.encode()[:16], w, h | (0x8000 if opaque else 0), off + len(data)); data += blob
open(out, 'wb').write(b'TMUI' + struct.pack('<I', len(names)) + table + data)
print(len(names), 'pictures,', 8 + len(table) + len(data), 'bytes')
big = Image.new('RGBA', (640, 200), (20, 40, 60, 255)); x = 4
for n in ['logo']: big.alpha_composite(pics[n], (4, 4))
x = 4
for k in range(4): big.alpha_composite(pics['go%d' % k], (x, 50)); x += pics['go%d' % k].width + 6
big.alpha_composite(pics['timerbg'], (300, 120)); x = 312
for ch in '-0:12.39': n = 'led_' + {':': 'colon', '.': 'dot', '-': 'minus'}.get(ch, ch); big.alpha_composite(pics[n], (x, 124)); x += pics[n].width + 1
big.convert('RGB').save('/tmp/claude-0/assets/v_big.png')
