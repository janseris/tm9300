#!/usr/bin/env python3
"""Pack a track for the Nokia 9300 viewer (v3): kit JSON (KitExport) + Challenge.Gbx + replay JSON (ReplayDump) +
texture average colours -> track.tmb. World in 1/16 m ints, y up.

  pack.py kit.json avgcolors.json track.Challenge.Gbx replay.json out.tmb [--lod N] [--budget N] [--only name] [--noplate] [--depthfix]

What it does to Nadeo's geometry (the phone draws ~1,000-1,500 triangles a frame well):
  - every solid at its lowest LOD; faces lit on the PC (the normal against one light), colours baked in
  - meshes over the budget decimated by vertex clustering (a grid, coarser until it fits)
  - meshes wider than a cell split into 32 m tiles by face middles (the base plate, the Tetris pieces): each a
    mesh and an instance of its own, so the view test and the far cut work on them
  - the tree solids: their mesh (lowest LOD: the trunks and branch cards) plus a low-poly canopy
    (an octahedron) for each cluster of sprites, in the leaf texture's colour
  - the base plate (the terrain under and around the map) from Media/Solid/Base/<size>, decimated and tiled
  - the car (RallyCar / SnowCar ...) decimated to ~600 triangles, stored as mesh "car" (drawn turned per frame)
  - meshes pre-turned for each direction an instance uses: the phone turns nothing per frame

Format TMB4 (little-endian):
  'TMB5' u32 nMesh u32 nInst u32 nPath i32 carMesh(-1 none; the car's class in the top byte: 0 Rally 1 Snow 2 Desert 3 Bay 4 Island)  i32 sky565 i32 ground565 i32 fog565  i32 fogStart i32 fogEnd (1/16 m)
  u32 nColl u32 nCInst  i32 startX startY startZ (1/16 m) i32 startYaw
  mesh: u16 nv u16 nf i16 bbox[6] (from the instance origin)  i16 v[nv*3]  u16 idx[nf*3]  u16 colour[nf]  (pad to 4)
  inst: u16 mesh u16 flags i32 x y z (the origin, world 1/16 m)  i32 bbox[6] (world)
  path: i32 x y z (car, 1/16 m) i16 yaw (0..4095, 0 = +z, clockwise = towards +x) i16 pitch
  coll mesh: u16 nv u16 nf i16 bbox[6]  i16 v[nv*3]  u16 idx[nf*3]  i16 normal[nf*3] (* 8192)  u8 surface[nf]  (pad to 4)
  cinst: u16 coll u16 kind (0 plain, 1 start, 2 checkpoint, 3 finish, 4 multilap)  i32 x y z  i32 bbox[6] (world)
  TMB5: i32 farDist (1/16 m)  u16 far[nInst] (the mesh drawn beyond farDist; 0xffff none)  (pad to 4)
The collision meshes are Nadeo's own collision solids (CPlugSurface), pre-turned like the visual ones; the surfaces:
  0 road (pavement, wood, metal), 1 dirt, 2 grass, 3 rock, 4 water, 5 none (not collidable: dropped), 6 turbo, 7 ice, 8 snow,
  11 turbo2 (0.18: the red pads, a big push),
  9 a wall (a face steeper than ~75 degrees, its normal as it is: the car slides along it)
"""
# the depth-order work-arounds of v0.3/0.4 (water under a low deck dropped, faces under low pieces dropped) are
# off: the renderer's depth test (tm9300 0.5, R3D 1.60) draws layered surfaces right; --depthfix turns them on
DEPTHFIX = False
CAR_TRIS = int(__import__('os').environ.get('CAR_TRIS', '600'))   # the car's triangles (vertex clustering below ~500 ruins its shape)
ALLWALLS = __import__('os').environ.get('ALLWALLS', '1') != '0'
NADEOWALLS = __import__('os').environ.get('NADEOWALLS', '0') != '0'   # 0.16: walls from Nadeo's collision meshes     # 0.8: every steep face of a collidable piece is a wall
WALL_NY = 0.45         # faces steeper than ~63 degrees are walls (the car hits them), not ground: a big stone's side, a bank
WALLSTAT = [0, 0]      # steep faces kept as walls / dropped (per solid when PACKSTATS is set)
import sys, json, struct, math, os, collections
sys.path.insert(0, os.path.dirname(__file__)); import assemble

def rgb565(r, g, b): return ((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3)
LIGHT = (0.45, 0.8, 0.4); _l = math.sqrt(sum(c * c for c in LIGHT)); LIGHT = tuple(c / _l for c in LIGHT)
TILE = 32.0     # m: the split size for wide meshes
FAR_DIST = 100  # m: beyond it an instance draws its far form (the fog starts there)
R_GRID = float(__import__('os').environ.get('R_GRID', '1.2'))    # m: the coarsest clustering of what is not driven on (walls, roofs)
D_GRID = float(__import__('os').environ.get('D_GRID', '1.4'))
# 0.16: the coarse grids and the budget only for the solids matching COARSE (a regex: Desert's sand and cliffs); the
# others (roads, ramps, garages) at 1.4 / 1.2 m and ROAD_BUDGET faces (a ramp clustered at 3 m lost its slope)
COARSE = __import__('os').environ.get('COARSE')
# 0.18: the solids (a regex) whose steep faces are never walls: Bay's turbo gantries (the replays drive through their
# posts: the game does not stop the car there)
NOWALLS = __import__('os').environ.get('NOWALLS')
ROAD_BUDGET = int(__import__('os').environ.get('ROAD_BUDGET', '0'))    # m: the coarsest of what is driven on (0.15: Desert's sand tiles need 3)
EDGE_STEP = 2.0 # m: the grid the pieces' edge corners snap to along the edge (the seams)
WIND = 'reverse'

def face_normal(A, B, C):
    e1 = (B[0] - A[0], B[1] - A[1], B[2] - A[2]); e2 = (C[0] - A[0], C[1] - A[1], C[2] - A[2])
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    l = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    return (n[0] / l, n[1] / l, n[2] / l) if l > 1e-9 else None, l * 0.5

def lit(col, n):
    shade = 0.55 + 0.45 * max(0.0, n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2])
    return rgb565(min(255, col[0] * shade), min(255, col[1] * shade), min(255, col[2] * shade))

class Mesh:
    """verts: [(x, y, z)] metres; faces: [(a, b, c, (colour565, surface))] wound for R3D (clockwise from the front
    in its frame); the surface class is the physics' (0 road ... 8 snow, 5 nothing: the car passes through)"""
    def __init__(self, verts, faces): self.verts = verts; self.faces = faces

