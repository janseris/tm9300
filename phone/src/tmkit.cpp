// tmkit.cpp - a track built from a .Challenge.Gbx with the environment's block kit (0.19). The steps are those of
// tools/pc/assemble.py (the blocks placed: variants, zone heights, footprints, the default terrain, the terrain
// tiles a ground piece covers dropped or cut) and pack.py (each piece's parts turned by quarter turns, their far
// forms and collision, the base plate, the car, the start from the start block), in the same order, so a track
// built here is pack.py's (the cut terrain tiles aside: here by a 16 x 16 mask a cell, there by the triangles).
#include "tmkit.h"
#include "tmgbx.h"
#include "tmlzma.h"
#ifdef TMKIT_DEBUG
#include <stdio.h>
#endif

struct TmKMesh { int nv, nf; short bb[6]; short* v; unsigned short* f; unsigned short* col; };
struct TmKColl { int nv, nf; short bb[6]; short* v; unsigned short* f; short* n; unsigned char* s; };
struct TmKInst { int solid, block; int x, y, z; int d; int ground; };

static void FreeMesh(TmKMesh* m) { if (m) { delete[] m->v; delete[] m->f; delete[] m->col; delete m; } }
static void FreeColl(TmKColl* c) { if (c) { delete[] c->v; delete[] c->f; delete[] c->n; delete[] c->s; delete c; } }

static void Box(const short* v, int nv, short* bb)
    {
    if (nv <= 0) { for (int k = 0; k < 6; k++) bb[k] = 0; return; }
    for (int k = 0; k < 3; k++) bb[k] = bb[k + 3] = v[k];
    for (int i = 1; i < nv; i++)
        for (int k = 0; k < 3; k++)
            {
            short c = v[i * 3 + k];
            if (c < bb[k]) bb[k] = c;
            if (c > bb[k + 3]) bb[k + 3] = c;
            }
    }

static int Lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// x / 16 rounded half to even (Python's round)
static int RoundDiv(long long aV, int aD)
    {
    long long q = aV / aD, r = aV % aD;
    if (r < 0) { r += aD; q--; }
    if (r * 2 > aD || (r * 2 == aD && (q & 1))) q++;
    return (int)q;
    }

static int PopCount(int v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

// ---- a growing pointer list ----
struct TPtrList
    {
    void** p; int n, cap;
    TPtrList() : p(0), n(0), cap(0) {}
    ~TPtrList() { delete[] p; }
    int Add(void* a)
        {
        if (n == cap)
            {
            int c = cap ? cap * 2 : 256;
            void** q = new void*[c];
            if (!q) return -1;
            for (int i = 0; i < n; i++) q[i] = p[i];
            delete[] p; p = q; cap = c;
            }
        p[n] = a; return n++;
        }
    };

// ---- a buffered writer over MTmOut ----
struct TWriter
    {
    MTmOut* o; unsigned char buf[4096]; int n; int total; int err;
    TWriter(MTmOut* a) : o(a), n(0), total(0), err(0) {}
    void Flush() { if (n && !err) err = o->Write(buf, n); n = 0; }
    void B(const void* a, int len)
        {
        const unsigned char* s = (const unsigned char*)a;
        total += len;
        while (len > 0)
            {
            int k = 4096 - n; if (k > len) k = len;
            for (int i = 0; i < k; i++) buf[n + i] = s[i];
            n += k; s += k; len -= k;
            if (n == 4096) Flush();
            }
        }
    void U8(int v) { unsigned char b = (unsigned char)v; B(&b, 1); }
    void U16(int v) { unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) }; B(b, 2); }
    void U32(int v) { unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; B(b, 4); }
    void Pad4() { while (total & 3) U8(0); }
    };

// ---- the kit ----

TmKit::TmKit() : iFile(0), iFileSize(0), iProps(0), iNParts(0), iDir(0), iDirSize(0),
    iMeshV0(0), iMeshF0(0), iCollV0(0), iCollF0(0), iChunkNV(0), iChunkNF(0), iChunkCV(0), iChunkCF(0)
    {
    iNInst = iNMesh = iNColl = iNCInst = iTris = iUnknown = iDropped = iChunks = iUnpacked = 0;
    iStart[0] = iStart[1] = iStart[2] = iStart[3] = 0;
    }

TmKit::~TmKit() { Close(); }

void TmKit::Free()
    {
    delete[] iDir; iDir = 0;
    delete[] iMeshV0; delete[] iMeshF0; delete[] iCollV0; delete[] iCollF0;
    delete[] iChunkNV; delete[] iChunkNF; delete[] iChunkCV; delete[] iChunkCF;
    iMeshV0 = iMeshF0 = iCollV0 = iCollF0 = iChunkNV = iChunkNF = iChunkCV = iChunkCF = 0;
    }

void TmKit::Close() { Free(); iFile = 0; iFileSize = 0; }

int TmKit::Unpack(int aPart, unsigned char*& aOut)
    {
    aOut = 0;
    if (aPart < 0 || aPart >= iNParts) return -1;
    const unsigned char* e = iFile + 12 + aPart * 12;
    int raw = Rd32(e), packed = Rd32(e + 4), off = Rd32(e + 8);
    if (raw <= 0 || raw > 4 * 1024 * 1024 || packed <= 0 || off < 0 || off + packed > iFileSize) return -2;
    unsigned short* probs = new unsigned short[TmLzmaProbs(iProps)];
    if (!probs) return -3;
    aOut = new unsigned char[raw + 4];
    if (!aOut) { delete[] probs; return -3; }
    int got = TmLzmaDecode(iFile + off, packed, aOut, raw, iProps, probs);
    delete[] probs;
    if (got != raw) { delete[] aOut; aOut = 0; return -4; }
    return raw;
    }

