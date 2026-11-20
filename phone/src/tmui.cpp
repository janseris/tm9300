// tmui.cpp - the menus after TrackMania United Forever's (see tmui.h). Plain C++: no library calls.
#include "tmui.h"

// ---- small helpers (no C library on the phone) ----
static int SLen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void SCpy(char* d, const char* s, int aMax) { int i = 0; while (s[i] && i < aMax) { d[i] = s[i]; i++; } d[i] = 0; }
static char* SCat(char* d, const char* s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }
static char* SNum(char* d, int v)
    {
    while (*d) d++;
    if (v < 0) { *d++ = '-'; v = -v; }
    char t[12]; int n = 0; do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0; return d;
    }
static int ISqrt(int a) { if (a <= 0) return 0; int r = 0, b = 1 << 30; while (b > a) b >>= 2; while (b) { if (a >= r + b) { a -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

int TmFont::Width(const char* aText) const
    {
    int w = 0;
    for (; *aText; aText++) { int c = (unsigned char) *aText - 32; if (c < 0 || c >= 95) c = 0; w += iW[c]; }
    return w;
    }

// ---- the pictures' pack ----
bool TmFontLoad(const unsigned char* d, int n, TmFont& f)
    {
    if (n < 16 + 760 || d[0] != 'T' || d[1] != 'M' || d[2] != 'F' || d[3] != 'T') return false;
    const unsigned char* p = d + 4;
    f.iH = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
    f.iAsc = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
    f.iStripW = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
    for (int i = 0; i < 190; i++, p += 4) { const int v = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); if (i < 95) f.iX[i] = v; else f.iW[i - 95] = v; }
    if (f.iH <= 0 || f.iStripW <= 0 || n < 16 + 760 + f.iStripW * f.iH) return false;
    f.iMask = (unsigned char*) p;
    return true;
    }

bool TmFontScale(const TmFont& src, int h, bool bold, TmFont& out)
    {
    if (h < 4) return false;
    const int sh = src.iH;
    out.iH = h; out.iAsc = (src.iAsc * h + sh / 2) / sh;
    int x = 0;
    for (int c = 0; c < 95; c++)
        {
        // a glyph: its picture (the sheet's cell less its 2-pixel bearings) made the height's size, then a gap
        // (the bearings, at least a pixel): rounded once for the whole cell, small type spread unevenly
        int w;
        if (!c) { w = (src.iW[c] * h + sh / 2) / sh; if (w < 2) w = 2; }
        else
            {
            int gap = (4 * h + sh / 2) / sh; if (gap < 1) gap = 1;
            int b = ((src.iW[c] - 4) * h + sh / 2) / sh; if (b < 1) b = 1;
            w = b + gap + (bold ? 1 : 0);
            }
        out.iX[c] = x; out.iW[c] = w; x += w;
        }
    out.iStripW = x;
    out.iMask = new unsigned char[x * h];
    if (!out.iMask) return false;
    for (int i = 0; i < x * h; i++) out.iMask[i] = 0;
    // each target pixel: the sheet's pixels under it, weighted by how much of each it covers (1/256 pixel units)
    for (int c = 1; c < 95; c++)
        {
        int gap = (4 * h + sh / 2) / sh; if (gap < 1) gap = 1;
        const int sw = src.iW[c] - 4, tw = out.iW[c] - gap - (bold ? 1 : 0);
        const unsigned char* sm = src.iMask + src.iX[c] + 2;
        unsigned char* om = out.iMask + out.iX[c];
        for (int ty = 0; ty < h; ty++)
            {
            const int y0 = (ty * sh * 256) / h, y1 = ((ty + 1) * sh * 256) / h;
            for (int tx = 0; tx < tw; tx++)
                {
                const int x0 = (tx * sw * 256) / tw, x1 = ((tx + 1) * sw * 256) / tw;
                int sum = 0, area = 0;
                for (int sy = y0 >> 8; sy < sh && (sy << 8) < y1; sy++)
                    {
                    const int wy = (y1 < ((sy + 1) << 8) ? y1 : ((sy + 1) << 8)) - (y0 > (sy << 8) ? y0 : (sy << 8));
                    for (int sx = x0 >> 8; sx < sw && (sx << 8) < x1; sx++)
                        {
                        const int wx = (x1 < ((sx + 1) << 8) ? x1 : ((sx + 1) << 8)) - (x0 > (sx << 8) ? x0 : (sx << 8));
                        sum += sm[sy * src.iStripW + sx] * ((wx * wy) >> 8); area += (wx * wy) >> 8;
                        }
                    }
                int v = area ? sum / area : 0;
                v = (v + ISqrt(v * 255)) >> 1;          // the thin strokes a little darker
                om[ty * out.iStripW + tx] = (unsigned char) (v > 255 ? 255 : v);
                }
            if (bold) for (int tx = tw; tx > 0; tx--) { unsigned char* q = om + ty * out.iStripW + tx; if (q[-1] > q[0]) q[0] = q[-1]; }
            }
        }
    return true;
    }

static bool SEq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int Rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static int Rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
bool TmUiPack::Load(const unsigned char* d, int len)
    {
    iN = 0;
    if (len < 8 || d[0] != 'T' || d[1] != 'M' || d[2] != 'U' || d[3] != 'I') return false;
    const int n = Rd32(d + 4);
    if (n < 0 || n > KMax || 8 + n * 24 > len) return false;
    for (int i = 0; i < n; i++)
        {
        const unsigned char* e = d + 8 + i * 24;
        for (int k = 0; k < 16; k++) iNames[i][k] = (char) e[k];
        iNames[i][16] = 0;
        TmImage& m = iImg[i];
        m.w = Rd16(e + 16); m.h = Rd16(e + 18) & 0x7fff;
        const bool opaque = (Rd16(e + 18) & 0x8000) != 0;      // no alpha kept
        const int off = Rd32(e + 20);
        if (off < 0 || off + m.w * m.h * (opaque ? 2 : 3) > len || (off & 1)) return false;
        m.px = (const TmPix*) (d + off); m.a = opaque ? 0 : d + off + m.w * m.h * 2;
        }
    iN = n;
    return true;
    }
const TmImage* TmUiPack::Find(const char* aName) const
    {
    for (int i = 0; i < iN; i++) if (SEq(iNames[i], aName)) return &iImg[i];
    return 0;
    }

// ---- drawing ----
namespace TmDraw
{
// the pack for Icon, Coin and Led: on the phone in the thread's slot (a program there may not have writable
// static data), on the PC a plain static
#ifdef __SYMBIAN32__
}
#include <e32std.h>
namespace TmDraw
{
void SetPack(const TmUiPack* aPack) { Dll::SetTls((TAny*) aPack); }
static inline const TmUiPack* Pack() { return (const TmUiPack*) Dll::Tls(); }
#else
static const TmUiPack* gPackP = 0;
void SetPack(const TmUiPack* aPack) { gPackP = aPack; }
static inline const TmUiPack* Pack() { return gPackP; }
#endif
TmPix Rgb(int r, int g, int b)
    {
    if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (b < 0) b = 0; if (b > 255) b = 255;
    return (TmPix) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
TmPix Mix(TmPix a, TmPix b, int t)
    {
    if (t <= 0) return a; if (t >= 256) return b;
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (TmPix) (((ar + (((br - ar) * t) >> 8)) << 11) | ((ag + (((bg - ag) * t) >> 8)) << 5) | (ab + (((bb - ab) * t) >> 8)));
    }
void Fill(TmPix* f, int s, int x, int y, int w, int h, TmPix c)
    {
    for (int j = 0; j < h; j++) { TmPix* p = f + (y + j) * s + x; for (int i = 0; i < w; i++) p[i] = c; }
    }
void Shade(TmPix* f, int s, int x, int y, int w, int h, int t, TmPix c)
    {
    for (int j = 0; j < h; j++) { TmPix* p = f + (y + j) * s + x; for (int i = 0; i < w; i++) p[i] = Mix(p[i], c, t); }
    }
// a rounded box with a vertical gradient
void Round(TmPix* f, int s, int x, int y, int w, int h, int r, TmPix top, TmPix bot)
    {
    for (int j = 0; j < h; j++)
        {
        int in = 0;
        if (j < r) { int d = r - j; in = r - ISqrt(r * r - (d - 1) * (d - 1)); }
        else if (j >= h - r) { int d = j - (h - r) + 1; in = r - ISqrt(r * r - (d - 1) * (d - 1)); }
        TmPix c = Mix(top, bot, h > 1 ? (j * 256) / (h - 1) : 0);
        TmPix* p = f + (y + j) * s + x;
        for (int i = in; i < w - in; i++) p[i] = c;
        }
    }
void RoundLine(TmPix* f, int s, int x, int y, int w, int h, int r, TmPix c)
    {
    for (int j = 0; j < h; j++)
        {
        int in = 0, in2 = 0;
        if (j < r) { int d = r - j; in = r - ISqrt(r * r - (d - 1) * (d - 1)); int d2 = r - j - 1; in2 = d2 > 0 ? r - ISqrt(r * r - (d2 - 1) * (d2 - 1)) : 0; }
        else if (j >= h - r) { int d = j - (h - r) + 1; in = r - ISqrt(r * r - (d - 1) * (d - 1)); int d2 = d - 1; in2 = d2 > 0 ? r - ISqrt(r * r - (d2 - 1) * (d2 - 1)) : 0; }
        TmPix* p = f + (y + j) * s + x;
        if (j == 0 || j == h - 1) { for (int i = in; i < w - in; i++) p[i] = c; continue; }
        for (int i = in; i <= in2 && i < w; i++) { p[i] = c; p[w - 1 - i] = c; }
        p[in] = c; p[w - 1 - in] = c;
        }
    }
static void Glyph(TmPix* f, int s, const TmFont& fn, int x, int y, int g, TmPix c, int maxX)
    {
    const int w = fn.iW[g];
    for (int j = 0; j < fn.iH; j++)
        {
        const unsigned char* m = fn.iMask + j * fn.iStripW + fn.iX[g];
        TmPix* p = f + (y + j) * s + x;
        for (int i = 0; i < w && x + i < maxX; i++)
            {
            const int a = m[i];
            if (a >= 250) p[i] = c; else if (a > 8) p[i] = Mix(p[i], c, a);
            }
        }
    }
int Text(TmPix* f, int s, const TmFont& fn, int x, int y, const char* t, TmPix c, int maxX)
    {
    for (; *t; t++)
        {
        int g = (unsigned char) *t - 32; if (g < 0 || g >= 95) g = 0;
        if (x + fn.iW[g] > maxX) break;
        Glyph(f, s, fn, x, y, g, c, maxX);
        x += fn.iW[g];
        }
    return x;
    }
void TextCentre(TmPix* f, int s, const TmFont& fn, int x0, int x1, int y, const char* t, TmPix c)
    {
    Text(f, s, fn, x0 + ((x1 - x0) - fn.Width(t)) / 2, y, t, c, x1);
    }
static int Cell(const TmFont& fn) { int w = 0; for (int d = 16; d < 26; d++) if (fn.iW[d] > w) w = fn.iW[d]; return w; }
int ClockWidth(const TmFont& fn, const char* t)
    {
    const int cell = Cell(fn); int w = 0;
    for (; *t; t++) { int g = (unsigned char) *t - 32; if (g < 0 || g >= 95) g = 0; w += (*t >= '0' && *t <= '9') ? cell : fn.iW[g]; }
    return w;
    }
void Clock(TmPix* f, int s, const TmFont& fn, int x, int y, const char* t, TmPix c)
    {
    const int cell = Cell(fn);
    for (; *t; t++)
        {
        int g = (unsigned char) *t - 32; if (g < 0 || g >= 95) g = 0;
        if (*t >= '0' && *t <= '9') { Glyph(f, s, fn, x + (cell - fn.iW[g]) / 2, y, g, c, 640); x += cell; }
        else { Glyph(f, s, fn, x, y, g, c, 640); x += fn.iW[g]; }
        }
    }
void FormatTime(char* o, int ms)
    {
    if (ms < 0) ms = 0;
    const int cs = ms / 10;
    o[0] = 0; SNum(o, cs / 6000); SCat(o, ":");
    const int sec = (cs / 100) % 60, cc = cs % 100;
    char* e = o + SLen(o); *e++ = (char) ('0' + sec / 10); *e++ = (char) ('0' + sec % 10); *e++ = '.'; *e++ = (char) ('0' + cc / 10); *e++ = (char) ('0' + cc % 10); *e = 0;
    }
void Blit(TmPix* f, int s, int x, int y, const TmImage* m, int cy0, int cy1)
    {
    if (!m) return;
    for (int j = 0; j < m->h; j++)
        {
        const int yy = y + j;
        if (yy < cy0 || yy >= cy1) continue;
        const TmPix* sp = m->px + j * m->w;
        TmPix* p = f + yy * s + x;
        if (!m->a) { for (int i = 0; i < m->w; i++) p[i] = sp[i]; continue; }
        const unsigned char* sa = m->a + j * m->w;
        for (int i = 0; i < m->w; i++)
            {
            const int a = sa[i];
            if (a >= 250) p[i] = sp[i]; else if (a > 6) p[i] = Mix(p[i], sp[i], a);
            }
        }
    }
static const char* LedName(char c, char* b)
    {
    b[0] = 0;
    if (c >= '0' && c <= '9') { SCat(b, "led_"); char d[2] = { c, 0 }; SCat(b, d); }
    else if (c == ':') SCat(b, "led_colon"); else if (c == '.') SCat(b, "led_dot"); else if (c == '-') SCat(b, "led_minus"); else if (c == '+') SCat(b, "led_plus");
    return b;
    }
int LedWidth(const char* t)
    {
    int w = 0; char b[16];
    for (; *t; t++) { const TmImage* m = Pack() ? Pack()->Find(LedName(*t, b)) : 0; w += m ? m->w + 1 : 8; }
    return w;
    }
int Led(TmPix* f, int s, int x, int y, const char* t)
    {
    char b[16];
    for (; *t; t++) { const TmImage* m = Pack() ? Pack()->Find(LedName(*t, b)) : 0; if (m) { Blit(f, s, x, y, m); x += m->w + 1; } else x += 8; }
    return x;
    }
// a medal: a coin with a rim, lit from the upper left (the game's coin from the pack when there is one)
void Coin(TmPix* f, int s, int cx, int cy, int r, int kind)
    {
    if (Pack() && kind != 4)
        {
        static const char* const KC[6] = { "nadeo", "gold", "silver", "bronze", "", "none" };
        char b[24]; b[0] = 0; SCat(b, r >= 8 ? "m20_" : "m13_"); SCat(b, KC[kind < 0 || kind > 5 ? 5 : kind]);
        const TmImage* m = Pack()->Find(b);
        if (m) { Blit(f, s, cx - m->w / 2, cy - m->h / 2, m); return; }
        }
    static const int col[6][3] = { { 90, 200, 90 }, { 236, 214, 120 }, { 214, 214, 214 }, { 196, 160, 104 }, { 90, 150, 230 }, { 70, 80, 86 } };
    const int* k = col[kind < 0 || kind > 5 ? 5 : kind];
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            {
            const int d2 = i * i + j * j;
            if (d2 > r * r) continue;
            const int d = ISqrt(d2 * 256) ;                  // * 16
            int l = 200 - ((i + j) * 60) / r;                 // light from the upper left
            if (d > (r - 2) * 16) l = 140;                    // the rim
            else if (d > (r - 3) * 16) l = 255;
            if (kind == 5) l = (l * 3) / 4;
            TmPix* p = f + (cy + j) * s + cx + i;
            *p = Rgb((k[0] * l) >> 8, (k[1] * l) >> 8, (k[2] * l) >> 8);
            }
    // the mark across the middle (the game's coin has a logo there)
    if (kind < 5 && r >= 6) Fill(f, s, cx - r / 2, cy - 1, r, 2, Rgb((k[0] * 140) >> 8, (k[1] * 140) >> 8, (k[2] * 140) >> 8));
    }
static void Disc(TmPix* f, int s, int cx, int cy, int r, TmPix hi, TmPix lo)
    {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) f[(cy + j) * s + cx + i] = Mix(hi, lo, ((i + j + 2 * r) * 256) / (4 * r + 1));
    }
static void Person(TmPix* f, int s, int x, int y, TmPix hi, TmPix lo)
    {
    Disc(f, s, x + 6, y + 4, 4, hi, lo);
    for (int j = 0; j < 9; j++)
        {
        int half = 3 + (j < 4 ? j : 4) + (j > 6 ? 1 : 0);
        for (int i = -half; i <= half; i++) f[(y + 10 + j) * s + x + 6 + i] = Mix(hi, lo, ((i + half) * 256) / (2 * half + 1) / 2 + j * 14);
        }
    }
static void Flag(TmPix* f, int s, int x, int y, TmPix a, TmPix b)
    {
    // a chequered flag in a wave, its pole on the left
    for (int j = 0; j < 14; j++)
        for (int i = 0; i < 18; i++)
            {
            const int wy = j + ((i * 5 / 18) - (i > 9 ? (i - 9) / 2 : 0));
            if (wy < 0 || wy >= 16) continue;
            const bool on = ((i / 3) + (j / 3)) & 1;
            f[(y + 2 + j + (i * 3) / 18) * s + x + 4 + i] = on ? a : b;
            }
    Fill(f, s, x + 2, y + 2, 2, 19, Rgb(60, 60, 60));
    }
void Icon(TmPix* f, int s, int x, int y, int kind)
    {
    if (Pack())
        {
        static const char* const KI[] = { "i_solo", "i_party", "i_quit", "i_back", "i_flagw", "i_flagg", "i_flagb", "i_flagr", "i_flagk",
            "i_lock", "i_lan", "", "", "i_carbig", "i_online", "i_editors", "i_profile", "i_united", "i_nations", "i_star", "i_mycamp",
            "i_browse", "i_races", "i_platforms", "i_stunts", "i_puzzle", "i_medals" };
        if (kind >= 0 && kind < (int) (sizeof(KI) / sizeof(KI[0])) && KI[kind][0])
            {
            const TmImage* m = Pack()->Find(KI[kind]);
            if (m) { Blit(f, s, x, y, m); return; }
            }
        }
    const TmPix green = Rgb(90, 250, 70), dgreen = Rgb(10, 120, 20);
    switch (kind)
        {
        case EIcoPlayer: Person(f, s, x + 6, y + 1, green, dgreen); break;
        case EIcoFolder:
            {
            // 0.19: a folder: its tab, its body
            const TmPix y1 = Rgb(255, 214, 90), y2 = Rgb(200, 140, 30);
            Round(f, s, x + 2, y + 4, 10, 6, 2, y1, y1);
            Round(f, s, x + 2, y + 7, 21, 14, 2, y1, y2);
            Fill(f, s, x + 3, y + 9, 19, 1, Rgb(255, 236, 160));
            break;
            }
        case EIcoDrive:
            {
            // 0.19: a drive (the phone's memory, the card): a card with its contacts
            Round(f, s, x + 4, y + 3, 17, 19, 2, Rgb(170, 180, 190), Rgb(90, 100, 110));
            for (int k = 0; k < 4; k++) Fill(f, s, x + 7 + k * 3, y + 4, 2, 5, Rgb(250, 210, 80));
            Fill(f, s, x + 7, y + 13, 11, 5, Rgb(60, 140, 230));
            break;
            }
        case EIcoParty: Person(f, s, x + 1, y + 1, green, dgreen); Person(f, s, x + 11, y + 3, green, dgreen); break;
        case EIcoQuit: case EIcoBack:
            {
            // a red arrow pointing left, a block's depth under it
            const TmPix r1 = Rgb(255, 80, 80), r2 = Rgb(170, 20, 30);
            for (int j = 0; j < 13; j++)
                {
                const int half = j < 6 ? j : 12 - j;
                for (int i = 0; i <= half; i++) f[(y + 4 + j) * s + x + 3 + 6 - i] = Mix(r1, r2, j * 18);
                }
            Fill(f, s, x + 9, y + 7, 14, 7, r1); Fill(f, s, x + 9, y + 14, 14, 3, r2);
            if (kind == EIcoQuit) Fill(f, s, x + 20, y + 4, 4, 13, r2);
            break;
            }
        case EIcoFlagWhite: Flag(f, s, x, y, Rgb(250, 250, 250), Rgb(150, 150, 150)); break;
        case EIcoFlagGreen: Flag(f, s, x, y, Rgb(80, 230, 60), Rgb(20, 120, 30)); break;
        case EIcoFlagBlue: Flag(f, s, x, y, Rgb(80, 140, 255), Rgb(20, 50, 170)); break;
        case EIcoFlagRed: Flag(f, s, x, y, Rgb(255, 80, 70), Rgb(160, 20, 20)); break;
        case EIcoFlagBlack: Flag(f, s, x, y, Rgb(90, 90, 90), Rgb(20, 20, 20)); break;
        case EIcoLock:
            {
            const TmPix g1 = Rgb(250, 220, 90), g2 = Rgb(170, 130, 30);
            for (int a = 0; a < 3; a++) for (int j = -7; j <= 0; j++) for (int i = -7; i <= 7; i++) { int d = i * i + j * j; if (d <= 49 && d >= 25 - a * 0) f[(y + 9 + j) * s + x + 12 + i] = Rgb(200, 200, 210); }
            Round(f, s, x + 3, y + 8, 19, 13, 3, g1, g2);
            Fill(f, s, x + 6, y + 14, 13, 2, Rgb(120, 90, 20));
            break;
            }
        case EIcoLan: Person(f, s, x + 4, y + 1, green, dgreen); Fill(f, s, x + 2, y + 19, 20, 2, Rgb(80, 120, 200)); break;
        case EIcoRally: Disc(f, s, x + 12, y + 11, 8, Rgb(150, 210, 90), Rgb(60, 110, 40)); Fill(f, s, x + 5, y + 12, 15, 3, Rgb(150, 100, 60)); break;
        case EIcoSnow: for (int j = 0; j < 16; j++) { int half = j / 2; for (int i = -half; i <= half; i++) f[(y + 3 + j) * s + x + 12 + i] = j < 6 ? Rgb(250, 250, 255) : Rgb(70, 130, 90); } break;
        default: break;
        }
    }
}   // TmDraw

using namespace TmDraw;

// ---- the menu ----
static const char* const KSeriesName[5] = { "White", "Green", "Blue", "Red", "Black" };
// the environments (TmUiTrack::iEnv) and their rows' order in the game's grid: Stadium, Island, Desert | Rally, Bay,
// Coast | Snow (three a page)
static const char* const KEnvName[7] = { "Rally", "Snow", "Desert", "Island", "Bay", "Coast", "Stadium" };
static const int KEnvRank[7] = { 3, 6, 2, 1, 4, 5, 0 };
enum { LEFT_W = 196, RIGHT_X = 204 };

TmMenu::TmMenu() : iDirty(true), iScreen(ETitle), iVersion(""), iRaceFile(-1), iRaceOpp(EOppGold), iPlayers(1), iAttempts(3), iHsMode(0), iTimeLimit(300), iNProfiles(0), iProfilesChanged(false), iSeriesN(0),
    iLockMsg(false), iPack(0), iBig(0), iText(0), iSmall(0), iNTracks(0), iBg(0), iInGrid(false), iGridCol(0), iGridRow(0), iNEnvs(0), iOppSel(0), iHsRow(2), iChooser(false), iChooserSel(0), iRowH(24), iPlateH(29), iEditing(-1), iSeriesSel(0), iTitleTick(0), iBrowseSel(0), iOppFrom(ESolo), iEntries(0), iNEntries(0), iBrowseHot(false), iNPick(0)
    {
    iFolders = 0; iBrowsePath[0] = 0; iHotPath[0] = 0;
    for (int i = 0; i < 10; i++) iSel[i] = 0;
    for (int p = 0; p < KMaxPlayers; p++) iNames[p][0] = 0;
    }
TmMenu::~TmMenu() { delete[] iBg; delete[] iEntries; }

bool TmMenu::Init(const TmFont* aBig, const TmFont* aText, const TmFont* aSmall, const TmUiTrack* aTracks, int aN, const TmUiPack* aPack)
    {
    iBig = aBig; iText = aText; iSmall = aSmall; iPack = aPack;
    // the environments with a track installed, in the game's order
    iNEnvs = 0;
    for (int rank = 0; rank < 7; rank++)
        for (int e = 0; e < 7; e++)
            if (KEnvRank[e] == rank) for (int i = 0; i < aN; i++) if (aTracks[i].iEnv == e) { iEnvs[iNEnvs++] = e; break; }
    // 0.14: the left list's sizes from the fonts (the phone's may be bigger than the PC's): the title plate holds the
    // title and its line under it, a row its text with room
    iPlateH = iBig->iH + iSmall->iH - 3; if (iPlateH < 29) iPlateH = 29; if (iPlateH > 44) iPlateH = 44;
    iRowH = iText->iH + 7; if (iRowH < 22) iRowH = 22;
    SetPack(aPack);
    iNTracks = aN < KMaxTracks ? aN : KMaxTracks;
    for (int i = 0; i < iNTracks; i++) iTracks[i] = aTracks[i];
    if (!iBg) iBg = new TmPix[640 * 200];
    if (!iBg) return false;
    DrawBackground();
    iDirty = true;
    return true;
    }

void TmMenu::SetBest(int aFile, int aSteps)
    {
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == aFile && (!iTracks[i].iBest || aSteps < iTracks[i].iBest)) iTracks[i].iBest = aSteps;
    iDirty = true;
    }

int TmMenu::Medals() const
    {
    int n = 0;
    for (int i = 0; i < iNTracks; i++)
        {
        const TmUiTrack& t = iTracks[i];
        if (!t.iBest) continue;
        const int ms = t.iBest * 20;
        for (int k = 0; k < 4; k++) if (t.iMedal[k] && ms <= t.iMedal[k]) n++;
        }
    return n;
    }

// the background: the menus' blue light with the glass globe on it (the land a noise of darker teal)
static int Hash(int x, int y) { unsigned h = (unsigned) (x * 374761393 + y * 668265263); h = (h ^ (h >> 13)) * 1274126177u; return (int) ((h ^ (h >> 16)) & 255); }
static int Noise(int x, int y, int cell)
    {
    const int cx = x / cell, cy = y / cell, fx = ((x % cell) * 256) / cell, fy = ((y % cell) * 256) / cell;
    const int a = Hash(cx, cy), b = Hash(cx + 1, cy), c = Hash(cx, cy + 1), d = Hash(cx + 1, cy + 1);
    const int ab = a + (((b - a) * fx) >> 8), cd = c + (((d - c) * fx) >> 8);
    return ab + (((cd - ab) * fy) >> 8);
    }
void TmMenu::DrawBackground()
    {
    const TmImage* bg = iPack ? iPack->Find("bg") : 0;
    if (bg && bg->w == 640 && bg->h == 200) { for (int i = 0; i < 640 * 200; i++) iBg[i] = bg->px[i]; return; }
    const int gx = 420, gy = 104, gr = 94;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 640; x++)
            {
            const int dx = x - gx, dy = (y - gy) * 2;
            int t = (ISqrt(dx * dx + dy * dy) * 256) / 360; if (t > 256) t = 256;
            TmPix c = Mix(Rgb(70, 180, 235), Rgb(8, 45, 70), t);
            const int ex = x - gx, ey = y - gy, e2 = ex * ex + ey * ey;
            if (e2 <= gr * gr)
                {
                const int edge = (ISqrt(e2) * 256) / gr;
                const int n = Noise(x + 1000, y * 2 + 1000, 22) + (Noise(x + 300, y * 2 + 500, 9) >> 2) - 32;
                const TmPix sea = Mix(Rgb(70, 160, 190), Rgb(20, 90, 130), edge), land = Mix(Rgb(40, 130, 150), Rgb(15, 80, 110), edge);
                c = n > 150 ? land : sea;
                if (n > 150 && n < 158) c = Mix(c, Rgb(200, 240, 250), 90);
                if (edge > 236) c = Mix(c, Rgb(210, 250, 255), (edge - 236) * 12);
                const int sx = ex + gr / 3, sy = ey + gr / 3;
                if (sx * sx + sy * sy * 3 < (gr * gr) / 3 && ey > -gr / 2 && ex < 0) c = Mix(c, Rgb(240, 255, 255), 70);
                }
            iBg[y * 640 + x] = c;
            }
    }

