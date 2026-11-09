#!/usr/bin/env python3
"""Read a TMF Challenge.Gbx: header XML + the block list (chunk 0x0304301F).
Own code; the LZO1X decompressor follows the published algorithm."""
import struct, sys, re, collections, os, tarfile, io


def lzo1x_decompress(src, dst_len):
    dst = bytearray()
    ip = 0
    n = len(src)
    def match(dist, length):
        s = len(dst) - dist
        if dist >= length:
            dst.extend(dst[s:s + length])
        else:
            for i in range(length):
                dst.append(dst[s + i])
    state = 0
    t = src[0]
    if t > 17:
        ip = 1
        t -= 17
        dst += src[ip:ip + t]; ip += t
        state = t if t < 4 else 4
    while ip < n:
        t = src[ip]; ip += 1
        if t < 16:
            if state == 0:
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 15 + src[ip]; ip += 1
                t += 3
                dst += src[ip:ip + t]; ip += t
                state = 4
                continue
            elif state < 4:
                dist = (src[ip] << 2) + (t >> 2) + 1; ip += 1
                match(dist, 2)
            else:
                dist = (src[ip] << 2) + (t >> 2) + 2049; ip += 1
                match(dist, 3)
        elif t >= 64:
            dist = (src[ip] << 3) + ((t >> 2) & 7) + 1; ip += 1
            match(dist, (t >> 5) + 1)
        elif t >= 32:
            length = t & 31
            if length == 0:
                while src[ip] == 0:
                    length += 255; ip += 1
                length += 31 + src[ip]; ip += 1
            ds = src[ip] | (src[ip + 1] << 8); ip += 2
            match((ds >> 2) + 1, length + 2)
            t = ds
        else:
            dist = (t & 8) << 11
            length = t & 7
            if length == 0:
                while src[ip] == 0:
                    length += 255; ip += 1
                length += 7 + src[ip]; ip += 1
            ds = src[ip] | (src[ip + 1] << 8); ip += 2
            dist += ds >> 2
            if dist == 0:
                break
            match(dist + 16384, length + 2)
            t = ds
        t &= 3
        if t:
            dst += src[ip:ip + t]; ip += t
        state = t
    return bytes(dst)


class Reader:
    def __init__(self, b, pos=0):
        self.b = b; self.p = pos
        self.lb = None  # lookback list
    def u8(self): v = self.b[self.p]; self.p += 1; return v
    def u16(self): v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v
    def i32(self): v = struct.unpack_from('<i', self.b, self.p)[0]; self.p += 4; return v
    def f32(self): v = struct.unpack_from('<f', self.b, self.p)[0]; self.p += 4; return v
    def string(self):
        n = self.u32()
        s = self.b[self.p:self.p + n].decode('utf-8', 'replace'); self.p += n; return s
    def lookback(self, first_has_version=False):
        if self.lb is None:
            if first_has_version:
                self.u32()
            self.lb = []
        idx = self.u32()
        if idx == 0xFFFFFFFF: return None
        if (idx & 0xC0000000) == 0:
            return f"#{idx}"  # collection number
        n = idx & 0x3FFFFFFF
        if n == 0:
            s = self.string(); self.lb.append(s); return s
        if n - 1 < len(self.lb): return self.lb[n - 1]
        return f"?{n}"
    def meta(self, first=False):
        return (self.lookback(first), self.lookback(), self.lookback())


def read_track(data):
    assert data[:3] == b'GBX'
    ver = struct.unpack_from('<H', data, 3)[0]
    fmt, rc, bc = data[5], data[6], data[7]
    unk = data[8] if ver >= 4 else None
    p = 9 if ver >= 4 else 8
    classid = struct.unpack_from('<I', data, p)[0]; p += 4
    hdr = {}
    if ver >= 6:
        usz = struct.unpack_from('<I', data, p)[0]; p += 4
        hstart = p
        r = Reader(data, p)
        n = r.u32()
        chunks = []
        for i in range(n):
            cid = r.u32(); sz = r.u32() & 0x7FFFFFFF
            chunks.append((cid, sz))
        for cid, sz in chunks:
            hdr[cid] = data[r.p:r.p + sz]; r.p += sz
        p = r.p
    numnodes = struct.unpack_from('<I', data, p)[0]; p += 4
    # reference table
    numext = struct.unpack_from('<I', data, p)[0]; p += 4
    assert numext == 0, "ref table not handled"
    # body
    if bc == ord('C'):
        usize, csize = struct.unpack_from('<II', data, p); p += 8
        body = lzo1x_decompress(data[p:p + csize], usize)
        assert len(body) == usize, (len(body), usize)
    else:
        body = data[p:]
    xml = (hdr.get(0x03043005) or hdr.get(0x24003005) or b'').decode('utf-8', 'replace')
    return xml, body


