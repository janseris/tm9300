// tmgbx.cpp - reading a TMF .Challenge.Gbx on the phone (0.19): the header XML (environment), the body
// (LZO1X, its published algorithm) and the block list of chunk 0x0304301F. Every read is bounds-checked: a
// fan file that is cut short or of an unknown layout fails, it does not crash.
#include "tmgbx.h"

// ---- LZO1X ----

int TmLzoDecompress(const unsigned char* aSrc, int aSrcLen, unsigned char* aDst, int aDstLen)
    {
    const unsigned char* ip = aSrc; const unsigned char* ie = aSrc + aSrcLen;
    unsigned char* op = aDst; unsigned char* oe = aDst + aDstLen;
#define NEED_IP(n) if (ie - ip < (int)(n)) return -1
#define NEED_OP(n) if (oe - op < (int)(n)) return -1
#define COPY_LIT(n) { int n_ = (n); NEED_IP(n_); NEED_OP(n_); while (n_-- > 0) *op++ = *ip++; }
#define MATCH(dist, len) { int d_ = (dist), l_ = (len); if (d_ > op - aDst) return -1; NEED_OP(l_); \
        const unsigned char* m_ = op - d_; while (l_-- > 0) *op++ = *m_++; }
    if (aSrcLen <= 0) return -1;
    int state = 0;
    unsigned int t = *ip;
    if (t > 17)
        {
        ip++; t -= 17;
        COPY_LIT(t);
        state = t < 4 ? t : 4;
        }
    while (ip < ie)
        {
        t = *ip++;
        if (t < 16)
            {
            if (state == 0)
                {
                if (t == 0)
                    {
                    NEED_IP(1);
                    while (*ip == 0) { t += 255; ip++; NEED_IP(1); }
                    t += 15 + *ip++;
                    }
                t += 3;
                COPY_LIT(t);
                state = 4;
                continue;
                }
            NEED_IP(1);
            if (state < 4) { int dist = (*ip++ << 2) + (t >> 2) + 1; MATCH(dist, 2); }
            else { int dist = (*ip++ << 2) + (t >> 2) + 2049; MATCH(dist, 3); }
            }
        else if (t >= 64)
            {
            NEED_IP(1);
            int dist = (*ip++ << 3) + ((t >> 2) & 7) + 1;
            MATCH(dist, (t >> 5) + 1);
            }
        else if (t >= 32)
            {
            unsigned int len = t & 31;
            if (len == 0)
                {
                NEED_IP(1);
                while (*ip == 0) { len += 255; ip++; NEED_IP(1); }
                len += 31 + *ip++;
                }
            NEED_IP(2);
            unsigned int ds = ip[0] | (ip[1] << 8); ip += 2;
            MATCH((ds >> 2) + 1, len + 2);
            t = ds;
            }
        else
            {
            int dist = (t & 8) << 11;
            unsigned int len = t & 7;
            if (len == 0)
                {
                NEED_IP(1);
                while (*ip == 0) { len += 255; ip++; NEED_IP(1); }
                len += 7 + *ip++;
                }
            NEED_IP(2);
            unsigned int ds = ip[0] | (ip[1] << 8); ip += 2;
            dist += ds >> 2;
            if (dist == 0) break;   // the end marker
            MATCH(dist + 16384, len + 2);
            t = ds;
            }
        t &= 3;
        if (t) COPY_LIT(t);
        state = t;
        }
    return op - aDst;
#undef NEED_IP
#undef NEED_OP
#undef COPY_LIT
#undef MATCH
    }

// ---- a bounds-checked reader ----

struct TGbxReader
    {
    const unsigned char* b; int n; int p; int bad;
    void Init(const unsigned char* aB, int aN, int aP) { b = aB; n = aN; p = aP; bad = 0; }
    bool Has(int k) { if (k < 0 || p + k > n) { bad = 1; return false; } return true; }
    unsigned int U8() { if (!Has(1)) return 0; return b[p++]; }
    unsigned int U16() { if (!Has(2)) return 0; unsigned int v = b[p] | (b[p + 1] << 8); p += 2; return v; }
    unsigned int U32() { if (!Has(4)) return 0; unsigned int v = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((unsigned int)b[p + 3] << 24); p += 4; return v; }
    unsigned int Peek32() { if (p + 4 > n) return 0; return b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((unsigned int)b[p + 3] << 24); }
    // a string: its bytes' start (into b) and length, -1 if it runs past the end
    int Str(int& aLen) { unsigned int l = U32(); if (bad || l > 0x100000 || !Has((int)l)) { bad = 1; aLen = 0; return -1; } int s = p; p += l; aLen = l; return s; }
    void Skip(int k) { if (Has(k)) p += k; }
    };