void TmMenu::DrawItem(TmPix* f, int s, int y, int icon, const char* text, bool sel, bool dim)
    {
    const int h = iRowH - 2;
    Round(f, s, 4, y, LEFT_W - 8, h, 4, Rgb(16, 30, 40), Rgb(8, 18, 24));
    RoundLine(f, s, 4, y, LEFT_W - 8, h, 4, sel ? Rgb(40, 90, 200) : Rgb(26, 44, 56));
    if (sel) RoundLine(f, s, 5, y + 1, LEFT_W - 10, h - 2, 3, Rgb(30, 60, 150));
    Icon(f, s, 9, y + (h - 24) / 2, icon);
    if (dim) Shade(f, s, 9, y, 24, h - 1, 140, Rgb(10, 20, 26));
    Text(f, s, *iText, 42, y + (h - iText->iH) / 2, text, dim ? Rgb(90, 104, 114) : (sel ? Rgb(90, 170, 255) : Rgb(240, 240, 240)), LEFT_W - 6);
    }

void TmMenu::DrawLeft(TmPix* f, int s, const char* title, const char* sub, int icon)
    {
    Fill(f, s, 0, 0, LEFT_W + 2, 200, Rgb(10, 22, 30));
    Fill(f, s, LEFT_W, 0, 2, 200, Rgb(20, 40, 52));
    Round(f, s, 3, 2, LEFT_W - 6, iPlateH, 6, Rgb(40, 80, 110), Rgb(14, 40, 62));
    RoundLine(f, s, 3, 2, LEFT_W - 6, iPlateH, 6, Rgb(120, 160, 190));
    Text(f, s, *iBig, 10, 2, title, Rgb(255, 255, 255), LEFT_W - 40);
    Text(f, s, *iSmall, 12, 2 + iPlateH - iSmall->iH - 1, sub, Rgb(60, 160, 255));
    Icon(f, s, LEFT_W - 40, 2 + (iPlateH - 30) / 2, icon);
    }