# the surface class from the texture's name (the physics drives on the drawn mesh itself, so it is what
# decides where the car stands): 0 road, 1 dirt, 2 grass, 3 rock / wall, 4 water, 5 none, 6 turbo, 7 ice, 8 snow
def tex_surface(tex):
    n = tex.lower()
    for key, surf in (('turbo2', 11), ('turbo', 6), ('treebranch', 5), ('trees', 5), ('hilltree', 5), ('warptree', 5), ('flag', 5), ('sign', 5),
                      ('advert', 5), ('envmap', 5), ('vegetation', 5), ('lamp', 5), ('alpha', 10), ('grid', 0),
                      ('ice', 7), ('snow', 8), ('toundra', 8), ('tundra', 8),
                      ('water', 4), ('sea', 4), ('lake', 4),
                      ('dirtyroad', 1), ('dirt', 1), ('field', 1), ('sand', 1),
                      ('grass', 2),
                      ('road', 0), ('asphalt', 0), ('bridge', 0), ('wood', 0), ('castle', 0), ('pavement', 0), ('freewheel', 0),
                      ('rock', 3), ('cliff', 3), ('stone', 3), ('trunk', 3), ('bark', 3), ('house', 3), ('temple', 3), ('tunnel', 3),
                      ('chalet', 3), ('windmill', 3), ('pylon', 3)):
        if key in n: return surf
    return 0

def tree_cards(L, mats, key='hilltree'):
    """0.15: the forest cards on the Alpine hills (crossed quads 17 x 28 m, a row of fir trees in the texture's alpha,
    which the phone cannot draw): each quad becomes two fir shapes, triangles from its foot to its top edge, both
    sides (they were drawn as whole dark rectangles)"""
    v = L['verts']; f = L['faces']; nv = L['norms']
    tree = [i for i in range(0, len(f), 4) if f[i + 3] >= 0 and key in mats[f[i + 3]]['texture'].lower()]
    if not tree: return L
    P = lambda k: (round(v[k * 3], 2), round(v[k * 3 + 1], 2), round(v[k * 3 + 2], 2))
    quads = {}
    for i in tree:
        for j in tree:
            if j <= i: continue
            a = {P(f[i]), P(f[i + 1]), P(f[i + 2])}; b = {P(f[j]), P(f[j + 1]), P(f[j + 2])}
            if len(a & b) == 2 and len(a | b) == 4: quads[tuple(sorted(a | b))] = f[i + 3]
    verts = list(v); norms = list(nv); faces = [f[i + k] for i in range(0, len(f), 4) if i not in set(tree) for k in range(4)]
    def add(p):
        verts.extend(p); norms.extend((0.0, 1.0, 0.0)); return len(verts) // 3 - 1
    for q, m in quads.items():
        q = sorted(q, key=lambda p: p[1]); b0, b1 = q[0], q[1]; t0, t1 = (q[2], q[3]) if math.dist(q[2], b0) < math.dist(q[3], b0) else (q[3], q[2])
        for k, hgt in ((0, 1.0), (1, 0.85)):
            lerp = lambda A, B, u: tuple(A[c] + (B[c] - A[c]) * u for c in range(3))
            f0, f1 = lerp(b0, b1, k / 2), lerp(b0, b1, (k + 1) / 2)
            top = lerp(t0, t1, (k + 0.5) / 2); foot = lerp(b0, b1, (k + 0.5) / 2); top = lerp(foot, top, hgt)
            i0, i1, i2 = add(f0), add(f1), add(top)
            faces += [i0, i1, i2, m, i0, i2, i1, m]
    return dict(L, verts=verts, norms=norms, faces=faces)

def from_lod(L, matcol, matsurf=lambda i: 0):
    v = L['verts']; n = L['norms']; f = L['faces']
    verts = [(v[i], v[i + 1], v[i + 2]) for i in range(0, len(v), 3)]
    norms = [(n[i], n[i + 1], n[i + 2]) for i in range(0, len(n), 3)]
    faces = []
    for i in range(0, len(f), 4):
        ia, ib, ic, m = f[i], f[i + 1], f[i + 2], f[i + 3]
        nrm, area = face_normal(verts[ia], verts[ib], verts[ic])
        if nrm is None: continue
        if WIND == 'normals':
            vn = [(norms[ia][k] + norms[ib][k] + norms[ic][k]) for k in range(3)]
            agree = nrm[0] * vn[0] + nrm[1] * vn[1] + nrm[2] * vn[2] >= 0
            if not agree: nrm = (-nrm[0], -nrm[1], -nrm[2])
            if agree: ib, ic = ic, ib
        else:
            # the kit's own order (consistent within Nadeo's meshes); 'keep': front = the cross product's side,
            # 'reverse': the other; the lighting normal is the front's
            if WIND == 'reverse': ib, ic = ic, ib
            else: nrm = (-nrm[0], -nrm[1], -nrm[2])
        faces.append((ia, ib, ic, (lit(matcol(m), nrm), matsurf(m))))
    return Mesh(verts, faces)

def surface_height(mesh, px, pz, near_y):
    """the mesh's height at (px, pz) on the layer nearest near_y (None: no face there); the point is on the
    piece's edge, so faces touching the edge count (a tolerance of 1 cm)"""
    best = None
    for fc in mesh.faces:
        A, B, C = mesh.verts[fc[0]], mesh.verts[fc[1]], mesh.verts[fc[2]]
        if min(A[0], B[0], C[0]) > px + 0.01 or max(A[0], B[0], C[0]) < px - 0.01: continue
        if min(A[2], B[2], C[2]) > pz + 0.01 or max(A[2], B[2], C[2]) < pz - 0.01: continue
        s1 = (B[0] - A[0]) * (pz - A[2]) - (B[2] - A[2]) * (px - A[0])
        s2 = (C[0] - B[0]) * (pz - B[2]) - (C[2] - B[2]) * (px - B[0])
        s3 = (A[0] - C[0]) * (pz - C[2]) - (A[2] - C[2]) * (px - C[0])
        eps = 0.05
        if not ((s1 >= -eps and s2 >= -eps and s3 >= -eps) or (s1 <= eps and s2 <= eps and s3 <= eps)): continue
        e1 = (B[0] - A[0], B[1] - A[1], B[2] - A[2]); e2 = (C[0] - A[0], C[1] - A[1], C[2] - A[2])
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if abs(n[1]) < 0.2 * math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-12: continue      # a wall: not a surface
        h = A[1] - (n[0] * (px - A[0]) + n[2] * (pz - A[2])) / n[1]
        if best is None or abs(h - near_y) < abs(best - near_y): best = h
    return best

