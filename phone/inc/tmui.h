// tmui.h - the menus after TrackMania United Forever's (0.11): drawn in plain C++ into the 640 x 200 frame, so the
// same code runs on the phone and in the PC preview. Text comes from glyph atlases (the phone's fonts drawn once at
// start-up; a TrueType font on the PC). Nothing is drawn a frame unless the menu changes.
#ifndef TMUI_H
#define TMUI_H

typedef unsigned short TmPix;   // RGB 565

// a font: ASCII 32..126 side by side in one strip, an 8-bit coverage per pixel
struct TmFont
    {
    int iH, iAsc;
    int iX[95], iW[95];
    int iStripW;
    unsigned char* iMask;       // iStripW * iH
    int Width(const char* aText) const;
    };

// a picture from the menu's pack (tm9300.ui: cut from the game's own files on the PC)
struct TmImage { int w, h; const TmPix* px; const unsigned char* a; };
class TmUiPack
    {
public:
    TmUiPack() : iN(0) {}
    bool Load(const unsigned char* aData, int aLen);    // aData stays the caller's
    const TmImage* Find(const char* aName) const;
    enum { KMax = 80 };
    int iN;
    char iNames[KMax][17];
    TmImage iImg[KMax];
    };

// the menu's view of an installed track
struct TmUiTrack
    {
    char iName[20];             // the file's name without .tmb (rallyA1)
    int iEnv;                   // 0 Rally, 1 Snow
    int iSeries;                // 0 White (A) .. 4 Black (E)
    int iNum;                   // 1..5 (E: 1)
    int iFile;                  // its index among the installed files
    const TmPix* iThumb;        // iThumbW x iThumbH (0: none)
    int iThumbW, iThumbH;
    int iMedal[4];              // ms: author, gold, silver, bronze (0: not known)
    int iBest;                  // the record, in 20 ms steps (0: none)
    };

enum TmUiKey { EUiUp, EUiDown, EUiLeft, EUiRight, EUiOk, EUiBack, EUiChar, EUiDel };
enum TmUiAction { EUiNone, EUiQuit, EUiRace };
// the opponents, as the app's ghost modes
enum TmUiOpp { EOppNone, EOppNadeo, EOppGold, EOppSilver, EOppBronze, EOppRecord, EOppLast };

class TmMenu
    {
public:
    enum TScreen { ETitle, EMain, ECampaign, EMode, ESolo, EOpponent, EParty, EHotSeat, ESeries };
    enum { KMaxTracks = 64, KMaxPlayers = 8, KNameLen = 12 };
    TmMenu();
    ~TmMenu();
    // the fonts (big, text, small), the installed tracks (copied); the background is drawn here once
    bool Init(const TmFont* aBig, const TmFont* aText, const TmFont* aSmall, const TmUiTrack* aTracks, int aN, const TmUiPack* aPack = 0);
    void SetBest(int aFile, int aSteps);     // a new record on a track (its medal shows on the grid)
    int Key(int aKey, int aChar = 0);        // TmUiAction
    void Draw(TmPix* aFrame, int aStride);   // the whole screen
    void Show(TScreen aScreen) { iScreen = aScreen; iDirty = true; }
    bool iDirty;
    TScreen iScreen;
    const char* iVersion;
    // after EUiRace: the race to start
    int iRaceFile;              // the track (installed file index)
    int iRaceOpp;               // TmUiOpp
    int iPlayers;               // 1: solo; 2..8 the hot seat
    char iNames[KMaxPlayers][KNameLen + 1];
    int iAttempts;              // the hot seat: rounds per track
    int iSeriesFiles[8], iSeriesN;   // the hot seat: the series' tracks in order
    int Medals() const;         // medals won over all tracks (a medal counts with those under it)
private:
    void DrawBackground();
    void DrawLeft(TmPix* aF, int aS, const char* aTitle, const char* aSub, int aIcon);
    void DrawItem(TmPix* aF, int aS, int aY, int aIcon, const char* aText, bool aSel, bool aDim = false);
    // a list screen: its items (icon, text, enabled, at the bottom), the selection moving over the enabled ones only
    struct TItem { int iIcon; const char* iText; bool iOn; int iRow; };
    int Items(TScreen aScreen, TItem* aItems) const;
    void DrawList(TmPix* aF, int aS, TScreen aScreen, bool aFocus);
    int ListKey(TScreen aScreen, int aKey);   // the item chosen (Ok / Right) or -1; -2 back
    void DrawMedalsPanel(TmPix* aF, int aS);
    void DrawGrid(TmPix* aF, int aS);
    void DrawOpponent(TmPix* aF, int aS);
    void DrawHotSeat(TmPix* aF, int aS);
    void DrawSeries(TmPix* aF, int aS);
    void DrawTitle(TmPix* aF, int aS);
    int TrackAt(int aSeries, int aEnv, int aNum) const;
    int SeriesCount(int aEnv, int aSeries) const;
    void OppRows(int* aRows, int& aN) const;
    const TmUiPack* iPack;
    const TmFont* iBig;
    const TmFont* iText;
    const TmFont* iSmall;
    TmUiTrack iTracks[KMaxTracks];
    int iNTracks;
    TmPix* iBg;                 // the background (gradient and globe), 640 x 200
    int iSel[10];               // the selected item per screen
    bool iInGrid;               // Solo: the focus in the track grid
    int iGridCol, iGridRow;
    int iOppSel;
    int iHsRow;                 // hot seat: 0 attempts, 1..8 the player slots, 9 start
    int iEditing;               // hot seat: the slot whose name is being typed (-1 none)
    int iSeriesSel;
    int iTitleTick;
    };