static int Lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static void CopyStr(char* aDst, int aCap, const unsigned char* aS, int aLen)
    {
    int k = 0;
    for (; k < aLen && k < aCap - 1; k++) aDst[k] = (char)aS[k];
    aDst[k] = 0;
    }

// an attribute's value in the header XML: attr="value"
static void XmlAttr(const unsigned char* aX, int aLen, const char* aAttr, char* aOut, int aCap)
    {
    aOut[0] = 0;
    int al = 0; while (aAttr[al]) al++;
    for (int i = 0; i + al + 2 < aLen; i++)
        {
        int k = 0;
        while (k < al && aX[i + k] == aAttr[k]) k++;
        if (k < al || aX[i + al] != '=' || aX[i + al + 1] != '"') continue;
        if (i > 0 && aX[i - 1] != ' ' && aX[i - 1] != '\t' && aX[i - 1] != '\n') continue;
        int s = i + al + 2, e = s;
        while (e < aLen && aX[e] != '"') e++;
        CopyStr(aOut, aCap, aX + s, e - s);
        return;
        }
    }

// a map name without its $ codes ($o, $s, $fff colours, $l[link] ...): printable ASCII only
static void PlainName(const unsigned char* aS, int aLen, char* aOut, int aCap)
    {
    int k = 0;
    for (int i = 0; i < aLen && k < aCap - 1; i++)
        {
        int c = aS[i];
        if (c == '$' && i + 1 < aLen)
            {
            int d = aS[i + 1];
            if (d == '$') { aOut[k++] = '$'; i++; continue; }
            bool hex = true;
            for (int j = 1; j <= 3; j++)
                {
                int h = i + j < aLen ? aS[i + j] : 0;
                if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) hex = false;
                }
            if (hex) { i += 3; continue; }
            if ((d == 'l' || d == 'L' || d == 'h' || d == 'H') && i + 2 < aLen && aS[i + 2] == '[')
                {
                int j = i + 3; while (j < aLen && aS[j] != ']') j++;
                i = j; continue;
                }
            i++; continue;
            }
        if (c >= 32 && c < 127) aOut[k++] = (char)c;
        }
    // trimmed
    while (k > 0 && aOut[k - 1] == ' ') k--;
    aOut[k] = 0;
    int s = 0; while (aOut[s] == ' ') s++;
    if (s) { int j = 0; while (aOut[s + j]) { aOut[j] = aOut[s + j]; j++; } aOut[j] = 0; }
    }

int TmGbxHeaderSize(const unsigned char* aStart, int aLen)
    {
    if (aLen < 17 || aStart[0] != 'G' || aStart[1] != 'B' || aStart[2] != 'X') return -1;
    const int ver = aStart[3] | (aStart[4] << 8);
    if (ver < 6) return -1;
    const unsigned int usz = aStart[13] | (aStart[14] << 8) | (aStart[15] << 16) | ((unsigned int)aStart[16] << 24);
    if (usz > 4 * 1024 * 1024) return -1;
    return 17 + (int)usz;
    }

int TmGbxHeader(const unsigned char* aData, int aSize, char* aEnv, int aEnvCap, char* aName, int aNameCap, int* aMedals,
    char* aUid, int aUidCap, char* aAuthor, int aAuthorCap)
    {
    aEnv[0] = aName[0] = 0;
    if (aUid && aUidCap) aUid[0] = 0;
    if (aAuthor && aAuthorCap) aAuthor[0] = 0;
    for (int k = 0; k < 4; k++) aMedals[k] = 0;
    TGbxReader r; r.Init(aData, aSize, 0);
    if (aSize < 16 || aData[0] != 'G' || aData[1] != 'B' || aData[2] != 'X') return -1;
    r.p = 3;
    unsigned int ver = r.U16();
    if (ver < 6) return -2;
    r.U8(); r.U8(); r.U8(); r.U8();
    unsigned int cls = r.U32();
    if (cls != 0x03043000 && cls != 0x24003000) return -3;
    r.U32();
    unsigned int n = r.U32();
    if (r.bad || n > 64) return -4;
    unsigned int ids[64], sizes[64];
    for (unsigned int i = 0; i < n; i++) { ids[i] = r.U32(); sizes[i] = r.U32() & 0x7FFFFFFF; }
    for (unsigned int i = 0; i < n; i++)
        {
        if (!r.Has((int)sizes[i])) return -5;
        const unsigned char* c = aData + r.p;
        if ((ids[i] == 0x03043002 || ids[i] == 0x24003002) && sizes[i] >= 21 && c[0] >= 3)
            for (int k = 0; k < 4; k++) aMedals[3 - k] = c[5 + k * 4] | (c[6 + k * 4] << 8) | (c[7 + k * 4] << 16) | (c[8 + k * 4] << 24);
        if (ids[i] == 0x03043005 || ids[i] == 0x24003005)
            {
            XmlAttr(c, sizes[i], "envir", aEnv, aEnvCap);
            char raw[160];
            XmlAttr(c, sizes[i], "name", raw, sizeof(raw));
            int l = 0; while (raw[l]) l++;
            PlainName((const unsigned char*)raw, l, aName, aNameCap);
            if (aUid && aUidCap) XmlAttr(c, sizes[i], "uid", aUid, aUidCap);
            if (aAuthor && aAuthorCap)
                {
                XmlAttr(c, sizes[i], "author", raw, sizeof(raw));
                int la = 0; while (raw[la]) la++;
                PlainName((const unsigned char*)raw, la, aAuthor, aAuthorCap);
                }
            }
        r.p += sizes[i];
        }
    return aEnv[0] ? 0 : -6;
    }

