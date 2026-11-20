#!/usr/bin/env python3
"""tmbz.py in.tmb out.tmb : the track packed for the phone (0.18): "TMBF", its size (u32), the LZMA properties byte
((pb * 5 + lp) * 9 + lc), the raw LZMA stream (preset 9 extreme, lc 0 lp 1 pb 1, a 16 MB dictionary) of the track
filtered first (TmTmbUnfilter in tmlzma.cpp undoes it): in every mesh and collision mesh the corners' x, y, z split
into their low bytes then their high bytes, the normals the same, the triangles' corner indices each taken from the
highest index so far + 1 (a new corner is 0). About a sixth of the plain file; plain LZMA ("TMBL") a fifth, zlib
("TMBZ") two fifths. The app still loads all of them and a plain .tmb"""
import sys, struct, lzma, array

def sections(d):
    """the (offset, kind, count) of the meshes' and the collision meshes' arrays: 'v' corners, 'i' indices, 'n' normals"""
    p = 4; nMesh, nInst, nPath = struct.unpack_from('<3I', d, p); p += 12 + 24
    nColl, nCInst = struct.unpack_from('<2I', d, p); p += 8 + 16
    out = []
    for _ in range(nMesh):
        nv, nf = struct.unpack_from('<2H', d, p); p += 16
        out.append((p, 'v', nv)); p += nv * 6
        out.append((p, 'i', nf)); p += nf * 6
        p += nf * 2
        if p % 4: p += 4 - p % 4
    p += nInst * 40 + nPath * 16
    for _ in range(nColl):
        nv, nf = struct.unpack_from('<2H', d, p); p += 16
        out.append((p, 'v', nv)); p += nv * 6
        out.append((p, 'i', nf)); p += nf * 6
        out.append((p, 'n', nf)); p += nf * 6
        p += nf
        if p % 4: p += 4 - p % 4
    return out

def filt(d):
    b = bytearray(d)
    for off, k, n in sections(d):
        raw = bytes(b[off:off + n * 6])
        if k == 'i':
            a = array.array('H'); a.frombytes(raw); m = 0
            for j, v in enumerate(a): a[j] = (m - v) & 0xffff; m = max(m, v + 1)
            raw = a.tobytes()
        else:
            raw = b''.join(raw[c * 2::6] for c in range(3)) + b''.join(raw[c * 2 + 1::6] for c in range(3))
        b[off:off + n * 6] = raw
    return bytes(b)

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    if d[:4] in (b'TMBF', b'TMBL', b'TMBZ'): open(sys.argv[2], 'wb').write(d); raise SystemExit
    lc, lp, pb = 0, 1, 1
    z = lzma.compress(filt(d), format=lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME, 'lc': lc, 'lp': lp, 'pb': pb, 'dict_size': 1 << 24}])
    open(sys.argv[2], 'wb').write(b'TMBF' + struct.pack('<I', len(d)) + bytes([(pb * 5 + lp) * 9 + lc]) + z)
