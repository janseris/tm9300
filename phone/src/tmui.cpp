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
        int w = (src.iW[c] * h + sh / 2) / sh; if (w < 1) w = 1;
        if (bold && c) w++;
        out.iX[c] = x; out.iW[c] = w; x += w;
        }
    out.iStripW = x;
    out.iMask = new unsigned char[x * h];
    if (!out.iMask) return false;
    for (int i = 0; i < x * h; i++) out.iMask[i] = 0;
    // each target pixel: the sheet's pixels under it, weighted by how much of each it covers (1/256 pixel units)
    for (int c = 1; c < 95; c++)
        {
        const int sw = src.iW[c], tw = out.iW[c] - (bold ? 1 : 0);
        const unsigned char* sm = src.iMask + src.iX[c];
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
static const char* const KEnvName[2] = { "Rally", "Snow" };
enum { LEFT_W = 196, RIGHT_X = 204 };

TmMenu::TmMenu() : iDirty(true), iScreen(ETitle), iVersion(""), iRaceFile(-1), iRaceOpp(EOppGold), iPlayers(1), iAttempts(3), iHsMode(0), iTimeLimit(300), iNProfiles(0), iProfilesChanged(false), iSeriesN(0),
    iPack(0), iBig(0), iText(0), iSmall(0), iNTracks(0), iBg(0), iInGrid(false), iGridCol(0), iGridRow(0), iOppSel(0), iHsRow(2), iChooser(false), iChooserSel(0), iRowH(24), iPlateH(29), iEditing(-1), iSeriesSel(0), iTitleTick(0)
    {
    for (int i = 0; i < 10; i++) iSel[i] = 0;
    for (int p = 0; p < KMaxPlayers; p++) iNames[p][0] = 0;
    }
TmMenu::~TmMenu() { delete[] iBg; }

bool TmMenu::Init(const TmFont* aBig, const TmFont* aText, const TmFont* aSmall, const TmUiTrack* aTracks, int aN, const TmUiPack* aPack)
    {
    iBig = aBig; iText = aText; iSmall = aSmall; iPack = aPack;
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
            ITEM(EIcoMyCampaign, "My Campaign", false, 3) ITEM(EIcoBrowse, "Browse", false, -2) ITEM(EIcoBack, "Back", true, -1)
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
    const int cw = 84, ch = 64, x0 = RIGHT_X + 6, y0 = 50;
    for (int env = 0; env < 2; env++)
        for (int n = 1; n <= 5; n++)
            {
            const int x = x0 + (n - 1) * cw, y = y0 + env * (ch + 7);
            const int ti = TrackAt(series, env, n);
            const bool sel = iInGrid && iGridRow == env && iGridCol == n - 1;
            Round(f, s, x, y, cw - 4, ch, 4, sel ? Rgb(255, 240, 170) : Rgb(220, 226, 230), sel ? Rgb(220, 180, 70) : Rgb(170, 178, 184));
            if (ti < 0)
                {
                Round(f, s, x + 3, y + 3, cw - 10, ch - 6, 3, Rgb(50, 110, 140), Rgb(26, 70, 96));
                Icon(f, s, x + (cw - 4) / 2 - 15, y + ch / 2 - 15, EIcoLock);
                continue;
                }
            const TmUiTrack& t = iTracks[ti];
            const int tw = cw - 10, th = ch - 6 - iSmall->iH - 1;
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
                if (k >= 0) Coin(f, s, x + tw - 8, y + th - 6, 9, k);
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
    aRows[aN++] = EOppNadeo; aRows[aN++] = EOppGold; aRows[aN++] = EOppSilver; aRows[aN++] = EOppBronze;
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
            const int sq = chh - 6;
            Fill(f, s, cx + 3, cy + 2, sq, sq, Rgb(230, 20, 20));
            TextCentre(f, s, nf, cx + sq + 4, cx + cw - 10, cy + (chh - 2 - nf.iH) / 2, iNames[p], Rgb(90, 90, 90));
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
        if (rows[r] >= 0) { Fill(f, s, x + 16, ry + 2, rh - 6, rh - 6, Rgb(230, 20, 20)); Text(f, s, *iSmall, x + w - 22, ry + 1, "x", Rgb(20, 20, 20)); }
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
    const int x = RIGHT_X + 40, w = 640 - x - 20;
    Round(f, s, x, 2, w, 196, 6, Rgb(50, 70, 82), Rgb(34, 50, 60));
    Round(f, s, x + 4, 4, w - 8, iText->iH + 4, 5, Rgb(240, 150, 30), Rgb(200, 110, 10));
    Text(f, s, *iText, x + 12, 6, "Available campaigns", Rgb(255, 255, 255));
    int count = 0; for (int e = 0; e < 2; e++) for (int k = 0; k < 5; k++) if (SeriesCount(e, k)) count++;
    char b[24]; b[0] = 0; SNum(b, count);
    Text(f, s, *iText, x + w - 12 - iText->Width(b), 6, b, Rgb(255, 255, 255));
    const int rh = (196 - iText->iH - 12) / 10;
    for (int row = 0; row < 10; row++)
        {
        const int e = row / 5, k = row % 5, n = SeriesCount(e, k);
        const int y = 10 + iText->iH + row * rh;
        const bool sel = row == iSeriesSel;
        Round(f, s, x + 8, y, w - 16, rh - 1, 2, sel ? Rgb(255, 250, 215) : Rgb(236, 236, 236), sel ? Rgb(230, 222, 170) : Rgb(212, 212, 212));
        char nm[32]; nm[0] = 0; SCat(nm, KEnvName[e]); SCat(nm, " "); SCat(nm, KSeriesName[k]);
        Text(f, s, *iSmall, x + 16, y + (rh - 1 - iSmall->iH) / 2, nm, n ? Rgb(40, 44, 48) : Rgb(150, 150, 150));
        char tr[24]; tr[0] = 0; SNum(tr, n); SCat(tr, n == 1 ? " track" : " tracks");
        Text(f, s, *iSmall, x + w - 20 - iSmall->Width(tr), y + (rh - 1 - iSmall->iH) / 2, tr, n ? Rgb(40, 44, 48) : Rgb(150, 150, 150));
        }
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
        case ESolo: case EOpponent:
            DrawLeft(f, s, "Play Solo", "Choose the difficulty", EIcoPlayer);
            DrawList(f, s, ESolo, true);
            DrawMedalsPanel(f, s);
            if (iSel[ESolo] < 5) DrawGrid(f, s);
            if (iScreen == EOpponent) DrawOpponent(f, s);
            break;
        case EParty: DrawLeft(f, s, "Party Mode", "Choose a mode", EIcoParty); DrawList(f, s, EParty, true); break;
        case EHotSeat:
            DrawLeft(f, s, "Hot Seat", "Register the players", EIcoParty);
            DrawItem(f, s, RowY(-1), EIcoBack, "Back", iHsRow == 11 && !iChooser);
            DrawHotSeat(f, s);
            break;
        case ESeries:
            DrawLeft(f, s, "Hot Seat", "Choose a series", EIcoParty);
            DrawItem(f, s, RowY(-1), EIcoBack, "Back", iSeriesSel == 10);
            DrawSeries(f, s);
            break;
        default: break;
        }
    }

int TmMenu::Key(int aKey, int aChar)
    {
    iDirty = true;
    switch (iScreen)
        {
        case ETitle: if (iNTracks) iScreen = EMain; return EUiNone;
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
                    for (int e = 0; e < 2 && !found; e++) for (int n = 1; n <= 5 && !found; n++) if (TrackAt(k, e, n) >= 0) { iGridRow = e; iGridCol = n - 1; found = true; }
                    }
                return EUiNone;
                }
            if (aKey == EUiBack) { iInGrid = false; return EUiNone; }
            if (aKey == EUiLeft) { if (iGridCol == 0) iInGrid = false; else iGridCol--; }
            else if (aKey == EUiRight) { if (iGridCol < 4) iGridCol++; }
            else if (aKey == EUiUp || aKey == EUiDown) iGridRow ^= 1;
            else if (aKey == EUiOk)
                {
                const int ti = TrackAt(iSel[ESolo], iGridRow, iGridCol + 1);
                if (ti >= 0) { iRaceFile = iTracks[ti].iFile; iScreen = EOpponent; iOppSel = 1; }
                }
            return EUiNone;
        case EOpponent:
            {
            int rows[8], n; OppRows(rows, n);
            if (aKey == EUiUp) iOppSel = (iOppSel + n - 1) % n;
            else if (aKey == EUiDown) iOppSel = (iOppSel + 1) % n;
            else if (aKey == EUiBack) iScreen = ESolo;
            else if (aKey == EUiOk) { iRaceOpp = rows[iOppSel]; iPlayers = 1; iSeriesN = 0; iScreen = ESolo; return EUiRace; }
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
            if (aKey == EUiBack || (aKey == EUiOk && iSeriesSel == 10)) { iScreen = EHotSeat; return EUiNone; }
            if (aKey == EUiUp) iSeriesSel = (iSeriesSel + 10) % 11;
            else if (aKey == EUiDown) iSeriesSel = (iSeriesSel + 1) % 11;
            else if (aKey == EUiOk)
                {
                const int env = iSeriesSel / 5, ser = iSeriesSel % 5;
                iSeriesN = 0;
                for (int n = 1; n <= 5 && iSeriesN < 8; n++) { int ti = TrackAt(ser, env, n); if (ti >= 0) iSeriesFiles[iSeriesN++] = iTracks[ti].iFile; }
                if (!iSeriesN) return EUiNone;
                int m = 0;
                for (int p = 0; p < KMaxPlayers; p++) if (iNames[p][0]) { if (m != p) SCpy(iNames[m], iNames[p], KNameLen); m++; }
                for (int p = m; p < KMaxPlayers; p++) iNames[p][0] = 0;
                iPlayers = m; iRaceFile = iSeriesFiles[0]; iRaceOpp = EOppNone;
                return EUiRace;
                }
            return EUiNone;
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
    char a[64], b[64]; SCpy(a, msg, 63); b[0] = 0;
    if (tx.Width(a) > x1 - x0 - 20)
        {
        const int l = SLen(a); int best = -1;
        for (int i = 0; i < l; i++) if (a[i] == ' ' && (best < 0 || (i - l / 2) * (i - l / 2) < (best - l / 2) * (best - l / 2))) best = i;
        if (best > 0) { SCpy(b, a + best + 1, 63); a[best] = 0; }
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