def read_packdesc(r):
    v = r.u8()
    if v >= 3: r.p += 32
    fp = r.string()
    if (len(fp) > 0 and v >= 1) or v >= 3: r.string()
    return fp


def read_skin_node(r):
    cid = r.u32()
    assert (cid & 0xFFF) == 0, hex(cid)
    while True:
        ch = r.u32()
        if ch == 0xFACADE01: return
        low = ch & 0xFFF
        nxt = struct.unpack_from('<I', r.b, r.p)[0]
        if nxt == 0x534B4950:  # PIKS: skippable
            r.u32(); sz = r.u32(); r.p += sz; continue
        if low == 0: r.string(); r.string()
        elif low == 1: r.string(); read_packdesc(r)
        elif low == 2: r.string(); read_packdesc(r); read_packdesc(r)
        else: raise ValueError(f"skin chunk {ch:08x}")


def parse_blocks(body):
    """Find chunk 0x0304301F and parse its block list."""
    key = b'\x1f\x30\x00\x24' if body.find(b'\x1f\x30\x00\x24') >= 0 else b'\x1f\x30\x04\x03'
    pos = 0
    while True:
        i = body.find(key, pos)
        if i < 0: raise ValueError("no 0x0304301F")
        r = Reader(body, i + 4)
        # lookback list: assume continuing; detect the version word (3) if present
        v = struct.unpack_from('<I', body, r.p)[0]
        if v == 3:
            r.u32()
        r.lb = []
        try:
            uid, col, auth = r.meta()
            name = r.string()
            deco = r.meta()
            size = (r.i32(), r.i32(), r.i32())
            need_unlock = r.u32()
            version = r.u32()
            nblocks = r.u32()
            if not (0 < nblocks < 20000 and 0 < size[0] < 256): raise ValueError
            blocks = []
            seen_nodes = set()
            for k in range(nblocks):
                bname = r.lookback()
                d = r.u8(); x, y, z = r.u8(), r.u8(), r.u8()
                flags = r.u32() if version >= 1 else r.u16()
                if flags == 0xFFFFFFFF:
                    continue
                if flags & 0x8000:
                    r.lookback()  # author
                    nr = r.u32()   # skin node ref index
                    if nr != 0xFFFFFFFF and nr != 0 and nr not in seen_nodes:
                        seen_nodes.add(nr)
                        read_skin_node(r)
                if version >= 2 and flags & 0x100000:
                    nr = r.u32()
                    if nr != 0xFFFFFFFF and nr != 0 and nr not in seen_nodes:
                        raise ValueError("inline params node")
                blocks.append((bname, d, (x, y, z), flags))
            return dict(uid=uid, name=name, deco=deco[0], size=size, version=version, blocks=blocks)
        except (ValueError, struct.error, IndexError) as e:
            if 'inline' in str(e): raise
            pos = i + 4


if __name__ == '__main__':
    src = sys.argv[1]
    files = []
    if src.endswith('.tgz'):
        tf = tarfile.open(src)
        for m in tf.getmembers():
            if m.name.lower().endswith('challenge.gbx'):
                files.append((m.name, tf.extractfile(m).read()))
    else:
        files.append((src, open(src, 'rb').read()))
    for name, data in files:
        xml, body = read_track(data)
        env = re.search(r'envir="(\w+)"', xml).group(1)
        mood = (re.search(r"mood=\"(\w+)\"", xml) or [None,"?"])[1] if re.search(r"mood=\"(\w+)\"", xml) else "?"
        try:
            t = parse_blocks(body)
        except Exception as e:
            print(f"{name}: {env} {mood} FAILED {e}"); continue
        cnt = collections.Counter(b[0] for b in t['blocks'])
        ground = sum(1 for b in t['blocks'] if b[3] & 0x1000)
        print(f"{name}: {env} {mood} size {t['size']} blocks {len(t['blocks'])} distinct {len(cnt)} ground {ground}")
        if '-v' in sys.argv:
            for k, v in cnt.most_common(): print(f"   {v:4d} {k}")
