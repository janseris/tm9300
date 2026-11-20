#!/usr/bin/env python3
"""tmbcmp.py a.tmb b.tmb : two plain TMB5 tracks compared (0.19): the counts, the starts, the placed drawn and
collision triangles each has that the other has not (world 1/16 m), the far forms' totals. load(path) decodes one."""
import struct, sys
def load(fn):
    d = open(fn,'rb').read()
    if d[:4] != b'TMB5': raise SystemExit('not raw TMB5')
    nm, ni, npth, car = struct.unpack_from('<IIIi', d, 4); p = 20
    sky, gnd, fog, f0, f1 = struct.unpack_from('<5i', d, p); p += 20
    nc, nci, sx, sy, sz, syaw = struct.unpack_from('<IIiiii', d, p); p += 24
    meshes = []
    for _ in range(nm):
        nv, nf = struct.unpack_from('<HH', d, p); bb = struct.unpack_from('<6h', d, p + 4); p += 16
        v = struct.unpack_from(f'<{nv*3}h', d, p); p += nv * 6
        f = struct.unpack_from(f'<{nf*3}H', d, p); p += nf * 6
        col = struct.unpack_from(f'<{nf}H', d, p); p += nf * 2
        if p & 2: p += 2
        meshes.append((v, f, col, bb))
    insts = []
    for _ in range(ni): insts.append(struct.unpack_from('<HHiii6i', d, p)); p += 40
    p += npth * 16
    colls = []
    for _ in range(nc):
        nv, nf = struct.unpack_from('<HH', d, p); bb = struct.unpack_from('<6h', d, p + 4); p += 16
        v = struct.unpack_from(f'<{nv*3}h', d, p); p += nv * 6
        f = struct.unpack_from(f'<{nf*3}H', d, p); p += nf * 6
        n = struct.unpack_from(f'<{nf*3}h', d, p); p += nf * 6
        s = struct.unpack_from(f'<{nf}B', d, p); p += nf
        while p & 3: p += 1
        colls.append((v, f, n, s, bb))
    cinsts = []
    for _ in range(nci): cinsts.append(struct.unpack_from('<HHiii6i', d, p)); p += 40
    fard = struct.unpack_from('<i', d, p)[0]; p += 4
    far = struct.unpack_from(f'<{ni}H', d, p)
    return dict(meshes=meshes, insts=insts, colls=colls, cinsts=cinsts, far=far, start=(sx, sy, sz, syaw), car=car, hdr=(sky,gnd,fog,f0,f1))
def placed(t):
    out = []
    for k, (mi, fl, x, y, z, *wb) in enumerate(t['insts']):
        v, f, col, bb = t['meshes'][mi]
        for i in range(0, len(f), 3):
            tri = tuple(sorted(tuple((v[f[i+j]*3]+x, v[f[i+j]*3+1]+y, v[f[i+j]*3+2]+z)) for j in range(3)))
            out.append((tri, col[i//3]))
    return out
def placed_far(t):
    out = []
    for k, (mi, fl, x, y, z, *wb) in enumerate(t['insts']):
        fm = t['far'][k]
        if fm == 0xffff: continue
        v, f, col, bb = t['meshes'][fm]
        out.append(len(f)//3)
    return out
def placed_coll(t):
    out = []
    for ci, kind, x, y, z, *wb in t['cinsts']:
        v, f, n, s, bb = t['colls'][ci]
        for i in range(0, len(f), 3):
            tri = tuple(sorted(tuple((v[f[i+j]*3]+x, v[f[i+j]*3+1]+y, v[f[i+j]*3+2]+z)) for j in range(3)))
            out.append((tri, n[i:i+3], s[i//3], kind))
    return out
if __name__ == '__main__':
    import collections
    a, b = load(sys.argv[1]), load(sys.argv[2])
    print('insts', len(a['insts']), len(b['insts']), 'meshes', len(a['meshes']), len(b['meshes']), 'colls', len(a['colls']), len(b['colls']), 'cinsts', len(a['cinsts']), len(b['cinsts']))
    print('start', a['start'], b['start'], 'car', a['car'], b['car'], 'hdr', a['hdr']==b['hdr'])
    pa, pb = collections.Counter(placed(a)), collections.Counter(placed(b))
    print('draw tris', sum(pa.values()), sum(pb.values()), 'only a', sum((pa-pb).values()), 'only b', sum((pb-pa).values()))
    ca, cb = collections.Counter(placed_coll(a)), collections.Counter(placed_coll(b))
    print('coll tris', sum(ca.values()), sum(cb.values()), 'only a', sum((ca-cb).values()), 'only b', sum((cb-ca).values()))
    print('far tris', sum(placed_far(a)), sum(placed_far(b)))