// the items of the list screens, as the game's: what the phone cannot do is there, greyed
int TmMenu::Items(TScreen sc, TItem* it) const
    {
    int n = 0;
    #define ITEM(icon, text, on, y) { it[n].iIcon = icon; it[n].iText = text; it[n].iOn = on; it[n].iRow = y; n++; }
    switch (sc)
        {
        case EMain:
            ITEM(EIcoPlayer, "Play Solo", true, 0) ITEM(EIcoOnline, "Play Online", false, 1) ITEM(EIcoParty, "Party Mode", true, 2)
            ITEM(EIcoEditors, "Editors", false, -3) ITEM(EIcoProfile, "Profile", false, -2) ITEM(EIcoQuit, "Quit", true, -1)
            break;
        case ECampaign:
            ITEM(EIcoUnited, "United", true, 0) ITEM(EIcoNations, "Nations", false, 1) ITEM(EIcoStar, "Star Track", false, 2)
            ITEM(EIcoMyCampaign, "My Campaign", false, 3) ITEM(EIcoBrowse, "Browse", true, -2) ITEM(EIcoBack, "Back", true, -1)
            break;
        case EMode:
            ITEM(EIcoRaces, "Races", true, 0) ITEM(EIcoPlatforms, "Platforms", false, 1) ITEM(EIcoStunts, "Stunts", false, 2)
            ITEM(EIcoPuzzle, "Puzzle", false, 3) ITEM(EIcoBack, "Back", true, -1)
            break;
        case ESolo: case EOpponent:
            for (int k = 0; k < 5; k++) ITEM(EIcoFlagWhite + k, KSeriesName[k], true, k)
            ITEM(EIcoBack, "Back", true, -1)
            break;
        case EParty:
            ITEM(EIcoParty, "Hot Seat", true, 0) ITEM(EIcoLan, "Local network", false, 1) ITEM(EIcoBack, "Back", true, -1)
            break;
        default: break;
        }
    #undef ITEM
    return n;
    }

void TmMenu::DrawList(TmPix* f, int s, TScreen sc, bool aFocus)
    {
    TItem it[10]; const int n = Items(sc, it);
    for (int i = 0; i < n; i++) DrawItem(f, s, RowY(it[i].iRow), it[i].iIcon, it[i].iText, aFocus && iSel[sc] == i, !it[i].iOn);
    }

// a row's top: from the title plate down (0, 1, ..), or from the foot up (-1 the last, -2 over it, a gap between)
int TmMenu::RowY(int aRow) const
    {
    if (aRow >= 0) return iPlateH + 5 + aRow * iRowH;
    return 200 - 2 - (-aRow) * iRowH - (aRow < -1 ? 4 : 0);
    }

int TmMenu::ListKey(TScreen sc, int aKey)
    {
    TItem it[10]; const int n = Items(sc, it);
    int& sel = iSel[sc];
    if (aKey == EUiUp || aKey == EUiDown)
        {
        const int step = aKey == EUiUp ? n - 1 : 1;
        for (int k = 0; k < n; k++) { sel = (sel + step) % n; if (it[sel].iOn) break; }
        return -1;
        }
    if (aKey == EUiBack) return -2;
    if (aKey == EUiOk || aKey == EUiRight) return it[sel].iOn ? sel : -1;
    return -1;
    }

void TmMenu::DrawMedalsPanel(TmPix* f, int s)
    {
    // the green-titled panel: the medals won (the game's "world ranking by medals")
    const int x = RIGHT_X + 30, w = 640 - x - 8;
    Round(f, s, x, 3, w, 38, 6, Rgb(46, 66, 78), Rgb(30, 46, 56));
    RoundLine(f, s, x, 3, w, 38, 6, Rgb(110, 140, 150));
    Round(f, s, x + 4, 6, w - 8, iSmall->iH + 3, 4, Rgb(50, 160, 50), Rgb(26, 110, 26));
    TextCentre(f, s, *iSmall, x + 4, x + w - 4, 7, "Your ranking by medals", Rgb(255, 255, 255));
    const int y = 10 + iSmall->iH;
    Round(f, s, x + 6, y, 90, 41 - y, 4, Rgb(26, 36, 42), Rgb(20, 28, 34));
    Icon(f, s, x + 10, y + (41 - y - 24) / 2, EIcoMedals);
    char b[16]; b[0] = 0; SNum(b, Medals());
    Text(f, s, *iText, x + 44, y + (41 - y - iText->iH) / 2, b, Rgb(255, 255, 255));
    }

// the game's campaign: each series' first track is open; the next one opens with a medal on the one before it in
// the same series: bronze on White (A), silver on Green and Blue (B, C), gold on Red (D) (a track before it that is
// not installed does not hold it shut); Black (E) opens with gold on every track of White to Red
static const int KNeedMedal[5] = { 3, 2, 2, 1, 1 };
static const char* const KNeedMsg[5] = { "To unlock this track you need the Nadeo medal on the previous track",
    "To unlock this track you need a gold medal on the previous track", "To unlock this track you need a silver medal on the previous track",
    "To unlock this track you need a bronze medal on the previous track", "To unlock this track you need a gold medal on all the previous tracks" };
static bool HasMedal(const TmUiTrack& t, int aMedal)
    {
    const int m = t.iMedal[aMedal];
    return t.iBest > 0 && (m <= 0 || t.iBest * 20 <= m);
    }

int TmMenu::PrevTrack(int ti) const
    {
    int best = -1;
    for (int i = 0; i < iNTracks; i++)
        if (iTracks[i].iEnv == iTracks[ti].iEnv && iTracks[i].iSeries == iTracks[ti].iSeries && iTracks[i].iNum < iTracks[ti].iNum && (best < 0 || iTracks[i].iNum > iTracks[best].iNum)) best = i;
    return best;
    }

bool TmMenu::Unlocked(int ti) const
    {
    if (iTracks[ti].iSeries > 4) return true;          // 0.18: a fan track
    if (iTracks[ti].iSeries == 4)
        {
        for (int i = 0; i < iNTracks; i++) if (iTracks[i].iEnv == iTracks[ti].iEnv && iTracks[i].iSeries < 4 && !HasMedal(iTracks[i], 1)) return false;
        return true;
        }
    const int p = PrevTrack(ti);
    return p < 0 || HasMedal(iTracks[p], KNeedMedal[iTracks[p].iSeries]);
    }

bool TmMenu::FileUnlocked(int aFile) const
    {
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == aFile) return Unlocked(i);
    return false;
    }

int TmMenu::NextFile(int aFile) const
    {
    int ti = -1;
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == aFile) ti = i;
    if (ti < 0 || iTracks[ti].iSeries > 4) return -1;
    const int key = iTracks[ti].iSeries * 10 + iTracks[ti].iNum;
    int best = -1, bestKey = 1 << 30;
    for (int i = 0; i < iNTracks; i++)
        {
        if (iTracks[i].iEnv != iTracks[ti].iEnv) continue;
        const int k = iTracks[i].iSeries * 10 + iTracks[i].iNum;
        if (k > key && k < bestKey) { bestKey = k; best = i; }
        }
    return best >= 0 ? iTracks[best].iFile : -1;
    }

int TmMenu::TrackAt(int aSeries, int aEnv, int aNum) const
    {
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iSeries == aSeries && iTracks[i].iEnv == aEnv && iTracks[i].iNum == aNum) return i;
    return -1;
    }
int TmMenu::SeriesCount(int aEnv, int aSeries) const
    {
    int n = 0; for (int i = 0; i < iNTracks; i++) if (iTracks[i].iEnv == aEnv && iTracks[i].iSeries == aSeries) n++;
    return n;
    }

