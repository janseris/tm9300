#!/usr/bin/env python3
"""blockkit.py kit.json avgcolors.json Env out.tmk [--budget N] [--far N] : an environment's block kit for the phone
(0.19), which builds a track from a .Challenge.Gbx itself (tmkit.cpp): every solid of the environment's blocks as
pack.py makes it (pack.Prep: simplified, split into tiles, walls marked; each part's drawn mesh, far form and
collision, unturned), how much of each cell a ground piece covers (the terrain tile under it is dropped or cut), the
blocks' variants, footprints, zone heights and waypoint kinds, the base plates and the car.
Env: Rally, Alpine, Speed, Bay or Island (Speed takes the Desert options of fantrack.py from the environment).
The output is cut from the game's own files: never in git.

File: "TMK2" u32 the LZMA properties byte ((pb * 5 + lp) * 9 + lc) u32 nParts; per part u32 rawSize packedSize
offset; each part a raw LZMA stream (lc 0 lp 1 pb 1, a 4 MB dictionary). Part 0 is the directory, the others the
chunks of mesh data (about 900 KB raw each: the phone unpacks one at a time and keeps what the track uses).
The directory:
  'TMKR' u32 version
  i32 env (the car class) blk blkH baseCell defaultZone(block, -1) sky ground fog fogStart fogEnd farDist carMesh
  i32 nBlocks nSolids nParts nMeshes nColls nPlates nPlateTiles nCover nCoverCells nRow nUnit nameBytes nChunks 0
  blocks[nBlocks] 16 bytes: u16 name u8 kind u8 flags(1 zone, 2 tunnel) i16 zoneHeight(1/16 cell) i16 startFwd(1/16 m)
                            u16 rows(into row[]) u16 units(into unit[], 0xffff none) u8 nUnits
                            u8 waypoint (1 Start 2 Checkpoint 3 Finish 4 StartFinish) u16 0
  row[nRow] u16: per block nGround, (n, solid * n) * nGround, nAir, (n, solid * n) * nAir  (0xffff: no solid)
  unit[nUnit] i8 x, z
  solids[nSolids] 8 bytes: u16 firstPart u8 nParts u8 cellsX u8 cellsZ u8 0 u16 cover(0xffff none)
  parts[nParts] 12 bytes: i16 ox oz (m) u16 mesh far coll (0xffff none) u16 0
  cover[nCover] 8 bytes: i8 cx0 cz0 u8 w h u32 firstCell;  cells[nCoverCells] 34 bytes: u16 mask9 (3 x 3 samples,
                            bit z * 3 + x), u8[32] fine (16 x 16 samples, bit z * 16 + x)
  plates[nPlates] 8 bytes: u16 sizeX sizeZ firstTile nTiles;  plateTiles[nPlateTiles] 8 bytes: i16 ox oz (m) u16 mesh 0
  meshTab[nMeshes] u16 nv nf;  collTab[nColls] u16 nv nf;  chunks[nChunks] u16 firstMesh endMesh firstColl endColl
  names: zero-ended lower-case block names
A chunk: its meshes' x, y, z (each i16 array as its low bytes then its high bytes); their indices u16 (each
  (highest so far + 1) - index, per mesh); colours u16; its collision meshes' x, y, z planes; indices (coded);
  normals nx, ny, nz planes (* 8192); surfaces u8
(each array padded to 4 bytes)"""
import sys, os, json, struct, math, lzma, re
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import pack, assemble
from pack import Mesh

CHUNK = 900 * 1024    # raw bytes of meshes a chunk holds (about)
CLASS = {'Rally': 0, 'Alpine': 1, 'Speed': 2, 'Bay': 3, 'Island': 4}

