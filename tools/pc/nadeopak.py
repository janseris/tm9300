#!/usr/bin/env python3
"""Minimal reader for TrackMania Forever NadeoPak v3 files (own implementation
after the public format description): lists the directory, extracts non-Gbx
files (textures). Gbx files inside need a class-aware decrypt (not here)."""
import hashlib, struct, sys, zlib, os

# ---------- Blowfish (standard constants) with Nadeo's little-endian word reads
import math
def _pi_hex(nwords):
    # standard Blowfish P/S arrays are the hex digits of pi; compute them with mpmath-free spigot
    # Use the known constants from a tiny embedded generator: simpler to use the 'cryptography' package? not needed.
    raise NotImplementedError

class Blowfish:
    P_INIT = None
    S_INIT = None
    def __init__(self, key):
        if Blowfish.P_INIT is None:
            Blowfish._load_consts()
        self.P = list(Blowfish.P_INIT)
        self.S = [list(s) for s in Blowfish.S_INIT]
        j = 0
        for i in range(18):
            data = 0
            for k in range(4):
                data |= key[j] << (k * 8)      # little-endian key words (Nadeo)
                j += 1
                if j >= len(key): j = 0
            self.P[i] ^= data & 0xffffffff
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self._enc(l, r)
            self.P[i], self.P[i + 1] = l, r
        for i in range(4):
            for j in range(0, 256, 2):
                l, r = self._enc(l, r)
                self.S[i][j], self.S[i][j + 1] = l, r

    @staticmethod
    def _load_consts():
        # pi hex digits via the 'pi.bin' generated once with a spigot (Bellard) below
        digits = _pi_fraction_hex(4168 * 2)  # 18 + 1024 words * 8 hex digits
        words = [int(digits[i:i + 8], 16) for i in range(0, 8 * 1042, 8)]
        Blowfish.P_INIT = words[:18]
        Blowfish.S_INIT = [words[18 + 256 * i: 18 + 256 * (i + 1)] for i in range(4)]

    def _F(self, x):
        S = self.S
        return ((((S[0][x >> 24] + S[1][(x >> 16) & 255]) & 0xffffffff) ^ S[2][(x >> 8) & 255]) + S[3][x & 255]) & 0xffffffff

    def _enc(self, l, r):
        P = self.P
        for i in range(16):
            l ^= P[i]
            r ^= self._F(l)
            l, r = r, l
        l, r = r, l
        r ^= P[16]; l ^= P[17]
        return l, r

    def _dec(self, l, r):
        P = self.P
        for i in range(17, 1, -1):
            l ^= P[i]
            r ^= self._F(l)
            l, r = r, l
        l, r = r, l
        l ^= P[0]; r ^= P[1]
        return l, r

    def decrypt_block(self, b8):
        l, r = struct.unpack('<II', b8)
        l, r = self._dec(l, r)
        return struct.pack('<II', l, r)