// the tracks of the chosen colour: a row per environment, five each (E: one); not installed: a padlock
void TmMenu::DrawGrid(TmPix* f, int s)
    {
    const int series = iSel[ESolo] < 5 ? iSel[ESolo] : 0;
    // the installed environments' rows, three a page as the game's; the page with the selection, its arrows at the foot
    const int page = iGridRow / 3, rows = iNEnvs - page * 3 < 3 ? iNEnvs - page * 3 : 3;
    const int x0 = RIGHT_X + 6, y0 = 46, cw = 84, gap = 5;
    const int arrowH = iNEnvs > 3 ? 12 : 0;
    int ch = rows > 0 ? (200 - y0 - 3 - arrowH) / (rows > 2 ? 3 : 2) - gap : 64; if (ch > 64) ch = 64;
    if (iNEnvs > 3)
        {
        // the game's two round buttons: up (a page before), down (a page after)
        const int ax = x0 + (5 * cw) / 2, ay = 200 - arrowH / 2 - 2;
        for (int k = 0; k < 2; k++)
            {
            const bool on = k ? (page + 1) * 3 < iNEnvs : page > 0;
            const int cx = ax + (k ? 8 : -8);
            Round(f, s, cx - 5, ay - 5, 11, 11, 5, on ? Rgb(240, 240, 240) : Rgb(120, 130, 140), on ? Rgb(190, 190, 190) : Rgb(90, 100, 110));
            if (on) for (int j = 0; j < 4; j++) Fill(f, s, cx - j, k ? ay + 2 - j : ay - 2 + j, 2 * j + 1, 1, Rgb(20, 20, 20));
            }
        }
    for (int r = 0; r < rows; r++)
        for (int n = 1; n <= 5; n++)
            {
            const int row = page * 3 + r, env = iEnvs[row];
            const int x = x0 + (n - 1) * cw, y = y0 + r * (ch + gap);
            const int ti = TrackAt(series, env, n);
            const bool sel = iInGrid && iGridRow == row && iGridCol == n - 1;
            Round(f, s, x, y, cw - 4, ch, 4, sel ? Rgb(255, 240, 170) : Rgb(220, 226, 230), sel ? Rgb(220, 180, 70) : Rgb(170, 178, 184));
            if (ti < 0)
                {
                Round(f, s, x + 3, y + 3, cw - 10, ch - 6, 3, Rgb(50, 110, 140), Rgb(26, 70, 96));
                Icon(f, s, x + (cw - 4) / 2 - 15, y + ch / 2 - 15, EIcoLock);
                continue;
                }
            const TmUiTrack& t = iTracks[ti];
            const int tw = cw - 10, th = ch - 6 - iSmall->iH - 1;
            if (!Unlocked(ti))
                {
                // not open yet, as the game's: a padlock and "(the conditions)" (Enter shows them), no picture, no name
                Round(f, s, x + 3, y + 3, tw, th, 3, Rgb(70, 72, 96), Rgb(40, 42, 60));
                Icon(f, s, x + (cw - 4) / 2 - 15, y + (th - 30 - iSmall->iH) / 2 + 3 > y + 1 ? y + (th - 30 - iSmall->iH) / 2 + 3 : y + 1, EIcoLock);
                TextCentre(f, s, *iSmall, x + 3, x + 3 + tw, y + th + 2 - iSmall->iH, "(conditions)", Rgb(200, 204, 220));
                continue;
                }
            if (t.iThumb)
                for (int j = 0; j < th; j++)
                    {
                    const int sy = (j * t.iThumbH) / th;
                    for (int i = 0; i < tw; i++) f[(y + 3 + j) * s + x + 3 + i] = t.iThumb[sy * t.iThumbW + (i * t.iThumbW) / tw];
                    }
            else Fill(f, s, x + 3, y + 3, tw, th, Rgb(60, 90, 60));
            // the medal won, on the picture's corner
            if (t.iBest)
                {
                const int ms = t.iBest * 20; int k = -1;
                for (int m = 3; m >= 0; m--) if (t.iMedal[m] && ms <= t.iMedal[m]) k = m;
                // each medal won, stacked as the game's: the lowest at the back, the best in front at the corner
                if (k >= 0) for (int m = 3; m >= k; m--) Coin(f, s, x + tw - 8 - (m - k) * 5, y + th - 6 - (m - k), 9, m);
                }
            char name[24]; name[0] = 0; SCat(name, KEnvName[env]);
            char l[2] = { "ABCDE"[series], 0 }; SCat(name, l);
            if (series < 4) SNum(name, n);
            TextCentre(f, s, *iSmall, x, x + cw - 4, y + ch - iSmall->iH - 2, name, Rgb(20, 24, 28));
            }
    }

// the opponents: the medals (the author's run slowed to each medal's time), the record, none
void TmMenu::OppRows(int* aRows, int& aN) const
    {
    aN = 0;
    bool fan = false;
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == iRaceFile && iTracks[i].iSeries > 4) fan = true;
    // (0.18: a fan track has no author's run: the medals' ghosts are made from it)
    if (!fan) { aRows[aN++] = EOppNadeo; aRows[aN++] = EOppGold; aRows[aN++] = EOppSilver; aRows[aN++] = EOppBronze; }
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == iRaceFile && iTracks[i].iBest) { aRows[aN++] = EOppRecord; break; }
    aRows[aN++] = EOppNone;
    }
void TmMenu::DrawOpponent(TmPix* f, int s)
    {
    Shade(f, s, 0, 0, 640, 200, 170, 0);
    const int w = 300, x = (640 - w) / 2;
    int rows[8], n; OppRows(rows, n);
    const int hh = iText->iH + 4;
    int rh = iText->iH + 8; if (rh < 22) rh = 22; if (hh + 7 + n * rh + 6 > 198) rh = (198 - hh - 13) / n;
    const int h = hh + 7 + n * rh + 6, y = (200 - h) / 2;
    Round(f, s, x, y, w, h, 6, Rgb(40, 42, 44), Rgb(28, 30, 32));
    RoundLine(f, s, x, y, w, h, 6, Rgb(90, 96, 100));
    Round(f, s, x + 4, y + 3, w - 8, hh, 5, Rgb(50, 170, 50), Rgb(20, 110, 20));
    TextCentre(f, s, *iText, x, x + w, y + 3 + (hh - iText->iH) / 2, "Choose an opponent", Rgb(255, 255, 255));
    const TmUiTrack* t = 0;
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == iRaceFile) t = &iTracks[i];
    for (int r = 0; r < n; r++)
        {
        const int ry = y + hh + 7 + r * rh;
        const bool sel = r == iOppSel;
        Round(f, s, x + 16, ry, w - 32, rh - 3, (rh - 3) / 2, sel ? Rgb(90, 96, 100) : Rgb(40, 42, 44), sel ? Rgb(60, 64, 68) : Rgb(32, 34, 36));
        int coin = 5, ms = 0; const char* name = "None";
        switch (rows[r])
            {
            case EOppNadeo: coin = 0; name = "Nadeo medal"; ms = t ? t->iMedal[0] : 0; break;
            case EOppGold: coin = 1; name = "Gold medal"; ms = t ? t->iMedal[1] : 0; break;
            case EOppSilver: coin = 2; name = "Silver medal"; ms = t ? t->iMedal[2] : 0; break;
            case EOppBronze: coin = 3; name = "Bronze medal"; ms = t ? t->iMedal[3] : 0; break;
            case EOppRecord: coin = 4; name = "Your record"; ms = t ? t->iBest * 20 : 0; break;
            default: break;
            }
        Coin(f, s, x + 40, ry + (rh - 3) / 2, rh / 2 - 2 < 10 ? rh / 2 - 2 : 10, coin);
        Text(f, s, *iText, x + 60, ry + (rh - 3 - iText->iH) / 2, name, sel ? Rgb(90, 170, 255) : Rgb(240, 240, 240));
        if (ms)
            {
            char b[16]; FormatTime(b, ms);
            Round(f, s, x + 180, ry + 4, 90, rh - 11, 6, Rgb(80, 90, 100), Rgb(60, 70, 80));
            Clock(f, s, *iSmall, x + 188, ry + 4 + (rh - 11 - iSmall->iH) / 2, b, Rgb(200, 210, 220));
            }
        }
    }

// the hot seat as the game's: the settings (the mode: attempts or time, and its value), the eight players' slots
// (Enter: a profile from the list, or a new one), Start; sized from the fonts to fit the 200 lines
void TmMenu::DrawHotSeat(TmPix* f, int s)
    {
    const int x = RIGHT_X + 10, w = 640 - x - 6;
    const int lh = iSmall->iH + 2;
    const int setH = 4 + lh + 2 * lh + 4;
    Round(f, s, x, 2, w, setH, 6, Rgb(50, 70, 82), Rgb(34, 50, 60));
    RoundLine(f, s, x, 2, w, setH, 6, Rgb(110, 140, 150));
    Round(f, s, x + 4, 4, w - 8, lh, 4, Rgb(110, 120, 126), Rgb(80, 90, 96));
    TextCentre(f, s, *iSmall, x, x + w, 5, "Settings", Rgb(255, 255, 255));
    char b[32];
    for (int r = 0; r < 2; r++)
        {
        const int ry = 4 + lh + r * lh + 2;
        const bool on = iHsRow == r;
        Text(f, s, *iSmall, x + w / 2 - 8 - iSmall->Width(r ? (iHsMode ? "Time limit" : "Attempts") : "Game mode"), ry, r ? (iHsMode ? "Time limit" : "Attempts") : "Game mode", Rgb(240, 240, 240));
        b[0] = 0;
        if (on) SCat(b, "< ");
        if (r == 0) SCat(b, iHsMode ? "Time" : "Attempts");
        else if (iHsMode) { char t[12]; t[0] = 0; SNum(t, iTimeLimit / 60); SCat(t, ":"); SCat(t, iTimeLimit % 60 < 10 ? "0" : ""); SNum(t, iTimeLimit % 60); SCat(b, t); }
        else SNum(b, iAttempts);
        if (on) SCat(b, " >");
        if (on) Fill(f, s, x + w / 2, ry - 1, 100, lh, Rgb(30, 50, 70));
        Text(f, s, *iSmall, x + w / 2 + 4, ry, b, r ? Rgb(240, 200, 60) : Rgb(60, 140, 255));
        }
    // the players: two columns of four, as tall as the room left
    const int startH = iText->iH + 4;
    const int py = 2 + setH + 3, ph = 200 - py - startH - 4;
    Round(f, s, x, py, w, ph, 6, Rgb(50, 70, 82), Rgb(34, 50, 60));
    Round(f, s, x + 4, py + 2, w - 8, lh, 4, Rgb(30, 160, 200), Rgb(10, 110, 150));
    TextCentre(f, s, *iSmall, x, x + w, py + 3, "Players", Rgb(255, 255, 255));
    const int cw = (w - 12) / 2, chh = (ph - lh - 8) / 4;
    const TmFont& nf = chh - 3 >= iText->iH + 2 ? *iText : *iSmall;
    for (int p = 0; p < KMaxPlayers; p++)
        {
        const int cx = x + 4 + (p & 1) * (cw + 4), cy = py + lh + 5 + (p >> 1) * chh;
        const bool sel = iHsRow == p + 2 && !iChooser;
        Round(f, s, cx, cy, cw, chh - 2, 3, sel ? Rgb(255, 250, 220) : Rgb(236, 236, 236), sel ? Rgb(230, 220, 170) : Rgb(214, 214, 214));
        if (iNames[p][0])
            {
            TextCentre(f, s, nf, cx + 4, cx + cw - 10, cy + (chh - 2 - nf.iH) / 2, iNames[p], Rgb(90, 90, 90));
            Text(f, s, *iSmall, cx + cw - 9, cy + 1, "x", Rgb(20, 20, 20));
            }
        else TextCentre(f, s, *iSmall, cx, cx + cw, cy + (chh - 2 - iSmall->iH) / 2, "Enter: add a player", Rgb(30, 90, 220));
        }
    const bool st = iHsRow == 10 && !iChooser;
    Round(f, s, x + w / 2 - 70, 200 - startH - 2, 140, startH, startH / 2, st ? Rgb(255, 250, 210) : Rgb(240, 240, 240), st ? Rgb(220, 210, 160) : Rgb(200, 200, 200));
    TextCentre(f, s, *iText, x + w / 2 - 70, x + w / 2 + 70, 200 - startH - 2 + 2, "Start", Rgb(30, 30, 30));
    if (iChooser) DrawChooser(f, s);
    }

