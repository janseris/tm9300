/* Fast memory helpers: ARM assembly on Symbian ARM (armi) builds, C everywhere else. C89, no libc,
 * no writable static data (fine in an EKA1 .app / DLL).
 *
 *   fastmem.c      the assembly (compiled to nothing unless FM_ASM)
 *   fastmem_ref.c  the C versions (compiled to nothing when FM_ASM)
 * Add both to the .mmp (and to the PC test build). -DFM_NO_ASM forces the C versions. */

#ifndef FASTMEM_H
#define FASTMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__MARM__) && !defined(__MARM_THUMB__) && !defined(FM_NO_ASM)
#define FM_ASM
#endif

/* forward copy; overlapping is fine only when dst < src (like removing from an array) */
void fm_memcpy(void* dst, const void* src, unsigned int n);
/* n bytes of (c & 255) */
void fm_memset(void* dst, int c, unsigned int n);
/* 0 for NULL */
unsigned int fm_strlen(const char* s);
/* count 16-bit values (dst 2-byte aligned): a 16 bpp screen, a z-buffer */
void fm_fill16(unsigned short* dst, unsigned int value, unsigned int count);
/* tm9300's depth test (0.6: a depth value a pixel): a near polygon's run on one row: the pixels x0..x1 get the
   colour, their depths w >> 10 (16 bits), w advancing by dw a pixel from its value at x0. pix and z are the row's
   starts (the frame's stride and the depth row are even). w stays within 0..2^26 (the caller checks the corners) */
void fm_zrun(unsigned short* pix, unsigned short* z, int x0, int x1, int w, int dw, unsigned int color);
/* the gap x0..x1 of a near polygon's row (drawn already): a pixel whose depth is less than w >> 10 gets the colour
   and the depth */
void fm_zgap(unsigned short* pix, unsigned short* z, int x0, int x1, int w, int dw, unsigned int color);
/* all the rows of a near polygon with one run a row at most (r3_cover's pass 0): per row the full span (16.16
   ends, 2 ints a row), the run (2 ints a row; a run with its start past its end: none), the gap before the run,
   the run, the gap after. w advances by zB a row and zA a pixel; the pixels drawn are counted */
typedef struct FmZRows
{
    const int* spans;           /* row y0's full span: [0] left, [1] right (16.16; left > right: an empty row) */
    const int* runs;            /* row y0's run (the same form) */
    int y0, y1;                 /* the rows */
    unsigned short* pix;        /* row y0 of the frame */
    int pixStride;              /* bytes a row */
    unsigned short* z;          /* row y0 of the depths */
    int zStride;                /* bytes a row */
    int w;                      /* the depth (w * 2^8) at pixel 0 of row y0 */
    int zA, zB;                 /* its change a pixel, a row */
    unsigned int color;
    int wmax;                   /* the last pixel of a row */
    int pixels;                 /* out: the pixels of the runs */
} FmZRows;
void fm_zrows(FmZRows* p);
/* count 32-bit values (dst 4-byte aligned) */
void fm_fill32(unsigned int* dst, unsigned int value, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif
