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
    {
    // the game's font, when its sheet is there, at the same heights
    int fn; sprintf(p, "%s/united.fnt", argv[1]); unsigned char* fd = ReadAll(p, fn);
    TmFont sheet;
    if (fd && TmFontLoad(fd, fn, sheet))
        {
        TmFont nb, nt, ns;
        const char* e = getenv("TMFONT"); int hb = 15, ht = 11, hs = 10;
        if (e) sscanf(e, "%d,%d,%d", &hb, &ht, &hs);
        TmFontScale(sheet, hb, true, nb); TmFontScale(sheet, ht, false, nt); TmFontScale(sheet, hs, false, ns);
        big = nb; text = nt; small = ns;
        printf("the game's font: %d %d %d\n", big.iH, text.iH, small.iH);
        }
    }
    int pn; unsigned char* pd = ReadAll(argv[2], pn);
    TmUiPack pack; if (!pack.Load(pd, pn)) printf("no pack\n");
    TmUiTrack tr[128]; int nt = 0;
    for (int i = 5; i < argc && nt < 128; i++)
        {
        TmUiTrack& t = tr[nt]; memset(&t, 0, sizeof(t));
        strncpy(t.iName, argv[i], 19);
        static const char* const env[7] = { "rally", "snow", "desert", "island", "bay", "coast", "stadium" };
        int kk = 5; t.iEnv = 0;
        for (int e = 0; e < 7; e++) if (!strncmp(argv[i], env[e], strlen(env[e]))) { t.iEnv = e; kk = (int) strlen(env[e]); }
        const char* k = argv[i] + kk;
        t.iSeries = k[0] - 'A'; t.iNum = k[1] ? k[1] - '0' : 1; t.iFile = nt;
        sprintf(p, "%s/%s.tmt", argv[3], argv[i]);
        int n; unsigned char* d = ReadAll(p, n);
        if (d) { int* h = (int*) d; t.iThumbW = h[1]; t.iThumbH = h[2]; for (int m = 0; m < 4; m++) t.iMedal[m] = h[3 + m]; t.iThumb = (const TmPix*) (d + 28); }
        if (nt == 0) t.iBest = 19500 / 20;          // a record on the first (a gold medal shows)
        if (nt == 1 && t.iMedal[2]) t.iBest = t.iMedal[2] / 20;     // silver on the second
        nt++;
        }
    TmMenu m; m.iVersion = "0.11";
    m.Init(&big, &text, &small, tr, nt, &pack);
    static TmPix frame[640 * 200];
    struct { const char* name; const char* keys; } shots[] = {
        { "title", "" }, { "main", "o" }, { "campaign", "oo" }, { "mode", "ooo" }, { "difficulty", "oooo" }, { "grid", "ooooo" },
        { "grid2", "ooooodr" }, { "lockmsg", "ooooorro" }, { "opponent", "oooooo" }, { "party", "odo" }, { "hotseat", "odoo" }, { "series", "odooo" } };
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
    // 0.14: the hot seat's chooser and the race's dialogs (over a grey 3D view)
    {
    TmMenu mm; mm.iVersion = "0.14"; mm.Init(&big, &text, &small, tr, nt, &pack);
    const char* prof[] = { "KNUCKLES", "PAVEL", "TERE", "ANNA", "BOB", "CYRIL", "DORA", "EMIL", "FRANZ" };
    for (int i = 0; i < 9; i++) { strcpy(mm.iProfiles[i], prof[i]); } mm.iNProfiles = 9;
    mm.Key(EUiOk); mm.Key(EUiDown); mm.Key(EUiOk); mm.Key(EUiOk);    // party, hot seat
    mm.Key(EUiOk); mm.Key(EUiOk);                     // slot 1: chooser, KNUCKLES
    mm.Key(EUiRight); mm.Key(EUiOk); mm.Key(EUiDown); mm.Key(EUiOk);     // slot 2: PAVEL... TERE
    mm.Key(EUiDown); mm.Key(EUiOk);                   // slot 3: chooser open
    for (int i = 0; i < 9; i++) mm.Key(EUiDown);
    mm.Draw(frame, 640); sprintf(p, "%s_chooser.ppm", argv[4]); Save(p, frame);
    mm.Key(EUiBack); mm.Key(EUiUp); mm.Key(EUiUp); mm.Key(EUiRight); mm.Key(EUiDown); mm.Key(EUiRight); mm.Key(EUiRight);
    mm.Draw(frame, 640); sprintf(p, "%s_hotseat2.ppm", argv[4]); Save(p, frame);
    }
    for (int d = 0; d < 4; d++)
        {
        for (int y = 0; y < 200; y++) for (int x = 0; x < 640; x++) frame[y * 640 + x] = (x < 160 || x >= 480) ? 0x0841 : (TmPix) (0x4208 + ((y >> 3) & 1) * 0x0841);
        if (d == 0) { const char* r[] = { "Choose opponents", "Restart", "Race panels: on", "Continue|Quit" }; TmDlg::Buttons(frame, 640, text, small, 170, 470, "Race menu", r, 4, 3, true); }
        if (d == 1) { const char* r[] = { "Continue", "Give up", "Quit" }; TmDlg::Buttons(frame, 640, text, small, 170, 470, "Hot Seat", r, 3, 0, true); }
        if (d == 2)
            {
            TmDlg::TRow r[] = { { "Bronze medal", 25070, false }, { "Silver medal", 20350, true }, { "Your record", 19910, false }, { "Your last run", 21000, false },
                { "Gold medal", 17830, true }, { "Nadeo medal", 16020, false }, { "Show names", -1, true } };
            TmDlg::Ghosts(frame, 640, text, small, 170, 470, r, 7, 4);
            }
        if (d == 3) TmDlg::Message(frame, 640, text, 170, 470, "Now it's player KNUCKLES's turn!");
        static const char* nm[] = { "dlg_race", "dlg_hotseat", "dlg_ghosts", "dlg_msg" };
        sprintf(p, "%s_%s.ppm", argv[4], nm[d]); Save(p, frame);
        }
    return 0;
    }