def cluster(mesh, g, keep_edges=None, fixed=None):
    """vertex clustering on a grid of g metres: the merged mesh (faces that collapse are dropped); keep_edges
    (x0, x1, z0, z1): a merged corner with a member on one of those planes stays on it (the pieces' seams
    stay closed: a collision mesh with a gap at a seam drops the car through)"""
    cells = {}; rep = []
    for i, p in enumerate(mesh.verts):
        if fixed and i in fixed:
            # a corner pinned where the pass before put it (its group: the corners merged with it then)
            grp, pos = fixed[i]
            key = ('fixed', grp)
            if key not in cells: cells[key] = [len(cells), list(pos), 1, [None, None], None, True]
            rep.append(cells[key][0]); continue
        key = (math.floor(p[0] / g), math.floor(p[1] / g), math.floor(p[2] / g))
        if keep_edges:
            # a corner on the piece's edge: snapped along the edge to the EDGE_STEP grid (the next piece snaps to
            # the same points, so the two edges are the same polyline and the seam closes); the height is the
            # original mesh's at that point (below), so it is the neighbour's too
            x0, x1, z0, z1 = keep_edges
            ex = x0 if abs(p[0] - x0) < 0.02 else x1 if abs(p[0] - x1) < 0.02 else None
            ez = z0 if abs(p[2] - z0) < 0.02 else z1 if abs(p[2] - z1) < 0.02 else None
            if ex is not None or ez is not None:
                sx = ex if ex is not None else round(p[0] / EDGE_STEP) * EDGE_STEP
                sz = ez if ez is not None else round(p[2] / EDGE_STEP) * EDGE_STEP
                key = ('edge', sx, sz, math.floor(p[1] / max(g, 1.0)))
                if key not in cells: cells[key] = [len(cells), [sx, 0.0, sz], 0, [None, None], [0.0, 0], 'edge']
                c = cells[key]; c[4][0] += p[1]; c[4][1] += 1
                rep.append(c[0]); continue
        if key not in cells: cells[key] = [len(cells), [0.0, 0.0, 0.0], 0, [None, None]]
        c = cells[key]; c[1][0] += p[0]; c[1][1] += p[1]; c[1][2] += p[2]; c[2] += 1
        rep.append(c[0])
    verts = [None] * len(cells)
    for c in cells.values():
        if len(c) > 5 and c[5] is True: verts[c[0]] = tuple(c[1]); continue        # pinned
        if len(c) > 5 and c[5] == 'edge':
            # the original mesh's height at the snapped edge point, the layer nearest the members' mean
            ym = c[4][0] / c[4][1]
            y = surface_height(mesh, c[1][0], c[1][2], ym)
            verts[c[0]] = (c[1][0], y if y is not None and abs(y - ym) < 0.6 else ym, c[1][2]); continue      # a wall's corner keeps its own height
        x, y, z = c[1][0] / c[2], c[1][1] / c[2], c[1][2] / c[2]
        verts[c[0]] = (x, y, z)
    faces = []; seen = set()
    for a0, b0, c0, col in mesh.faces:
        a, b, c = rep[a0], rep[b0], rep[c0]
        if a == b or b == c or a == c: continue
        key = tuple(sorted((a, b, c)))
        if key in seen: continue
        seen.add(key)
        # a face turned inside out by the merging (its corners moved past each other) would be culled as a
        # back face: its order put back so it faces the way it did (a bridge's deck had holes)
        n0, _ = face_normal(mesh.verts[a0], mesh.verts[b0], mesh.verts[c0])
        n1, _ = face_normal(verts[a], verts[b], verts[c])
        if n0 is not None and n1 is not None and n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2] < 0: b, c = c, b
        faces.append((a, b, c, col))
    out = Mesh(verts, faces); out.rep = rep
    return out

def decimate(mesh, budget, keep_edges=None):
    if len(mesh.faces) <= budget: return mesh
    g = 0.25
    while g < 64:
        m = cluster(mesh, g, keep_edges)
        if len(m.faces) <= budget: return m
        g *= 1.5
    return m

def is_drivable(mesh, fc):
    if fc[3][1] == 5: return False
    n, _ = face_normal(mesh.verts[fc[0]], mesh.verts[fc[1]], mesh.verts[fc[2]])
    return n is not None and abs(n[1]) >= 0.25

def decimate_keep(mesh, budget, keep_edges=None, r_grid=None, d_grid=None):
    """decimation that keeps what the car drives on: the faces it can stand on are clustered finely (to ~60 % of
    the budget), the rest (walls, roofs, decoration) coarsely to fit, with the drivable part's corners pinned
    where the fine pass put them so no crack opens at the foot of a wall; a checkpoint arch simplified as one
    piece lost its road"""
    if len(mesh.faces) <= budget: return mesh
    D = [fc for fc in mesh.faces if is_drivable(mesh, fc)]; R = [fc for fc in mesh.faces if not is_drivable(mesh, fc)]
    if not D or not R: return decimate(mesh, budget, keep_edges)
    bd = max(40, int(budget * 0.6)) if len(R) > budget * 0.4 else budget - len(R)
    g = 0.25; md = Mesh(mesh.verts, D); md.rep = list(range(len(mesh.verts)))
    # never coarser than ~1.3 m for what is driven on: a road through an arch kept its shape, the arch pays
    if d_grid is None: d_grid = D_GRID
    while len(md.faces) > bd and g < d_grid: md = cluster(Mesh(mesh.verts, D), g, keep_edges); g *= 1.5
    # the drivable corners pinned: original index -> (its merged corner's id, position)
    used_d = {k for fc in D for k in fc[:3]}
    fixed = {i: (md.rep[i], md.verts[md.rep[i]]) for i in used_d}
    # and the rest's corners at the same place as a drivable one (Nadeo splits corners between materials):
    # pinned with it, so the foot of a wall stays on the road
    at = {}
    for i in used_d:
        p = mesh.verts[i]; at[(round(p[0], 2), round(p[1], 2), round(p[2], 2))] = i
    for fc in R:
        for k in fc[:3]:
            if k in fixed: continue
            p = mesh.verts[k]; j = at.get((round(p[0], 2), round(p[1], 2), round(p[2], 2)))
            if j is not None: fixed[k] = fixed[j]
    # the rest never coarser than R_GRID: a thin wall merged across its thickness collapses and the wall is gone
    g2 = 0.5; m = mesh
    if r_grid is None: r_grid = R_GRID
    while g2 <= r_grid:
        m = cluster(mesh, g2, keep_edges, fixed)
        if len(m.faces) <= budget: break
        g2 *= 1.5
    return m

