#!/usr/bin/env python3
"""fantrack.py kitdir outdir track.Challenge.Gbx ... : fan tracks (any Challenge.Gbx of Rally, Snow, Desert, Bay or
Island, from TMX or the game's editor) for the phone (0.18): each into outdir/f_<name>.tmb (pack.py with no replay:
the start from the start block; packed with LZMA, tmbz.py) and f_<name>.tmt (the map's own thumbnail, its medal
times, and a trailer "TMTN", the environment, the map's name, which Browse shows). kitdir: the kits (KitExport:
rally_kit.json, alpine_kit.json, speed_kit.json, bay_kit.json, island_kit.json) and avgcolors.json.
The output is cut from the game's own files: never in git."""
import sys, os, re, io, struct, subprocess, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); import pack
ENV = {'Rally': (0, 'rally_kit.json', []), 'Alpine': (1, 'alpine_kit.json', ['--budget', '200']),
       'Speed': (2, 'speed_kit.json', ['--budget', '150']), 'Island': (3, 'island_kit.json', []), 'Bay': (4, 'bay_kit.json', [])}
DESERT_ENV = {'COARSE': 'RiverBeach|DesertTo|RockToRock|Cliff|SpeedToCanyon|Rocks', 'ROAD_BUDGET': '300', 'D_GRID': '3', 'R_GRID': '2'}

def header(d):
    i = d.find(b'<header'); j = d.find(b'</header>', i)
    return d[i:j].decode('utf-8', 'replace') if i >= 0 and j > i else ''

def plain_name(n):
    """the map's name without TrackMania's $ codes ($o, $s, $fff colours ...)"""
    n = re.sub(r'\$([0-9a-fA-F]{3}|[lLhH]\[[^\]]*\]|.)', lambda m: '$' if m.group(1) == '$' else '', n)
    return ''.join(c for c in n if 32 <= ord(c) < 127).strip() or 'Fan track'

def thumb(d, medals, env, title, w=74, h=46):
    from PIL import Image
    i = d.find(b'<Thumbnail.jpg>'); j = d.find(b'</Thumbnail.jpg>')
    try:
        im = Image.open(io.BytesIO(d[i + 15:j])).convert('RGB').transpose(Image.FLIP_TOP_BOTTOM)
        W, H = im.size; ch = W * h // w
        im = im.crop((0, (H - ch) // 2, W, (H - ch) // 2 + ch)).resize((w, h), Image.LANCZOS)
    except Exception:
        im = Image.new('RGB', (w, h), (60, 90, 60))
    px = b''.join(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in im.getdata())
    t = title.encode('ascii', 'replace')[:23]
    return struct.pack('<4s6i', b'TMT1', w, h, *medals) + px + b'TMTN' + bytes([env]) + t + b'\0' * (23 - len(t))

def main():
    kitdir, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    used = {f[:-4].lower() for f in os.listdir(outdir) if f.endswith('.tmb')}
    for g in sys.argv[3:]:
        d = open(g, 'rb').read()
        hx = header(d)
        m = re.search(r'envir="(\w+)"', hx)
        if not m or m.group(1) not in ENV: print(f"{g}: environment {m.group(1) if m else '?'} is not on the phone: skipped"); continue
        env, kit, opts = ENV[m.group(1)]
        nm = re.search(r'<ident[^>]* name="([^"]*)"', hx)
        title = plain_name(nm.group(1) if nm else os.path.basename(g).split('.')[0])
        base = 'f_' + (re.sub(r'[^A-Za-z0-9]', '', title)[:14] or 'track')
        name = base; k = 2
        while name.lower() in used: name = base[:14] + str(k); k += 1
        used.add(name.lower())
        raw = os.path.join(tempfile.gettempdir(), name + '.raw.tmb')
        envv = dict(os.environ, **(DESERT_ENV if env == 2 else {}))
        r = subprocess.run([sys.executable, os.path.join(HERE, 'pack.py')] + opts + ['--far', '60', os.path.join(kitdir, kit), os.path.join(kitdir, 'avgcolors.json'), g, '-', raw],
                           capture_output=True, text=True, env=envv)
        if r.returncode or not os.path.exists(raw): print(f"{g}: pack failed\n{r.stdout[-800:]}{r.stderr[-800:]}"); continue
        start = [l for l in r.stdout.splitlines() if 'start' in l]
        subprocess.run([sys.executable, os.path.join(HERE, 'tmbz.py'), raw, os.path.join(outdir, name + '.tmb')], check=True)
        open(os.path.join(outdir, name + '.tmt'), 'wb').write(thumb(d, pack.medal_times(g), env, title))
        os.remove(raw)
        print(f"{name}: \"{title}\" ({m.group(1)}) {os.path.getsize(os.path.join(outdir, name + '.tmb')) // 1024} KB; {start[0].strip() if start else ''}")

if __name__ == '__main__': main()
