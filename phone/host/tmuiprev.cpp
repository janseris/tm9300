// the menus on the PC (0.11): fonts from atlases (mkfont.py), the game's pictures (tm9300.ui), the thumbnails (.tmt),
// a key sequence per screen, each screen into a .ppm
//   ./tmuiprev fonts_dir pack.ui tmt_dir out_prefix track1 track2 ...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmui.h"
#include "tmgbx.h"
#include "tmjpeg.h"
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

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
// 0.19: Browse's folders on the PC: $TMROOT stands for Documents\\TrackMania\\Tracks\\ (the phone's lister: tm9300_app.cpp)
struct THostFolders : MTmFolders
    {
    TmMenu* m; int next; char paths[256][512]; int files[256]; int n;
    int ListFolder(const char* aPath, TmUiEntry* aOut, int aMax, bool aCampaigns)
        {
        int k = 0;
        if (!strncasecmp(aPath, "Campaigns\\", 10)) { if (!aPath[10]) { aOut[k].iKind = TmUiEntry::EFolder; strcpy(aOut[k].iName, "United"); aOut[k].iLabel[0] = 0; k++; } return k; }
        char dir[512]; snprintf(dir, sizeof(dir), "%s/%s", getenv("TMROOT"), aPath);
        for (char* q = dir; *q; q++) if (*q == '\\') *q = '/';
        DIR* d = opendir(dir); if (!d) return aPath[0] ? -1 : 0;
        if (!aPath[0] && aCampaigns) { aOut[k].iKind = TmUiEntry::EFolder; strcpy(aOut[k].iName, "Campaigns"); aOut[k].iLabel[0] = 0; k++; }
        struct dirent* e; int firstTrack;
        char names[256][256]; int nn = 0;
        while ((e = readdir(d)) && nn < 256) if (e->d_name[0] != '.') strcpy(names[nn++], e->d_name);
        closedir(d);
        for (int a = 1; a < nn; a++) for (int b = a; b > 0 && strcasecmp(names[b - 1], names[b]) > 0; b--) { char t[256]; strcpy(t, names[b]); strcpy(names[b], names[b - 1]); strcpy(names[b - 1], t); }
        for (int i = 0; i < nn && k < aMax; i++)
            {
            char full[1024]; snprintf(full, sizeof(full), "%s/%s", dir, names[i]); struct stat st; stat(full, &st);
            if (S_ISDIR(st.st_mode)) { aOut[k].iKind = TmUiEntry::EFolder; strncpy(aOut[k].iName, names[i], 39); aOut[k].iName[39] = 0; aOut[k].iLabel[0] = 0; k++; }
            }
        firstTrack = k;
        for (int i = 0; i < nn && k < aMax; i++)
            {
            char full[1024]; snprintf(full, sizeof(full), "%s/%s", dir, names[i]); struct stat st; stat(full, &st);
            if (S_ISDIR(st.st_mode) || strcasecmp(names[i] + strlen(names[i]) - 4, ".gbx")) continue;
            FILE* f = fopen(full, "rb"); unsigned char h[17]; if (!f || fread(h, 1, 17, f) != 17) { if (f) fclose(f); continue; }
            int hs = TmGbxHeaderSize(h, 17); unsigned char* b = new unsigned char[hs]; memcpy(b, h, 17); if (fread(b + 17, 1, hs - 17, f)) {} fclose(f);
            char env[16], title[24], uid[40], au[24]; int med[4];
            if (TmGbxHeader(b, hs, env, 16, title, 24, med, uid, 40, au, 24)) { delete[] b; continue; }
            static const char* const KE[5] = { "Rally", "Alpine", "Speed", "Island", "Bay" };
            int ev = -1; for (int q = 0; q < 5; q++) if (!strcmp(env, KE[q])) ev = q;
            if (ev < 0) { delete[] b; continue; }
            int file = -1; for (int q = 0; q < n; q++) if (!strcmp(paths[q], full)) file = files[q];
            if (file < 0)
                {
                TmUiTrack t; memset(&t, 0, sizeof(t)); t.iEnv = ev; t.iSeries = 5; t.iNum = 1; t.iFile = next++;
                strcpy(t.iTitle, title); strcpy(t.iAuthor, au); for (int q = 0; q < 4; q++) t.iMedal[q] = med[q];
                m->AddTrack(t); strcpy(paths[n], full); files[n++] = file = t.iFile;
                }
            delete[] b;
            aOut[k].iKind = TmUiEntry::ETrack; aOut[k].iTrack = file; strcpy(aOut[k].iName, title); aOut[k].iLabel[0] = 0; k++;
            }
        (void) firstTrack;
        return k;
        }
    void Picture(int aFile, TmUiTrack& aT)
        {
        for (int q = 0; q < n; q++) if (files[q] == aFile)
            {
            int sz; FILE* f = fopen(paths[q], "rb"); fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
            unsigned char* d = new unsigned char[sz]; if (fread(d, 1, sz, f)) {} fclose(f);
            for (int i = 0; i + 15 < sz; i++) if (!memcmp(d + i, "<Thumbnail.jpg>", 15))
                {
                TmPix* px = new TmPix[80 * 60];
                if (TmJpegDecode(d + i + 15, sz - i - 15, px, 80, 60, true)) { aT.iThumb = px; aT.iThumbW = 80; aT.iThumbH = 60; }
                break;
                }
            delete[] d;
            }
        }
    };

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
        // 0.18: a fan track (f_...): Browse, its environment and name from the picture's trailer
        if (!strncmp(argv[i], "f_", 2))
            {
            t.iSeries = 5; t.iNum = 1; t.iEnv = 0; strncpy(t.iTitle, argv[i] + 2, 23);
            const int tr = d ? 28 + t.iThumbW * t.iThumbH * 2 : 0;
            if (d && tr + 28 <= n && !memcmp(d + tr, "TMTN", 4)) { t.iEnv = d[tr + 4]; memcpy(t.iTitle, d + tr + 5, 23); t.iTitle[23] = 0; }
            }
        if (nt == 0) t.iBest = 19500 / 20;          // a record on the first (a gold medal shows)
        if (nt == 1 && t.iMedal[2]) t.iBest = t.iMedal[2] / 20;     // silver on the second
        nt++;
        }
    TmMenu m; m.iVersion = "0.11";
    m.Init(&big, &text, &small, tr, nt, &pack);
    static TmPix frame[640 * 200];
    struct { const char* name; const char* keys; } shots[] = {
        { "title", "" }, { "main", "o" }, { "campaign", "oo" }, { "mode", "ooo" }, { "difficulty", "oooo" }, { "grid", "ooooo" },
        { "grid2", "ooooodr" }, { "lockmsg", "ooooorro" }, { "opponent", "oooooo" }, { "party", "odo" }, { "browse", "oodo" }, { "browse2", "oododd" }, { "browseopp", "oodoo" }, { "hotseat", "odoo" }, { "series", "odooo" } };
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
    // 0.19: Browse as the game's explorer, the hot seat's track chooser ($TMROOT)
    if (getenv("TMROOT"))
        {
        // (keys from the screen each starts on: Browse from Play Solo's list, the chooser from the hot seat's series)
        struct { const char* name; int start; const char* keys; } bs[] = {
            { "x_top", 0, "" }, { "x_track", 0, "dd" }, { "x_my", 0, "odo" }, { "x_my2", 0, "ododdd" }, { "x_snow", 0, "odoooo" }, { "x_up", 0, "odooool" },
            { "x_series", 1, "" }, { "x_series2", 1, "ld" }, { "x_hot", 1, "ldo" }, { "x_hot2", 1, "ldododdo" }, { "x_hotplay", 1, "ldododdodd" }, { "x_seriesD", 1, "ddddddddddddd" }, { "x_official", 1, "lo" } };
        for (unsigned q = 0; q < sizeof(bs) / sizeof(bs[0]); q++)
            {
            static THostFolders hf; hf.next = 1000; hf.n = 0;
            TmMenu* mm = new TmMenu; mm->iVersion = "0.19"; mm->Init(&big, &text, &small, tr, nt, &pack);
            hf.m = mm; mm->iFolders = &hf;
            if (bs[q].start == 0) { mm->Show(TmMenu::ECampaign); for (int z = 0; z < 4; z++) mm->Key(EUiDown); mm->Key(EUiOk); }
            else { strcpy(mm->iNames[0], "KNUCKLES"); strcpy(mm->iNames[1], "PAVEL"); mm->Show(TmMenu::ESeries); }
            for (const char* k = bs[q].keys; *k; k++)
                {
                int key = *k == 'o' ? EUiOk : *k == 'd' ? EUiDown : *k == 'u' ? EUiUp : *k == 'l' ? EUiLeft : *k == 'r' ? EUiRight : EUiBack;
                mm->Key(key);
                }
            mm->Draw(frame, 640);
            sprintf(p, "%s_%s.ppm", argv[4], bs[q].name); Save(p, frame);
            delete mm;
            }
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
