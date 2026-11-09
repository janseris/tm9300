#!/usr/bin/env python3
"""Assemble campaign tracks from the block map (BlockMap.dll output) and count triangles.
usage: trackstats.py race_tracks.tgz rally_blocks.json Rally [-v]"""
import sys, json, tarfile, re, collections, math
sys.path.insert(0, __import__('os').path.dirname(__file__)); import gbxtrack
tgz, bmap, env = sys.argv[1:4]; verbose = '-v' in sys.argv
blocks = {b['name'].lower(): b for b in json.load(open(bmap))}
BLK = 32  # m per cell (Alpine, Rally, Speed); Bay/Island 64
def pick(bname, flags):
    b = blocks.get(bname.lower())
    if not b: return None, 'unknown'
    ground = bool(flags & 0x1000); variant = flags & 63; sub = (flags >> 6) & 63
    rows = b['groundMobils'] if ground else b['airMobils']
    if not rows: rows = b['airMobils'] if ground else b['groundMobils']
    if not rows: return None, 'no-mobils'
    row = rows[variant] if variant < len(rows) else rows[0]
    if not row: return None, 'empty-row'
    m = row[sub] if sub < len(row) else row[0]
    return m, 'ok'
tf = tarfile.open(tgz)
envdir = {'Alpine': 'Snow', 'Rally': 'Rally', 'Speed': 'Desert', 'Bay': 'Bay', 'Island': 'Island'}[env]
print(f"track      blocks  tris_lod0  tris_lodN  coll_tris  unknown   nearest-150m(lod0/lodN)  top pieces (lod0)")
for m in sorted(tf.getmembers(), key=lambda x: x.name):
    if not m.name.lower().endswith('challenge.gbx') or f'/{envdir}/' not in m.name: continue
    xml, body = gbxtrack.read_track(tf.extractfile(m).read()); t = gbxtrack.parse_blocks(body)
    tot0 = totN = coll = 0; unk = collections.Counter(); per = collections.Counter(); placed = []
    for bn, d, (x, y, z), fl in t['blocks']:
        mob, how = pick(bn, fl)
        if mob is None: unk[how + ':' + bn] += 1; continue
        if mob['tris'] < 0: unk['nosolid:' + bn] += 1; continue
        tot0 += mob['tris']; totN += mob['trisLast']; coll += max(mob['coll'], 0); per[bn] += mob['tris']
        placed.append((x * BLK, y * 8, z * BLK, mob['tris'], mob['trisLast']))
    # frame estimate: from the start block, everything within 150 m
    starts = [p for p, (bn, d, c, fl) in zip(placed, t['blocks']) if 'start' in bn.lower()]
    best = (0, 0)
    for (sx, sy, sz, _, _) in (starts or placed[:1]):
        n0 = sum(p[3] for p in placed if math.hypot(p[0] - sx, p[2] - sz) < 150)
        nN = sum(p[4] for p in placed if math.hypot(p[0] - sx, p[2] - sz) < 150)
        best = max(best, (n0, nN))
    # also the densest 150 m neighbourhood over all blocks (worst case)
    worst = max((sum(p[3] for p in placed if math.hypot(p[0] - q[0], p[2] - q[2]) < 150), sum(p[4] for p in placed if math.hypot(p[0] - q[0], p[2] - q[2]) < 150)) for q in placed[::7]) if placed else (0, 0)
    name = m.name.split('/')[-1].split('.')[0]
    print(f"{name:9s} {len(t['blocks']):6d} {tot0:10d} {totN:10d} {coll:10d} {sum(unk.values()):8d}   start {best[0]:6d}/{best[1]:5d}  worst {worst[0]:6d}/{worst[1]:5d}   " + ', '.join(f'{k}={v}' for k, v in per.most_common(3)))
    if verbose and unk: print('    unknown:', dict(unk.most_common(6)))

# ---- exporter model: what a simplification pass leaves (per placed block)
def model_tris(bname, mob):
    b = blocks.get(bname.lower()); kind = b['kind'] if b else ''
    n = mob['trisLast'] if mob['trisLast'] >= 0 else mob['tris']
    if kind == 'Flat': return min(n, 2)            # flat ground tile: one quad
    if kind == 'Frontier': return min(n, 24)       # shaped terrain tile, decimated
    if 'tree' in bname.lower(): return 8           # billboards
    if kind == 'Clip': return min(n, 4)
    return min(n, 400)                             # decor capped: far forms for the big ones
if '-model' in sys.argv:
    print("\nexporter model (terrain tiles 2/24, trees 8, decor <= 400, else lowest LOD); frame = densest 150 m disc / 3 (90 deg FOV)")
    for m in sorted(tf.getmembers(), key=lambda x: x.name):
        if not m.name.lower().endswith('challenge.gbx') or f'/{envdir}/' not in m.name: continue
        xml, body = gbxtrack.read_track(tf.extractfile(m).read()); t = gbxtrack.parse_blocks(body)
        placed = []
        for bn, d, (x, y, z), fl in t['blocks']:
            mob, how = pick(bn, fl)
            if mob is None or mob['tris'] < 0: continue
            placed.append((x * BLK, z * BLK, model_tris(bn, mob)))
        tot = sum(p[2] for p in placed)
        worst = max(sum(p[2] for p in placed if math.hypot(p[0] - q[0], p[1] - q[1]) < 150) for q in placed[::5])
        med = sorted(sum(p[2] for p in placed if math.hypot(p[0] - q[0], p[1] - q[1]) < 150) for q in placed[::5])
        print(f"{m.name.split('/')[-1].split('.')[0]:9s} track {tot:6d}   150 m disc: median {med[len(med)//2]:6d} worst {worst:6d}   -> frame ~ {med[len(med)//2]//3:5d} .. {worst//3:5d}")
