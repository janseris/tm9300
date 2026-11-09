import struct, sys, glob
def medals(path):
    d = open(path, 'rb').read()
    assert d[:3] == b'GBX'
    ver = struct.unpack_from('<H', d, 3)[0]; p = 5
    p += 4 if ver >= 6 else 3
    cls = struct.unpack_from('<I', d, p)[0]; p += 4
    if ver >= 6:
        usize = struct.unpack_from('<I', d, p)[0]; p += 4
        n = struct.unpack_from('<I', d, p)[0]; p += 4
        ch = []
        for i in range(n):
            cid, sz = struct.unpack_from('<II', d, p); p += 8; ch.append((cid, sz & 0x7fffffff))
        for cid, sz in ch:
            if cid in (0x03043002, 0x24003002):
                q = p; v = d[q]; q += 1
                if v < 3: raise SystemExit('old header')
                q += 4   # u1
                br, si, go, au = struct.unpack_from('<IIII', d, q)
                return v, br, si, go, au
            p += sz
    return None
if __name__ == '__main__':
    for f in sys.argv[1:]: print(f.split('/')[-1], medals(f))