def compact(mesh):
    used = sorted({k for fc in mesh.faces for k in fc[:3]}); remap = {k: i for i, k in enumerate(used)}
    return Mesh([mesh.verts[k] for k in used], [(remap[a], remap[b], remap[c], col) for a, b, c, col in mesh.faces])

def split_tiles(mesh, tile=TILE):
    """faces grouped by the tile their middle falls in (x, z): [(ox, oz, Mesh)] with the verts moved to the tile's
    origin (ox, 0, oz) in metres; a face with a corner more than 1,900 m from it (far scenery) is dropped"""
    groups = collections.defaultdict(list)
    for fc in mesh.faces:
        A, B, C = mesh.verts[fc[0]], mesh.verts[fc[1]], mesh.verts[fc[2]]
        mx = (A[0] + B[0] + C[0]) / 3; mz = (A[2] + B[2] + C[2]) / 3
        groups[(math.floor(mx / tile), math.floor(mz / tile))].append(fc)
    out = []
    for (tx, tz), fcs in sorted(groups.items()):
        ox, oz = tx * tile, tz * tile
        keep = [fc for fc in fcs if all(max(abs(mesh.verts[k][0] - ox), abs(mesh.verts[k][2] - oz), abs(mesh.verts[k][1])) < 1900 for k in fc[:3])]
        if not keep: continue
        m = compact(Mesh(mesh.verts, keep))
        m.verts = [(p[0] - ox, p[1], p[2] - oz) for p in m.verts]
        out.append((ox, oz, m))
    return out

def rotate(mesh, d, c):
    """the mesh turned d quarter turns about (c, c) in x, z (assemble.rot_xz)"""
    return Mesh([(lambda rx, rz: (rx, p[1], rz))(*assemble.rot_xz(p[0], p[2], d, c)) for p in mesh.verts], list(mesh.faces))

def octahedron(cx, cy, cz, r, col):
    """a canopy blob: 6 corners, 8 faces, wound clockwise from outside in R3D's frame (as the kit's faces come out)"""
    pts = [(cx, cy + r, cz), (cx, cy - r * 0.8, cz), (cx + r, cy, cz), (cx, cy, cz + r), (cx - r, cy, cz), (cx, cy, cz - r)]
    tris = [(0, 2, 3), (0, 3, 4), (0, 4, 5), (0, 5, 2), (1, 3, 2), (1, 4, 3), (1, 5, 4), (1, 2, 5)]
    faces = []
    for a, b, c in tris:
        n, _ = face_normal(pts[a], pts[b], pts[c])
        # face_normal's cross product points outwards for this order; R3D wants the opposite winding (see from_lod)
        faces.append((a, c, b, (lit(col, n), 5)))
    return Mesh(pts, faces)

def canopies(sprites, col):
    """clusters of sprite positions (within 4 m) -> one octahedron each"""
    pts = [(sprites[i], sprites[i + 1], sprites[i + 2], sprites[i + 3]) for i in range(0, len(sprites), 4)]
    clusters = []
    for p in pts:
        for cl in clusters:
            if math.dist(p[:3], cl['c']) < 4.5:
                cl['pts'].append(p); n = len(cl['pts']); cl['c'] = tuple(sum(q[k] for q in cl['pts']) / n for k in range(3)); break
        else: clusters.append({'c': p[:3], 'pts': [p]})
    verts = []; faces = []
    for cl in clusters:
        r = max(2.5, min(6.0, 0.45 * sum(q[3] for q in cl['pts']) / len(cl['pts']) + 0.6 * math.sqrt(len(cl['pts']))))
        m = octahedron(cl['c'][0], cl['c'][1], cl['c'][2], r, col)
        b = len(verts); verts += m.verts; faces += [(a + b, bb + b, c + b, cc) for a, bb, c, cc in m.faces]
    return Mesh(verts, faces)

def medal_times(path):
    """0.8: the track's medal times (ms) from its file's header (chunk 0x03043002, or 0x24003002 in the older
    class numbering): author ("Nadeo"), gold, silver, bronze; zeros when not found"""
    try:
        d = open(path, 'rb').read()
        if d[:3] != b'GBX': return (0, 0, 0, 0)
        ver = struct.unpack_from('<H', d, 3)[0]
        if ver < 6: return (0, 0, 0, 0)
        p = 13
        _, n = struct.unpack_from('<II', d, p); p += 8
        chunks = []
        for i in range(n):
            cid, sz = struct.unpack_from('<II', d, p); p += 8; chunks.append((cid, sz & 0x7fffffff))
        for cid, sz in chunks:
            if cid in (0x03043002, 0x24003002) and d[p] >= 3:
                br, si, go, au = struct.unpack_from('<IIII', d, p + 5)
                return (au, go, si, br)
            p += sz
    except (struct.error, IndexError, OSError): pass
    return (0, 0, 0, 0)

def coll_from(mesh):
    """the collision of a drawn mesh: its faces the car can stand on (|ny| >= WALL_NY, turned up) with a surface,
    as (a, b, c, (nx, ny, nz) * 8192, surface); what is drawn is what is driven on, seams and all"""
    tris = []
    for a, b, cc, (col, surf) in mesh.faces:
        if surf == 5: continue
        n, area = face_normal(mesh.verts[a], mesh.verts[b], mesh.verts[cc])
        if n is None: continue
        if abs(n[1]) < WALL_NY or surf == 9:
            # a wall (steeper than ~63 degrees, or a bevelled face the game's collision has a wall at): kept with its
            # normal as it is, for the car to hit, when the solid's own collision mesh has a wall there too
            # (mark_walls): a lamp post's base or a painted edge is not solid
            if surf != 9 or area < 0.05: continue
            tris.append((a, b, cc, tuple(int(round(k * 8192)) for k in n), 9)); continue
        if n[1] < 0: a, b, cc = a, cc, b; n = (-n[0], -n[1], -n[2])
        tris.append((a, b, cc, tuple(int(round(k * 8192)) for k in n), surf))
    return tris