// drawing helpers shared with the race's panel
namespace TmDraw
    {
    TmPix Rgb(int aR, int aG, int aB);
    TmPix Mix(TmPix aA, TmPix aB, int aT256);          // aA * (1 - t) + aB * t
    void Fill(TmPix* aF, int aS, int aX, int aY, int aW, int aH, TmPix aC);
    void Shade(TmPix* aF, int aS, int aX, int aY, int aW, int aH, int aT256, TmPix aC);    // blend a colour over
    void Round(TmPix* aF, int aS, int aX, int aY, int aW, int aH, int aR, TmPix aTop, TmPix aBottom);
    void RoundLine(TmPix* aF, int aS, int aX, int aY, int aW, int aH, int aR, TmPix aC);
    int Text(TmPix* aF, int aS, const TmFont& aFont, int aX, int aY, const char* aText, TmPix aC, int aMaxX = 640);
    void TextCentre(TmPix* aF, int aS, const TmFont& aFont, int aX0, int aX1, int aY, const char* aText, TmPix aC);
    void Clock(TmPix* aF, int aS, const TmFont& aFont, int aX, int aY, const char* aText, TmPix aC);    // equal digit cells
    int ClockWidth(const TmFont& aFont, const char* aText);
    void Coin(TmPix* aF, int aS, int aCx, int aCy, int aR, int aKind);      // 0 Nadeo, 1 gold, 2 silver, 3 bronze, 4 record, 5 none
    void Icon(TmPix* aF, int aS, int aX, int aY, int aKind);
    void Blit(TmPix* aF, int aS, int aX, int aY, const TmImage* aImg, int aClipY0 = 0, int aClipY1 = 200);
    void SetPack(const TmUiPack* aPack);             // the game's pictures for Icon and Coin (0: drawn ones)
    // the race's timer in the game's LED digits ("-0:12.39"), its width
    int Led(TmPix* aF, int aS, int aX, int aY, const char* aText);
    int LedWidth(const char* aText);
    void FormatTime(char* aOut, int aMs);            // m:ss.cc
    }

enum TmIcon { EIcoPlayer, EIcoParty, EIcoQuit, EIcoBack, EIcoFlagWhite, EIcoFlagGreen, EIcoFlagBlue, EIcoFlagRed, EIcoFlagBlack,
    EIcoLock, EIcoLan, EIcoRally, EIcoSnow, EIcoCar, EIcoOnline, EIcoEditors, EIcoProfile, EIcoUnited, EIcoNations, EIcoStar,
    EIcoMyCampaign, EIcoBrowse, EIcoRaces, EIcoPlatforms, EIcoStunts, EIcoPuzzle, EIcoMedals };

#endif