// ---- the track ----

TmGbxTrack::TmGbxTrack() : iVersion(0), iNBlocks(0), iBlocks(0), iNNames(0), iPool(0), iPoolUsed(0)
    {
    iEnv[0] = iTitle[0] = iUid[0] = 0; iSize[0] = iSize[1] = iSize[2] = 0;
    iMedals[0] = iMedals[1] = iMedals[2] = iMedals[3] = 0;
    }

TmGbxTrack::~TmGbxTrack() { Free(); }

void TmGbxTrack::Free()
    {
    delete[] iBlocks; iBlocks = 0; iNBlocks = 0;
    delete[] iPool; iPool = 0; iPoolUsed = 0; iNNames = 0;
    }

int TmGbxTrack::FindName(const char* aName) const
    {
    for (int i = 0; i < iNNames; i++)
        {
        const char* a = Name(i); const char* b = aName;
        while (*a && Lower(*a) == Lower(*b)) { a++; b++; }
        if (!*a && !*b) return i;
        }
    return -1;
    }

int TmGbxTrack::AddName(const unsigned char* aS, int aLen)
    {
    for (int i = 0; i < iNNames; i++)
        {
        const char* a = Name(i); int k = 0;
        while (k < aLen && a[k] == (char)aS[k]) k++;
        if (k == aLen && a[k] == 0) return i;
        }
    if (iNNames >= KGbxMaxNames || iPoolUsed + aLen + 1 > KGbxNamePool) return -1;
    iNameOff[iNNames] = iPoolUsed;
    for (int k = 0; k < aLen; k++) iPool[iPoolUsed++] = (char)aS[k];
    iPool[iPoolUsed++] = 0;
    return iNNames++;
    }

// a lookback string: its name index (-1: none / a collection number / unknown)
struct TLookback
    {
    int list[KGbxMaxNames];     // lookback slot -> name index
    int n;
    };

static int ReadLookback(TGbxReader& r, TLookback& lb, TmGbxTrack* aT, char* aCopy = 0, int aCap = 0)
    {
    unsigned int idx = r.U32();
    if (r.bad || idx == 0xFFFFFFFF) return -1;
    if ((idx & 0xC0000000) == 0) return -1;     // a collection number
    unsigned int k = idx & 0x3FFFFFFF;
    if (k == 0)
        {
        int len; int s = r.Str(len);
        if (s < 0) return -1;
        if (aCopy) CopyStr(aCopy, aCap, r.b + s, len);
        int ni = aT ? aT->AddName(r.b + s, len) : -1;
        if (lb.n < KGbxMaxNames) lb.list[lb.n++] = ni;
        return ni;
        }
    if ((int)k - 1 < lb.n)
        {
        int ni = lb.list[k - 1];
        if (aCopy && aT && ni >= 0) { const char* s = aT->Name(ni); int l = 0; while (s[l]) l++; CopyStr(aCopy, aCap, (const unsigned char*)s, l); }
        return ni;
        }
    return -1;
    }

static void ReadPackDesc(TGbxReader& r)
    {
    unsigned int v = r.U8();
    if (v >= 3) r.Skip(32);
    int len; r.Str(len);
    if ((len > 0 && v >= 1) || v >= 3) { int l2; r.Str(l2); }
    }

