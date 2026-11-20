// tmjpeg.cpp - a baseline JPEG decoder (0.19) for the maps' thumbnails: markers, Huffman tables, the blocks (DC
// prediction, AC run lengths, restart intervals), dequantised and turned back by an integer inverse DCT, the colour
// planes upsampled and converted to RGB565, cut and scaled to the size asked. Own code after ITU T.81.
#include "tmjpeg.h"

static const unsigned char KZigZag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

struct THuff
    {
    unsigned char sym[256];
    int mincode[17], maxcode[18], valptr[17];
    bool ok;
    };

struct TComp { int id, h, v, tq, td, ta, pred; int bw, bh; unsigned char* plane; };

struct TJpeg
    {
    const unsigned char* p; const unsigned char* end;
    unsigned int bits; int nbits; bool marker;
    unsigned short q[4][64];
    THuff dc[4], ac[4];
    TComp c[3]; int nc;
    int w, h, hmax, vmax, restart;
    int Byte()
        {
        if (marker || p >= end) return 0;
        int b = *p++;
        if (b == 0xFF)
            {
            int n = p < end ? *p : 0;
            if (n == 0) p++;
            else { marker = true; p--; return 0; }   // a marker: no more data bits
            }
        return b;
        }
    int Bit()
        {
        if (nbits == 0) { bits = Byte(); nbits = 8; }
        nbits--;
        return (bits >> nbits) & 1;
        }
    int Bits(int n) { int v = 0; while (n-- > 0) v = (v << 1) | Bit(); return v; }
    int Decode(const THuff& t)
        {
        int code = 0;
        for (int l = 1; l <= 16; l++)
            {
            code = (code << 1) | Bit();
            if (t.maxcode[l] >= 0 && code <= t.maxcode[l] && code >= t.mincode[l]) return t.sym[t.valptr[l] + code - t.mincode[l]];
            }
        return -1;
        }
    };

static int Extend(int v, int n) { return n == 0 ? 0 : (v < (1 << (n - 1)) ? v - (1 << n) + 1 : v); }

// the inverse DCT: rows then columns, 13-bit fixed point cosines (the plain separable form: 1,024 products a block)
static void Idct(const int* in, unsigned char* out, int stride)
    {
    static const int KC[8][8] = {   // round(cos((2x+1)u pi/16) * C(u) * 8192 / 2)
        { 2896, 4017, 3784, 3406, 2896, 2276, 1567, 799 },
        { 2896, 3406, 1567, -799, -2896, -4017, -3784, -2276 },
        { 2896, 2276, -1567, -4017, -2896, 799, 3784, 3406 },
        { 2896, 799, -3784, -2276, 2896, 3406, -1567, -4017 },
        { 2896, -799, -3784, 2276, 2896, -3406, -1567, 4017 },
        { 2896, -2276, -1567, 4017, -2896, -799, 3784, -3406 },
        { 2896, -3406, 1567, 799, -2896, 4017, -3784, 2276 },
        { 2896, -4017, 3784, -3406, 2896, -2276, 1567, -799 } };
    int tmp[64];
    for (int y = 0; y < 8; y++)
        {
        const int* r = in + y * 8;
        bool zero = true; for (int u = 1; u < 8; u++) if (r[u]) zero = false;
        for (int x = 0; x < 8; x++)
            {
            if (zero) { tmp[y * 8 + x] = r[0] * 2896 >> 3; continue; }
            int s = 0;
            for (int u = 0; u < 8; u++) s += r[u] * KC[x][u];
            tmp[y * 8 + x] = s >> 3;            // (scale 1024)
            }
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++)
            {
            long long s = 0;
            for (int v = 0; v < 8; v++) s += (long long)tmp[v * 8 + x] * KC[y][v];
            int val = (int)((s + (1LL << 22)) >> 23) + 128;   // (scale 1024 * 8192)
            out[y * stride + x] = (unsigned char)(val < 0 ? 0 : val > 255 ? 255 : val);
            }
    }

static bool BuildHuff(THuff& t, const unsigned char* counts, const unsigned char* syms, int nsym)
    {
    int k = 0, code = 0;
    for (int i = 0; i < nsym && i < 256; i++) t.sym[i] = syms[i];
    for (int l = 1; l <= 16; l++)
        {
        const int n = counts[l - 1];
        if (n) { t.valptr[l] = k; t.mincode[l] = code; code += n; k += n; t.maxcode[l] = code - 1; }
        else { t.maxcode[l] = -1; t.mincode[l] = 0; t.valptr[l] = 0; }
        code <<= 1;
        }
    t.ok = k <= 256;
    return t.ok;
    }

