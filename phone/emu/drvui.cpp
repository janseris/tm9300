// the menus in the ARM emulator (0.11): the same screens as host/tmuiprev, drawn by the phone's compiler's code
#include "tmui.h"
static char* gHeap = (char*) 0x400000;
void* operator new(unsigned int n) { void* p = gHeap; gHeap += (n + 7) & ~7; return p; }
void* operator new[](unsigned int n) { return operator new(n); }
void operator delete(void*) {}
void operator delete[](void*) {}
extern "C" void __pure_virtual() {}
extern "C" void* memset(void* d, int c, unsigned int n) { char* p = (char*) d; while (n--) *p++ = (char) c; return d; }
extern "C" void* memcpy(void* d, const void* s, unsigned int n) { char* p = (char*) d; const char* q = (const char*) s; while (n--) *p++ = *q++; return d; }
static TmFont gF[3]; static TmUiPack gPack; static TmUiTrack gT[2];
static bool LoadFont(const unsigned char* d, TmFont& f)
    {
    const int* h = (const int*) (d + 4); f.iH = h[0]; f.iAsc = h[1]; f.iStripW = h[2];
    const int* x = (const int*) (d + 16); for (int i = 0; i < 95; i++) { f.iX[i] = x[i]; f.iW[i] = x[95 + i]; }
    f.iMask = (unsigned char*) d + 16 + 760; return true;
    }
extern "C" int emu_init() { return 0; }
// fonts at f0..f2, the pack at aPack, two tracks (rallyA1 with its thumbnail at aThumb, snowA1 without); the keys as
// a string ("o" ok, "d" down ...); the screen into aFrame
extern "C" int emu_ui(const int* aArgs)
    {
    const unsigned char* f0 = (const unsigned char*) aArgs[0]; const unsigned char* f1 = (const unsigned char*) aArgs[1]; const unsigned char* f2 = (const unsigned char*) aArgs[2];
    const unsigned char* aPack = (const unsigned char*) aArgs[3]; const int aPackLen = aArgs[4]; const unsigned char* aThumb = (const unsigned char*) aArgs[5];
    const char* aKeys = (const char*) aArgs[6]; TmPix* aFrame = (TmPix*) aArgs[7];
    LoadFont(f0, gF[0]); LoadFont(f1, gF[1]); LoadFont(f2, gF[2]);
    if (!gPack.Load(aPack, aPackLen)) return -1;
    const int* h = (const int*) aThumb;
    for (int i = 0; i < 2; i++) { TmUiTrack& t = gT[i]; char* n = t.iName; const char* s = i ? "snowA1" : "rallyA1"; while ((*n++ = *s++)); t.iEnv = i; t.iSeries = 0; t.iNum = 1; t.iFile = i;
        t.iThumb = i ? 0 : (const TmPix*) (aThumb + 28); t.iThumbW = h[1]; t.iThumbH = h[2]; for (int m = 0; m < 4; m++) t.iMedal[m] = h[3 + m]; t.iBest = i ? 0 : 975; }
    TmMenu* m = new TmMenu; m->iVersion = "0.11";
    if (!m->Init(&gF[0], &gF[1], &gF[2], gT, 2, &gPack)) return -2;
    for (const char* k = aKeys; *k; k++) m->Key(*k == 'o' ? EUiOk : *k == 'd' ? EUiDown : *k == 'r' ? EUiRight : EUiBack);
    m->Draw(aFrame, 640);
    delete m;
    return 0;
    }