// the profiles not in a slot yet, then "Create a new profile" (-1) and "Cancel" (-2)
void TmMenu::ChooserRows(int* aRows, int& aN) const
    {
    aN = 0;
    for (int i = 0; i < iNProfiles; i++)
        {
        bool used = false;
        for (int p = 0; p < KMaxPlayers; p++) { const char* a = iNames[p]; const char* b = iProfiles[i]; while (*a && *a == *b) { a++; b++; } if (iNames[p][0] && *a == *b) used = true; }
        if (!used) aRows[aN++] = i;
        }
    aRows[aN++] = -1; aRows[aN++] = -2;
    }

void TmMenu::DrawChooser(TmPix* f, int s)
    {
    int rows[KMaxProfiles + 2], n; ChooserRows(rows, n);
    const int w = 260, x = RIGHT_X + (640 - RIGHT_X - w) / 2;
    const int rh = iText->iH + 6, headH = iText->iH + 5;
    int vis = (196 - headH - 10) / rh; if (vis > n) vis = n;
    const int h = headH + 8 + vis * rh + 4, y = (200 - h) / 2;
    int first = iChooserSel - vis + 1; if (first < 0) first = 0; if (first > n - vis) first = n - vis;
    Round(f, s, x, y, w, h, 6, Rgb(40, 42, 44), Rgb(28, 30, 32));
    RoundLine(f, s, x, y, w, h, 6, Rgb(90, 96, 100));
    Round(f, s, x + 4, y + 3, w - 8, headH, 5, Rgb(12, 12, 14), Rgb(4, 4, 6));
    TextCentre(f, s, *iText, x, x + w, y + 3 + (headH - iText->iH) / 2, "Choose a profile", Rgb(255, 255, 255));
    for (int k = 0; k < vis; k++)
        {
        const int r = first + k, ry = y + headH + 7 + k * rh;
        const bool on = r == iChooserSel;
        const char* t = rows[r] >= 0 ? iProfiles[rows[r]] : rows[r] == -1 ? (iEditing >= 0 ? "" : "Create a new profile") : "Cancel";
        Round(f, s, x + 10, ry, w - 20, rh - 2, (rh - 2) / 2, on ? Rgb(255, 255, 255) : Rgb(226, 226, 226), on ? Rgb(200, 220, 255) : Rgb(190, 190, 190));
        if (rows[r] >= 0) Text(f, s, *iSmall, x + w - 22, ry + 1, "x", Rgb(20, 20, 20));
        if (rows[r] == -1 && iEditing >= 0)
            {
            // a new profile's name being typed
            char nm[KNameLen + 3]; nm[0] = 0; SCat(nm, iNames[KMaxPlayers - 1 < iEditing ? 0 : iEditing]); SCat(nm, "_");
            TextCentre(f, s, *iText, x + 10, x + w - 10, ry + (rh - 2 - iText->iH) / 2, nm, Rgb(30, 90, 220));
            }
        else TextCentre(f, s, *iText, x + 10, x + w - 10, ry + (rh - 2 - iText->iH) / 2, t, on ? Rgb(30, 90, 220) : Rgb(40, 40, 40));
        }
    if (first > 0) TextCentre(f, s, *iSmall, x, x + w, y + headH + 3, "^", Rgb(200, 200, 200));
    if (first + vis < n) TextCentre(f, s, *iSmall, x, x + w, y + h - iSmall->iH, "v", Rgb(200, 200, 200));
    }

void TmMenu::DrawSeries(TmPix* f, int s)
    {
    // 0.19: as the game's: a panel, "Available campaigns" on its orange plate, a row a series (the environment's
    // mark, the series, its tracks), as many rows as fit, the page under them
    const TmPix orange1 = Rgb(240, 160, 30), orange2 = Rgb(200, 110, 10);
    const int x = LEFT_W + 30, w = 640 - x - 16;
    Round(f, s, x, 2, w, 196, 6, Rgb(64, 30, 26), Rgb(36, 14, 14));
    RoundLine(f, s, x, 2, w, 196, 6, Rgb(120, 70, 60));
    const int hh = iText->iH + 4;
    Round(f, s, x + 4, 5, w - 8, hh, 5, orange1, orange2);
    Text(f, s, *iText, x + 12, 7, "Available campaigns", Rgb(255, 255, 255));
    int count = 0; for (int e = 0; e < iNEnvs; e++) for (int k = 0; k < 5; k++) if (SeriesCount(iEnvs[e], k)) count++;
    char b[24]; b[0] = 0; SNum(b, count);
    Text(f, s, *iText, x + w - 12 - iText->Width(b), 7, b, Rgb(255, 255, 255));
    const int nrows = iNEnvs * 5;
    const int ly = 5 + hh + 4, rh = iText->iH + 4, foot = iSmall->iH + 4;
    const int per = (196 - ly - foot) / rh > 0 ? (196 - ly - foot) / rh : 1;
    Fill(f, s, x + 4, ly - 1, w - 8, per * rh + 2, Rgb(44, 40, 36));
    const int page = iSeriesSel < nrows ? iSeriesSel / per : 0, pages = (nrows + per - 1) / per;
    static const unsigned char KEnvCol[7][3] = { { 70, 150, 40 }, { 120, 170, 230 }, { 220, 170, 80 }, { 40, 170, 150 }, { 120, 120, 130 }, { 230, 120, 40 }, { 60, 90, 200 } };
    for (int r = 0; r < per && page * per + r < nrows; r++)
        {
        const int row = page * per + r;
        const int e = iEnvs[row / 5], k = row % 5, n = SeriesCount(e, k);
        const int y = ly + r * rh;
        const bool sel = row == iSeriesSel;
        const TmPix blue = Rgb(40, 140, 255);
        Fill(f, s, x + 8, y, w - 16, rh - 2, sel ? Rgb(36, 52, 70) : Rgb(212, 212, 212));
        Fill(f, s, x + 8, y, 4, rh - 2, sel ? blue : Rgb(80, 80, 80));
        if (sel) { Fill(f, s, x + 8, y, w - 16, 1, blue); Fill(f, s, x + 8, y + rh - 3, w - 16, 1, blue); Fill(f, s, x + w - 9, y, 1, rh - 2, blue); }
        const TmPix ec = Rgb(KEnvCol[e][0], KEnvCol[e][1], KEnvCol[e][2]);
        Round(f, s, x + 16, y + 2, rh - 6, rh - 6, 3, ec, Mix(ec, 0, 90));
        const TmPix tc = sel ? blue : n ? Rgb(40, 44, 48) : Rgb(150, 150, 150);
        char nm[32]; nm[0] = 0; SCat(nm, KEnvName[e]); SCat(nm, " "); SCat(nm, KSeriesName[k]);
        Text(f, s, *iText, x + 16 + rh, y + (rh - 2 - iText->iH) / 2, nm, tc);
        char tr[24]; tr[0] = 0; SNum(tr, n); SCat(tr, n == 1 ? " track" : " tracks");
        Text(f, s, *iText, x + w - 20 - iText->Width(tr), y + (rh - 2 - iText->iH) / 2, tr, tc);
        }
    // the page
    char pg[16]; pg[0] = 0; SNum(pg, page + 1); SCat(pg, " / "); SNum(pg + SLen(pg), pages);
    TextCentre(f, s, *iSmall, x, x + w, 196 - foot + 1, pg, Rgb(60, 140, 255));
    }

void TmMenu::DrawTitle(TmPix* f, int s)
    {
    // the start-up: the loading screen's picture (or a sky over a snow slope), the game's logo, the orange bar
    const TmImage* pic = iPack ? iPack->Find("title") : 0;
    if (pic && pic->w == 640 && pic->h == 200) Blit(f, s, 0, 0, pic);
    else for (int y = 0; y < 200; y++)
        {
        const TmPix c = y < 110 ? Mix(Rgb(40, 70, 130), Rgb(200, 220, 240), (y * 256) / 110) : Mix(Rgb(235, 238, 242), Rgb(120, 130, 140), ((y - 110) * 256) / 90);
        Fill(f, s, 0, y, 640, 1, c);
        }
    if (!pic) for (int i = 0; i < 640; i++) { int h = 105 + ((Noise(i, 7, 40) - 128) >> 3); for (int y = h; y < 112; y++) f[y * s + i] = Rgb(235, 240, 245); }
    const TmImage* logo = iPack ? iPack->Find("logo") : 0;
    // the logo where the game has it, bottom right, and the phone's name under it
    if (logo) Blit(f, s, 640 - logo->w - 6, 200 - logo->h - iSmall->iH - 6, logo);
    else Text(f, s, *iBig, 400, 150, "TrackMania United", Rgb(255, 255, 255));
    Text(f, s, *iSmall, 640 - 8 - iSmall->Width("for the Nokia 9300"), 200 - iSmall->iH - 3, "for the Nokia 9300", Rgb(60, 140, 230));
    const int bh = iText->iH + 4 > 16 ? iText->iH + 4 : 16;
    Round(f, s, 100, 128, 440, bh + 6, (bh + 6) / 2, Rgb(60, 60, 64), Rgb(20, 20, 24));
    Round(f, s, 104, 131, 432, bh, bh / 2, Rgb(255, 190, 60), Rgb(220, 120, 0));
    TextCentre(f, s, *iText, 104, 536, 131 + (bh - iText->iH) / 2, iNTracks ? "Press any key" : "No tracks installed", Rgb(255, 255, 255));
    char v[24]; v[0] = 0; SCat(v, "v"); SCat(v, iVersion);
    Text(f, s, *iSmall, 6, 200 - iSmall->iH - 3, v, Rgb(200, 210, 230));
    }

// 0.18: the fan tracks (converted from any Challenge.Gbx of the five environments on the PC: tools/pc/fantrack.py),
// listed by name on the left; the chosen one's picture, environment, medals and record on the right
static const char* const KEnvName2[7] = { "Rally", "Snow", "Desert", "Island", "Bay", "Coast", "Stadium" };
static int SCmpI(const char* a, const char* b)
    {
    for (;; a++, b++)
        {
        int x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return x - y;
        }
    }
int TmMenu::FanTracks(int* aIdx) const
    {
    int n = 0;
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iSeries > 4) aIdx[n++] = i;
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && SCmpI(iTracks[aIdx[b - 1]].iTitle, iTracks[aIdx[b]].iTitle) > 0; b--) { const int t = aIdx[b]; aIdx[b] = aIdx[b - 1]; aIdx[b - 1] = t; }
    return n;
    }

bool TmMenu::AddTrack(const TmUiTrack& aT)
    {
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == aT.iFile) { iTracks[i] = aT; return true; }
    if (iNTracks >= KMaxTracks) return false;
    iTracks[iNTracks++] = aT;
    return true;
    }

const TmUiTrack* TmMenu::TrackOfFile(int aFile) const
    {
    for (int i = 0; i < iNTracks; i++) if (iTracks[i].iFile == aFile) return &iTracks[i];
    return 0;
    }

// 0.19: a folder's entries: ".." first below the top, then what the app lists (the folders, then the tracks); with
// no lister, the installed fan tracks
void TmMenu::BrowseOpen(const char* aPath)
    {
    char* cur = iBrowseHot ? iHotPath : iBrowsePath;
    if (!iEntries) iEntries = new TmUiEntry[KMaxEntries];
    iNEntries = 0; iBrowseSel = 0;
    if (!iEntries) return;
    if (!iFolders)
        {
        int idx[KMaxTracks]; const int n = FanTracks(idx);
        for (int k = 0; k < n && iNEntries < KMaxEntries; k++)
            {
            TmUiEntry& e = iEntries[iNEntries++];
            e.iKind = TmUiEntry::ETrack; e.iTrack = iTracks[idx[k]].iFile; e.iLabel[0] = 0;
            SCpy(e.iName, iTracks[idx[k]].iTitle[0] ? iTracks[idx[k]].iTitle : iTracks[idx[k]].iName, 39);
            }
        cur[0] = 0;
        return;
        }
    char path[KPathLen]; SCpy(path, aPath, KPathLen - 1);
    for (int attempt = 0; attempt < 2; attempt++)
        {
        const int n = iFolders->ListFolder(path, iEntries, KMaxEntries, iBrowseHot);
        if (n >= 0) { iNEntries = n; break; }
        path[0] = 0;          // (the folder gone: the top)
        }
    SCpy(cur, path, KPathLen - 1);
    BrowsePicture();
    }

void TmMenu::BrowsePicture()
    {
    if (!iFolders || iBrowseSel >= iNEntries || iEntries[iBrowseSel].iKind != TmUiEntry::ETrack) return;
    for (int i = 0; i < iNTracks; i++)
        if (iTracks[i].iFile == iEntries[iBrowseSel].iTrack && !iTracks[i].iThumb && iTracks[i].iThumbW == 0)
            {
            iFolders->Picture(iTracks[i].iFile, iTracks[i]);
            if (!iTracks[i].iThumb) iTracks[i].iThumbW = -1;     // (tried)
            }
    }