int TmKit::Open(const unsigned char* aFile, int aSize)
    {
    Close();
    if (aSize < 24 || aFile[0] != 'T' || aFile[1] != 'M' || aFile[2] != 'K' || aFile[3] != '2') return -1;
    iFile = aFile; iFileSize = aSize;
    iProps = Rd32(aFile + 4); iNParts = Rd32(aFile + 8);
    if (iNParts < 2 || iNParts > 256 || 12 + iNParts * 12 > aSize) { Close(); return -2; }
    int n = Unpack(0, iDir);
    if (n < 112) { Close(); return -3; }
    iDirSize = n;
    const unsigned char* d = iDir;
    if (d[0] != 'T' || d[1] != 'M' || d[2] != 'K' || d[3] != 'R') { Close(); return -4; }
    int* h = &iEnv;
    for (int k = 0; k < 12; k++) h[k] = Rd32(d + 8 + k * 4);
    int* c = &iNB;
    for (int k = 0; k < 13; k++) c[k] = Rd32(d + 56 + k * 4);
    int off = 112;
#define SECT(ptr, bytes) { ptr = d + off; off += (bytes); }
#define PAD4 off = (off + 3) & ~3
    SECT(iBlocks, iNB * 16);
    SECT(iRows, iNRow * 2); PAD4;
    SECT(iUnits, iNUnit * 2); PAD4;
    SECT(iSolids, iNS * 8);
    SECT(iParts, iNP * 12);
    SECT(iCovers, iNCov * 8);
    SECT(iCells, iNCell * 34); PAD4;
    SECT(iPlates, iNPl * 8);
    SECT(iPTiles, iNPt * 8);
    SECT(iMeshTab, iNM * 4);
    SECT(iCollTab, iNC * 4);
    SECT(iChunkTab, iNChunk * 8);
    SECT(iNames, iNameBytes);
#undef SECT
#undef PAD4
    if (off > iDirSize || iNChunk != iNParts - 1) { Close(); return -5; }
    // each mesh's first corner and face within its chunk, and the chunks' totals
    iMeshV0 = new int[iNM + 1]; iMeshF0 = new int[iNM + 1]; iCollV0 = new int[iNC + 1]; iCollF0 = new int[iNC + 1];
    iChunkNV = new int[iNChunk]; iChunkNF = new int[iNChunk]; iChunkCV = new int[iNChunk]; iChunkCF = new int[iNChunk];
    if (!iMeshV0 || !iMeshF0 || !iCollV0 || !iCollF0 || !iChunkNV || !iChunkNF || !iChunkCV || !iChunkCF) { Close(); return -6; }
    for (int ch = 0; ch < iNChunk; ch++)
        {
        const unsigned char* e = iChunkTab + ch * 8;
        int m0 = Rd16(e), m1 = Rd16(e + 2), c0 = Rd16(e + 4), c1 = Rd16(e + 6);
        if (m1 < m0 || m1 > iNM || c1 < c0 || c1 > iNC) { Close(); return -7; }
        int nv = 0, nf = 0;
        for (int m = m0; m < m1; m++) { iMeshV0[m] = nv; iMeshF0[m] = nf; nv += Rd16(iMeshTab + m * 4); nf += Rd16(iMeshTab + m * 4 + 2); }
        iChunkNV[ch] = nv; iChunkNF[ch] = nf;
        nv = nf = 0;
        for (int m = c0; m < c1; m++) { iCollV0[m] = nv; iCollF0[m] = nf; nv += Rd16(iCollTab + m * 4); nf += Rd16(iCollTab + m * 4 + 2); }
        iChunkCV[ch] = nv; iChunkCF[ch] = nf;
        }
    return 0;
    }

int TmKit::FindBlock(const char* aName) const
    {
    // the names are sorted (lower case): a binary search
    int lo = 0, hi = iNB - 1;
    while (lo <= hi)
        {
        int mid = (lo + hi) >> 1;
        const char* a = (const char*)iNames + Rd16(iBlocks + mid * 16);
        const char* b = aName;
        int cmp = 0;
        for (;;)
            {
            int x = (unsigned char)*a, y = Lower((unsigned char)*b);
            if (x != y) { cmp = x < y ? -1 : 1; break; }
            if (!x) break;
            a++; b++;
            }
        if (cmp == 0) return mid;
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
        }
    return -1;
    }

// the chunk arrays' offsets
struct TChunkView
    {
    const unsigned char *xl, *xh, *yl, *yh, *zl, *zh, *idx, *col;
    const unsigned char *cxl, *cxh, *cyl, *cyh, *czl, *czh, *cidx, *nxl, *nxh, *nyl, *nyh, *nzl, *nzh, *surf;
    int size;
    };

static int SetView(TChunkView& v, const unsigned char* d, int aSize, int nv, int nf, int cv, int cf)
    {
    int off = 0;
#define PL(lo, hi, n) { lo = d + off; hi = d + off + (n); off = (off + 2 * (n) + 3) & ~3; }
    PL(v.xl, v.xh, nv); PL(v.yl, v.yh, nv); PL(v.zl, v.zh, nv);
    v.idx = d + off; off = (off + nf * 6 + 3) & ~3;
    v.col = d + off; off = (off + nf * 2 + 3) & ~3;
    PL(v.cxl, v.cxh, cv); PL(v.cyl, v.cyh, cv); PL(v.czl, v.czh, cv);
    v.cidx = d + off; off = (off + cf * 6 + 3) & ~3;
    PL(v.nxl, v.nxh, cf); PL(v.nyl, v.nyh, cf); PL(v.nzl, v.nzh, cf);
    v.surf = d + off; off = (off + cf + 3) & ~3;
#undef PL
    v.size = off;
    return off <= aSize ? 0 : -1;
    }

// a mesh of the chunk, turned aD quarter turns about (aC, aC)
static TmKMesh* GenMesh(const TChunkView& cv, int v0, int f0, int nv, int nf, int aD, int aC)
    {
    TmKMesh* m = new TmKMesh;
    if (!m) return 0;
    m->nv = nv; m->nf = nf;
    m->v = new short[nv * 3 + 1]; m->f = new unsigned short[nf * 3 + 1]; m->col = new unsigned short[nf + 1];
    if (!m->v || !m->f || !m->col) { FreeMesh(m); return 0; }
    for (int i = 0; i < nv; i++)
        {
        int k = v0 + i;
        int x = (short)(cv.xl[k] | (cv.xh[k] << 8)), y = (short)(cv.yl[k] | (cv.yh[k] << 8)), z = (short)(cv.zl[k] | (cv.zh[k] << 8));
        int X = x - aC, Z = z - aC;
        for (int s = 0; s < aD; s++) { int t = X; X = Z; Z = -t; }
        m->v[i * 3] = (short)(X + aC); m->v[i * 3 + 1] = (short)y; m->v[i * 3 + 2] = (short)(Z + aC);
        }
    int hi = 0;
    for (int i = 0; i < nf * 3; i++)
        {
        const unsigned char* p = cv.idx + (f0 * 3 + i) * 2;
        int c = p[0] | (p[1] << 8);
        int v = (hi - c) & 0xffff;
        if (v >= nv) v = 0;
        m->f[i] = (unsigned short)v;
        if (v + 1 > hi) hi = v + 1;
        }
    for (int i = 0; i < nf; i++) { const unsigned char* p = cv.col + (f0 + i) * 2; m->col[i] = (unsigned short)(p[0] | (p[1] << 8)); }
    Box(m->v, nv, m->bb);
    return m;
    }

static TmKColl* GenColl(const TChunkView& cv, int v0, int f0, int nv, int nf, int aD, int aC)
    {
    TmKColl* m = new TmKColl;
    if (!m) return 0;
    m->nv = nv; m->nf = nf;
    m->v = new short[nv * 3 + 1]; m->f = new unsigned short[nf * 3 + 1]; m->n = new short[nf * 3 + 1]; m->s = new unsigned char[nf + 1];
    if (!m->v || !m->f || !m->n || !m->s) { FreeColl(m); return 0; }
    for (int i = 0; i < nv; i++)
        {
        int k = v0 + i;
        int x = (short)(cv.cxl[k] | (cv.cxh[k] << 8)), y = (short)(cv.cyl[k] | (cv.cyh[k] << 8)), z = (short)(cv.czl[k] | (cv.czh[k] << 8));
        int X = x - aC, Z = z - aC;
        for (int s = 0; s < aD; s++) { int t = X; X = Z; Z = -t; }
        m->v[i * 3] = (short)(X + aC); m->v[i * 3 + 1] = (short)y; m->v[i * 3 + 2] = (short)(Z + aC);
        }
    int hi = 0;
    for (int i = 0; i < nf * 3; i++)
        {
        const unsigned char* p = cv.cidx + (f0 * 3 + i) * 2;
        int c = p[0] | (p[1] << 8);
        int v = (hi - c) & 0xffff;
        if (v >= nv) v = 0;
        m->f[i] = (unsigned short)v;
        if (v + 1 > hi) hi = v + 1;
        }
    for (int i = 0; i < nf; i++)
        {
        int k = f0 + i;
        int nx = (short)(cv.nxl[k] | (cv.nxh[k] << 8)), ny = (short)(cv.nyl[k] | (cv.nyh[k] << 8)), nz = (short)(cv.nzl[k] | (cv.nzh[k] << 8));
        for (int s = 0; s < aD; s++) { int t = nx; nx = nz; nz = -t; }
        m->n[i * 3] = (short)nx; m->n[i * 3 + 1] = (short)ny; m->n[i * 3 + 2] = (short)nz;
        m->s[i] = cv.surf[k];
        }
    Box(m->v, nv, m->bb);
    return m;
    }

// a terrain tile partly under a ground piece (pack.py): split until no edge is over 8.5 m, the faces whose middle
// the pieces cover (aMask: the cell's 16 x 16 samples) dropped; 0 when nothing is left (or out of memory)
static TmKMesh* CutMesh(const TmKMesh& m, int aX, int aY, int aZ, int aBlk, int aCellX, int aCellZ, const unsigned char* aMask)
    {
    // corners in 1/1024 m (the midpoints of up to six halvings stay whole), world
    int cap = m.nv + 256, nv = m.nv;
    int* V = new int[cap * 3];
    int fcap = m.nf * 4 + 64, scap = m.nf * 4 + 64;
    int* stack = new int[scap * 4]; int ns = 0;
    int* fine = new int[fcap * 4]; int nfine = 0;
    int hsize = 1024; while (hsize < cap * 2) hsize <<= 1;
    int* hk = new int[hsize * 2]; int* hv = new int[hsize];
    if (!V || !stack || !fine || !hk || !hv) { delete[] V; delete[] stack; delete[] fine; delete[] hk; delete[] hv; return 0; }
    for (int i = 0; i < hsize; i++) hk[i * 2] = -1;
    for (int i = 0; i < nv; i++)
        {
        V[i * 3] = m.v[i * 3] * 64 + aX * 1024; V[i * 3 + 1] = m.v[i * 3 + 1] * 64 + aY * 1024; V[i * 3 + 2] = m.v[i * 3 + 2] * 64 + aZ * 1024;
        }
    for (int i = 0; i < m.nf; i++) { stack[ns * 4] = m.f[i * 3]; stack[ns * 4 + 1] = m.f[i * 3 + 1]; stack[ns * 4 + 2] = m.f[i * 3 + 2]; stack[ns * 4 + 3] = m.col[i]; ns++; }
    bool bad = false;
    const long long KMax = 8704LL * 8704LL;      // (8.5 m)^2 in 1/1024 m
#define GROW(arr, capv, need, width) if ((need) > capv) { int nc_ = capv * 2; while (nc_ < (need)) nc_ *= 2; int* q_ = new int[nc_ * (width)]; \
        if (!q_) { bad = true; break; } for (int i_ = 0; i_ < capv * (width); i_++) q_[i_] = arr[i_]; delete[] arr; arr = q_; capv = nc_; }
    while (ns > 0 && !bad)
        {
        ns--;
        int a = stack[ns * 4], b = stack[ns * 4 + 1], c = stack[ns * 4 + 2], col = stack[ns * 4 + 3];
        int e[3][2] = { { a, b }, { b, c }, { c, a } };
        long long dmax = 0;
        for (int k = 0; k < 3; k++)
            {
            long long dx = V[e[k][0] * 3] - V[e[k][1] * 3], dy = V[e[k][0] * 3 + 1] - V[e[k][1] * 3 + 1], dz = V[e[k][0] * 3 + 2] - V[e[k][1] * 3 + 2];
            long long d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > dmax) dmax = d2;
            }
        if (dmax <= KMax)
            {
            GROW(fine, fcap, nfine + 1, 4);
            fine[nfine * 4] = a; fine[nfine * 4 + 1] = b; fine[nfine * 4 + 2] = c; fine[nfine * 4 + 3] = col; nfine++;
            continue;
            }
        int mid[3];
        for (int k = 0; k < 3 && !bad; k++)
            {
            int p = e[k][0], q = e[k][1];
            int lo = p < q ? p : q, hi2 = p < q ? q : p;
            unsigned int h = ((unsigned int)lo * 2654435761u ^ (unsigned int)hi2 * 40503u) & (hsize - 1);
            while (hk[h * 2] >= 0 && !(hk[h * 2] == lo && hk[h * 2 + 1] == hi2)) h = (h + 1) & (hsize - 1);
            if (hk[h * 2] < 0)
                {
                if (nv > 60000) { bad = true; break; }
                if ((nv - m.nv) * 2 >= hsize)
                    {
                    // the midpoint table full: twice the size
                    int ns2 = hsize * 2;
                    int* k2 = new int[ns2 * 2]; int* v2 = new int[ns2];
                    if (!k2 || !v2) { delete[] k2; delete[] v2; bad = true; break; }
                    for (int i = 0; i < ns2; i++) k2[i * 2] = -1;
                    for (int i = 0; i < hsize; i++)
                        {
                        if (hk[i * 2] < 0) continue;
                        unsigned int h2 = ((unsigned int)hk[i * 2] * 2654435761u ^ (unsigned int)hk[i * 2 + 1] * 40503u) & (ns2 - 1);
                        while (k2[h2 * 2] >= 0) h2 = (h2 + 1) & (ns2 - 1);
                        k2[h2 * 2] = hk[i * 2]; k2[h2 * 2 + 1] = hk[i * 2 + 1]; v2[h2] = hv[i];
                        }
                    delete[] hk; delete[] hv; hk = k2; hv = v2; hsize = ns2;
                    h = ((unsigned int)lo * 2654435761u ^ (unsigned int)hi2 * 40503u) & (hsize - 1);
                    while (hk[h * 2] >= 0) h = (h + 1) & (hsize - 1);
                    }
                GROW(V, cap, nv + 1, 3);
                for (int j = 0; j < 3; j++) V[nv * 3 + j] = (V[p * 3 + j] + V[q * 3 + j]) / 2;
                hk[h * 2] = lo; hk[h * 2 + 1] = hi2; hv[h] = nv++;
                }
            mid[k] = hv[h];
            }
        if (bad) break;
        int ab = mid[0], bc = mid[1], ca = mid[2];
        GROW(stack, scap, ns + 4, 4);
        int add[4][3] = { { a, ab, ca }, { ab, b, bc }, { ca, bc, c }, { ab, bc, ca } };
        for (int k = 0; k < 4; k++) { stack[ns * 4] = add[k][0]; stack[ns * 4 + 1] = add[k][1]; stack[ns * 4 + 2] = add[k][2]; stack[ns * 4 + 3] = col; ns++; }
        }
#undef GROW
    TmKMesh* out = 0;
    if (!bad)
        {
        // the faces kept: their middle not under the pieces
        int nk = 0;
        const long long cellSpan = 3LL * 1024 * aBlk;
        for (int i = 0; i < nfine; i++)
            {
            long long sx = (long long)V[fine[i * 4] * 3] + V[fine[i * 4 + 1] * 3] + V[fine[i * 4 + 2] * 3];
            long long sz = (long long)V[fine[i * 4] * 3 + 2] + V[fine[i * 4 + 1] * 3 + 2] + V[fine[i * 4 + 2] * 3 + 2];
            long long rx = sx - aCellX * cellSpan, rz = sz - aCellZ * cellSpan;
            bool drop = false;
            if (rx >= 0 && rx < cellSpan && rz >= 0 && rz < cellSpan)
                {
                int q = (int)(rx * 16 / cellSpan), j = (int)(rz * 16 / cellSpan);
                int bit = j * 16 + q;
                drop = (aMask[bit >> 3] >> (bit & 7)) & 1;
                }
            if (!drop) { for (int k = 0; k < 4; k++) fine[nk * 4 + k] = fine[i * 4 + k]; nk++; }
            }
        if (nk > 0)
            {
            // compact: the corners used, in their order
            int* remap = new int[nv];
            out = new TmKMesh;
            if (remap && out)
                {
                for (int i = 0; i < nv; i++) remap[i] = -1;
                for (int i = 0; i < nk * 4; i++) if ((i & 3) != 3) remap[fine[i]] = 0;
                int n2 = 0;
                for (int i = 0; i < nv; i++) if (remap[i] >= 0) remap[i] = n2++;
                out->nv = n2; out->nf = nk;
                out->v = new short[n2 * 3 + 1]; out->f = new unsigned short[nk * 3 + 1]; out->col = new unsigned short[nk + 1];
                if (out->v && out->f && out->col)
                    {
                    for (int i = 0; i < nv; i++)
                        {
                        if (remap[i] < 0) continue;
                        int r = remap[i];
                        out->v[r * 3] = (short)RoundDiv(V[i * 3] - aX * 1024, 64);
                        out->v[r * 3 + 1] = (short)RoundDiv(V[i * 3 + 1] - aY * 1024, 64);
                        out->v[r * 3 + 2] = (short)RoundDiv(V[i * 3 + 2] - aZ * 1024, 64);
                        }
                    for (int i = 0; i < nk; i++)
                        {
                        out->f[i * 3] = (unsigned short)remap[fine[i * 4]]; out->f[i * 3 + 1] = (unsigned short)remap[fine[i * 4 + 1]];
                        out->f[i * 3 + 2] = (unsigned short)remap[fine[i * 4 + 2]]; out->col[i] = (unsigned short)fine[i * 4 + 3];
                        }
                    Box(out->v, n2, out->bb);
                    }
                else { FreeMesh(out); out = 0; }
                }
            else { delete out; out = 0; }
            delete[] remap;
            }
        }
    delete[] V; delete[] stack; delete[] fine; delete[] hk; delete[] hv;
    return out;
    }