static bool ReadSkinNode(TGbxReader& r)
    {
    unsigned int cid = r.U32();
    if (r.bad || (cid & 0xFFF) != 0) return false;
    for (int guard = 0; guard < 64; guard++)
        {
        unsigned int ch = r.U32();
        if (r.bad) return false;
        if (ch == 0xFACADE01) return true;
        unsigned int low = ch & 0xFFF;
        if (r.Peek32() == 0x534B4950)      // "PIKS": a skippable chunk
            {
            r.U32(); unsigned int sz = r.U32(); r.Skip(sz); continue;
            }
        int l;
        if (low == 0) { r.Str(l); r.Str(l); }
        else if (low == 1) { r.Str(l); ReadPackDesc(r); }
        else if (low == 2) { r.Str(l); ReadPackDesc(r); ReadPackDesc(r); }
        else return false;
        }
    return false;
    }

int TmGbxTrack::ParseBlocks(const unsigned char* aB, int aLen)
    {
    // chunk 0x0304301F (or its older class id 0x2400301F): searched for in the body, as the chunks before it
    // have layouts of their own; a false match fails its sanity checks and the search goes on
    static const unsigned char KKeyA[4] = { 0x1f, 0x30, 0x04, 0x03 };
    static const unsigned char KKeyB[4] = { 0x1f, 0x30, 0x00, 0x24 };
    TLookback* lb = new TLookback;
    if (!lb) return -20;
    TmGbxBlock* tmp = new TmGbxBlock[KGbxMaxBlocks];
    if (!tmp) { delete lb; return -21; }
    unsigned int seen[256]; int nseen;
    int res = -22;
    for (int i = 0; i + 8 <= aLen && res < 0; i++)
        {
        if (!((aB[i] == KKeyA[0] && aB[i + 1] == KKeyA[1] && aB[i + 2] == KKeyA[2] && aB[i + 3] == KKeyA[3]) ||
              (aB[i] == KKeyB[0] && aB[i + 1] == KKeyB[1] && aB[i + 2] == KKeyB[2] && aB[i + 3] == KKeyB[3]))) continue;
        TGbxReader r; r.Init(aB, aLen, i + 4);
        if (r.Peek32() == 3) r.U32();          // the lookback list's version word
        lb->n = 0;
        int savedNames = iNNames, savedPool = iPoolUsed;
        ReadLookback(r, *lb, this, iUid, sizeof(iUid)); ReadLookback(r, *lb, this); ReadLookback(r, *lb, this);
        int tl; int ts = r.Str(tl);
        ReadLookback(r, *lb, this); ReadLookback(r, *lb, this); ReadLookback(r, *lb, this);
        int sx = (int)r.U32(), sy = (int)r.U32(), sz = (int)r.U32();
        r.U32();                                 // need-unlock
        int version = (int)r.U32();
        unsigned int nb = r.U32();
        if (r.bad || ts < 0 || nb == 0 || nb >= 20000 || sx <= 0 || sx >= 256 || sz <= 0 || sz >= 256) { iNNames = savedNames; iPoolUsed = savedPool; continue; }
        int n = 0; nseen = 0; bool ok = true;
        for (unsigned int k = 0; k < nb && ok; k++)
            {
            int name = ReadLookback(r, *lb, this);
            unsigned int d = r.U8(), x = r.U8(), y = r.U8(), z = r.U8();
            unsigned int fl = version >= 1 ? r.U32() : r.U16();
            if (r.bad) { ok = false; break; }
            if (fl == 0xFFFFFFFF) continue;
            if (fl & 0x8000)
                {
                ReadLookback(r, *lb, this);      // the author
                unsigned int nr = r.U32();
                if (nr != 0xFFFFFFFF && nr != 0)
                    {
                    bool was = false;
                    for (int q = 0; q < nseen; q++) if (seen[q] == nr) was = true;
                    if (!was) { if (nseen < 256) seen[nseen++] = nr; if (!ReadSkinNode(r)) { ok = false; break; } }
                    }
                }
            if (version >= 2 && (fl & 0x100000))
                {
                unsigned int nr = r.U32();
                bool was = false;
                for (int q = 0; q < nseen; q++) if (seen[q] == nr) was = true;
                if (nr != 0xFFFFFFFF && nr != 0 && !was) { res = -23; ok = false; break; }   // inline parameters: not handled
                }
            if (r.bad || name < 0) { ok = false; break; }
            if (n >= KGbxMaxBlocks) { res = -24; ok = false; break; }
            TmGbxBlock& b = tmp[n++];
            b.iName = (unsigned short)name; b.iDir = (unsigned char)d; b.iX = (unsigned char)x; b.iY = (unsigned char)y; b.iZ = (unsigned char)z; b.iFlags = fl;
            }
        if (!ok) { iNNames = savedNames; iPoolUsed = savedPool; if (res == -23 || res == -24) break; continue; }
        CopyStr(iTitle, sizeof(iTitle), aB + ts, tl);
        iSize[0] = sx; iSize[1] = sy; iSize[2] = sz; iVersion = version;
        iBlocks = new TmGbxBlock[n > 0 ? n : 1];
        if (!iBlocks) { res = -25; break; }
        for (int k = 0; k < n; k++) iBlocks[k] = tmp[k];
        iNBlocks = n;
        res = 0;
        }
    delete[] tmp; delete lb;
    return res;
    }

