#!/usr/bin/env python3
"""Assemble a TMF track from a KitExport kit: placed solids in world space (metres, y up).
   from assemble import os
import Track; t = Track(kitjson, trackbytes); t.instances -> [(solidname, x, y, z, dir, groundflag)]
   t.triangles(lod) -> list of (p0, p1, p2, matIndex) in world metres."""
import json, math, sys, os
sys.path.insert(0, os.path.dirname(__file__)); import gbxtrack

ENV_BLK = {'Alpine': 32, 'Rally': 32, 'Speed': 32, 'Bay': 64, 'Island': 64, 'Stadium': 32}
ENV_BLK_H = {"Alpine": 16}
DEFAULT_ZONE = {'Rally': 'RallyFlat', 'Alpine': 'AlpineSnowBase', 'Speed': 'DesertFlat', 'Bay': 'BayFlat', 'Island': 'IslandFlat'}
ZONE_HEIGHT_FIX = {'RallyRockyGrass': -1.5, 'RallyRockyGrassHills': 2.5, 'RallyRockyGrassCliffs': 1.5}
BASE_CELL = 3       # the default terrain's cell (its top one cell up: 32 m in Rally, 64 m in Alpine)
BLK_H = 8

def rot_xz(x, z, d, c):
    """rotate (x, z) about (c, c) by d * 90 degrees clockwise seen from above (TM: North, East, South, West)"""
    x -= c; z -= c
    for _ in range(d % 4):
        x, z = z, -x
    return x + c, z + c