def _pi_fraction_hex(ndigits):
    """Hex digits of pi's fractional part (big-int formula, exact)."""
    # Machin-like with integer arithmetic: pi = 16 atan(1/5) - 4 atan(1/239)
    prec = ndigits * 4 + 64
    def atan_inv(x):
        # atan(1/x) * 2^prec
        total = 0
        term = (1 << prec) // x
        x2 = x * x
        k = 0
        sign = 1
        while term:
            total += sign * (term // (2 * k + 1))
            term //= x2
            k += 1
            sign = -sign
        return total
    pi = 16 * atan_inv(5) - 4 * atan_inv(239)
    frac = pi - (3 << prec)
    frac >>= 64
    h = format(frac, 'x').rjust(ndigits, '0')
    return h[:ndigits]


class NadeoStream:
    """Blowfish CBC-ish stream as the game does it (pak version < 12)."""
    def __init__(self, data, key, iv):
        self.bf = Blowfish(key)
        self.data = data
        self.pos = 0
        self.iv = iv
        self.ivxor = 0
        self.buf = b''
        self.bufidx = 0
        self.total = 0

    def initialize(self, bts, off, cnt):
        for i in range(cnt):
            lo = self.ivxor & 0xffffffff
            hi = self.ivxor >> 32
            lo = ((bts[off + i] | 0xAA) ^ (((lo << 13) | (hi >> 19)) & 0xffffffff)) & 0xffffffff
            hi = ((self.ivxor << 13) >> 32) & 0xffffffff
            self.ivxor = (hi << 32) | lo

    def read(self, n):
        out = bytearray()
        if self.total == 0:
            self.iv ^= self.ivxor; self.ivxor = 0
        for _ in range(n):
            if self.bufidx % 8 == 0:
                if self.bufidx == 0x100:
                    self.iv ^= self.ivxor; self.ivxor = 0; self.bufidx = 0
                blk = self.data[self.pos:self.pos + 8]
                self.pos += 8
                if len(blk) < 8:
                    return bytes(out)
                nextiv = struct.unpack('<Q', blk)[0]
                dec = struct.unpack('<Q', self.bf.decrypt_block(blk))[0] ^ self.iv
                self.buf = struct.pack('<Q', dec)
                self.iv = nextiv
            out.append(self.buf[self.bufidx & 7])
            self.bufidx += 1
            self.total += 1
        return bytes(out)

    def u32(self): return struct.unpack('<I', self.read(4))[0]
    def i32(self): return struct.unpack('<i', self.read(4))[0]
    def u64(self): return struct.unpack('<Q', self.read(8))[0]
    def string(self):
        n = self.i32()
        return self.read(n).decode('utf-8', 'replace') if n else ''


def read_packlist(path):
    d = open(path, 'rb').read()
    ver, num = d[0], d[1]
    crc, salt = struct.unpack_from('<II', d, 2)
    pos = 10
    namekey = hashlib.md5(("6611992868945B0B59536FC3226F3FD0" + str(salt)).encode()).digest()
    packs = {}
    for _ in range(num):
        flags, nlen = d[pos], d[pos + 1]; pos += 2
        name = bytes(b ^ namekey[i % 16] for i, b in enumerate(d[pos:pos + nlen])).decode(); pos += nlen
        enc = d[pos:pos + 32]; pos += 32
        kk = hashlib.md5((name + str(salt) + "B97C1205648A66E04F86A1B5D5AF9862").encode()).digest()
        keystr = bytes(b ^ kk[i % 16] for i, b in enumerate(enc)).decode()
        packs[name.lower()] = bytes.fromhex(keystr)
    return packs


def pak_key(basekey):
    return hashlib.md5((basekey.hex().upper() + "NadeoPak").encode('ascii')).digest()


class Pak:
    def __init__(self, path, key):
        self.f = open(path, 'rb')
        hdr = self.f.read(8)
        assert hdr == b'NadeoPak', hdr
        self.version = struct.unpack('<i', self.f.read(4))[0]
        assert self.version < 6
        self.key = key
        iv = struct.unpack('<Q', self.f.read(8))[0]
        # the header is at most ~1 MB; read generously
        encdata = self.f.read(2 * 1024 * 1024)
        s = NadeoStream(encdata, key, iv)
        self.header_md5 = s.read(16)
        self.gbx_headers_start = s.u32()
        self.header_max_size = s.i32()
        self.gbx_headers_size = s.i32(); self.gbx_headers_compr = s.i32()
        self.pack_mask = s.read(16)
        self.flags = s.u32()
        nf = s.i32()
        folders = []
        for i in range(nf):
            parent = s.i32(); name = s.string()
            if not name.endswith('\\') and not name.endswith('/'): name += '\\'
            folders.append((name, None if parent == -1 else parent))
        if len(folders) > 2 and len(folders[2][0]) > 4:
            nb = folders[2][0].encode('utf-16-le')
            s.initialize(nb, 4, 4)
        def fpath(idx):
            parts = []
            while idx is not None and idx != -1:
                parts.append(folders[idx][0].rstrip('\\')); idx = folders[idx][1]
            return '\\'.join(reversed(parts))
        n = s.i32()
        self.files = []
        for i in range(n):
            fi = s.i32(); name = s.string(); u01 = s.u32()
            usize = s.i32(); csize = s.i32(); off = s.u32(); cls = s.u32()
            fl = s.u64()
            self.files.append(dict(path=fpath(fi) + '\\' + name, name=name, u01=u01, usize=usize,
                                   csize=csize, off=off, cls=cls, flags=fl))

    def extract(self, fe):
        """Only for files that need no class-aware init (non-Gbx)."""
        self.f.seek(self.header_max_size + fe['off'])
        enc = (fe['flags'] & 0x4000000000000) == 0 and (fe['flags'] & 0x2000000000000) == 0
        comp = (fe['flags'] & 0x3C) != 0
        if enc:
            iv = struct.unpack('<Q', self.f.read(8))[0]
            raw = self.f.read(fe['csize'] if comp else fe['usize'])
            s = NadeoStream(raw, self.key, iv)
            data = s.read(len(raw))
        else:
            data = self.f.read(fe['csize'] if comp else fe['usize'])
        if comp:
            data = zlib.decompress(data)
        return data


if __name__ == '__main__':
    packs = read_packlist(sys.argv[1])
    for p in sys.argv[2:]:
        name = os.path.splitext(os.path.basename(p))[0]
        pk = Pak(p, pak_key(packs[name.lower()]))
        print(f"== {name}: version {pk.version}, {len(pk.files)} files, data at {pk.header_max_size}")
        for fe in pk.files:
            print(f"{fe['cls']:08x} {fe['usize']:9d} {fe['flags']:016x} {fe['path']}")