def mark_walls(mesh, s, nowalls=False):
    """the mesh's steep faces (|ny| < WALL_NY) become surface 9 (solid) when Nadeo's collision solid has a steep face
    within 0.6 m of their middle facing the same way, else 5 (nothing): what the game lets the car through, we do"""
    c = s.get('coll')
    walls = []
    # the piece has collision in the game at all (any face not NotCollidable)
    collidable = bool(c and c['tris'] and any(c['surfs'][c['tris'][i + 3]] != 'NotCollidable' for i in range(0, len(c['tris']), 4)))
    if c and c['tris']:
        v = c['verts']
        for i in range(0, len(c['tris']), 4):
            a, b, cc = c['tris'][i], c['tris'][i + 1], c['tris'][i + 2]
            if c['surfs'][c['tris'][i + 3]] == 'NotCollidable': continue
            A, B, C = (v[a * 3], v[a * 3 + 1], v[a * 3 + 2]), (v[b * 3], v[b * 3 + 1], v[b * 3 + 2]), (v[cc * 3], v[cc * 3 + 1], v[cc * 3 + 2])
            n, area = face_normal(A, B, C)
            if n is None or abs(n[1]) >= WALL_NY or area < 0.05: continue
            walls.append((A, n, (min(A[0], B[0], C[0]) - 0.6, max(A[0], B[0], C[0]) + 0.6, min(A[1], B[1], C[1]) - 0.6, max(A[1], B[1], C[1]) + 0.6, min(A[2], B[2], C[2]) - 0.6, max(A[2], B[2], C[2]) + 0.6)))
    out = []
    for fc in mesh.faces:
        a, b, cc, (col, surf) = fc
        n, _ = face_normal(mesh.verts[a], mesh.verts[b], mesh.verts[cc])
        if n is None or surf == 5: out.append(fc); continue
        m = [(mesh.verts[a][k] + mesh.verts[b][k] + mesh.verts[cc][k]) / 3 for k in range(3)]
        # 0.18: a see-through texture (its alpha: a grid, a sign, a toll's barrier, a railing) is solid only where the
        # game's own collision has a wall too (Bay's toll gates stood across the road as walls), and driven on as road
        seethrough = surf == 10
        if seethrough: surf = 0; fc = (a, b, cc, (col, 0))
        if abs(n[1]) >= WALL_NY:
            # 0.6: a bevelled face (26..63 degrees: a rail's side, a wall's chamfer) is a wall when the game's own
            # collision has a steep face right there facing the same way (horizontally): the snow rails, the castle
            # walls (drawn bevelled, solid in the game); else a slope the car drives on
            if abs(n[1]) < 0.9 and not nowalls:
                hn = math.hypot(n[0], n[2])
                for A, wn, bb in walls:
                    if not (bb[0] <= m[0] <= bb[1] and bb[2] <= m[1] <= bb[3] and bb[4] <= m[2] <= bb[5]): continue
                    wh = math.hypot(wn[0], wn[2])
                    if wh < 0.5 or abs(wn[0] * n[0] + wn[2] * n[2]) / (hn * wh) < 0.7: continue
                    if abs(wn[0] * (m[0] - A[0]) + wn[1] * (m[1] - A[1]) + wn[2] * (m[2] - A[2])) < 0.5:
                        fc = (a, b, cc, (col, 9)); WALLSTAT[0] += 1; break
            out.append(fc); continue
        # 0.8: in the game a piece with any collision is solid by its shape: every steep face of it is a wall (the
        # car checks walls 0.25 m and 0.6 m over its wheels, so a kerb or a painted edge still lets it over). Was:
        # only where Nadeo's collision had a wall within 0.6 m facing the same way, which the simplified shape of
        # a fence, a post, a house or a rock often missed (the car drove through them)
        solid = collidable if ALLWALLS and not seethrough and not nowalls else False
        if (not ALLWALLS or seethrough) and not nowalls:
            for A, wn, bb in walls:
                if not (bb[0] <= m[0] <= bb[1] and bb[2] <= m[1] <= bb[3] and bb[4] <= m[2] <= bb[5]): continue
                if abs(wn[0] * n[0] + wn[1] * n[1] + wn[2] * n[2]) < 0.7: continue
                if abs(wn[0] * (m[0] - A[0]) + wn[1] * (m[1] - A[1]) + wn[2] * (m[2] - A[2])) < 0.6: solid = True; break
        out.append((a, b, cc, (col, 9 if solid else 5)))
        WALLSTAT[0 if solid else 1] += 1
    return Mesh(mesh.verts, out)

def drop_water_under(mesh):
    """water faces (surface 4) with a drivable face of the same piece right over them (a low bridge's deck 0.3 m
    over its own water) are dropped: the renderer's depth sort cannot keep two surfaces that close apart"""
    ups = [fc for fc in mesh.faces if fc[3][1] not in (4, 5) and is_drivable(mesh, fc)]
    if not ups: return mesh
    boxes = []
    for fc in ups:
        A, B, C = mesh.verts[fc[0]], mesh.verts[fc[1]], mesh.verts[fc[2]]
        boxes.append((min(A[0], B[0], C[0]), max(A[0], B[0], C[0]), min(A[2], B[2], C[2]), max(A[2], B[2], C[2]), fc))
    out = []; dropped = 0
    for fc in mesh.faces:
        if fc[3][1] != 4: out.append(fc); continue
        m = [(mesh.verts[fc[0]][k] + mesh.verts[fc[1]][k] + mesh.verts[fc[2]][k]) / 3 for k in range(3)]
        under = False
        for x0, x1, z0, z1, uf in boxes:
            if not (x0 <= m[0] <= x1 and z0 <= m[2] <= z1): continue
            h = surface_height(Mesh(mesh.verts, [uf]), m[0], m[2], m[1])
            if h is not None and 0 < h - m[1] < 1.5: under = True; break
        if under: dropped += 1
        else: out.append(fc)
    return Mesh(mesh.verts, out) if dropped else mesh

def block_kind(bn):
    n = bn.lower()
    if 'multilap' in n: return 4
    if 'finish' in n: return 3
    if 'checkpoint' in n or 'checkpoint' in n.replace(' ', ''): return 2
    if 'start' in n: return 1
    return 0