bool TmJpegDecode(const unsigned char* aData, int aLen, unsigned short* aOut, int aW, int aH, bool aFlipY)
    {
    if (aLen < 4 || aData[0] != 0xFF || aData[1] != 0xD8 || aW <= 0 || aH <= 0) return false;
    TJpeg* j = new TJpeg;
    if (!j) return false;
    j->nc = 0; j->restart = 0; j->w = j->h = 0;
    for (int i = 0; i < 4; i++) { j->dc[i].ok = j->ac[i].ok = false; }
    for (int i = 0; i < 3; i++) j->c[i].plane = 0;
    const unsigned char* p = aData + 2; const unsigned char* end = aData + aLen;
    bool ok = false, scan = false;
    while (p + 4 <= end && !scan)
        {
        if (p[0] != 0xFF) { p++; continue; }
        const int m = p[1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
        if (m == 0xD9) break;
        const int len = (p[2] << 8) | p[3];
        const unsigned char* s = p + 4; const unsigned char* se = p + 2 + len;
        if (se > end || len < 2) break;
        if (m == 0xDB)
            {
            while (s < se)
                {
                const int pq = s[0] >> 4, tq = s[0] & 3; s++;
                for (int k = 0; k < 64; k++) { j->q[tq][k] = pq ? (s[k * 2] << 8) | s[k * 2 + 1] : s[k]; }
                s += pq ? 128 : 64;
                }
            }
        else if (m == 0xC4)
            {
            while (s + 17 <= se)
                {
                const int tc = s[0] >> 4, th = s[0] & 3;
                int n = 0; for (int k = 0; k < 16; k++) n += s[1 + k];
                if (s + 17 + n > se) break;
                BuildHuff(tc ? j->ac[th] : j->dc[th], s + 1, s + 17, n);
                s += 17 + n;
                }
            }
        else if (m == 0xC0 || m == 0xC1)
            {
            if (s[0] != 8) break;
            j->h = (s[1] << 8) | s[2]; j->w = (s[3] << 8) | s[4]; j->nc = s[5];
            if (j->nc != 1 && j->nc != 3) break;
            j->hmax = j->vmax = 1;
            for (int k = 0; k < j->nc; k++)
                {
                TComp& c = j->c[k];
                c.id = s[6 + k * 3]; c.h = s[7 + k * 3] >> 4; c.v = s[7 + k * 3] & 15; c.tq = s[8 + k * 3] & 3;
                if (c.h < 1 || c.h > 2 || c.v < 1 || c.v > 2) { j->nc = 0; break; }
                if (c.h > j->hmax) j->hmax = c.h;
                if (c.v > j->vmax) j->vmax = c.v;
                }
            }
        else if (m == 0xC2 || m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) break;   // not baseline
        else if (m == 0xDD) j->restart = (s[0] << 8) | s[1];
        else if (m == 0xDA)
            {
            const int ns = s[0];
            for (int k = 0; k < ns; k++)
                for (int i = 0; i < j->nc; i++)
                    if (j->c[i].id == s[1 + k * 2]) { j->c[i].td = s[2 + k * 2] >> 4 & 3; j->c[i].ta = s[2 + k * 2] & 3; }
            p = se; scan = ns == j->nc;
            break;
            }
        p = se;
        }
    if (scan && j->nc && j->w > 0 && j->h > 0 && j->w <= 2048 && j->h <= 2048)
        {
        const int mw = 8 * j->hmax, mh = 8 * j->vmax;
        const int mx = (j->w + mw - 1) / mw, my = (j->h + mh - 1) / mh;
        bool mem = true;
        for (int i = 0; i < j->nc; i++)
            {
            TComp& c = j->c[i];
            c.bw = mx * c.h * 8; c.bh = my * c.v * 8; c.pred = 0;
            c.plane = new unsigned char[c.bw * c.bh];
            if (!c.plane) mem = false;
            }
        int* blk = new int[64];
        if (mem && blk)
            {
            j->p = p; j->end = end; j->bits = 0; j->nbits = 0; j->marker = false;
            bool bad = false;
            int left = j->restart;
            for (int y = 0; y < my && !bad; y++)
                for (int x = 0; x < mx && !bad; x++)
                    {
                    if (j->restart && left == 0)
                        {
                        // a restart marker: the bits realigned, the predictions reset
                        j->nbits = 0; j->marker = false;
                        while (j->p + 1 < end && !(j->p[0] == 0xFF && j->p[1] >= 0xD0 && j->p[1] <= 0xD7)) j->p++;
                        if (j->p + 1 < end) j->p += 2;
                        for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
                        left = j->restart;
                        }
                    if (j->restart) left--;
                    for (int i = 0; i < j->nc && !bad; i++)
                        {
                        TComp& c = j->c[i];
                        const THuff& hd = j->dc[c.td]; const THuff& ha = j->ac[c.ta];
                        if (!hd.ok || !ha.ok) { bad = true; break; }
                        for (int by = 0; by < c.v && !bad; by++)
                            for (int bx = 0; bx < c.h && !bad; bx++)
                                {
                                for (int k = 0; k < 64; k++) blk[k] = 0;
                                int t = j->Decode(hd);
                                if (t < 0 || t > 11) { bad = true; break; }
                                c.pred += Extend(j->Bits(t), t);
                                { int v = c.pred * j->q[c.tq][0]; blk[0] = v < -32767 ? -32767 : v > 32767 ? 32767 : v; }
                                for (int k = 1; k < 64; )
                                    {
                                    const int rs = j->Decode(ha);
                                    if (rs < 0) { bad = true; break; }
                                    const int r = rs >> 4, sz = rs & 15;
                                    if (sz == 0) { if (r == 15) { k += 16; continue; } break; }
                                    k += r;
                                    if (k > 63) { bad = true; break; }
                                    int v = Extend(j->Bits(sz), sz) * j->q[c.tq][k];
                                    blk[KZigZag[k]] = v < -32767 ? -32767 : v > 32767 ? 32767 : v;   // (a damaged file's: kept in range)
                                    k++;
                                    }
                                Idct(blk, c.plane + ((y * c.v + by) * 8) * c.bw + (x * c.h + bx) * 8, c.bw);
                                }
                        }
                    }
            if (!bad)
                {
                // the picture's middle at aW:aH, sampled into the output (each output pixel the mean of the 2 x 2
                // source pixels nearest its middle)
                int cw = j->w, ch = j->h;
                if (cw * aH > ch * aW) cw = ch * aW / aH; else ch = cw * aH / aW;
                const int x0 = (j->w - cw) / 2, y0 = (j->h - ch) / 2;
                for (int oy = 0; oy < aH; oy++)
                    {
                    int sy = y0 + (oy * ch + ch / 2) / aH;
                    if (aFlipY) sy = j->h - 1 - sy;
                    for (int ox = 0; ox < aW; ox++)
                        {
                        const int sx = x0 + (ox * cw + cw / 2) / aW;
                        int rgb[3];
                        int Y = 0, Cb = 128, Cr = 128;
                        for (int i = 0; i < j->nc; i++)
                            {
                            const TComp& c = j->c[i];
                            int px = sx * c.h / j->hmax, py = sy * c.v / j->vmax;
                            int px1 = px + 1 < c.bw ? px + 1 : px, py1 = py + 1 < c.bh ? py + 1 : py;
                            const int v = (c.plane[py * c.bw + px] + c.plane[py * c.bw + px1] + c.plane[py1 * c.bw + px] + c.plane[py1 * c.bw + px1] + 2) >> 2;
                            if (i == 0) Y = v; else if (i == 1) Cb = v; else Cr = v;
                            }
                        if (j->nc == 1) Cb = Cr = 128;
                        rgb[0] = Y + ((91881 * (Cr - 128)) >> 16);
                        rgb[1] = Y - ((22554 * (Cb - 128) + 46802 * (Cr - 128)) >> 16);
                        rgb[2] = Y + ((116130 * (Cb - 128)) >> 16);
                        for (int k = 0; k < 3; k++) rgb[k] = rgb[k] < 0 ? 0 : rgb[k] > 255 ? 255 : rgb[k];
                        aOut[oy * aW + ox] = (unsigned short)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
                        }
                    }
                ok = true;
                }
            }
        delete[] blk;
        }
    for (int i = 0; i < 3; i++) delete[] j->c[i].plane;
    delete j;
    return ok;
    }
