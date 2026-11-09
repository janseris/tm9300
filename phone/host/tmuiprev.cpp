// the menus on the PC (0.11): fonts from atlases (mkfont.py), the game's pictures (tm9300.ui), the thumbnails (.tmt),
// a key sequence per screen, each screen into a .ppm
//   ./tmuiprev fonts_dir pack.ui tmt_dir out_prefix track1 track2 ...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmui.h"

static unsigned char* ReadAll(const char* p, int& n)
    {
    FILE* f = fopen(p, "rb"); if (!f) { n = 0; return 0; }
    fseek(f, 0, SEEK_END); n = (int) ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = new unsigned char[n]; if (fread(d, 1, n, f) != (size_t) n) n = 0; fclose(f); return d;
    }
static bool LoadFont(const char* p, TmFont& f)
    {
    int n; unsigned char* d = ReadAll(p, n); if (!d || n < 16) return false;
    int* h = (int*) (d + 4); f.iH = h[0]; f.iAsc = h[1]; f.iStripW = h[2];
    memcpy(f.iX, d + 16, 95 * 4); memcpy(f.iW, d + 16 + 380, 95 * 4); f.iMask = d + 16 + 760; return true;
    }
static void Save(const char* name, const TmPix* f)
    {
    FILE* o = fopen(name, "wb"); fprintf(o, "P6 640 200 255\n");
    for (int i = 0; i < 640 * 200; i++) { int c = f[i]; unsigned char rgb[3] = { (unsigned char) (((c >> 11) & 31) << 3), (unsigned char) (((c >> 5) & 63) << 2), (unsigned char) ((c & 31) << 3) }; fwrite(rgb, 1, 3, o); }
    fclose(o);
    }
int main(int argc, char** argv)
    {
    char p[512];
    TmFont big, text, small;
    sprintf(p, "%s/big.fnt", argv[1]); LoadFont(p, big);
    sprintf(p, "%s/text.fnt", argv[1]); LoadFont(p, text);
    sprintf(p, "%s/small.fnt", argv[1]); LoadFont(p, small);
    int pn; unsigned char* pd = ReadAll(argv[2], pn);
    TmUiPack pack; if (!pack.Load(pd, pn)) printf("no pack\n");
    TmUiTrack tr[64]; int nt = 0;
    for (int i = 5; i < argc && nt < 64; i++)
        {
        TmUiTrack& t = tr[nt]; memset(&t, 0, sizeof(t));
        strncpy(t.iName, argv[i], 19);
        t.iEnv = argv[i][0] == 's' ? 1 : 0;
        const char* k = argv[i] + (t.iEnv ? 4 : 5);
        t.iSeries = k[0] - 'A'; t.iNum = k[1] ? k[1] - '0' : 1; t.iFile = nt;
        sprintf(p, "%s/%s.tmt", argv[3], argv[i]);
        int n; unsigned char* d = ReadAll(p, n);
        if (d) { int* h = (int*) d; t.iThumbW = h[1]; t.iThumbH = h[2]; for (int m = 0; m < 4; m++) t.iMedal[m] = h[3 + m]; t.iThumb = (const TmPix*) (d + 28); }
        if (nt == 0) t.iBest = 19500 / 20;          // a record on the first (a gold medal shows)
        nt++;
        }
    TmMenu m; m.iVersion = "0.11";
    m.Init(&big, &text, &small, tr, nt, &pack);
    static TmPix frame[640 * 200];
    struct { const char* name; const char* keys; } shots[] = {
        { "title", "" }, { "main", "o" }, { "campaign", "oo" }, { "mode", "ooo" }, { "difficulty", "oooo" }, { "grid", "ooooo" },
        { "grid2", "ooooodr" }, { "opponent", "oooooo" }, { "party", "odo" }, { "hotseat", "odoo" }, { "series", "odooo" } };
    for (unsigned s = 0; s < sizeof(shots) / sizeof(shots[0]); s++)
        {
        TmMenu mm; mm.iVersion = "0.11"; mm.Init(&big, &text, &small, tr, nt, &pack);
        for (const char* k = shots[s].keys; *k; k++)
            {
            int key = *k == 'o' ? EUiOk : *k == 'd' ? EUiDown : *k == 'u' ? EUiUp : *k == 'l' ? EUiLeft : *k == 'r' ? EUiRight : EUiBack;
            if (!strcmp(shots[s].name, "hotseat") && k[1] == 0) { mm.Key(key); mm.Key(EUiOk); mm.Key(EUiChar, 'k'); mm.Key(EUiChar, 'n'); mm.Key(EUiChar, 'u'); mm.Key(EUiOk); mm.Key(EUiRight); mm.Key(EUiOk); mm.Key(EUiChar, 't'); mm.Key(EUiChar, 'e'); mm.Key(EUiOk); continue; }
            mm.Key(key);
            }
        mm.Draw(frame, 640);
        sprintf(p, "%s_%s.ppm", argv[4], shots[s].name); Save(p, frame);
        }
    return 0;
    }