def main():
    a = sys.argv[1:]
    lod = -1; budget = 300; only = None; plate = True; farbudget = 60
    args = []; i = 0
    while i < len(a):
        if a[i] == '--lod': lod = int(a[i + 1]); i += 2
        elif a[i] == '--budget': budget = int(a[i + 1]); i += 2
        elif a[i] == '--far': farbudget = int(a[i + 1]); i += 2
        elif a[i] == '--only': only = a[i + 1]; i += 2
        elif a[i] == '--noplate': plate = False; i += 1
        elif a[i] == '--depthfix': globals()['DEPTHFIX'] = True; i += 1
        elif a[i] == '--wind': globals()['WIND'] = a[i + 1]; i += 2
        else: args.append(a[i]); i += 1
    kitf, colf, trackf, replayf, outf = args[:5]
    kit = json.load(open(kitf)); cols = json.load(open(colf)); mats = kit['materials']
    t = assemble.Track(kit, open(trackf, "rb").read(), rotsign=int(os.environ.get("ROTSIGN", "-1")))
    if only: t.instances = [x for x in t.instances if x[1] == only or x[0] == only]
    print(f"{os.path.basename(trackf)}: {t.env} {t.size}, {len(t.instances)} instances, unknown {t.unknown}")
    def matcol(i):
        if i < 0: return (128, 128, 128)
        tex = mats[i]['texture']; c = cols.get(tex)
        if c: return tuple(c)
        tl = tex.lower()
        if 'sea' in tl or 'water' in tl or 'lake' in tl: return (60, 95, 140)
        if 'ice' in tl: return (190, 215, 235)
        return (120, 120, 120)
    def matsurf(i): return tex_surface(mats[i]['texture']) if 0 <= i < len(mats) else 0
    leaf = None
    for m in mats:
        if 'leaf' in m['texture'].lower() or 'TreeSprite' in m['texture']: leaf = cols.get(m['texture']); break
    leaf = tuple(leaf) if leaf else (70, 110, 40)
    # 1. the solids used, as meshes in their local frame (metres), simplified
    base_meshes = {}      # solid name -> [ (dx_tile, dz_tile, Mesh) ]  (tiles for wide meshes, else [(0, 0, mesh)])
    def solid_mesh(sname):
        if sname in base_meshes: return base_meshes[sname]
        s = t.solids[sname]
        L = s['lods'][lod if 0 <= lod < len(s['lods']) else len(s['lods']) - 1]
        m = from_lod(tree_cards(L, mats), matcol, matsurf)
        if s['sprites'] and 'tree' in sname.lower():
            c = canopies(s['sprites'], leaf)
            b = len(m.verts); m = Mesh(m.verts + c.verts, m.faces + [(x + b, y + b, z + b, col) for x, y, z, col in c.faces])
        bb = s['bbox']
        wide = (bb[3] - bb[0]) > TILE * 1.5 or (bb[5] - bb[2]) > TILE * 1.5
        if wide:
            # simplified whole (the piece's outer edges kept, the drivable part fine), then split into tiles: the
            # tiles share their corners exactly, so no seam opens between them
            ntiles = max(1, int(math.ceil((bb[3] - bb[0]) / TILE))) * max(1, int(math.ceil((bb[5] - bb[2]) / TILE)))
            m = decimate_keep(m, budget * ntiles, (bb[0], bb[3], bb[2], bb[5]))
            parts = [(tx, tz, pm) for tx, tz, pm in split_tiles(m)]
        else:
            # the corners on the piece's edges stay on them when it is simplified: the seams between pieces
            # stay closed (gaps showed, and the car fell through them)
            fine = COARSE is not None and not __import__('re').search(COARSE, sname)
            if fine: parts = [(0, 0, compact(decimate_keep(m, ROAD_BUDGET or budget, (bb[0], bb[3], bb[2], bb[5]), 1.2, 1.4)))]
            else: parts = [(0, 0, compact(decimate_keep(m, budget, (bb[0], bb[3], bb[2], bb[5]))))]
        WALLSTAT[0] = WALLSTAT[1] = 0
        nowalls = NOWALLS is not None and __import__('re').search(NOWALLS, sname)
        parts = [(tx, tz, (drop_water_under(mark_walls(pm, s, nowalls)) if DEPTHFIX else mark_walls(pm, s, nowalls))) for tx, tz, pm in parts]
        if os.environ.get('PACKSTATS') and WALLSTAT[0] + WALLSTAT[1]: print(f"    walls {sname}: {WALLSTAT[0]} solid, {WALLSTAT[1]} dropped; collision {len(s['coll']['tris']) // 4 if s.get('coll') and s['coll'].get('tris') else 0} tris")
        base_meshes[sname] = parts
        return parts
    # 2. the instances: (solid, dir) -> a pre-turned mesh; wide solids: one instance per tile
    meshes = []; meshkey = {}; insts = []; farkey = {}
    def add_mesh(m):
        iv = [(int(round(p[0] * 16)), int(round(p[1] * 16)), int(round(p[2] * 16))) for p in m.verts]
        for p in iv:
            if max(abs(c) for c in p) > 32000: raise SystemExit(f"a corner out of the 16-bit range: {p}")
        bb = [min(p[0] for p in iv), min(p[1] for p in iv), min(p[2] for p in iv), max(p[0] for p in iv), max(p[1] for p in iv), max(p[2] for p in iv)] if iv else [0] * 6
        meshes.append((iv, m.faces, bb)); return len(meshes) - 1
    c = t.blk / 2
    colls = []; collkey = {}; cinsts = []
    for sname, bn, x, y, z, d, ground, kind in t.instances:
        for ox, oz, pm in solid_mesh(sname):
            if not pm.faces: continue
            key = (sname, ox, oz, d)
            if key not in meshkey:
                rm = rotate(Mesh([(p[0] + ox, p[1], p[2] + oz) for p in pm.verts], pm.faces), d, c)
                meshkey[key] = add_mesh(rm)
                # its far form: the same piece beyond the far distance, a fifth of the faces (the drawing cost is
                # per polygon, and a piece 100 m off is a few pixels a face anyway)
                fm = compact(decimate(rm, farbudget)) if len(rm.faces) > farbudget * 2 else None
                farkey[key] = add_mesh(fm) if fm is not None and fm.faces else -1
                # its collision: the same corners, the faces the car stands on
                tris = coll_from(rm)
                iv = meshes[meshkey[key]][0]
                # 0.16: the walls from Nadeo's own collision mesh (its steep faces, both sides), not the drawn mesh's
                # simplified ones, for pieces in one part: the car's body box meets them as the game's car does
                sc = t.solids[sname].get('coll')
                if NADEOWALLS and ox == 0 and oz == 0 and len(solid_mesh(sname)) == 1 and sc and sc.get('tris'):
                    tris = [tr for tr in tris if tr[4] != 9]
                    cv = sc['verts']; extra = []
                    for i in range(0, len(sc['tris']), 4):
                        if sc['surfs'][sc['tris'][i + 3]] == 'NotCollidable': continue
                        P = [tuple(cv[sc['tris'][i + q] * 3 + k] for k in range(3)) for q in range(3)]
                        n, area = face_normal(*P)
                        if n is None or abs(n[1]) >= WALL_NY or area < 0.02: continue
                        extra.append(P)
                    if extra:
                        rmw = rotate(Mesh([p2 for P in extra for p2 in P], []), d, c)
                        iv = list(iv); b0 = len(iv)
                        for p2 in rmw.verts: iv.append((int(round(p2[0] * 16)), int(round(p2[1] * 16)), int(round(p2[2] * 16))))
                        for k in range(len(extra)):
                            A, B, C = rmw.verts[k * 3], rmw.verts[k * 3 + 1], rmw.verts[k * 3 + 2]
                            n, _ = face_normal(A, B, C)
                            if n is None: continue
                            nn = tuple(int(round(q * 8192)) for q in n)
                            tris.append((b0 + k * 3, b0 + k * 3 + 1, b0 + k * 3 + 2, nn, 9))
                            tris.append((b0 + k * 3, b0 + k * 3 + 2, b0 + k * 3 + 1, tuple(-q for q in nn), 9))
                if tris:
                    used = sorted({k for tr in tris for k in tr[:3]}); remap = {k: i for i, k in enumerate(used)}
                    civ = [iv[k] for k in used]
                    cbb = [min(p[0] for p in civ), min(p[1] for p in civ), min(p[2] for p in civ), max(p[0] for p in civ), max(p[1] for p in civ), max(p[2] for p in civ)]
                    colls.append((civ, [(remap[a], remap[b], remap[cc], n, surf) for a, b, cc, n, surf in tris], cbb)); collkey[key] = len(colls) - 1
                else: collkey[key] = -1
            mi = meshkey[key]; iv, faces, bb = meshes[mi]
            # a zone tile partly under a block's ground: split finer and cut where the block's ground is (the two
            # would flicker drawn on top of each other); a mesh of this instance's own
            cut = t.zone_cut.get((int(x // t.blk), int(z // t.blk))) if bn.lower() in t.zone_height else None
            if cut:
                verts = [(p[0] / 16.0 + x, p[1] / 16.0 + y, p[2] / 16.0 + z) for p in iv]; cache = {}
                def mid(a, b):
                    k2 = (min(a, b), max(a, b))
                    if k2 not in cache: cache[k2] = len(verts); verts.append(tuple((verts[a][j] + verts[b][j]) / 2 for j in range(3)))
                    return cache[k2]
                stack = [(a2, b2, c2, col) for a2, b2, c2, col in faces]; fine = []
                while stack:
                    a2, b2, c2, col = stack.pop()
                    if max(math.dist(verts[a2], verts[b2]), math.dist(verts[b2], verts[c2]), math.dist(verts[c2], verts[a2])) <= 8.5: fine.append((a2, b2, c2, col)); continue
                    ab, bc, ca = mid(a2, b2), mid(b2, c2), mid(c2, a2)
                    stack += [(a2, ab, ca, col), (ab, b2, bc, col), (ca, bc, c2, col), (ab, bc, ca, col)]
                keep = [fc for fc in fine if not any(ov((verts[fc[0]][0] + verts[fc[1]][0] + verts[fc[2]][0]) / 3, (verts[fc[0]][2] + verts[fc[1]][2] + verts[fc[2]][2]) / 3) for ov in cut)]
                if keep:
                    cm = compact(Mesh([(p[0] - x, p[1] - y, p[2] - z) for p in verts], keep))
                    mi = add_mesh(cm); iv, faces, bb = meshes[mi]
                else: continue
            insts.append((mi, 0, x * 16, y * 16, z * 16, [x * 16 + bb[0], y * 16 + bb[1], z * 16 + bb[2], x * 16 + bb[3], y * 16 + bb[4], z * 16 + bb[5]], farkey[key] if not cut else -1))
            ci = collkey[key]
            if ci >= 0:
                cbb = colls[ci][2]
                cinsts.append((ci, block_kind(bn), x * 16, y * 16, z * 16, [x * 16 + cbb[0], y * 16 + cbb[1], z * 16 + cbb[2], x * 16 + cbb[3], y * 16 + cbb[4], z * 16 + cbb[5]]))
    print(f"  collision: {len(colls)} meshes, {len(cinsts)} instances, {sum(len(colls[i[0]][1]) for i in cinsts)} placed triangles; kinds {collections.Counter(i[1] for i in cinsts)}")
    # 2b. a piece lying just under another (a lake under a low bridge, its deck 0.3 m over the water): the renderer
    # sorts polygons by their middle's depth, so two near-level surfaces fight and the deck gets holes; the lower
    # piece's faces under the upper piece's footprint (1 m inside it) and within 3 m below its top are dropped
    cutkey = {}; ncut = 0
    for ia in range(len(insts) if DEPTHFIX else 0):
        mi, fl, x, y, z, wb, far = insts[ia]
        if fl: continue
        covers = []
        for ib in range(len(insts)):
            if ib == ia: continue
            mb, flb, xb, yb, zb, wbb, farb = insts[ib]
            if flb: continue
            ox = min(wb[3], wbb[3]) - max(wb[0], wbb[0]); oz = min(wb[5], wbb[5]) - max(wb[2], wbb[2])
            if ox <= 0 or oz <= 0 or ox * oz < (wb[3] - wb[0]) * (wb[5] - wb[2]) // 4: continue
            if wbb[4] > wb[4] + 4 and wbb[4] < wb[4] + 3 * 16: covers.append((wbb[0] + 16, wbb[2] + 16, wbb[3] - 16, wbb[5] - 16, wbb[4]))
        if not covers: continue
        key = (mi, x, y, z, tuple(covers))
        if key not in cutkey:
            iv, faces, bb = meshes[mi]
            keep = []
            for fc in faces:
                cx = x + (iv[fc[0]][0] + iv[fc[1]][0] + iv[fc[2]][0]) // 3; cz = z + (iv[fc[0]][2] + iv[fc[1]][2] + iv[fc[2]][2]) // 3
                cy = y + max(iv[fc[0]][1], iv[fc[1]][1], iv[fc[2]][1])
                if any(c[0] <= cx <= c[2] and c[1] <= cz <= c[3] and cy < c[4] for c in covers): continue
                keep.append(fc)
            if len(keep) < len(faces):
                meshes.append((iv, keep, bb)); cutkey[key] = len(meshes) - 1; ncut += len(faces) - len(keep)
            else: cutkey[key] = mi
        if cutkey[key] != mi: insts[ia] = (cutkey[key], fl, x, y, z, wb, far)
    if ncut: print(f"  {ncut} faces dropped under low pieces over them")
    # 3. the base plate: one tile per 32 m piece, at the map's origin (its mesh is in the map's frame, y from the deco)
    if plate:
        pname = 'Base/%dx%d' % (t.size[0], t.size[2])
        if pname not in t.solids: pname = 'Base/30x30'
        if pname in t.solids:
            s = t.solids[pname]; L = s['lods'][-1]
            m = from_lod(L, matcol)
            # only what can be seen from the map (the fog ends at 180 m): faces within 300 m of it
            x1, z1 = t.size[0] * t.blk + 300, t.size[2] * t.blk + 300
            m = compact(Mesh(m.verts, [fc for fc in m.faces if all(-300 < m.verts[k][0] < x1 and -300 < m.verts[k][2] < z1 for k in fc[:3])]))
            m = decimate(m, 1500)
            n0 = 0
            for ox, oz, pm in split_tiles(m, 64.0):
                if not pm.faces: continue
                mi = add_mesh(pm); iv, faces, bb = meshes[mi]; n0 += len(faces)
                X, Z = int(ox * 16), int(oz * 16)
                insts.append((mi, 1, X, 0, Z, [X + bb[0], bb[1], Z + bb[2], X + bb[3], bb[4], Z + bb[5]], -1))
            print(f"  base plate {pname}: {len(L['faces']) // 4} -> {n0} triangles in {sum(1 for i in insts if i[1] == 1)} tiles")
    # 4. the car
    car = -1
    for cname in ('RallyCar', 'SnowCar', 'American', 'SpeedCar', 'BayCar', 'SportCar', 'IslandCar', 'CoastCar'):
        if cname in t.solids:
            m = compact(decimate(from_lod(t.solids[cname]['lods'][0], matcol), CAR_TRIS))
            car = add_mesh(m); print(f"  car {cname}: {t.solids[cname]['tris'][0]} -> {len(m.faces)} triangles"); break
    # 5. the path from the replay
    rp = json.load(open(replayf))['samples'] if replayf != '-' else []
    path = []; prev = 0
    for i, smp in enumerate(rp):
        p = smp['p']; nxt = rp[i + 1]['p'] if i + 1 < len(rp) else p
        dx, dz = nxt[0] - p[0], nxt[2] - p[2]
        yaw = int(round(math.degrees(math.atan2(dx, dz)) * 4096 / 360)) % 4096 if abs(dx) + abs(dz) >= 0.05 else prev
        prev = yaw
        path.append((int(round(p[0] * 16)), int(round(p[1] * 16)), int(round(p[2] * 16)), yaw, 0))
    # 0.18: the samples before the car first moves take its first heading (the start's yaw: IslandA1 starts towards -z,
    # it was 0, +z, and the car drove off the wrong way)
    first = next((k for k in range(len(rp) - 1) if abs(rp[k + 1]['p'][0] - rp[k]['p'][0]) + abs(rp[k + 1]['p'][2] - rp[k]['p'][2]) >= 0.05), None)
    if first:
        path = [(x, y, z, path[first][3], pt) for x, y, z, yw, pt in path[:first]] + path[first:]
    sky = rgb565(130, 170, 220); ground = rgb565(70, 90, 30) if t.env == 'Rally' else rgb565(205, 145, 90) if t.env == 'Speed' else rgb565(225, 228, 235)
    if t.env == 'Speed': sky = rgb565(150, 180, 215)        # 0.15: Desert: sand under a paler sky
    fog = sky
    out = bytearray()
    start = path[0] if path else (0, 0, 0, 0, 0)
    # the car's class (its physics tables on the phone) in the top byte of the car mesh field: 0 Rally, 1 Snow, 2 Desert, 3 Bay, 4 Island
    carclass = {'Rally': 0, 'Alpine': 1, 'Speed': 2, 'Bay': 3, 'Island': 4}.get(t.env, 0)
    out += struct.pack('<4sIIIi', b'TMB5', len(meshes), len(insts), len(path), car if car < 0 else car | (carclass << 24))
    out += struct.pack('<iiiii', sky, ground, fog, 100 * 16, 180 * 16)
    out += struct.pack('<IIiiii', len(colls), len(cinsts), start[0], start[1], start[2], start[3])
    for iv, faces, bb in meshes:
        out += struct.pack('<HH6h', len(iv), len(faces), *bb)
        for p in iv: out += struct.pack('<3h', *p)
        for fc in faces: out += struct.pack('<3H', fc[0], fc[1], fc[2])
        for fc in faces: out += struct.pack('<H', fc[3][0])
        if len(out) & 2: out += b'\0\0'
    for mi, fl, x, y, z, wb, far in insts: out += struct.pack('<HHiii6i', mi, fl, x, y, z, *wb)
    for p in path: out += struct.pack('<iiihh', *p)
    for iv, tris, bb in colls:
        out += struct.pack('<HH6h', len(iv), len(tris), *bb)
        for p in iv: out += struct.pack('<3h', *p)
        for tr in tris: out += struct.pack('<3H', tr[0], tr[1], tr[2])
        for tr in tris: out += struct.pack('<3h', *tr[3])
        for tr in tris: out += struct.pack('<B', tr[4])
        while len(out) & 3: out += b'\0'
    for ci, kind, x, y, z, wb in cinsts: out += struct.pack('<HHiii6i', ci, kind, x, y, z, *wb)
    # TMB5: the far form of each instance (0xffff none) and the distance it takes over at (1/16 m)
    out += struct.pack('<i', FAR_DIST * 16)
    for inst in insts: out += struct.pack('<H', inst[6] if inst[6] >= 0 else 0xffff)
    while len(out) & 3: out += b'\0'
    # 0.8: a trailer the loader finds at the very end: "MEDL" and the medal times (ms): author, gold, silver, bronze
    medals = medal_times(trackf)
    out += struct.pack('<4s4i', b'MEDL', *medals)
    print(f"  medals (ms): author {medals[0]} gold {medals[1]} silver {medals[2]} bronze {medals[3]}")
    open(outf, 'wb').write(out)
    tot = sum(len(meshes[i[0]][1]) for i in insts)
    print(f"wrote {outf}: {len(out)} bytes, {len(meshes)} meshes, {len(insts)} instances, {tot} placed triangles, {len(path)} path samples")
    big = collections.Counter()
    for sname, parts in base_meshes.items(): big[sname] = sum(len(pm.faces) for _, _, pm in parts)
    print("  biggest solids after the pass:", big.most_common(8))

if __name__ == '__main__': main()