class Track:
    def __init__(self, kit, data, rotsign=-1):
        self.kit = kit if isinstance(kit, dict) else json.load(open(kit))
        self.blocks = {b['name'].lower(): b for b in self.kit['blocks']}
        # a zone block (Flat / Frontier) is placed at its cell minus the zone's height (the replays agree: hills -16 m)
        self.zone_height = {z['block'].lower(): z['height'] for z in self.kit.get('zones', []) if z['block']}
        # fitted against the replays where the zone's own height does not place it (the RockyGrass family sits
        # a cell and a half higher than its height says; its hills and cliffs two cells lower than that)
        for bn, h in ZONE_HEIGHT_FIX.items():
            if bn.lower() in self.zone_height: self.zone_height[bn.lower()] = h
        self.solids = self.kit['solids']
        xml, body = gbxtrack.read_track(data)
        self.env = __import__('re').search(r'envir="(\w+)"', xml).group(1)
        self.blk = ENV_BLK.get(self.env, 32)
        self.blk_h = ENV_BLK_H.get(self.env, BLK_H)
        self.t = gbxtrack.parse_blocks(body)
        self.size = self.t['size']
        self.rotsign = rotsign
        self.instances = []
        self.unknown = {}
        # the map's default terrain: a column with no zone block holds the base flat (grass in Rally, snow in
        # Alpine) at the base level (its top at 32 m / 64 m: the cars land on it in the replays); the Gbx only
        # stores the terrain the author raised or painted
        default_zone = DEFAULT_ZONE.get(self.env)
        if default_zone and default_zone.lower() in self.blocks:
            zone_cols = {(x, z) for bn, d, (x, y, z), fl in self.t['blocks'] if bn.lower() in self.zone_height}
            self.default_cells = 0
            for x in range(self.size[0]):
                for z in range(self.size[2]):
                    if (x, z) not in zone_cols: self.t['blocks'].append((default_zone, 0, (x, BASE_CELL, z), 0x1000)); self.default_cells += 1
        for bn, d, (x, y, z), fl in self.t['blocks']:
            b = self.blocks.get(bn.lower())
            if not b: self.unknown[bn] = self.unknown.get(bn, 0) + 1; continue
            ground = bool(fl & 0x1000); variant = fl & 63; sub = (fl >> 6) & 63
            rows = b['ground'] if ground else b['air']
            # the variant rows the flag asks for may be there but empty (a castle's hill bridge has only a ground
            # row and no ground flag): the other side's then
            if not any(any(r) for r in rows): rows = b['air'] if ground else b['ground']
            if not any(any(r) for r in rows): continue
            row = rows[variant] if variant < len(rows) else rows[0]
            if not row: continue
            sname = row[sub] if sub < len(row) else row[0]
            if not sname: continue
            yc = y - self.zone_height.get(bn.lower(), 0)
            # a block of several units: the Gbx cell is the lowest corner of its footprint once turned, so the
            # turned mesh (about the first unit's middle) is shifted by the turned footprint's lowest unit
            sx, sz = 0, 0
            units = b.get('units')
            if not units:
                # no unit list in the kit (Alpine's big pieces): the footprint from the solid's box
                bb = self.solids[sname]['bbox']
                cx1 = max(1, int(round(bb[3] / self.blk))); cz1 = max(1, int(round(bb[5] / self.blk)))
                units = [[ux, 0, uz] for ux in range(cx1) for uz in range(cz1)]
            for u in units:
                ux, uz = rot_xz(u[0], u[2], (d * self.rotsign) % 4, 0)
                if ux < sx: sx = ux
                if uz < sz: sz = uz
            # the instance's turn is the effective one (the Gbx direction times rotsign): place() and the packer use it as is
            self.instances.append((sname, bn, (x - sx) * self.blk, int(round(yc * self.blk_h)), (z - sz) * self.blk, (d * self.rotsign) % 4, ground, b['kind']))
        self.zone_cut = {}
        self.drop_covered_zones()

    def drop_covered_zones(self):
        """a ground block brings its own terrain (every Rally one covers its cell's footprint, the Tetris pieces
        several cells'), so the zone tile under it is not there in the game: dropped, by the cells whose middle
        the block's collision mesh covers (a Clip is a small connector: it hides nothing)"""
        covered = set(); self.zone_cut = {}
        for sname, bn, x, y, z, d, ground, kind in self.instances:
            if not ground or bn.lower() in self.zone_height: continue
            s = self.solids.get(sname); c = s.get('coll') if s else None
            if not c or not c['tris']: continue
            v = c['verts']; pts = self.place([(v[i], v[i + 1], v[i + 2]) for i in range(0, len(v), 3)], x, y, z, d)
            xs = [p[0] for p in pts]; zs = [p[2] for p in pts]
            cx0, cx1 = int(min(xs) // self.blk), int(max(xs) // self.blk); cz0, cz1 = int(min(zs) // self.blk), int(max(zs) // self.blk)
            tr = c['tris']
            def over(px, pz):
                for i in range(0, len(tr), 4):
                    A, B, C = pts[tr[i]], pts[tr[i + 1]], pts[tr[i + 2]]
                    s1 = (B[0] - A[0]) * (pz - A[2]) - (B[2] - A[2]) * (px - A[0])
                    s2 = (C[0] - B[0]) * (pz - B[2]) - (C[2] - B[2]) * (px - B[0])
                    s3 = (A[0] - C[0]) * (pz - C[2]) - (A[2] - C[2]) * (px - C[0])
                    if (s1 >= 0 and s2 >= 0 and s3 >= 0) or (s1 <= 0 and s2 <= 0 and s3 <= 0): return True
                return False
            for cx in range(cx0, cx1 + 1):
                for cz in range(cz0, cz1 + 1):
                    if (cx, cz) in covered: continue
                    # the whole cell, not just its middle (a rock in a field is decoration on the tile, not a tile);
                    # a part of it: the tile stays but is cut where the block's ground is (the packer: zone_cut)
                    n = sum(1 for fx in (0.15, 0.5, 0.85) for fz in (0.15, 0.5, 0.85) if over((cx + fx) * self.blk, (cz + fz) * self.blk))
                    if n >= 6: covered.add((cx, cz))
                    elif n >= 1: self.zone_cut.setdefault((cx, cz), []).append(over)
        before = len(self.instances)
        self.instances = [i for i in self.instances if not (i[1].lower() in self.zone_height and (int(i[2] // self.blk), int(i[4] // self.blk)) in covered)]
        for c in covered: self.zone_cut.pop(c, None)
        self.dropped_zones = before - len(self.instances)

    def place(self, pts, x, y, z, d):
        """local solid points (list of (px, py, pz)) into world"""
        c = self.blk / 2
        out = []
        for px, py, pz in pts:
            rx, rz = rot_xz(px, pz, d % 4, c)
            out.append((x + rx, y + py, z + rz))
        return out

    def triangles(self, lod=-1, kinds=None, coll=False):
        tris = []
        for sname, bn, x, y, z, d, ground, kind in self.instances:
            if kinds and kind not in kinds: continue
            s = self.solids[sname]
            if coll:
                v = s['coll']['verts']; f = s['coll']['tris']
            else:
                L = s['lods'][lod if lod >= 0 and lod < len(s['lods']) else len(s['lods']) - 1]
                v = L['verts']; f = L['faces']
            pts = self.place([(v[i], v[i + 1], v[i + 2]) for i in range(0, len(v), 3)], x, y, z, d)
            for i in range(0, len(f), 4):
                tris.append((pts[f[i]], pts[f[i + 1]], pts[f[i + 2]], f[i + 3], sname))
        return tris