// the reference table (fan tracks that use files of their own): read past it
static bool SkipFolders(TGbxReader& r, int aDepth)
    {
    unsigned int n = r.U32();
    if (r.bad || n > 1000 || aDepth > 16) return false;
    for (unsigned int i = 0; i < n; i++)
        {
        int l; r.Str(l);
        if (r.bad || !SkipFolders(r, aDepth + 1)) return false;
        }
    return true;
    }

int TmGbxTrack::Read(const unsigned char* aData, int aSize)
    {
    Free();
    iEnv[0] = iTitle[0] = iUid[0] = 0;
    iMedals[0] = iMedals[1] = iMedals[2] = iMedals[3] = 0;
    TGbxReader r; r.Init(aData, aSize, 0);
    if (aSize < 16 || aData[0] != 'G' || aData[1] != 'B' || aData[2] != 'X') return -1;
    r.p = 3;
    unsigned int ver = r.U16();
    if (ver < 3 || ver > 6) return -2;
    unsigned int fmt = r.U8(); r.U8(); unsigned int bodyC = r.U8();
    if (fmt != 'B') return -3;
    if (ver >= 4) r.U8();
    r.U32();                                 // the class id
    if (ver >= 6)
        {
        r.U32();                             // the header's size
        unsigned int n = r.U32();
        if (r.bad || n > 64) return -4;
        unsigned int ids[64], sizes[64];
        for (unsigned int i = 0; i < n; i++) { ids[i] = r.U32(); sizes[i] = r.U32() & 0x7FFFFFFF; }
        for (unsigned int i = 0; i < n; i++)
            {
            if (!r.Has((int)sizes[i])) return -5;
            if ((ids[i] == 0x03043002 || ids[i] == 0x24003002) && sizes[i] >= 21 && aData[r.p] >= 3)
                {
                // the medal times (pack.py medal_times): bronze, silver, gold, author
                const unsigned char* m = aData + r.p + 5;
                for (int k = 0; k < 4; k++) iMedals[3 - k] = m[k * 4] | (m[k * 4 + 1] << 8) | (m[k * 4 + 2] << 16) | (m[k * 4 + 3] << 24);
                }
            if (ids[i] == 0x03043005 || ids[i] == 0x24003005)
                {
                // the XML (after its own length word)
                const unsigned char* x = aData + r.p; int xl = sizes[i];
                XmlAttr(x, xl, "envir", iEnv, sizeof(iEnv));
                }
            r.p += sizes[i];
            }
        }
    r.U32();                                 // the node count
    unsigned int next = r.U32();             // the reference table
    if (r.bad) return -6;
    if (next > 0)
        {
        if (next > 10000) return -7;
        r.U32();                             // the ancestor level
        if (!SkipFolders(r, 0)) return -8;
        for (unsigned int i = 0; i < next; i++)
            {
            unsigned int fl = r.U32();
            int l;
            if ((fl & 4) == 0) r.Str(l); else r.U32();
            r.U32();                         // the node index
            if (ver >= 5) r.U32();           // use file
            if ((fl & 4) == 0) r.U32();      // the folder index
            if (r.bad) return -9;
            }
        }
    iPool = new char[KGbxNamePool];
    if (!iPool) return -10;
    int res;
    if (bodyC == 'C')
        {
        unsigned int usize = r.U32(), csize = r.U32();
        if (r.bad || usize > 8 * 1024 * 1024 || !r.Has((int)csize)) return -11;
        unsigned char* body = new unsigned char[usize + 1];
        if (!body) return -12;
        int got = TmLzoDecompress(aData + r.p, csize, body, usize);
        if (got != (int)usize) { delete[] body; return -13; }
        res = ParseBlocks(body, usize);
        delete[] body;
        }
    else
        res = ParseBlocks(aData + r.p, aSize - r.p);
    if (res < 0) { Free(); return res; }
    return 0;
    }