int TmMenu::PickIndex(int aFile) const
    {
    for (int k = 0; k < iNPick; k++) if (iPick[k] == aFile) return k;
    return -1;
    }

// the parent of a folder's path ("E:\\A\\B\\" -> "E:\\A\\", "E:\\" -> ""); the last part's name into aLast
static void ParentPath(char* aPath, char* aLast, int aLastCap)
    {
    int n = SLen(aPath);
    if (n > 0 && aPath[n - 1] == '\\') n--;
    int k = n; while (k > 0 && aPath[k - 1] != '\\') k--;
    int j = 0; for (int i = k; i < n && j < aLastCap - 1; i++) aLast[j++] = aPath[i];
    aLast[j] = 0;
    aPath[k] = 0;
    }

// 0.19: Browse as the game's: the title and Back on the left; the folder in the middle (an orange plate with the
// tracks it holds, the path with the up button, the folders and the tracks); the chosen track on the right (its
// name, author, picture, medals, environment and Play)
void TmMenu::DrawBrowse(TmPix* f, int s)
    {
    const int n = iNEntries;
    const char* curPath = iBrowseHot ? iHotPath : iBrowsePath;
    if (iBrowseHot) DrawLeft(f, s, "Hot Seat", "Choose the tracks", EIcoParty);
    else DrawLeft(f, s, "Play Solo", "Choose a track", EIcoPlayer);
    DrawItem(f, s, RowY(-1), EIcoBack, "Back", iBrowseSel == n + (iBrowseHot ? 1 : 0));
    const TmPix orange1 = Rgb(240, 160, 30), orange2 = Rgb(200, 110, 10), panel1 = Rgb(64, 30, 26), panel2 = Rgb(36, 14, 14);
    int tracks = 0;
    for (int k = 0; k < n; k++) if (iEntries[k].iKind == TmUiEntry::ETrack) tracks++;
    // the middle: the folder
    const int mx = LEFT_W + 8, mw = 244;
    Round(f, s, mx, 2, mw, 196, 6, panel1, panel2);
    RoundLine(f, s, mx, 2, mw, 196, 6, Rgb(120, 70, 60));
    const int hh = iText->iH + 4;
    Round(f, s, mx + 4, 5, mw - 8, hh, 5, orange1, orange2);
    Text(f, s, *iText, mx + 10, 7, "Available tracks:", Rgb(255, 255, 255));
    { char b[12]; b[0] = 0; SNum(b, tracks); Text(f, s, *iText, mx + mw - 12 - iText->Width(b), 7, b, Rgb(255, 255, 255)); }
    // the path, the up button before it
    const int py = 5 + hh + 3, ph = iSmall->iH + 8;
    Round(f, s, mx + 4, py, mw - 8, ph, 4, Rgb(40, 34, 34), Rgb(26, 22, 22));
    Round(f, s, mx + 6, py + 1, ph + 6, ph - 2, (ph - 2) / 2, Rgb(230, 230, 230), Rgb(150, 150, 150));
    {
    // (a folder with an arrow up)
    const int ix = mx + 9 + (ph - 8) / 2, iy = py + 3;
    Fill(f, s, ix, iy + 2, 12, ph - 9, Rgb(240, 200, 80)); Fill(f, s, ix, iy + 1, 5, 2, Rgb(240, 200, 80));
    for (int j = 0; j < 4; j++) Fill(f, s, ix + 6 - j, iy + 2 + j, 1 + 2 * j, 1, Rgb(230, 120, 20));
    Fill(f, s, ix + 5, iy + 6, 3, ph - 13 > 1 ? ph - 13 : 1, Rgb(230, 120, 20));
    }
    const char* path = curPath[0] ? curPath : (iFolders ? "" : "Fan tracks\\");
    while (path[0] && iSmall->Width(path) > mw - 30 - ph) path++;
    Text(f, s, *iSmall, mx + 14 + ph, py + 4, path, Rgb(140, 210, 230));
    // the list
    const int ly = py + ph + 4, lh = 196 - ly, rh = iText->iH + 6, rows = lh / rh > 0 ? lh / rh : 1;
    Fill(f, s, mx + 4, ly, mw - 8, lh, Rgb(44, 40, 36));
    int first = iBrowseSel < n ? iBrowseSel - rows / 2 : n - rows; if (first > n - rows) first = n - rows; if (first < 0) first = 0;
    for (int r = 0; r < rows && first + r < n; r++)
        {
        // a row as the game's: a light bar, an orange tab for a track (a dark one for a folder), the environment's
        // mark, the name, the author's time on the right; the chosen one white, framed in orange
        const TmUiEntry& e = iEntries[first + r];
        const int y = ly + 1 + r * rh;
        const bool sel = iBrowseSel == first + r;
        const bool trk = e.iKind == TmUiEntry::ETrack;
        // (as the game's: a chosen track a dark blue bar framed in blue, its text blue; in the hot seat's chooser the
        // ticked ones so and the one under the cursor white with blue text)
        const bool picked = trk && (iBrowseHot ? PickIndex(e.iTrack) >= 0 : sel);
        const TmPix blue = Rgb(40, 140, 255);
        if (picked && !(iBrowseHot && sel)) Fill(f, s, mx + 6, y, mw - 12, rh - 2, Rgb(36, 52, 70));
        else Fill(f, s, mx + 6, y, mw - 12, rh - 2, sel ? Rgb(252, 252, 252) : Rgb(196, 196, 196));
        Fill(f, s, mx + 6, y, 4, rh - 2, picked || (sel && trk) ? blue : trk ? orange1 : Rgb(80, 80, 80));
        if (picked || sel) { const TmPix fc = picked || trk ? blue : orange1; Fill(f, s, mx + 6, y, mw - 12, 1, fc); Fill(f, s, mx + 6, y + rh - 3, mw - 12, 1, fc); Fill(f, s, mx + mw - 7, y, 1, rh - 2, fc); }
        const TmPix tc = picked || (sel && trk) ? blue : Rgb(40, 40, 40);
        const TmUiTrack* t = trk ? TrackOfFile(e.iTrack) : 0;
        int tx = mx + 14;
        if (t)
            {
            // the environment's mark: a small badge in its colour
            static const unsigned char KEnvCol[7][3] = { { 70, 150, 40 }, { 120, 170, 230 }, { 220, 170, 80 }, { 40, 170, 150 }, { 120, 120, 130 }, { 230, 120, 40 }, { 60, 90, 200 } };
            const int ec = t->iEnv >= 0 && t->iEnv < 7 ? t->iEnv : 0;
            Round(f, s, tx, y + 2, rh - 6, rh - 6, 3, Rgb(KEnvCol[ec][0], KEnvCol[ec][1], KEnvCol[ec][2]), Mix(Rgb(KEnvCol[ec][0], KEnvCol[ec][1], KEnvCol[ec][2]), 0, 90));
            tx += rh - 2;
            }
        char b[48];
        if (e.iKind == TmUiEntry::EUp) SCpy(b, "..\\", 40);
        else { SCpy(b, e.iLabel[0] ? e.iLabel : e.iName, 40); if (!trk) { const int l = SLen(b); b[l] = '\\'; b[l + 1] = 0; } }
        int right = mx + mw - 10;
        if (t && t->iMedal[0] > 0)
            {
            char tm[16]; FormatTime(tm, t->iMedal[0]);
            const int w = ClockWidth(*iSmall, tm);
            Clock(f, s, *iSmall, right - w, y + (rh - 2 - iSmall->iH) / 2, tm, tc);
            right -= w + 6;
            }
        Text(f, s, *iText, tx, y + (rh - 2 - iText->iH) / 2, b, tc, right);
        }
    if (first > 0) Fill(f, s, mx + mw / 2 - 6, ly + 1, 12, 2, Rgb(200, 200, 200));
    if (first + rows < n) Fill(f, s, mx + mw / 2 - 6, 193, 12, 2, Rgb(200, 200, 200));
    if (n == 0)
        {
        TextCentre(f, s, *iSmall, mx, mx + mw, ly + 20, "No tracks here", Rgb(220, 220, 220));
        TextCentre(f, s, *iSmall, mx, mx + mw, ly + 24 + iSmall->iH, "Copy .Challenge.Gbx files of Rally,", Rgb(170, 170, 170));
        TextCentre(f, s, *iSmall, mx, mx + mw, ly + 26 + 2 * iSmall->iH, "Snow, Desert, Bay or Island here", Rgb(170, 170, 170));
        }
    // the right: the chosen track ("???" for a folder, as the game's)
    const int rx = mx + mw + 8, rw = 640 - rx - 4;
    Round(f, s, rx, 2, rw, 196, 6, panel1, panel2);
    RoundLine(f, s, rx, 2, rw, 196, 6, Rgb(120, 70, 60));
    const TmUiEntry* e = iBrowseSel < n ? &iEntries[iBrowseSel] : 0;
    const TmUiTrack* t = e && e->iKind == TmUiEntry::ETrack ? TrackOfFile(e->iTrack) : 0;
    Round(f, s, rx + 4, 5, rw - 8, hh, 5, orange1, orange2);
    TextCentre(f, s, *iText, rx + 6, rx + rw - 6, 7, t ? (t->iTitle[0] ? t->iTitle : t->iName) : "???", Rgb(255, 255, 255));
    int y = 5 + hh + 3;
    // the author, the environment on the right
    Person(f, s, rx + 6, y, Rgb(250, 200, 60), Rgb(170, 110, 20));
    if (t)
        {
        const char* en = t->iEnv >= 0 && t->iEnv < 7 ? KEnvName2[t->iEnv] : "?";
        const int ew = iSmall->Width(en);
        Text(f, s, *iSmall, rx + rw - 8 - ew, y + 2, en, Rgb(90, 170, 255));
        if (t->iAuthor[0]) Text(f, s, *iSmall, rx + 22, y + 2, t->iAuthor, Rgb(230, 230, 230), rx + rw - 14 - ew);
        }
    y += 18;
    // the picture, 4:3 as the game's (white without one)
    const int th = 72, tw = 96, tx = rx + (rw - tw) / 2;
    if (t && t->iThumb && t->iThumbW > 0 && t->iThumbH > 0)
        for (int j = 0; j < th; j++)
            {
            const int sy = (j * t->iThumbH) / th;
            for (int i = 0; i < tw; i++) f[(y + j) * s + tx + i] = t->iThumb[sy * t->iThumbW + (i * t->iThumbW) / tw];
            }
    else Fill(f, s, tx, y, tw, th, Rgb(236, 236, 236));
    RoundLine(f, s, tx - 1, y - 1, tw + 2, th + 2, 1, Rgb(150, 150, 150));
    y += th + 4;
    // the medals: bronze, silver, gold, the author's (as the game's), their times under them
    for (int k = 0; k < 4; k++)
        {
        const int m = 3 - k;
        const int cx = rx + (rw * (2 * k + 1)) / 8;
        const bool has = t && t->iMedal[m] > 0;
        if (has) Coin(f, s, cx, y + 7, 7, m);
        else Round(f, s, cx - 7, y, 15, 15, 7, Rgb(90, 50, 46), Rgb(60, 30, 28));
        if (has) { char b[16]; FormatTime(b, t->iMedal[m]); TextCentre(f, s, *iSmall, cx - 24, cx + 24, y + 16, b, Rgb(220, 220, 220)); }
        }
    y += 17 + iSmall->iH + 1;
    // the record (the game's: the mode and the best time)
    if (t && t->iBest)
        {
        char b[40]; SCpy(b, "Record ", 10); FormatTime(b + SLen(b), t->iBest * 20);
        TextCentre(f, s, *iSmall, rx, rx + rw, y, b, Rgb(255, 210, 90));
        }
    // Play
    const int by = 195 - hh;
    const bool canPlay = iBrowseHot ? iNPick > 0 : t != 0;
    Round(f, s, rx + 6, by, rw - 12, hh, hh / 2, canPlay ? Rgb(250, 250, 250) : Rgb(150, 150, 150), canPlay ? Rgb(200, 200, 200) : Rgb(110, 110, 110));
    if (iBrowseHot && iBrowseSel == n) RoundLine(f, s, rx + 6, by, rw - 12, hh, hh / 2, Rgb(40, 140, 255));
    char pl[24]; SCpy(pl, "Play", 8);
    if (iBrowseHot && iNPick) { SCat(pl, " ("); SNum(pl + SLen(pl), iNPick); SCat(pl, ")"); }
    TextCentre(f, s, *iText, rx + 6, rx + rw - 6, by + 2, pl, iBrowseHot && iBrowseSel == n ? Rgb(40, 140, 255) : Rgb(40, 40, 40));
    }