def main():
    a = sys.argv[1:]; budget = 300; farbudget = 60; args = []; i = 0
    while i < len(a):
        if a[i] == '--budget': budget = int(a[i + 1]); i += 2
        elif a[i] == '--far': farbudget = int(a[i + 1]); i += 2
        else: args.append(a[i]); i += 1
    kitf, colf, env, outf = args
    kit = json.load(open(kitf)); cols = json.load(open(colf))
    prep = pack.Prep(kit, cols, -1, budget, farbudget)
    blk = assemble.ENV_BLK.get(env, 32); blk_h = assemble.ENV_BLK_H.get(env, assemble.BLK_H)
    zone_h = {z['block'].lower(): z['height'] for z in kit.get('zones', []) if z['block']}
    for bn, h in assemble.ZONE_HEIGHT_FIX.items():
        if bn.lower() in zone_h: zone_h[bn.lower()] = h
    wp = {b['name'].lower(): b.get('wp') for b in kit['blocks'] if b.get('wp')}
    pack.BLOCK_WP.update(wp)
    blocks = {}
    for b in kit['blocks']: blocks[b['name'].lower()] = b       # the last of a name, as assemble.Track
    blist = sorted(blocks)
    # the solids the blocks use, and those whose cover is needed (ground pieces that are not terrain, tunnels)
    sidx = {}; snames = []; need_cover = set()
    for bn in blist:
        b = blocks[bn]
        g_empty = not any(any(r) for r in b.get('ground') or [])
        for side in ('ground', 'air'):
            for row in b.get(side) or []:
                for sn in row:
                    if sn and sn in kit['solids'] and sn not in sidx: sidx[sn] = len(snames); snames.append(sn)
                    # (a ground block whose ground rows are empty uses its air rows)
                    if sn and bn not in zone_h and (side == 'ground' or 'tunnel' in bn or g_empty): need_cover.add(sn)
    meshes = []; colls = []; parts = []; solids = []; covers = []; cells = []
    def add_mesh(iv, faces):
        meshes.append((iv, [(f[0], f[1], f[2], f[3][0]) for f in faces])); return len(meshes) - 1
    for k, sn in enumerate(snames):
        s = kit['solids'][sn]
        first = len(parts)
        for ox, oz, pm in prep.parts(sn):
            if not pm.faces: continue
            (iv, faces), far, coll = prep.forms(sn, ox, oz, pm)
            mi = add_mesh(iv, faces)
            fi = add_mesh(*far) if far else 0xffff
            ci = 0xffff
            if coll: colls.append(coll); ci = len(colls) - 1
            parts.append((int(ox), int(oz), mi, fi, ci))
        bb = s['bbox']
        cx1 = max(1, int(round(bb[3] / blk))); cz1 = max(1, int(round(bb[5] / blk)))
        cov = 0xffff
        c = s.get('coll')
        if sn in need_cover and c and c['tris']:
            v = c['verts']; tr = c['tris']
            pts = [(v[i], v[i + 2]) for i in range(0, len(v), 3)]
            tris = [(pts[tr[i]], pts[tr[i + 1]], pts[tr[i + 2]]) for i in range(0, len(tr), 4)]
            # (as assemble.py: no triangles with no area seen from above)
            tris = [(A, B, C) for A, B, C in tris if abs((B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0])) > 1e-4]
            def over(px, pz):
                for A, B, C in tris:
                    s1 = (B[0] - A[0]) * (pz - A[1]) - (B[1] - A[1]) * (px - A[0])
                    s2 = (C[0] - B[0]) * (pz - B[1]) - (C[1] - B[1]) * (px - B[0])
                    s3 = (A[0] - C[0]) * (pz - C[1]) - (A[1] - C[1]) * (px - C[0])
                    if (s1 >= 0 and s2 >= 0 and s3 >= 0) or (s1 <= 0 and s2 <= 0 and s3 <= 0): return True
                return False
            xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
            gx0, gx1 = int(min(xs) // blk), int(max(xs) // blk); gz0, gz1 = int(min(zs) // blk), int(max(zs) // blk)
            if gx1 - gx0 < 64 and gz1 - gz0 < 64:
                c0 = len(cells)
                for gz in range(gz0, gz1 + 1):
                    for gx in range(gx0, gx1 + 1):
                        m9 = 0
                        for j, fz in enumerate((0.15, 0.5, 0.85)):
                            for q, fx in enumerate((0.15, 0.5, 0.85)):
                                if over((gx + fx) * blk, (gz + fz) * blk): m9 |= 1 << (j * 3 + q)
                        fine = bytearray(32)
                        if m9:
                            # a 2 m square (Island 4 m) is covered when all its corners are: a terrain face whose middle
                            # falls in it is dropped (pack.py tests the middle itself; erring this way leaves the terrain
                            # under a piece's edge, never a hole beside it)
                            cor = [[over((gx + q / 16) * blk, (gz + j / 16) * blk) for q in range(17)] for j in range(17)]
                            for j in range(16):
                                for q in range(16):
                                    if cor[j][q] and cor[j][q + 1] and cor[j + 1][q] and cor[j + 1][q + 1]: fine[(j * 16 + q) >> 3] |= 1 << ((j * 16 + q) & 7)
                        cells.append((m9, bytes(fine)))
                covers.append((gx0, gz0, gx1 - gx0 + 1, gz1 - gz0 + 1, c0)); cov = len(covers) - 1
        solids.append((first, len(parts) - first, cx1, cz1, cov))
        if k % 50 == 0: print(f"  {k}/{len(snames)} solids, {len(meshes)} meshes", flush=True)
    # the blocks
    rows = []; units = []; brec = []; names = bytearray(); name_off = {}
    for bn in blist:
        b = blocks[bn]
        name_off[bn] = len(names); names += bn.encode('latin-1', 'replace') + b'\0'
        r0 = len(rows)
        for side in ('ground', 'air'):
            rr = b.get(side) or []
            rows.append(len(rr))
            for row in rr:
                rows.append(len(row)); rows += [sidx.get(sn, 0xffff) if sn else 0xffff for sn in row]
        u = b.get('units'); u0 = 0xffff; nu = 0
        if u: u0 = len(units); nu = len(u); units += [(int(p[0]), int(p[2])) for p in u]
        flags = (1 if bn in zone_h else 0) | (2 if 'tunnel' in bn else 0)
        zh = int(round(zone_h.get(bn, 0) * 16))
        fwd = int(round(pack.START_FWD.get(bn, 0.0) * 16))
        kind = pack.block_kind(bn)
        if wp.get(bn) == 'StartFinish': kind = 4
        wpc = {'Start': 1, 'Checkpoint': 2, 'Finish': 3, 'StartFinish': 4}.get(wp.get(bn), 0)
        brec.append((name_off[bn], kind, flags, zh, fwd, r0, u0, nu, wpc))
    dz = assemble.DEFAULT_ZONE.get(env); dzi = blist.index(dz.lower()) if dz and dz.lower() in blocks else -1
    # the base plates: each Base/<x>x<z> as pack.py makes it for a map of that size
    plates = []; ptiles = []
    for pn in sorted(n for n in kit['solids'] if re.match(r'Base/\d+x\d+$', n)):
        sx, sz = map(int, pn[5:].split('x'))
        L = kit['solids'][pn]['lods'][-1]
        m = pack.from_lod(L, prep.matcol)
        x1, z1 = sx * blk + 300, sz * blk + 300
        m = pack.compact(Mesh(m.verts, [fc for fc in m.faces if all(-300 < m.verts[k][0] < x1 and -300 < m.verts[k][2] < z1 for k in fc[:3])]))
        m = pack.decimate(m, 1500)
        t0 = len(ptiles)
        for ox, oz, pm in pack.split_tiles(m, 64.0):
            if not pm.faces: continue
            iv = [(int(round(p[0] * 16)), int(round(p[1] * 16)), int(round(p[2] * 16))) for p in pm.verts]
            ptiles.append((int(ox), int(oz), add_mesh(iv, pm.faces)))
        plates.append((sx, sz, t0, len(ptiles) - t0))
    car = -1
    for cname in ('RallyCar', 'SnowCar', 'American', 'SpeedCar', 'BayCar', 'SportCar', 'IslandCar', 'CoastCar'):
        if cname in kit['solids']:
            m = pack.compact(pack.decimate(pack.from_lod(kit['solids'][cname]['lods'][0], prep.matcol), pack.CAR_TRIS))
            iv = [(int(round(p[0] * 16)), int(round(p[1] * 16)), int(round(p[2] * 16))) for p in m.verts]
            car = add_mesh(iv, m.faces); break
    rgb = pack.rgb565
    sky = rgb(130, 170, 220); ground = rgb(70, 90, 30) if env == 'Rally' else rgb(205, 145, 90) if env == 'Speed' else rgb(225, 228, 235)
    if env == 'Speed': sky = rgb(150, 180, 215)
    # the chunks: the meshes and collision meshes in groups of about CHUNK raw bytes, in the solids' order (a track's
    # pieces come from a few of them; the phone unpacks one at a time and keeps what the track uses)
    chunks = []; cm0 = cc0 = 0; size = 0
    msz = lambda m: len(m[0]) * 6 + len(m[1]) * 8
    csz = lambda c: len(c[0]) * 6 + len(c[1]) * 13
    for p in parts:
        size += msz(meshes[p[2]]) + (msz(meshes[p[3]]) if p[3] != 0xffff else 0) + (csz(colls[p[4]]) if p[4] != 0xffff else 0)
        if size >= CHUNK:
            m1 = (p[3] if p[3] != 0xffff else p[2]) + 1
            c1 = max([q[4] for q in parts[:parts.index(p) + 1] if q[4] != 0xffff] + [cc0 - 1]) + 1
            chunks.append((cm0, m1, cc0, c1)); cm0, cc0 = m1, c1; size = 0
    chunks.append((cm0, len(meshes), cc0, len(colls)))
    rgb = pack.rgb565
    sky = rgb(130, 170, 220); ground = rgb(70, 90, 30) if env == 'Rally' else rgb(205, 145, 90) if env == 'Speed' else rgb(225, 228, 235)
    if env == 'Speed': sky = rgb(150, 180, 215)
    def pad(out):
        while len(out) & 3: out.append(0)
    def planes16(out, vals):
        b = struct.pack(f'<{len(vals)}h', *vals)
        out.extend(b[0::2]); out.extend(b[1::2]); pad(out)
    def coded(tris):
        m = 0; o = []
        for t in tris:
            for v in t[:3]: o.append((m - v) & 0xffff); m = max(m, v + 1)
        return o
    # the directory
    out = bytearray()
    out += struct.pack('<4sI', b'TMKR', 1)
    out += struct.pack('<12i', CLASS[env], blk, blk_h, assemble.BASE_CELL, dzi, sky, ground, sky, 100 * 16, 180 * 16, pack.FAR_DIST * 16, car)
    out += struct.pack('<14i', len(brec), len(solids), len(parts), len(meshes), len(colls), len(plates), len(ptiles), len(covers), len(cells), len(rows), len(units), len(names), len(chunks), 0)
    for r in brec: out += struct.pack('<HBBhhHHBBH', r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], 0)
    out += struct.pack(f'<{len(rows)}H', *rows); pad(out)
    for u in units: out += struct.pack('<bb', *u)
    pad(out)
    for s in solids: out += struct.pack('<HBBBBH', s[0], s[1], min(255, s[2]), min(255, s[3]), 0, s[4])
    for p in parts: out += struct.pack('<hhHHHH', p[0], p[1], p[2], p[3], p[4], 0)
    for c in covers: out += struct.pack('<bbBBI', *c)
    for m9, fine in cells: out += struct.pack('<H', m9) + fine
    pad(out)
    for p in plates: out += struct.pack('<4H', *p)
    for p in ptiles: out += struct.pack('<hhHH', p[0], p[1], p[2], 0)
    for iv, f in meshes: out += struct.pack('<HH', len(iv), len(f))
    for iv, t in colls: out += struct.pack('<HH', len(iv), len(t))
    for c in chunks: out += struct.pack('<4H', *c)
    out += names; pad(out)
    raws = [bytes(out)]
    # the chunks' data
    for m0, m1, c0, c1 in chunks:
        out = bytearray(); ms = meshes[m0:m1]; cs = colls[c0:c1]
        for k in range(3): planes16(out, [p[k] for iv, f in ms for p in iv])
        idx = []
        for iv, f in ms: idx += coded(f)
        out += struct.pack(f'<{len(idx)}H', *idx); pad(out)
        out += struct.pack(f'<{sum(len(f) for iv, f in ms)}H', *[fc[3] for iv, f in ms for fc in f]); pad(out)
        for k in range(3): planes16(out, [p[k] for iv, t in cs for p in iv])
        idx = []
        for iv, t in cs: idx += coded(t)
        out += struct.pack(f'<{len(idx)}H', *idx); pad(out)
        for k in range(3): planes16(out, [tr[3][k] for iv, t in cs for tr in t])
        out += bytes(tr[4] for iv, t in cs for tr in t); pad(out)
        raws.append(bytes(out))
    filters = [{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME, 'lc': 0, 'lp': 1, 'pb': 1, 'dict_size': 1 << 22}]
    zs = [lzma.compress(r, format=lzma.FORMAT_RAW, filters=filters) for r in raws]
    props = (1 * 5 + 1) * 9 + 0
    head = struct.pack('<4sII', b'TMK2', props, len(zs))
    off = len(head) + 12 * len(zs)
    tab = b''
    for r, z in zip(raws, zs): tab += struct.pack('<III', len(r), len(z), off); off += len(z)
    open(outf, 'wb').write(head + tab + b''.join(zs))
    nmf = sum(len(m[1]) for m in meshes); ncf = sum(len(c[1]) for c in colls)
    print(f"{outf}: {env}: {len(brec)} blocks, {len(solids)} solids, {len(parts)} parts, {len(meshes)} meshes ({nmf} faces), "
          f"{len(colls)} collision meshes ({ncf} faces), {len(covers)} covers ({len(cells)} cells), {len(plates)} plates; "
          f"directory {len(raws[0]) // 1024} KB, {len(chunks)} chunks up to {max(len(r) for r in raws[1:]) // 1024} KB; raw {sum(map(len, raws)) // 1024} KB, packed {off // 1024} KB")

if __name__ == '__main__': main()
