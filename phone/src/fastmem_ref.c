/* The C versions of fastmem.h, for every build without FM_ASM (the PC, emulator, THUMB). */

#include "fastmem.h"

#ifndef FM_ASM

void fm_memcpy(void* dst, const void* src, unsigned int n)
{
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
}

void fm_memset(void* dst, int c, unsigned int n)
{
    char* d = (char*)dst;
    while (n--) *d++ = (char)c;
}

unsigned int fm_strlen(const char* s)
{
    unsigned int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

void fm_fill16(unsigned short* dst, unsigned int value, unsigned int count)
{
    while (count--) *dst++ = (unsigned short)value;
}

void fm_fill32(unsigned int* dst, unsigned int value, unsigned int count)
{
    while (count--) *dst++ = value;
}

void fm_zrun(unsigned short* pix, unsigned short* z, int x0, int x1, int w, int dw, unsigned int color)
{
    int x = x0;
    /* a depth a pixel pair (the pair's first pixel's for both), as the assembly; an odd first pixel alone */
    if ((x & 1) && x <= x1) { pix[x] = (unsigned short) color; z[x] = (unsigned short) ((unsigned int) w >> 10); w += dw; x++; }
    for (; x + 1 <= x1; x += 2, w += 2 * dw) { pix[x] = pix[x + 1] = (unsigned short) color; z[x] = z[x + 1] = (unsigned short) ((unsigned int) w >> 10); }
    if (x <= x1) { pix[x] = (unsigned short) color; z[x] = (unsigned short) ((unsigned int) w >> 10); }
}

void fm_zgap(unsigned short* pix, unsigned short* z, int x0, int x1, int w, int dw, unsigned int color)
{
    int x;
    for (x = x0; x <= x1; x++, w += dw)
    {
        unsigned int zv = (unsigned int) w >> 10;
        if (zv > z[x]) { z[x] = (unsigned short) zv; pix[x] = (unsigned short) color; }
    }
}

void fm_zrows(FmZRows* p)
{
    const int* sv = p->spans; const int* rn = p->runs;
    unsigned short* pix = p->pix; unsigned short* z = p->z;
    int w = p->w, y, pixels = 0;
    for (y = p->y0; y <= p->y1; y++, sv += 2, rn += 2, w += p->zB, pix = (unsigned short*) ((char*) pix + p->pixStride), z = (unsigned short*) ((char*) z + p->zStride))
    {
        int xa, xb, ra, rb;
        if (sv[0] > sv[1]) continue;
        xa = (sv[0] + 0xffff) >> 16; xb = sv[1] >> 16;
        if (xa < 0) xa = 0;
        if (xb > p->wmax) xb = p->wmax;
        if (xa > xb) continue;
        ra = xa; rb = xa - 1;
        if (rn[0] <= rn[1])
        {
            ra = (rn[0] + 0xffff) >> 16; rb = rn[1] >> 16;
            if (ra < xa) ra = xa;
            if (rb > xb) rb = xb;
        }
        if (xa < ra) fm_zgap(pix, z, xa, ra - 1, w + p->zA * xa, p->zA, p->color);
        if (ra <= rb) { fm_zrun(pix, z, ra, rb, w + p->zA * ra, p->zA, p->color); pixels += rb - ra + 1; }
        if (rb < xb) fm_zgap(pix, z, rb + 1, xb, w + p->zA * (rb + 1), p->zA, p->color);
    }
    p->pixels = pixels;
}

#endif