void TmMenu::Draw(TmPix* f, int s)
    {
    iDirty = false;
    if (iScreen == ETitle) { DrawTitle(f, s); return; }
    for (int y = 0; y < 200; y++) { const TmPix* b = iBg + y * 640; TmPix* p = f + y * s; for (int x = 0; x < 640; x++) p[x] = b[x]; }
    switch (iScreen)
        {
        case EMain: DrawLeft(f, s, "Welcome!", "Choose an activity", EIcoCar); DrawList(f, s, EMain, true); DrawMedalsPanel(f, s); break;
        case ECampaign: DrawLeft(f, s, "Play Solo", "Choose a campaign", EIcoPlayer); DrawList(f, s, ECampaign, true); DrawMedalsPanel(f, s); break;
        case EMode: DrawLeft(f, s, "Play Solo", "Choose a campaign", EIcoPlayer); DrawList(f, s, EMode, true); DrawMedalsPanel(f, s); break;
        case EBrowse: DrawBrowse(f, s); break;
        case ESolo: case EOpponent:
            if (iScreen == EOpponent && iOppFrom == EBrowse) { DrawBrowse(f, s); DrawOpponent(f, s); break; }
            DrawLeft(f, s, "Play Solo", "Choose the difficulty", EIcoPlayer);
            DrawList(f, s, ESolo, true);
            DrawMedalsPanel(f, s);
            if (iSel[ESolo] < 5) DrawGrid(f, s);
            if (iScreen == EOpponent) DrawOpponent(f, s);
            if (iLockMsg)
                {
                Shade(f, s, 0, 0, 640, 200, 110, 0);
                TmDlg::Message(f, s, *iText, 150, 560, (iSel[ESolo] == 4 ? KNeedMsg[4] : KNeedMsg[KNeedMedal[iSel[ESolo] < 5 ? iSel[ESolo] : 0]]));
                }
            break;
        case EParty: DrawLeft(f, s, "Party Mode", "Choose a mode", EIcoParty); DrawList(f, s, EParty, true); break;
        case EHotSeat:
            DrawLeft(f, s, "Hot Seat", "Register the players", EIcoParty);
            DrawItem(f, s, RowY(-1), EIcoBack, "Back", iHsRow == 11 && !iChooser);
            DrawHotSeat(f, s);
            break;
        case ESeries:
            {
            DrawLeft(f, s, "Hot Seat", "Choose a series", EIcoParty);
            const int ns = iNEnvs * 5;
            DrawItem(f, s, RowY(-4), EIcoFolder, "Official tracks", iSeriesSel == ns);
            DrawItem(f, s, RowY(-3), EIcoFolder, "My tracks", iSeriesSel == ns + 1);
            DrawItem(f, s, RowY(-2), EIcoFolder, "Downloaded tracks", iSeriesSel == ns + 2);
            DrawItem(f, s, RowY(-1), EIcoBack, "Back", iSeriesSel == ns + 3);
            DrawSeries(f, s);
            break;
            }
        case ETracks: DrawBrowse(f, s); break;
        default: break;
        }
    }

// 0.19: the explorer's keys: up and down over the entries (then Play in the hot seat's chooser, then Back), Enter
// opens a folder, plays a track (Solo: the opponent's choice) or ticks it (the hot seat), Left / Esc a folder up
void TmMenu::BrowseKey(int aKey, TScreen aBack)
    {
    const int n = iNEntries, nr = n + (iBrowseHot ? 2 : 1);
    char* cur = iBrowseHot ? iHotPath : iBrowsePath;
    const bool ok = aKey == EUiOk || aKey == EUiRight;
    if (aKey == EUiUp) { iBrowseSel = (iBrowseSel + nr - 1) % nr; BrowsePicture(); }
    else if (aKey == EUiDown) { iBrowseSel = (iBrowseSel + 1) % nr; BrowsePicture(); }
    else if (iBrowseHot && ok && iBrowseSel == n) {}      // (Play: the caller)
    else if ((ok && iBrowseSel == nr - 1) || ((aKey == EUiBack || aKey == EUiLeft) && !cur[0])) iScreen = aBack;
    else if (aKey == EUiBack || aKey == EUiLeft)
        {
        // up a folder, the one we came from chosen
        char path[KPathLen], last[40]; SCpy(path, cur, KPathLen - 1);
        ParentPath(path, last, sizeof(last));
        BrowseOpen(path);
        for (int k = 0; k < iNEntries; k++) if (iEntries[k].iKind == TmUiEntry::EFolder && !SCmpI(iEntries[k].iName, last)) iBrowseSel = k;
        BrowsePicture();
        }
    else if (ok && iBrowseSel < n && iEntries[iBrowseSel].iKind == TmUiEntry::EFolder)
        {
        char path[KPathLen]; SCpy(path, cur, KPathLen - 1);
        const int l = SLen(path), m = SLen(iEntries[iBrowseSel].iName);
        if (l + m + 2 < KPathLen) { SCpy(path + l, iEntries[iBrowseSel].iName, m); path[l + m] = '\\'; path[l + m + 1] = 0; BrowseOpen(path); }
        }
    else if (ok && iBrowseSel < n && iEntries[iBrowseSel].iKind == TmUiEntry::ETrack)
        {
        const int file = iEntries[iBrowseSel].iTrack;
        if (!iBrowseHot) { iRaceFile = file; iOppFrom = EBrowse; iScreen = EOpponent; iOppSel = 0; }
        else
            {
            // ticked or unticked (as the game's: a press each, no Shift or Ctrl)
            const int k = PickIndex(file);
            if (k >= 0) { for (int i = k; i + 1 < iNPick; i++) iPick[i] = iPick[i + 1]; iNPick--; }
            else if (iNPick < KMaxSeries) iPick[iNPick++] = file;
            }
        }
    }

// the hot seat's players packed (the empty slots out) and its first race asked for
int TmMenu::StartHotSeat()
    {
    int m = 0;
    for (int p = 0; p < KMaxPlayers; p++) if (iNames[p][0]) { if (m != p) SCpy(iNames[m], iNames[p], KNameLen); m++; }
    for (int p = m; p < KMaxPlayers; p++) iNames[p][0] = 0;
    iPlayers = m; iRaceFile = iSeriesFiles[0]; iRaceOpp = EOppNone;
    return EUiRace;
    }

int TmMenu::Key(int aKey, int aChar)
    {
    iDirty = true;
    switch (iScreen)
        {
        case ETitle:
            // Esc on the start-up screen leaves the game; any other key goes on to the menus
            if (aKey == EUiBack) return EUiQuit;
            if (iNTracks) iScreen = EMain;
            return EUiNone;
        case EMain:
            {
            const int k = ListKey(EMain, aKey);
            if (k == -2) iScreen = ETitle;
            else if (k == 0) iScreen = ECampaign;
            else if (k == 2) iScreen = EParty;
            else if (k == 5) return EUiQuit;
            return EUiNone;
            }
        case ECampaign:
            {
            const int k = ListKey(ECampaign, aKey);
            if (k == -2 || k == 5 || aKey == EUiLeft) iScreen = EMain;
            else if (k == 0) iScreen = EMode;
            else if (k == 4) { iScreen = EBrowse; iBrowseHot = false; BrowseOpen(iBrowsePath); }
            return EUiNone;
            }
        case EBrowse: BrowseKey(aKey, ECampaign); return EUiNone;
        case ETracks:
            {
            const bool play = aKey == EUiOk && iBrowseSel == iNEntries;
            BrowseKey(aKey, ESeries);
            if (play && iNPick > 0)
                {
                iSeriesN = 0;
                for (int k = 0; k < iNPick; k++) iSeriesFiles[iSeriesN++] = iPick[k];
                return StartHotSeat();
                }
            return EUiNone;
            }
        case EMode:
            {
            const int k = ListKey(EMode, aKey);
            if (k == -2 || k == 4 || aKey == EUiLeft) iScreen = ECampaign;
            else if (k == 0) { iScreen = ESolo; iInGrid = false; }
            return EUiNone;
            }
        case ESolo:
            if (!iInGrid)
                {
                if (aKey == EUiLeft) { iScreen = EMode; return EUiNone; }
                const int k = ListKey(ESolo, aKey);
                if (k == -2 || k == 5) iScreen = EMode;
                else if (k >= 0 && k < 5)
                    {
                    // into the grid, on the first installed track
                    iInGrid = true; iGridRow = 0; iGridCol = 0;
                    bool found = false;
                    for (int r = 0; r < iNEnvs && !found; r++) for (int n = 1; n <= 5 && !found; n++) if (TrackAt(k, iEnvs[r], n) >= 0) { iGridRow = r; iGridCol = n - 1; found = true; }
                    }
                return EUiNone;
                }
            if (iLockMsg) { if (aKey == EUiOk || aKey == EUiBack) iLockMsg = false; return EUiNone; }
            if (aKey == EUiBack) { iInGrid = false; return EUiNone; }
            if (aKey == EUiLeft) { if (iGridCol == 0) iInGrid = false; else iGridCol--; }
            else if (aKey == EUiRight) { if (iGridCol < 4) iGridCol++; }
            else if ((aKey == EUiUp || aKey == EUiDown) && iNEnvs > 0) iGridRow = (iGridRow + (aKey == EUiUp ? iNEnvs - 1 : 1)) % iNEnvs;
            else if (aKey == EUiOk && iNEnvs > 0)
                {
                const int ti = TrackAt(iSel[ESolo], iEnvs[iGridRow], iGridCol + 1);
                if (ti >= 0 && !Unlocked(ti)) iLockMsg = true;
                else if (ti >= 0) { iRaceFile = iTracks[ti].iFile; iOppFrom = ESolo; iScreen = EOpponent; iOppSel = 1; }
                }
            return EUiNone;
        case EOpponent:
            {
            int rows[8], n; OppRows(rows, n);
            if (aKey == EUiUp) iOppSel = (iOppSel + n - 1) % n;
            else if (aKey == EUiDown) iOppSel = (iOppSel + 1) % n;
            else if (aKey == EUiBack) iScreen = iOppFrom;
            else if (aKey == EUiOk) { iRaceOpp = rows[iOppSel]; iPlayers = 1; iSeriesN = 0; iScreen = iOppFrom; return EUiRace; }
            return EUiNone;
            }
        case EParty:
            {
            const int k = ListKey(EParty, aKey);
            if (k == -2 || k == 2 || aKey == EUiLeft) iScreen = EMain;
            else if (k == 0) { iScreen = EHotSeat; iHsRow = 2; iEditing = -1; iChooser = false; }
            return EUiNone;
            }
        case EHotSeat:
            {
            if (iChooser)
                {
                int rows[KMaxProfiles + 2], n; ChooserRows(rows, n);
                if (iEditing >= 0)
                    {
                    // a new profile's name: letters, digits, a space; Backspace takes one off; Enter keeps it
                    char* nm = iNames[iEditing];
                    if (aKey == EUiChar && aChar >= 32 && aChar < 127 && SLen(nm) < KNameLen) { int l = SLen(nm); char c = (char) aChar; if (c >= 'a' && c <= 'z') c = (char) (c - 32); nm[l] = c; nm[l + 1] = 0; }
                    else if (aKey == EUiDel) { int l = SLen(nm); if (l) nm[l - 1] = 0; }
                    else if (aKey == EUiBack) { nm[0] = 0; iEditing = -1; }
                    else if (aKey == EUiOk && nm[0])
                        {
                        if (iNProfiles < KMaxProfiles) { SCpy(iProfiles[iNProfiles++], nm, KNameLen); iProfilesChanged = true; }
                        iEditing = -1; iChooser = false;
                        }
                    return EUiNone;
                    }
                if (aKey == EUiUp) iChooserSel = (iChooserSel + n - 1) % n;
                else if (aKey == EUiDown) iChooserSel = (iChooserSel + 1) % n;
                else if (aKey == EUiBack) iChooser = false;
                else if (aKey == EUiDel && rows[iChooserSel] >= 0)
                    {
                    // the profile removed
                    for (int i = rows[iChooserSel]; i + 1 < iNProfiles; i++) SCpy(iProfiles[i], iProfiles[i + 1], KNameLen);
                    iNProfiles--; iProfilesChanged = true;
                    if (iChooserSel >= n - 1) iChooserSel = n - 2;
                    }
                else if (aKey == EUiOk)
                    {
                    const int slot = iHsRow - 2;
                    if (rows[iChooserSel] >= 0) { SCpy(iNames[slot], iProfiles[rows[iChooserSel]], KNameLen); iChooser = false; }
                    else if (rows[iChooserSel] == -1) { iEditing = slot; iNames[slot][0] = 0; }
                    else iChooser = false;
                    }
                return EUiNone;
                }
            if (aKey == EUiBack) { iScreen = EParty; return EUiNone; }
            const bool slot = iHsRow >= 2 && iHsRow <= 9, leftCol = slot && !((iHsRow - 2) & 1);
            if (aKey == EUiUp) { if (iHsRow == 11) iHsRow = 10; else if (iHsRow == 10) iHsRow = 8; else if (iHsRow >= 4) iHsRow -= 2; else if (iHsRow >= 2) iHsRow = 1; else if (iHsRow == 1) iHsRow = 0; }
            else if (aKey == EUiDown) { if (iHsRow == 0) iHsRow = 1; else if (iHsRow == 1) iHsRow = 2; else if (iHsRow <= 7) iHsRow += 2; else if (iHsRow <= 9) iHsRow = 10; else iHsRow = 11; }
            else if (aKey == EUiLeft || aKey == EUiRight)
                {
                const int d = aKey == EUiLeft ? -1 : 1;
                if (iHsRow == 0) iHsMode ^= 1;
                else if (iHsRow == 1) { if (iHsMode) { iTimeLimit += d * 30; if (iTimeLimit < 60) iTimeLimit = 60; if (iTimeLimit > 900) iTimeLimit = 900; } else { iAttempts += d; if (iAttempts < 1) iAttempts = 1; if (iAttempts > 10) iAttempts = 10; } }
                else if (slot) { if (d < 0 && leftCol) iHsRow = 11; else if (d < 0) iHsRow--; else if (leftCol) iHsRow++; }
                else if (iHsRow == 11 && d > 0) iHsRow = 2;
                }
            else if (aKey == EUiDel && slot) iNames[iHsRow - 2][0] = 0;
            else if (aKey == EUiOk)
                {
                if (slot) { iChooser = true; iChooserSel = 0; iEditing = -1; }
                else if (iHsRow == 11) iScreen = EParty;
                else if (iHsRow == 10)
                    {
                    int n = 0; for (int p = 0; p < KMaxPlayers; p++) if (iNames[p][0]) n++;
                    if (n >= 2) { iScreen = ESeries; iSeriesSel = 0; }
                    }
                }
            return EUiNone;
            }
        case ESeries:
            {
            // 0.19: the campaigns' series in the middle, then on the left Official tracks, My tracks, Downloaded
            // tracks (the explorer, several tracks ticked) and Back, as the game's; Left / Right between the two
            const int ns = iNEnvs * 5, nr = ns + 4;
            if (aKey == EUiBack || (aKey == EUiOk && iSeriesSel == nr - 1)) { iScreen = EHotSeat; return EUiNone; }
            if (aKey == EUiUp) iSeriesSel = (iSeriesSel + nr - 1) % nr;
            else if (aKey == EUiDown) iSeriesSel = (iSeriesSel + 1) % nr;
            else if (aKey == EUiLeft && iSeriesSel < ns) iSeriesSel = ns;
            else if ((aKey == EUiOk || aKey == EUiRight) && iSeriesSel >= ns && iSeriesSel < ns + 3)
                {
                static const char* const KRoot[3] = { "Campaigns\\", "Challenges\\My Challenges\\", "Challenges\\Downloaded\\" };
                iBrowseHot = true; iNPick = 0; iScreen = ETracks;
                BrowseOpen(KRoot[iSeriesSel - ns]);
                }
            else if (aKey == EUiRight && iSeriesSel == nr - 1) iSeriesSel = 0;
            else if (aKey == EUiOk && iSeriesSel < ns)
                {
                const int env = iEnvs[iSeriesSel / 5], ser = iSeriesSel % 5;
                iSeriesN = 0;
                for (int n = 1; n <= 5 && iSeriesN < KMaxSeries; n++) { int ti = TrackAt(ser, env, n); if (ti >= 0) iSeriesFiles[iSeriesN++] = iTracks[ti].iFile; }
                if (!iSeriesN) return EUiNone;
                return StartHotSeat();
                }
            return EUiNone;
            }
        default: return EUiNone;
        }
    }