int TmKit::Build(const TmGbxTrack& aT, MTmOut& aOut, const int* aPath, int aNPath)
    {
    iNInst = iNMesh = iNColl = iNCInst = iTris = iUnknown = iDropped = iChunks = iUnpacked = 0;
    if (!iDir) return -1;
    const int sx = aT.iSize[0], sz = aT.iSize[2];
    if (sx <= 0 || sz <= 0 || sx > 255 || sz > 255) return -2;
    int res = 0;
    // the Gbx's block names in the kit
    int* nameBlock = new int[aT.iNNames + 1];
    int maxInst = aT.iNBlocks + sx * sz + 1;
    TmKInst* inst = new TmKInst[maxInst];
    unsigned char* zcol = new unsigned char[sx * sz];
    unsigned char* covered = new unsigned char[sx * sz];
    unsigned char* cut = new unsigned char[sx * sz * 33];
    const int nKeys = iNP * 4;
    TmKMesh** gDraw = new TmKMesh*[nKeys]; TmKMesh** gFar = new TmKMesh*[nKeys]; TmKColl** gColl = new TmKColl*[nKeys];
    int* kMesh = new int[nKeys]; int* kFar = new int[nKeys]; int* kColl = new int[nKeys];
    unsigned char* need = new unsigned char[nKeys];
    TmKMesh** gPlate = 0; TmKMesh* gCar = 0;
    int plate = -1;
    TPtrList meshes, colls;          // the track's (TmKMesh*, TmKColl*), in pack.py's order
    TPtrList ownMeshes;              // the cut tiles (freed here)
    int* insts = 0; int* cinsts = 0; int nInst = 0, nCInst = 0;
    if (!nameBlock || !inst || !zcol || !covered || !cut || !gDraw || !gFar || !gColl || !kMesh || !kFar || !kColl || !need) { res = -3; goto done; }
    for (int i = 0; i < nKeys; i++) { gDraw[i] = gFar[i] = 0; gColl[i] = 0; kMesh[i] = kFar[i] = kColl[i] = -1; need[i] = 0; }
    for (int i = 0; i < sx * sz; i++) { zcol[i] = covered[i] = 0; }
    for (int i = 0; i < sx * sz * 33; i++) cut[i] = 0;
    for (int i = 0; i < aT.iNNames; i++) nameBlock[i] = FindBlock(aT.Name(i));
    {
    // 1. the blocks placed (assemble.Track): the Gbx's, then the default terrain where no zone block stands
    int nInstAll = 0;
    for (int i = 0; i < aT.iNBlocks; i++)
        {
        int kb = nameBlock[aT.iBlocks[i].iName];
        if (kb >= 0 && (iBlocks[kb * 16 + 3] & 1))
            {
            int x = aT.iBlocks[i].iX, z = aT.iBlocks[i].iZ;
            if (x < sx && z < sz) zcol[x * sz + z] = 1;
            }
        }
    int nDefault = 0;
    if (iDefZone >= 0)
        for (int x = 0; x < sx; x++) for (int z = 0; z < sz; z++) if (!zcol[x * sz + z]) nDefault++;
    int nAll = aT.iNBlocks + nDefault;
    int dx = 0, dz = 0;
    for (int i = 0; i < nAll; i++)
        {
        int kb, d, x, y, z; unsigned int fl;
        if (i < aT.iNBlocks)
            {
            const TmGbxBlock& g = aT.iBlocks[i];
            kb = nameBlock[g.iName]; d = g.iDir; x = g.iX; y = g.iY; z = g.iZ; fl = g.iFlags;
            if (kb < 0) { iUnknown++; continue; }
            }
        else
            {
            while (zcol[dx * sz + dz]) { dz++; if (dz == sz) { dz = 0; dx++; } }
            kb = iDefZone; d = 0; x = dx; y = iBaseCell; z = dz; fl = 0x1000;
            dz++; if (dz == sz) { dz = 0; dx++; }
            }
        const unsigned char* b = iBlocks + kb * 16;
        int ground = (fl & 0x1000) ? 1 : 0, variant = fl & 63, sub = (fl >> 6) & 63;
        // the rows: ground, then air
        const unsigned char* R = iRows + Rd16(b + 8) * 2;
        int pos = 0;
        int sideAt[2], sideN[2], sideAny[2];
        for (int s = 0; s < 2; s++)
            {
            sideN[s] = Rd16(R + pos * 2); pos++; sideAt[s] = pos; sideAny[s] = 0;
            for (int r = 0; r < sideN[s]; r++)
                {
                int n = Rd16(R + pos * 2);
                for (int k = 0; k < n; k++) if (Rd16(R + (pos + 1 + k) * 2) != 0xffff) sideAny[s] = 1;
                pos += 1 + n;
                }
            }
        int side = ground ? 0 : 1;
        if (!sideAny[side]) side = 1 - side;
        if (!sideAny[side]) continue;
        int rr = variant < sideN[side] ? variant : 0;
        pos = sideAt[side];
        for (int r = 0; r < rr; r++) pos += 1 + Rd16(R + pos * 2);
        int n = Rd16(R + pos * 2);
        if (n == 0) continue;
        int solid = Rd16(R + (pos + 1 + (sub < n ? sub : 0)) * 2);
        if (solid == 0xffff || solid >= iNS) continue;
        // the height: the cell minus the zone's height (1/16 cell), in whole metres
        int ym = RoundDiv((long long)(y * 16 - Rs16(b + 4)) * iBlkH, 16);
        // the footprint once turned: its lowest unit
        int de = ((-d) % 4 + 4) % 4;
        int fx = 0, fz = 0;
        int nu = b[12];
        const unsigned char* sd = iSolids + solid * 8;
        int cx1 = sd[3], cz1 = sd[4];
        int nUnits = nu ? nu : cx1 * cz1;
        for (int u = 0; u < nUnits; u++)
            {
            int ux, uz;
            if (nu) { const unsigned char* up = iUnits + (Rd16(b + 10) + u) * 2; ux = (signed char)up[0]; uz = (signed char)up[1]; }
            else { ux = u / cz1; uz = u % cz1; }
            for (int s = 0; s < de; s++) { int t = ux; ux = uz; uz = -t; }
            if (ux < fx) fx = ux;
            if (uz < fz) fz = uz;
            }
        TmKInst& I = inst[nInstAll++];
        I.solid = solid; I.block = kb; I.x = (x - fx) * iBlk; I.y = ym; I.z = (z - fz) * iBlk; I.d = de; I.ground = ground;
        }
    // 2. the terrain tiles under ground pieces (drop_covered_zones): dropped where a piece covers 6 of a cell's 9
    // samples, cut where it covers some
    for (int i = 0; i < nInstAll; i++)
        {
        const TmKInst& I = inst[i];
        int bfl = iBlocks[I.block * 16 + 3];
        if (bfl & 1) continue;
        if (!I.ground && !(bfl & 2)) continue;
        int cov = Rd16(iSolids + I.solid * 8 + 6);
        if (cov == 0xffff || cov >= iNCov) continue;
        const unsigned char* cv = iCovers + cov * 8;
        int cx0 = (signed char)cv[0], cz0 = (signed char)cv[1], w = cv[2], h = cv[3], first = Rd32(cv + 4);
        for (int gz = 0; gz < h; gz++)
            for (int gx = 0; gx < w; gx++)
                {
                int ci = first + gz * w + gx;
                if (ci >= iNCell) continue;
                const unsigned char* cell = iCells + ci * 34;
                int m9 = Rd16(cell);
                if (!m9) continue;
                int lx = cx0 + gx, lz = cz0 + gz;
                for (int s = 0; s < I.d; s++) { int t = lx; lx = lz; lz = -t; }
                int wx = I.x / iBlk + lx, wz = I.z / iBlk + lz;
                if (wx < 0 || wz < 0 || wx >= sx || wz >= sz) continue;
                int wc = wx * sz + wz;
                if (covered[wc]) continue;
                int n = PopCount(m9);
                if (n >= 6) { covered[wc] = 1; continue; }
                unsigned char* cm = cut + wc * 33;
                cm[32] = 1;
                for (int bit = 0; bit < 256; bit++)
                    {
                    if (!((cell[2 + (bit >> 3)] >> (bit & 7)) & 1)) continue;
                    int q = bit & 15, j = bit >> 4;
                    for (int s = 0; s < I.d; s++) { int t = q; q = j; j = 15 - t; }
                    int nb = j * 16 + q;
                    cm[nb >> 3] |= (unsigned char)(1 << (nb & 7));
                    }
                }
        }
#ifdef TMKIT_DEBUG
    for (int i = 0; i < sx * sz; i++) if (covered[i] || cut[i * 33 + 32]) printf("cell %d %d %s\n", i / sz, i % sz, covered[i] ? "covered" : "cut");
#endif
    int nKeep = 0;
    for (int i = 0; i < nInstAll; i++)
        {
        const TmKInst& I = inst[i];
        if (iBlocks[I.block * 16 + 3] & 1)
            {
            int cx = I.x / iBlk, cz = I.z / iBlk;
            if (cx >= 0 && cz >= 0 && cx < sx && cz < sz && covered[cx * sz + cz]) { iDropped++; continue; }
            }
        inst[nKeep++] = I;
        }
    // the parts the track uses, in each turn
    for (int i = 0; i < nKeep; i++)
        {
        const unsigned char* sd = iSolids + inst[i].solid * 8;
        int p0 = Rd16(sd), np = sd[2];
        for (int p = p0; p < p0 + np && p < iNP; p++) need[p * 4 + inst[i].d] = 1;
        }
    // the base plate: Base/<x>x<z>, else Base/30x30
    for (int k = 0; k < iNPl; k++) if (Rd16(iPlates + k * 8) == sx && Rd16(iPlates + k * 8 + 2) == sz) plate = k;
    if (plate < 0) for (int k = 0; k < iNPl; k++) if (Rd16(iPlates + k * 8) == 30 && Rd16(iPlates + k * 8 + 2) == 30) plate = k;
    int pt0 = plate >= 0 ? Rd16(iPlates + plate * 8 + 4) : 0, npt = plate >= 0 ? Rd16(iPlates + plate * 8 + 6) : 0;
    if (npt) { gPlate = new TmKMesh*[npt]; if (!gPlate) { res = -3; goto done; } for (int k = 0; k < npt; k++) gPlate[k] = 0; }
    // 3. the chunks: each unpacked in turn, the meshes the track uses taken out turned
    const int C16 = iBlk * 8;
    for (int ch = 0; ch < iNChunk && res == 0; ch++)
        {
        const unsigned char* e = iChunkTab + ch * 8;
        int m0 = Rd16(e), m1 = Rd16(e + 2);
        bool any = iCar >= m0 && iCar < m1;
        for (int k = 0; k < npt && !any; k++) { int m = Rd16(iPTiles + (pt0 + k) * 8 + 4); if (m >= m0 && m < m1) any = true; }
        for (int key = 0; key < nKeys && !any; key++)
            if (need[key]) { int m = Rd16(iParts + (key >> 2) * 12 + 4); if (m >= m0 && m < m1) any = true; }
        if (!any) continue;
        unsigned char* data = 0;
        int n = Unpack(ch + 1, data);
        if (n < 0) { res = -10; break; }
        iChunks++; iUnpacked += n;
        TChunkView cv;
        if (SetView(cv, data, n, iChunkNV[ch], iChunkNF[ch], iChunkCV[ch], iChunkCF[ch]) < 0) { delete[] data; res = -11; break; }
        for (int key = 0; key < nKeys && res == 0; key++)
            {
            if (!need[key]) continue;
            const unsigned char* pr = iParts + (key >> 2) * 12;
            int m = Rd16(pr + 4), fm = Rd16(pr + 6), cm = Rd16(pr + 8);
            if (m < m0 || m >= m1) continue;
            int d = key & 3;
            gDraw[key] = GenMesh(cv, iMeshV0[m], iMeshF0[m], Rd16(iMeshTab + m * 4), Rd16(iMeshTab + m * 4 + 2), d, C16);
            if (!gDraw[key]) res = -12;
            if (fm != 0xffff && fm < iNM)
                {
                if (fm < m0 || fm >= m1) res = -13;
                else if (!(gFar[key] = GenMesh(cv, iMeshV0[fm], iMeshF0[fm], Rd16(iMeshTab + fm * 4), Rd16(iMeshTab + fm * 4 + 2), d, C16))) res = -12;
                }
            if (cm != 0xffff && cm < iNC && res == 0)
                {
                int c0 = Rd16(e + 4), c1 = Rd16(e + 6);
                if (cm < c0 || cm >= c1) res = -13;
                else if (!(gColl[key] = GenColl(cv, iCollV0[cm], iCollF0[cm], Rd16(iCollTab + cm * 4), Rd16(iCollTab + cm * 4 + 2), d, C16))) res = -12;
                }
            }
        for (int k = 0; k < npt && res == 0; k++)
            {
            int m = Rd16(iPTiles + (pt0 + k) * 8 + 4);
            if (m >= m0 && m < m1 && !(gPlate[k] = GenMesh(cv, iMeshV0[m], iMeshF0[m], Rd16(iMeshTab + m * 4), Rd16(iMeshTab + m * 4 + 2), 0, 0))) res = -12;
            }
        if (iCar >= m0 && iCar < m1 && res == 0 && !(gCar = GenMesh(cv, iMeshV0[iCar], iMeshF0[iCar], Rd16(iMeshTab + iCar * 4), Rd16(iMeshTab + iCar * 4 + 2), 0, 0))) res = -12;
        delete[] data;
        }
    if (res) goto done;
    // 4. the instances (pack.py's loop): each part's turned mesh, far form and collision the first time it is used
    insts = new int[(nKeep * 8 + npt + 1) * 6]; cinsts = new int[(nKeep * 8 + 1) * 3];
    // (a solid has up to 255 parts: grown below if needed)
    int instCap = nKeep * 8 + npt + 1, cinstCap = nKeep * 8 + 1;
    if (!insts || !cinsts) { res = -3; goto done; }
    for (int i = 0; i < nKeep && res == 0; i++)
        {
        const TmKInst& I = inst[i];
        const unsigned char* sd = iSolids + I.solid * 8;
        int p0 = Rd16(sd), np = sd[2];
        bool zone = iBlocks[I.block * 16 + 3] & 1;
        int cx = I.x / iBlk, cz = I.z / iBlk;
        const unsigned char* cm = (zone && cx >= 0 && cz >= 0 && cx < sx && cz < sz && cut[(cx * sz + cz) * 33 + 32] && !covered[cx * sz + cz]) ? cut + (cx * sz + cz) * 33 : 0;
        for (int p = p0; p < p0 + np && p < iNP && res == 0; p++)
            {
            int key = p * 4 + I.d;
            if (!gDraw[key]) { res = -14; break; }
            if (kMesh[key] < 0)
                {
                kMesh[key] = meshes.Add(gDraw[key]);
                kFar[key] = gFar[key] ? meshes.Add(gFar[key]) : -1;
                kColl[key] = gColl[key] ? colls.Add(gColl[key]) : -1;
                }
            int mi = kMesh[key], far = kFar[key];
            if (cm)
                {
                TmKMesh* c = CutMesh(*gDraw[key], I.x, I.y, I.z, iBlk, cx, cz, cm);
                if (!c) continue;
                ownMeshes.Add(c);
                mi = meshes.Add(c); far = -1;
                }
            if (nInst >= instCap || nCInst >= cinstCap)
                {
                int nc = instCap * 2, ncc = cinstCap * 2;
                int* a = new int[nc * 6]; int* b2 = new int[ncc * 3];
                if (!a || !b2) { delete[] a; delete[] b2; res = -3; break; }
                for (int k = 0; k < nInst * 6; k++) a[k] = insts[k];
                for (int k = 0; k < nCInst * 3; k++) b2[k] = cinsts[k];
                delete[] insts; delete[] cinsts; insts = a; cinsts = b2; instCap = nc; cinstCap = ncc;
                }
            int* in = insts + nInst * 6; nInst++;
            in[0] = mi; in[1] = 0; in[2] = I.x * 16; in[3] = I.y * 16; in[4] = I.z * 16; in[5] = far;
            if (kColl[key] >= 0)
                {
                int* ci = cinsts + nCInst * 3; nCInst++;
                ci[0] = kColl[key]; ci[1] = iBlocks[I.block * 16 + 2]; ci[2] = i;
                }
            }
        }
    if (res) goto done;
    // the base plate's tiles
    for (int k = 0; k < npt; k++)
        {
        if (!gPlate[k]) continue;
        if (nInst >= instCap) { res = -3; goto done; }
        int* in = insts + nInst * 6; nInst++;
        in[0] = meshes.Add(gPlate[k]); in[1] = 1; in[2] = Rs16(iPTiles + (pt0 + k) * 8) * 16; in[3] = 0; in[4] = Rs16(iPTiles + (pt0 + k) * 8 + 2) * 16; in[5] = -1;
        }
    int car = gCar ? meshes.Add(gCar) : -1;
    if (meshes.n > 0xfffe || colls.n > 0xfffe) { res = -15; goto done; }
    // 5. the start (pack.py block_start): the first Start block (else StartFinish), along its way, on the highest
    // ground there up to 20 m over the block
    {
    int si = -1;
    if (aPath && aNPath > 0) { for (int k = 0; k < 4; k++) iStart[k] = aPath[k]; si = -2; }
    for (int pass = 1; si == -1 && pass <= 4 && si < 0; pass += 3)
        for (int i = 0; i < nKeep; i++) if (iBlocks[inst[i].block * 16 + 13] == pass) { si = i; break; }
    if (si >= 0)
        {
        const TmKInst& I = inst[si];
        int ox = 0, oz = Rs16(iBlocks + I.block * 16 + 6);
        for (int s = 0; s < I.d; s++) { int t = ox; ox = oz; oz = -t; }
        int wx = I.x * 16 + iBlk * 8 + ox, wz = I.z * 16 + iBlk * 8 + oz;     // 1/16 m
        bool have = false; long long bn = 0; int bd = 1;           // the best height: bn / bd (1/16 m)
        for (int k = 0; k < nCInst; k++)
            {
            const TmKColl* c = (const TmKColl*)colls.p[cinsts[k * 3]];
            const TmKInst& J = inst[cinsts[k * 3 + 2]];
            int cx = J.x * 16, cy = J.y * 16, cz = J.z * 16;
            if (wx < cx + c->bb[0] || wx > cx + c->bb[3] || wz < cz + c->bb[2] || wz > cz + c->bb[5]) continue;
            int px = wx - cx, pz = wz - cz;
            for (int f = 0; f < c->nf; f++)
                {
                int s = c->s[f];
                const short* n = c->n + f * 3;
                if (s == 5 || s == 9 || s == 12 || n[1] < 3686) continue;
                const short* A = c->v + c->f[f * 3] * 3; const short* B = c->v + c->f[f * 3 + 1] * 3; const short* C = c->v + c->f[f * 3 + 2] * 3;
                long long s1 = (long long)(B[0] - A[0]) * (pz - A[2]) - (long long)(B[2] - A[2]) * (px - A[0]);
                long long s2 = (long long)(C[0] - B[0]) * (pz - B[2]) - (long long)(C[2] - B[2]) * (px - B[0]);
                long long s3 = (long long)(A[0] - C[0]) * (pz - C[2]) - (long long)(A[2] - C[2]) * (px - C[0]);
                if (!((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0))) continue;
                long long hn = (long long)(cy + A[1]) * n[1] + (long long)n[0] * (A[0] - px) + (long long)n[2] * (A[2] - pz);
                int hd = n[1];
                if (hn > (long long)(I.y + 20) * 16 * hd) continue;
                if (!have || hn * bd > bn * hd) { have = true; bn = hn; bd = hd; }
                }
            }
        iStart[0] = wx; iStart[2] = wz; iStart[3] = I.d * 1024;
        iStart[1] = have ? RoundDiv(bn, bd) : (I.y + 1) * 16;
        }
    else if (si == -1) { iStart[0] = iStart[1] = iStart[2] = iStart[3] = 0; }
    }
    // 6. the TMB5 file
    {
    TWriter w(&aOut);
    w.B("TMB5", 4); w.U32(meshes.n); w.U32(nInst); w.U32(aPath ? aNPath : 0); w.U32(car < 0 ? -1 : (car | (iEnv << 24)));
    w.U32(iSky); w.U32(iGround); w.U32(iFog); w.U32(iFogStart); w.U32(iFogEnd);
    w.U32(colls.n); w.U32(nCInst); w.U32(iStart[0]); w.U32(iStart[1]); w.U32(iStart[2]); w.U32(iStart[3]);
    for (int i = 0; i < meshes.n; i++)
        {
        const TmKMesh* m = (const TmKMesh*)meshes.p[i];
        w.U16(m->nv); w.U16(m->nf);
        for (int k = 0; k < 6; k++) w.U16(m->bb[k]);
        for (int k = 0; k < m->nv * 3; k++) w.U16(m->v[k]);
        for (int k = 0; k < m->nf * 3; k++) w.U16(m->f[k]);
        for (int k = 0; k < m->nf; k++) w.U16(m->col[k]);
        if (w.total & 2) w.U16(0);
        }
    for (int i = 0; i < nInst; i++)
        {
        const int* in = insts + i * 6;
        const TmKMesh* m = (const TmKMesh*)meshes.p[in[0]];
        w.U16(in[0]); w.U16(in[1]); w.U32(in[2]); w.U32(in[3]); w.U32(in[4]);
        w.U32(in[2] + m->bb[0]); w.U32(in[3] + m->bb[1]); w.U32(in[4] + m->bb[2]); w.U32(in[2] + m->bb[3]); w.U32(in[3] + m->bb[4]); w.U32(in[4] + m->bb[5]);
        iTris += m->nf;
        }
    for (int i = 0; aPath && i < aNPath; i++)
        {
        const int* q = aPath + i * 5;
        w.U32(q[0]); w.U32(q[1]); w.U32(q[2]); w.U16(q[3]); w.U16(q[4]);
        }
    for (int i = 0; i < colls.n; i++)
        {
        const TmKColl* c = (const TmKColl*)colls.p[i];
        w.U16(c->nv); w.U16(c->nf);
        for (int k = 0; k < 6; k++) w.U16(c->bb[k]);
        for (int k = 0; k < c->nv * 3; k++) w.U16(c->v[k]);
        for (int k = 0; k < c->nf * 3; k++) w.U16(c->f[k]);
        for (int k = 0; k < c->nf * 3; k++) w.U16(c->n[k]);
        w.B(c->s, c->nf);
        w.Pad4();
        }
    for (int i = 0; i < nCInst; i++)
        {
        const int* ci = cinsts + i * 3;
        const TmKColl* c = (const TmKColl*)colls.p[ci[0]];
        const TmKInst& J = inst[ci[2]];
        int x = J.x * 16, y = J.y * 16, z = J.z * 16;
        w.U16(ci[0]); w.U16(ci[1]); w.U32(x); w.U32(y); w.U32(z);
        w.U32(x + c->bb[0]); w.U32(y + c->bb[1]); w.U32(z + c->bb[2]); w.U32(x + c->bb[3]); w.U32(y + c->bb[4]); w.U32(z + c->bb[5]);
        }
    w.U32(iFarDist);
    for (int i = 0; i < nInst; i++) w.U16(insts[i * 6 + 5] >= 0 ? insts[i * 6 + 5] : 0xffff);
    w.Pad4();
    w.B("MEDL", 4); for (int k = 0; k < 4; k++) w.U32(aT.iMedals[k]);
    w.Flush();
    if (w.err) res = -20;
    iNInst = nInst; iNMesh = meshes.n; iNColl = colls.n; iNCInst = nCInst;
    }
    }
done:
    for (int k = 0; k < ownMeshes.n; k++) FreeMesh((TmKMesh*)ownMeshes.p[k]);
    if (gDraw) for (int k = 0; k < nKeys; k++) { FreeMesh(gDraw[k]); FreeMesh(gFar[k]); FreeColl(gColl[k]); }
    if (gPlate) { int npt2 = plate >= 0 ? Rd16(iPlates + plate * 8 + 6) : 0; for (int k = 0; k < npt2; k++) FreeMesh(gPlate[k]); }
    FreeMesh(gCar);
    delete[] gPlate; delete[] gDraw; delete[] gFar; delete[] gColl; delete[] kMesh; delete[] kFar; delete[] kColl; delete[] need;
    delete[] nameBlock; delete[] inst; delete[] zcol; delete[] covered; delete[] cut; delete[] insts; delete[] cinsts;
    return res;
    }