// 0.14: the race's dialogs. Each is measured first: the text font if all of it fits the 200 lines, else the small
// one; a list longer than that still scrolls to keep the selected row in view.
namespace TmDlg
{
using namespace TmDraw;
static void Box(TmPix* f, int s, int x0, int x1, int y, int h)
    {
    Round(f, s, x0, y, x1 - x0, h, 6, Rgb(46, 48, 50), Rgb(26, 28, 30));
    RoundLine(f, s, x0, y, x1 - x0, h, 6, Rgb(96, 100, 104));
    }
static void Head(TmPix* f, int s, const TmFont& fn, int x0, int x1, int y, int h, const char* t)
    {
    Round(f, s, x0 + 4, y, x1 - x0 - 8, h, 5, Rgb(12, 12, 14), Rgb(2, 2, 4));
    TextCentre(f, s, fn, x0, x1, y + (h - fn.iH) / 2, t, Rgb(60, 220, 60));
    }
static void Pill(TmPix* f, int s, const TmFont& fn, int x0, int x1, int y, int h, const char* t, int n, bool sel)
    {
    Round(f, s, x0, y, x1 - x0, h, h / 2, sel ? Rgb(255, 255, 255) : Rgb(70, 74, 78), sel ? Rgb(200, 220, 255) : Rgb(44, 46, 50));
    if (sel) RoundLine(f, s, x0, y, x1 - x0, h, h / 2, Rgb(60, 140, 255));
    char b[40]; int i = 0; while (i < n && i < 39) { b[i] = t[i]; i++; } b[i] = 0;
    TextCentre(f, s, fn, x0, x1, y + (h - fn.iH) / 2, b, sel ? Rgb(20, 70, 200) : Rgb(235, 235, 235));
    }
static const char* Bar(const char* t) { while (*t && *t != '|') t++; return t; }

void Buttons(TmPix* f, int s, const TmFont& tx, const TmFont& sm, int x0, int x1, const char* title,
    const char* const* rows, int nr, int sel, bool gapLast)
    {
    const TmFont* fn = &tx;
    int bh = tx.iH + 6, hh = tx.iH + 5, gap = 3;
    int need = 4 + hh + 6 + nr * (bh + gap) + (gapLast ? 6 : 0) + 3;
    if (need > 200) { fn = &sm; bh = sm.iH + 5; hh = sm.iH + 4; gap = 2; need = 4 + hh + 6 + nr * (bh + gap) + (gapLast ? 6 : 0) + 3; }
    // still too tall: as many rows as fit, scrolled to the selected button's row
    int vis = nr, first = 0;
    int selRow = 0; { int b = 0; for (int r = 0; r < nr; r++) { const int k = *Bar(rows[r]) ? 2 : 1; if (sel >= b && sel < b + k) selRow = r; b += k; } }
    if (need > 200) { vis = (200 - 4 - hh - 6 - (gapLast ? 6 : 0) - 3) / (bh + gap); first = selRow - vis + 1; if (first < 0) first = 0; if (first > nr - vis) first = nr - vis; need = 200; }
    const int y = (200 - need) / 2;
    Box(f, s, x0, x1, y, need);
    Head(f, s, *fn, x0, x1, y + 3, hh, title);
    int ry = y + 3 + hh + 6, b = 0;
    for (int r = 0; r < nr; r++)
        {
        const char* t = rows[r]; const char* bar = Bar(t);
        const int k = *bar ? 2 : 1;
        if (r < first || r >= first + vis) { b += k; continue; }
        if (gapLast && r == nr - 1) ry += 6;
        if (k == 1) Pill(f, s, *fn, x0 + 14, x1 - 14, ry, bh, t, (int) (bar - t), sel == b);
        else
            {
            const int mid = (x0 + x1) / 2;
            Pill(f, s, *fn, x0 + 14, mid - 3, ry, bh, t, (int) (bar - t), sel == b);
            Pill(f, s, *fn, mid + 3, x1 - 14, ry, bh, bar + 1, SLen(bar + 1), sel == b + 1);
            }
        b += k; ry += bh + gap;
        }
    if (first > 0) TextCentre(f, s, sm, x0, x1, y + 3 + hh - 2, "^", Rgb(200, 200, 200));
    if (first + vis < nr) TextCentre(f, s, sm, x0, x1, y + need - sm.iH, "v", Rgb(200, 200, 200));
    }

void Ghosts(TmPix* f, int s, const TmFont& tx, const TmFont& sm, int x0, int x1, const TRow* rows, int n, int sel)
    {
    // the title, the rows (a medal or a name, its time, a tick), OK; the small font for the rows if they do not fit
    const int hh = sm.iH + 4;
    const TmFont* fn = &tx; int rh = tx.iH + 4;
    const int okH = tx.iH + 6;
    int room = 200 - 6 - hh - 4 - okH - 6;
    if (n * rh > room) { fn = &sm; rh = sm.iH + 3; }
    int vis = room / rh; if (vis > n) vis = n;
    const int need = 6 + hh + 4 + vis * rh + 4 + okH + 3, y = (200 - need) / 2;
    const int listSel = sel < n ? sel : n - 1;
    int first = listSel - vis + 1; if (first < 0) first = 0; if (first > n - vis) first = n - vis;
    Box(f, s, x0, x1, y, need);
    Head(f, s, sm, x0, x1, y + 3, hh, "Choose the opponents' ghosts:");
    int ry = y + 3 + hh + 4;
    Round(f, s, x0 + 8, ry - 2, x1 - x0 - 16, vis * rh + 4, 3, Rgb(232, 234, 236), Rgb(212, 214, 216));
    for (int k = 0; k < vis; k++)
        {
        const TRow& r = rows[first + k];
        const bool on = first + k == sel;
        if (on) Fill(f, s, x0 + 10, ry, x1 - x0 - 20, rh, Rgb(90, 150, 240));
        const TmPix c = on ? Rgb(255, 255, 255) : Rgb(30, 30, 30);
        // the tick box
        const int bs = rh - 5;
        Fill(f, s, x0 + 14, ry + 2, bs, bs, Rgb(80, 80, 80));
        Fill(f, s, x0 + 15, ry + 3, bs - 2, bs - 2, r.iOn ? Rgb(60, 200, 60) : Rgb(250, 250, 250));
        Text(f, s, *fn, x0 + 20 + bs, ry + (rh - fn->iH) / 2, r.iName, c, x1 - 90);
        const int xr = x1 - (vis < n ? 30 : 16);
        if (r.iMs > 0) { char b[16]; FormatTime(b, r.iMs); Clock(f, s, *fn, xr - ClockWidth(*fn, b), ry + (rh - fn->iH) / 2, b, c); }
        else if (r.iMs < 0) Text(f, s, *fn, xr - fn->Width(r.iOn ? "on" : "off"), ry + (rh - fn->iH) / 2, r.iOn ? "on" : "off", c);
        ry += rh;
        }
    if (first > 0) TextCentre(f, s, sm, x1 - 28, x1 - 10, y + 3 + hh + 4, "^", Rgb(30, 30, 30));
    if (first + vis < n) TextCentre(f, s, sm, x1 - 28, x1 - 10, ry - sm.iH, "v", Rgb(30, 30, 30));
    const int mid = (x0 + x1) / 2;
    Pill(f, s, tx, mid - 50, mid + 50, y + need - okH - 3, okH, "OK", 2, sel >= n);
    }

void Message(TmPix* f, int s, const TmFont& tx, int x0, int x1, const char* msg)
    {
    // one line, or two split at the space nearest the middle if it is wider than the box
    char a[128], b[128]; SCpy(a, msg, 127); b[0] = 0;
    if (tx.Width(a) > x1 - x0 - 20)
        {
        const int l = SLen(a); int best = -1;
        for (int i = 0; i < l; i++) if (a[i] == ' ' && (best < 0 || (i - l / 2) * (i - l / 2) < (best - l / 2) * (best - l / 2))) best = i;
        if (best > 0) { SCpy(b, a + best + 1, 127); a[best] = 0; }
        }
    const int lines = b[0] ? 2 : 1, okH = tx.iH + 6;
    const int need = 8 + lines * (tx.iH + 2) + 8 + okH + 6, y = (200 - need) / 2;
    Box(f, s, x0, x1, y, need);
    TextCentre(f, s, tx, x0, x1, y + 8, a, Rgb(255, 255, 255));
    if (b[0]) TextCentre(f, s, tx, x0, x1, y + 8 + tx.iH + 2, b, Rgb(255, 255, 255));
    const int mid = (x0 + x1) / 2;
    Pill(f, s, tx, mid - 50, mid + 50, y + need - okH - 6, okH, "OK", 2, true);
    }
}
