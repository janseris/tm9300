// tm9300_app.cpp - Series 80 (Nokia 9300) application shell for TrackMania 9300.
// 0.11: the game's menus (tmui: the title, Play Solo with its colours and tracks, the opponent, Party Mode's hot seat)
// and the race laid out as the game's (the LED timer, the countdown, the opponent and the players on the left, the
// track and the speed on the right). After Hrimfell's shell: direct screen access when the phone allows it, a CIdle
// loop, keys by scan code.
#include <eikapp.h>
#include <eikdoc.h>
#include <eikappui.h>
#include <eikenv.h>
#include <coecntrl.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>
#include <w32std.h>
#include <f32file.h>
#include <e32std.h>
#include <cknenv.h>
#include <skin.h>
#include <hal.h>
#include <es_sock.h>
#include <in_sock.h>
#include "tmcore.h"
#include "tmphys.h"
#include "tmui.h"
#include "fastmem.h"

const TUid KUidTm9300 = { 0x0E3D9310 };
enum { SCR_W = 640, SCR_H = 200 };
enum { VIEW_W = 320, VIEW_H = 200, VIEW_X = (SCR_W - VIEW_W) / 2 };   // the 3D view, in the middle of the screen
_LIT(KTrackDir, "?:\\system\\apps\\tm9300\\");
_LIT(KTrackPattern, "*.tmb");
_LIT(KUiPack, "tm9300.ui");
_LIT(KUiFont, "tm9300.fnt");
_LIT(KBenchDir, "C:\\Data\\TM9300\\");
_LIT(KBenchFile, "C:\\Data\\TM9300\\bench.txt");
_LIT8(KBenchHost, "192.168.137.1");
const TInt KBenchPort = 8000;
enum { BENCH_VARIANTS = 5, BENCH_LOG = 6144 };
enum { BAR_W = 64, BAR_H = 152 };           // the counters, top left (F)
enum { SIDE_W = VIEW_X, RIGHT_X = VIEW_X + VIEW_W };
static const char* const KVersionC = "0.14";
enum { KMaxFiles = 64 };

// the benchmark's log to the PC (the local server's /results, into its uploads folder), after Hrimfell's
class CBenchSender : public CActive
    {
public:
    CBenchSender() : CActive(EPriorityStandard) { CActiveScheduler::Add(this); }
    ~CBenchSender() { Cancel(); CloseSock(); delete iBody; if (iServOpen) iServ.Close(); }
    TInt iResult;               // 0 sending, 1 sent, < 0 failed
    void Send(const TDesC8& aLog)
        {
        if (iState != EIdle) return;
        delete iBody; iBody = 0;
        iBody = HBufC8::New(aLog.Length() + 256);
        if (!iBody) { iResult = -1; return; }
        TPtr8 b = iBody->Des();
        b.Format(_L8("POST /results?name=tm9300_bench HTTP/1.0\r\nHost: 192.168.137.1:8000\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"), aLog.Length());
        b.Append(aLog);
        if (!iServOpen) { if (iServ.Connect() != KErrNone) { iResult = -2; return; } iServOpen = ETrue; }
        if (iSock.Open(iServ, KAfInet, KSockStream, KProtocolInetTcp) != KErrNone) { iResult = -3; return; }
        iSockOpen = ETrue;
        TInetAddr addr;
        TBuf<32> host; host.Copy(KBenchHost);
        addr.Input(host);
        addr.SetPort(KBenchPort);
        iState = EConnecting; iResult = 0;
        iSock.Connect(addr, iStatus);
        SetActive();
        }
private:
    enum TState { EIdle, EConnecting, ESending, EReply };
    void DoCancel() { if (iSockOpen) iSock.CancelAll(); }
    void RunL()
        {
        TInt err = iStatus.Int();
        if (err != KErrNone && !(iState == EReply && err == KErrEof && iReply.Length())) { CloseSock(); iResult = err < 0 ? err : -9; return; }
        switch (iState)
            {
            case EConnecting: iState = ESending; iSock.Write(*iBody, iStatus); SetActive(); break;
            case ESending: iState = EReply; iReply.Zero(); iSock.RecvOneOrMore(iReply, 0, iStatus, iLen); SetActive(); break;
            case EReply: CloseSock(); iResult = (iReply.Length() >= 12 && iReply.Mid(9, 3) == _L8("200")) ? 1 : -8; break;
            default: break;
            }
        }
    TInt RunError(TInt aError) { CloseSock(); iResult = aError; return KErrNone; }
    void CloseSock() { if (iSockOpen) { iSock.CancelAll(); iSock.Close(); iSockOpen = EFalse; } iState = EIdle; }
    RSocketServ iServ; RSocket iSock;
    TBool iServOpen, iSockOpen;
    TState iState;
    HBufC8* iBody;
    TBuf8<64> iReply;
    TSockXfrLength iLen;
    };

// keys: driving: the arrows (up throttle, down brake / reverse, left / right steer), Enter back to the last checkpoint,
// Backspace the start, Esc the pause; H the race's panels, F the counters; V the flythrough, B the benchmark
enum { KEY_LEFT = 1, KEY_RIGHT = 2, KEY_UP = 4, KEY_DOWN = 8 };

// a 3 x 5 font for the counters (digits, a few letters), drawn into the frame
static const TUint16 KFont[] =
    {
    0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,   // 0-9 (5 rows of 3 bits, top row high)
    };
static void DrawDigit(TUint16* aFrame, TInt aStride, TInt aX, TInt aY, TInt aD, TUint16 aCol)
    {
    if (aD < 0 || aD > 9) return;
    TUint16 g = KFont[aD];
    for (TInt r = 0; r < 5; r++)
        for (TInt c = 0; c < 3; c++)
            if (g & (1 << (14 - r * 3 - c)))
                {
                TUint16* p = aFrame + (aY + r * 2) * aStride + aX + c * 2;
                p[0] = p[1] = p[aStride] = p[aStride + 1] = aCol;
                }
    }

static void DrawNumber(TUint16* aFrame, TInt aStride, TInt aX, TInt aY, TInt aN, TUint16 aCol)
    {
    TInt d[10], n = 0;
    if (aN < 0) aN = 0;
    do { d[n++] = aN % 10; aN /= 10; } while (aN && n < 10);
    for (TInt i = n - 1; i >= 0; i--, aX += 8) DrawDigit(aFrame, aStride, aX, aY, d[i], aCol);
    }

// 0.8: text in the phone's own fonts: drawn with a bitmap context into a small bitmap, its rows then copied into the
// frame (the frame is not a bitmap the system can draw on)
class CTextCanvas
    {
public:
    CTextCanvas() : iBmp(0), iDev(0), iGc(0), iW(0), iH(0) {}
    ~CTextCanvas() { delete iGc; delete iDev; delete iBmp; }
    TBool Create(TInt aW, TInt aH)
        {
        iBmp = new CFbsBitmap;
        if (!iBmp || iBmp->Create(TSize(aW, aH), EColor64K) != KErrNone) return EFalse;
        TRAPD(err, iDev = CFbsBitmapDevice::NewL(iBmp));
        if (err != KErrNone || !iDev) return EFalse;
        if (iDev->CreateContext(iGc) != KErrNone || !iGc) { iGc = 0; return EFalse; }
        iW = aW; iH = aH;
        Fill(TRect(0, 0, aW, aH), 0);
        return ETrue;
        }
    TBool Ok() const { return iGc != 0; }
    static TRgb Rgb(TUint16 aC) { return TRgb(((aC >> 11) & 31) << 3, ((aC >> 5) & 63) << 2, (aC & 31) << 3); }
    void Fill(const TRect& aR, TUint16 aCol)
        {
        iGc->SetPenStyle(CGraphicsContext::ENullPen); iGc->SetBrushStyle(CGraphicsContext::ESolidBrush);
        iGc->SetBrushColor(Rgb(aCol)); iGc->DrawRect(aR);
        }
    void Frame(const TRect& aR, TUint16 aCol)
        {
        iGc->SetPenStyle(CGraphicsContext::ESolidPen); iGc->SetPenColor(Rgb(aCol)); iGc->SetBrushStyle(CGraphicsContext::ENullBrush);
        iGc->DrawRect(aR);
        }
    // the text with its top at aY; its width back
    TInt Text(const CFont* aFont, const TDesC& aText, TInt aX, TInt aY, TUint16 aCol)
        {
        iGc->UseFont(aFont);
        iGc->SetPenStyle(CGraphicsContext::ESolidPen); iGc->SetPenColor(Rgb(aCol)); iGc->SetBrushStyle(CGraphicsContext::ENullBrush);
        iGc->DrawText(aText, TPoint(aX, aY + aFont->AscentInPixels()));
        iGc->DiscardFont();
        return aFont->TextWidthInPixels(aText);
        }
    // digits in cells as wide as the widest digit (a running clock does not shake); other characters as they are
    void Clock(const CFont* aFont, const TDesC& aText, TInt aX, TInt aY, TUint16 aCol)
        {
        TInt cell = 0;
        for (TInt d = '0'; d <= '9'; d++) { TBuf<1> b; b.Append(TChar(d)); TInt w = aFont->TextWidthInPixels(b); if (w > cell) cell = w; }
        for (TInt i = 0; i < aText.Length(); i++)
            {
            TPtrC c = aText.Mid(i, 1);
            TInt w = aFont->TextWidthInPixels(c);
            if (aText[i] >= '0' && aText[i] <= '9') { Text(aFont, c, aX + (cell - w) / 2, aY, aCol); aX += cell; }
            else { Text(aFont, c, aX, aY, aCol); aX += w; }
            }
        }
    // rows aY0..aY1 into the frame at (aX, aY)
    void CopyTo(TUint16* aFrame, TInt aStride, TInt aX, TInt aY, TInt aY0, TInt aY1)
        {
        if (aY0 < 0) aY0 = 0;
        if (aY1 > iH) aY1 = iH;
        iBmp->LockHeap();
        const TUint16* p = (const TUint16*) iBmp->DataAddress();
        const TInt st = CFbsBitmap::ScanLineLength(iW, EColor64K) / 2;
        for (TInt y = aY0; y < aY1; y++) Mem::Copy(aFrame + (aY + y) * aStride + aX, p + y * st, iW * 2);
        iBmp->UnlockHeap();
        }
    CFbsBitmap* iBmp;
    CFbsBitmapDevice* iDev;
    CFbsBitGc* iGc;
    TInt iW, iH;
    };


// 0.11: a phone font as the menus' atlas (ASCII drawn once into a strip; the coverage from the green channel)
static TBool BuildFont(const CFont* aFont, TmFont& aOut)
    {
    aOut.iMask = 0;
    if (!aFont) return EFalse;
    aOut.iH = aFont->HeightInPixels(); aOut.iAsc = aFont->AscentInPixels();
    TInt x = 0;
    for (TInt c = 0; c < 95; c++) { TBuf<1> b; b.Append(TChar(32 + c)); aOut.iW[c] = aFont->TextWidthInPixels(b); aOut.iX[c] = x; x += aOut.iW[c] + 1; }
    aOut.iStripW = x;
    CTextCanvas cv;
    if (!cv.Create(x, aOut.iH)) return EFalse;
    for (TInt c = 0; c < 95; c++) { TBuf<1> b; b.Append(TChar(32 + c)); cv.Text(aFont, b, aOut.iX[c], 0, 0xFFFF); }
    aOut.iMask = new TUint8[x * aOut.iH];
    if (!aOut.iMask) return EFalse;
    cv.iBmp->LockHeap();
    const TUint16* p = (const TUint16*) cv.iBmp->DataAddress();
    const TInt st = CFbsBitmap::ScanLineLength(x, EColor64K) / 2;
    for (TInt y = 0; y < aOut.iH; y++)
        for (TInt k = 0; k < x; k++)
            {
            const TUint16 c = p[y * st + k];
            aOut.iMask[y * x + k] = c == 0xFFFF ? 255 : (TUint8) (((c >> 5) & 63) * 4);
            }
    cv.iBmp->UnlockHeap();
    return ETrue;
    }

static void ToC(char* aOut, const TDesC& aIn, TInt aMax)
    {
    TInt i = 0; for (; i < aIn.Length() && i < aMax; i++) aOut[i] = (char) aIn[i];
    aOut[i] = 0;
    }
static void CCat(char* aD, const char* aS) { while (*aD) aD++; while (*aS) *aD++ = *aS++; *aD = 0; }

class CTmView : public CCoeControl, public MDirectScreenAccess
    {
public:
    void ConstructL(const TRect& aRect);
    ~CTmView();
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
    void Draw(const TRect& aRect) const;
    static TInt IdleCb(TAny* aThis);
    void Frame();
    void SetActive(TBool aOn);
    void AbortNow(RDirectScreenAccess::TTerminationReasons aReason);
    void Restart(RDirectScreenAccess::TTerminationReasons aReason);
private:
    // 0.11: the installed tracks and the menus
    void ScanTracks();
    void GameFont();
    void MenuKeyL(const TKeyEvent& aKey);
    void StartRace();
    void BackToMenu();
    TFileName iDir;                 // the program's folder: the tracks, their pictures, the menus' pictures
    TBuf<20> iFiles[KMaxFiles];
    TInt iNFiles;
    TmUiTrack iUiTracks[KMaxFiles];
    TUint8* iThumbs[KMaxFiles];
    TUint8* iPackData;
    TmUiPack iPack;
    TmFont iFBig, iFText, iFSmall;
    TmMenu* iUi;
    TBool iInMenu;
    // the race
    TBool LoadTrack(TInt aIndex);
    void UnloadTrack();
    TInt iTrackIndex, iTrackCount;
    TBuf<16> iTrackName;            // the file's name without .tmb
    char iTrackC[20];
    TBool StartDirect();
    void StopDirect();
    void Blit(const TUint16* aSrc, TBool aAll);
    CFbsBitmap* iBmp;
    CIdle* iIdle;
    RChunk iChunk;
    TUint8* iData; TInt iDataSize;
    TUint16* iBack;                 // the screen's picture (640 x 200), kept for the program's life
    TmTrack* iTrack;
    TmView* iView;
    TBool iLoaded, iRunning, i4K, iDsaOn, iFirstBlit;
    CDirectScreenAccess* iDsa;
    TUint8* iFb;
    TInt iFbStride, iFbBpp;
    TTime iStart, iSecond;
    TInt iPauseAt, iSpeed;          // the flythrough: ms into the path when paused (-1 running); speed %
    TInt iFrames, iFps, iDrawMs, iBlitMs, iDrawSum, iBlitSum, iTris;
    TUint32 iKeys;
    TInt iYawOff, iPitchOff;
    TBool iShowInfo;
    TInt iFarM, iDrawCar;
    TBuf<64> iStatus;
    TBool iDriving;
    TmGround* iGround;
    TmCar* iCar;
    TmFollowCam* iCam;
    TTime iDriveStart;
    TInt iDriveSteps;
    void DriveReset();
    void DriveFrame(TInt& aCx, TInt& aCy, TInt& aCz, TInt& aYaw, TInt& aPitch, TInt& aCarX, TInt& aCarY, TInt& aCarZ, TInt& aCarYaw);
    void BenchStart();
    void BenchVariant(TInt aV);
    void BenchFrame(TInt aPos, TInt aDrawUs, TInt aBlitUs);
    void BenchEnd();
    TInt iBench;
    TInt iBenchFrames, iBenchDrawUs, iBenchBlitUs, iBenchMaxUs, iBenchDrawn, iBenchPolys, iBenchVis, iBenchLastPos, iBenchSecFrames, iBenchSec;
    TBuf8<BENCH_LOG> iLog;
    TBuf8<160> iSecs;
    CBenchSender* iSender;
    TInt iSendShown;
    // 0.11: the race laid out as the game's: the panels at the sides (drawn when something on them changes), the
    // timer, the countdown, the checkpoints' times, the speed and the result over the view (each frame)
    TBool iHudOn;
    TBool iSidesDirty, iSidesBlit;
    void DrawSides();
    void DrawOverlay();
    void DrawResult();
    // the pause (Esc), as the game's dialogs (0.14): the race stands still while one is open. Solo: "Race menu"
    // (Choose opponents, Restart, Race panels, Continue | Quit) and its ghost chooser; the hot seat: "Hot Seat"
    // (Continue, Give up, Quit) and the turn's message
    enum { EPmRace, EPmHotSeat, EPmGhosts, EPmTurn };
    TBool iPause, iPauseDirty;
    TInt iPauseKind, iPauseSel;
    TTime iPauseSince;
    void PauseOpen(TInt aKind = -1);
    void PauseClose();
    void PauseKeyL(TInt aKey);
    void DrawPause();
    void TogglePanels();
    TInt GhostRows(TmDlg::TRow* aRows, TInt* aModes) const;
    // the ghosts: the track's replay (the author; the medals are it on a slower clock), the record, the last run (the
    // hot seat: the player's best of his turn); up to four at once (iGhostMask), the fastest of them the reference for
    // the checkpoints' times
    enum { EGhostOff, EGhostNadeo, EGhostGold, EGhostSilver, EGhostBronze, EGhostRecord, EGhostLast, EGhostModes };
    TmGhost iGhostAuthor, iGhostRecord, iGhostLast, iRun;
    TInt iGhostMask, iGhostMode;
    void GhostRef();
    const TmGhost* Ghost() const { TInt n, d; return GhostOf(iGhostMode, n, d); }
    const TmGhost* Ghost(TInt& aNum, TInt& aDen) const { return GhostOf(iGhostMode, aNum, aDen); }
    const TmGhost* GhostOf(TInt aMode, TInt& aNum, TInt& aDen) const;
    TInt GhostFinish() const { return GhostFinishOf(iGhostMode); }
    TInt GhostFinishOf(TInt aMode) const;
    TInt GhostCp(TInt aK) const;
    const char* GhostName(TInt aMode) const;
    TInt iRaceSteps;                // steps since the start; negative during the countdown
    TInt iSeenCps;
    TBool iSeenFinish;
    TInt iSplitUntil, iSplitDiff, iSplitTime, iSplitCp, iSplitHas;
    void SaveRecord();
    void LoadRecord();
    void GhostFile(TDes& aName) const;
    TBool iRunIsRecord;
    // the hot seat as the game's: a player's turn is all his attempts (or his time), "Now it's player X's turn!"
    // before it; after the last player the standings, then the series' next track
    TInt iPlayers, iPlayer, iAttempts, iRound;     // iRound: the attempts the player has used
    TInt iHsMode, iTimeLimit, iTurnSteps;          // the time mode: seconds a turn, the steps driven in it
    char iNames[8][16];
    TInt iPlayerBest[8];
    TInt iSeries[8], iSeriesN, iSeriesPos;
    TBool iStandings;
    // 0.14: the campaign: a medal opens the next track, and Enter goes on to it (-1: none; iUnlockedNow: just opened)
    TInt iNextFile;
    TBool iUnlockedNow;
    void GoNextTrack();
    void NextRun();
    TBool TurnOver() const;
    void EndTurn();
    void HsRestart();
    void TurnStart();
    // the players' profiles (the hot seat's names), kept in C:\Data\TM9300\profiles.txt
    void LoadProfiles();
    void SaveProfiles();
    };

void CTmView::ConstructL(const TRect& aRect)
    {
    CreateWindowL();
    SetRect(aRect);
    iBmp = new (ELeave) CFbsBitmap;
    TDisplayMode mode = Window().DisplayMode();
    i4K = (mode == EColor4K);
    User::LeaveIfError(iBmp->Create(TSize(SCR_W, SCR_H), i4K ? EColor4K : EColor64K));
    iIdle = CIdle::NewL(CActive::EPriorityIdle);
    iSpeed = 100; iPauseAt = -1; iShowInfo = EFalse; iFarM = 180; iDrawCar = 1;
    iFirstBlit = ETrue;
    TInt addr = 0, off = 0, w = 0, h = 0;
    if (HAL::Get(HALData::EDisplayMemoryAddress, addr) == KErrNone &&
        HAL::Get(HALData::EDisplayOffsetToFirstPixel, off) == KErrNone &&
        HAL::Get(HALData::EDisplayOffsetBetweenLines, iFbStride) == KErrNone &&
        HAL::Get(HALData::EDisplayBitsPerPixel, iFbBpp) == KErrNone &&
        HAL::Get(HALData::EDisplayXPixels, w) == KErrNone &&
        HAL::Get(HALData::EDisplayYPixels, h) == KErrNone &&
        addr && (iFbBpp == 16 || iFbBpp == 12) && w >= SCR_W && h >= SCR_H && iFbStride >= SCR_W * 2 && !(iFbStride & 3))
        {
        iFb = (TUint8*)addr + off;
        if ((TUint32)iFb & 3) iFb = 0;
        }
    if (iFb) iDsa = CDirectScreenAccess::NewL(iCoeEnv->WsSession(), *iCoeEnv->ScreenDevice(), Window(), *this);
    iBack = (TUint16*) User::AllocL(SCR_W * SCR_H * 2);
    for (TInt i = 0; i < SCR_W * SCR_H; i++) iBack[i] = 0;
    iHudOn = ETrue; iPlayers = 1; iGhostMask = 1 << EGhostGold; iGhostMode = EGhostGold; iAttempts = 1;
    // the menus: the phone's fonts as atlases, the game's pictures, the tracks and their pictures
    BuildFont(iEikonEnv->TitleFont(), iFBig); BuildFont(iEikonEnv->NormalFont(), iFText); BuildFont(iEikonEnv->DenseFont(), iFSmall);
    ScanTracks();
    GameFont();
    iUi = new (ELeave) TmMenu;
    iUi->iVersion = KVersionC;
    if (iFBig.iMask && iFText.iMask && iFSmall.iMask) iUi->Init(&iFBig, &iFText, &iFSmall, iUiTracks, iNFiles, iPack.iN ? &iPack : 0);
    LoadProfiles();
    iInMenu = ETrue;
    iLoaded = iFBig.iMask && iFText.iMask && iFSmall.iMask;
    if (!iLoaded) iStatus = _L("The fonts could not be prepared");
    ActivateL();
    SetActive(ETrue);
    }

CTmView::~CTmView()
    {
    if (iIdle) iIdle->Cancel();
    delete iIdle;
    if (iDsa) iDsa->Cancel();
    delete iDsa;
    delete iBmp;
    delete iView;
    delete iCam; delete iCar; delete iGround;
    delete iTrack;
    delete iSender;
    delete iUi;
    for (TInt i = 0; i < KMaxFiles; i++) User::Free(iThumbs[i]);
    User::Free(iPackData);
    delete[] iFBig.iMask; delete[] iFText.iMask; delete[] iFSmall.iMask;
    User::Free(iBack);
    iChunk.Close();
    }

// the tracks beside the program (sorted by name), each one's picture and medals (.tmt) and record (its ghost's
// finish); the menus' pictures (tm9300.ui)
void CTmView::ScanTracks()
    {
    RFs& fs = iCoeEnv->FsSession();
    iDir = KTrackDir;
    TFileName app = iEikonEnv->EikAppUi()->Application()->AppFullName();
    iDir[0] = app.Length() ? app[0] : (TText)'C';
    iNFiles = 0;
    CDir* list = 0;
    TFileName pat(iDir); pat.Append(KTrackPattern);
    if (fs.GetDir(pat, KEntryAttNormal, ESortByName, list) == KErrNone && list)
        for (TInt i = 0; i < list->Count() && iNFiles < KMaxFiles; i++)
            {
            TBuf<20> n((*list)[i].iName.Left(20));
            TInt dot = n.LocateReverse('.'); if (dot > 0) n.SetLength(dot);
            TmUiTrack& t = iUiTracks[iNFiles];
            Mem::FillZ(&t, sizeof(t));
            ToC(t.iName, n, 19);
            // rallyA1 / snowE: the environment, the series' letter (A White .. E Black), the number
            TBuf<20> lo(n); lo.LowerCase();
            TInt k = 0;
            if (lo.Find(_L("rally")) == 0) { t.iEnv = 0; k = 5; } else if (lo.Find(_L("snow")) == 0) { t.iEnv = 1; k = 4; } else continue;
            if (k >= n.Length()) continue;
            TInt letter = n[k]; if (letter >= 'a') letter -= 32;
            if (letter < 'A' || letter > 'E') continue;
            t.iSeries = letter - 'A';
            t.iNum = k + 1 < n.Length() && n[k + 1] >= '1' && n[k + 1] <= '5' ? n[k + 1] - '0' : 1;
            t.iFile = iNFiles;
            iFiles[iNFiles] = n;
            // its picture and medals
            TFileName tn(iDir); tn.Append(n); tn.Append(_L(".tmt"));
            RFile f;
            if (f.Open(fs, tn, EFileRead | EFileShareReadersOnly) == KErrNone)
                {
                TInt sz = 0; f.Size(sz);
                if (sz > 28 && sz < 64000)
                    {
                    TUint8* b = (TUint8*) User::Alloc(sz);
                    TPtr8 p(b, 0, sz);
                    if (b && f.Read(p) == KErrNone && p.Length() == sz && b[0] == 'T' && b[1] == 'M' && b[2] == 'T')
                        {
                        const TInt* hd = (const TInt*) b;
                        if (hd[1] > 0 && hd[2] > 0 && 28 + hd[1] * hd[2] * 2 <= sz) { t.iThumbW = hd[1]; t.iThumbH = hd[2]; t.iThumb = (const TmPix*) (b + 28); }
                        for (TInt m = 0; m < 4; m++) t.iMedal[m] = hd[3 + m];
                        iThumbs[iNFiles] = b;
                        }
                    else User::Free(b);
                    }
                f.Close();
                }
            // its record: the ghost's finish ("TMG1", points, finish)
            TFileName gn(KBenchDir); gn.Append(n); gn.Append(_L(".ghost"));
            if (f.Open(fs, gn, EFileRead | EFileShareReadersOnly) == KErrNone)
                {
                TBuf8<16> h;
                if (f.Read(h) == KErrNone && h.Length() == 16 && h[0] == 'T' && h[3] == '1') t.iBest = h[8] | (h[9] << 8) | (h[10] << 16) | (h[11] << 24);
                f.Close();
                }
            iNFiles++;
            }
    delete list;
    iTrackCount = iNFiles;
    // the menus' pictures
    TFileName pn(iDir); pn.Append(KUiPack);
    RFile f;
    if (f.Open(fs, pn, EFileRead | EFileShareReadersOnly) == KErrNone)
        {
        TInt sz = 0; f.Size(sz);
        if (sz > 8 && sz < 4000000)
            {
            iPackData = (TUint8*) User::Alloc(sz);
            if (iPackData)
                {
                TPtr8 p(iPackData, 0, sz);
                if (f.Read(p) != KErrNone || p.Length() != sz || !iPack.Load(iPackData, sz)) { User::Free(iPackData); iPackData = 0; iPack.iN = 0; }
                }
            }
        f.Close();
        }
    }

// 0.14: the game's own font (tm9300.fnt, its sheet) at the game's sizes; the title's bold
void CTmView::GameFont()
    {
    RFs& fs = iCoeEnv->FsSession();
    TFileName pn(iDir); pn.Append(KUiFont);
    RFile f;
    if (f.Open(fs, pn, EFileRead | EFileShareReadersOnly) != KErrNone) return;
    TInt sz = 0; f.Size(sz);
    TUint8* d = sz > 0 && sz < 400000 ? (TUint8*) User::Alloc(sz) : 0;
    if (d)
        {
        TPtr8 p(d, 0, sz);
        TmFont sheet;
        if (f.Read(p) == KErrNone && p.Length() == sz && TmFontLoad(d, sz, sheet))
            {
            // the sizes the game's own (its text against its menus' rows and plates, as far as reading allows):
            // the titles 15 lines (bold), the text 11, the small 10
            TmFont* fonts[3] = { &iFBig, &iFText, &iFSmall };
            const TInt sizes[3] = { 15, 11, 10 };
            for (TInt k = 0; k < 3; k++)
                {
                if (!fonts[k]->iMask) continue;
                TmFont g;
                if (TmFontScale(sheet, sizes[k], k == 0, g)) { delete[] fonts[k]->iMask; *fonts[k] = g; }
                }
            }
        User::Free(d);
        }
    f.Close();
    }

void CTmView::BenchStart()
    {
    iLog.Zero();
    TInt f = 0, t = 0; HAL::Get(HALData::EMemoryRAMFree, f); HAL::Get(HALData::EMemoryRAM, t);
    TBuf8<64> name8; name8.Copy(iStatus);
    iLog.AppendFormat(_L8("tm9300 bench v0.14: %S, %d meshes %d instances %d triangles, display %d bpp direct %d, RAM free %d KB of %d KB\r\n"),
        &name8, iTrack->iNMesh, iTrack->iNInst, iTrack->iTris, iFbBpp, iFb ? 1 : 0, f / 1024, t / 1024);
    iLog.Append(_L8("variant: frames, fps, draw ms (avg), copy ms (avg), slowest frame ms, polys drawn, submitted, instances visible (avg), direct; fps by second\r\n"));
    iShowInfo = EFalse;
    BenchVariant(0);
    }

void CTmView::BenchVariant(TInt aV)
    {
    iBench = aV;
    iView->iMeshMode = 1; iView->iFarCut = 180 * 16; iView->iSkipEnd = 0; iYawOff = 0; iPitchOff = 0; iSpeed = 100; iDrawCar = 1;
    iBenchFrames = iBenchDrawUs = iBenchBlitUs = iBenchMaxUs = iBenchDrawn = iBenchPolys = iBenchVis = 0;
    iBenchLastPos = 0; iBenchSecFrames = 0; iBenchSec = 0; iSecs.Zero();
    switch (aV)
        {
        case 1: iDrawCar = 0; break;                        // the car left out (its cost)
        case 2: iView->iFarCut = 90 * 16; break;            // half the view distance
        case 3: iView->iSkipEnd = 1; break;                 // the geometry only: nothing drawn
        case 4: break;                                      // no copy to the screen (Frame skips it)
        default: break;
        }
    iStart.UniversalTime(); iPauseAt = -1;
    }

void CTmView::BenchFrame(TInt aPos, TInt aDrawUs, TInt aBlitUs)
    {
    if (aPos < iBenchLastPos)
        {
        // the replay wrapped: this variant is done
        _LIT8(KNames0, "mesh 180m"); _LIT8(KNames1, "mesh 180m no car"); _LIT8(KNames2, "mesh 90m"); _LIT8(KNames3, "geometry only"); _LIT8(KNames4, "mesh 180m no copy");
        const TDesC8* names[BENCH_VARIANTS] = { &KNames0, &KNames1, &KNames2, &KNames3, &KNames4 };
        TInt n = iBenchFrames ? iBenchFrames : 1;
        TInt ms = iTrack->iNPath * 100;
        iLog.AppendFormat(_L8("%S: %d frames, %d.%d fps, %d ms, %d ms, %d ms, %d, %d, %d, %d;"), names[iBench], iBenchFrames,
            iBenchFrames * 1000 / ms, (iBenchFrames * 10000 / ms) % 10, iBenchDrawUs / n / 1000, iBenchBlitUs / n / 1000, iBenchMaxUs / 1000,
            iBenchDrawn / n, iBenchPolys / n, iBenchVis / n, iDsaOn ? 1 : 0);
        iLog.Append(iSecs); iLog.Append(_L8("\r\n"));
        if (iBench + 1 < BENCH_VARIANTS) BenchVariant(iBench + 1); else BenchEnd();
        return;
        }
    iBenchLastPos = aPos;
    iBenchFrames++; iBenchDrawUs += aDrawUs; iBenchBlitUs += aBlitUs;
    if (aDrawUs + aBlitUs > iBenchMaxUs) iBenchMaxUs = aDrawUs + aBlitUs;
    iBenchDrawn += iView->iStats.drawn; iBenchPolys += iView->iStats.polys; iBenchVis += iView->iStats.visible;
    iBenchSecFrames++;
    if (aPos / 1000 != iBenchSec) { if (iSecs.Length() < 150) iSecs.AppendFormat(_L8(" %d"), iBenchSecFrames); iBenchSec = aPos / 1000; iBenchSecFrames = 0; }
    }

void CTmView::BenchEnd()
    {
    iBench = -1;
    iShowInfo = ETrue;
    iView->iMeshMode = 1; iView->iFarCut = iFarM * 16; iView->iSkipEnd = 0; iDrawCar = 1;
    RFs& fs = iCoeEnv->FsSession();
    fs.MkDirAll(KBenchDir);
    RFile f;
    if (f.Replace(fs, KBenchFile, EFileWrite) == KErrNone) { f.Write(iLog); f.Close(); }
    if (!iSender) iSender = new CBenchSender;
    if (iSender) iSender->Send(iLog);
    iSendShown = 0;
    // done: paused at the start with the counters shown (Space runs on, B runs the benchmark again); or back to the car
    iPauseAt = 0;
    if (iGround) { iDriving = ETrue; DriveReset(); }
    }


void CTmView::UnloadTrack()
    {
    delete iCam; iCam = 0; delete iCar; iCar = 0; delete iGround; iGround = 0;
    delete iView; iView = 0; delete iTrack; iTrack = 0;
    iChunk.Close(); iData = 0;
    iDriving = EFalse; iBench = -1;
    iSeenFinish = EFalse; iRunIsRecord = EFalse; iPause = EFalse;
    }

TBool CTmView::LoadTrack(TInt aIndex)
    {
    if (aIndex < 0 || aIndex >= iNFiles) return EFalse;
    RFs& fs = iCoeEnv->FsSession();
    iTrackIndex = aIndex;
    iTrackName = iFiles[aIndex].Left(16);
    ToC(iTrackC, iTrackName, 16);
    TFileName name(iDir); name.Append(iFiles[aIndex]); name.Append(_L(".tmb"));
    iStatus = iFiles[aIndex];
    RFile f;
    if (f.Open(fs, name, EFileRead | EFileShareReadersOnly) != KErrNone) { iStatus = _L("Cannot open the track"); return EFalse; }
    TInt size = 0;
    f.Size(size);
    TInt need = size + 64;
    if (iChunk.CreateLocal(need, need) != KErrNone) { f.Close(); iStatus = _L("No memory for the track"); return EFalse; }
    iData = iChunk.Base();
    iDataSize = size;
    TPtr8 p(iData, 0, size);
    TInt err = f.Read(p);
    f.Close();
    if (err != KErrNone || p.Length() != size) { iStatus = _L("Cannot read the track"); UnloadTrack(); return EFalse; }
    iTrack = new TmTrack;
    iView = new TmView;
    if (!iTrack || !iView || !iTrack->Load(iData, size) || !iView->Init(VIEW_W, VIEW_H, 2500)) { iStatus = _L("Bad track file"); UnloadTrack(); return EFalse; }
    iView->iFarCut = iFarM * 16;
    iView->iMeshMode = 1;
    for (TInt i = 0; i < SCR_W * SCR_H; i++) iBack[i] = 0;
    iFirstBlit = ETrue;
    iBench = -1;
    iDriving = EFalse;
    if (iTrack->iNCInst > 0)
        {
        iGround = new TmGround; iCar = new TmCar; iCam = new TmFollowCam;
        if (iGround && iCar && iCam && iGround->Init(*iTrack)) { iDriving = ETrue; DriveReset(); }
        }
    iGhostAuthor.Clear(); iGhostRecord.Clear(); iGhostLast.Clear();
    if (iDriving) { iGhostAuthor.FromPath(*iTrack, *iGround, *iCar); LoadRecord(); }
    iSidesDirty = ETrue;
    if (!iDriving) BenchStart();
    return ETrue;
    }

void CTmView::SetActive(TBool aOn)
    {
    if (aOn && !iRunning)
        {
        iStart.UniversalTime(); iSecond = iStart;
        if (iPauseAt >= 0) { iStart -= TTimeIntervalMicroSeconds(iPauseAt * 1000); iPauseAt = -1; }
        iIdle->Start(TCallBack(IdleCb, this));
        iRunning = ETrue;
        }
    else if (!aOn && iRunning)
        {
        iIdle->Cancel();
        iRunning = EFalse;
        iKeys = 0;
        StopDirect();
        }
    }


TInt CTmView::IdleCb(TAny* aThis)
    {
    CTmView* v = (CTmView*)aThis;
    if (!v->iLoaded) { v->DrawNow(); return EFalse; }
    v->Frame();
    return ETrue;
    }

TBool CTmView::StartDirect()
    {
    if (iDsaOn) return ETrue;
    if (!iDsa) return EFalse;
    TRAPD(err, iDsa->StartL());
    if (err != KErrNone) return EFalse;
    RRegion* reg = iDsa->DrawingRegion();
    if (!reg || reg->Count() != 1 || reg->BoundingRect().Width() < SCR_W || reg->BoundingRect().Height() < SCR_H)
        {
        iDsa->Cancel();
        return EFalse;
        }
    iDsaOn = ETrue; iFirstBlit = ETrue;
    return ETrue;
    }

void CTmView::StopDirect()
    {
    if (iDsaOn) { iDsa->Cancel(); iDsaOn = EFalse; DrawDeferred(); }
    }

void CTmView::AbortNow(RDirectScreenAccess::TTerminationReasons /*aReason*/) { iDsaOn = EFalse; }
void CTmView::Restart(RDirectScreenAccess::TTerminationReasons /*aReason*/) {}


void CTmView::DriveReset()
    {
    // the run before: the last ghost when it finished, the record again with its points past the line
    if (iSeenFinish && iRun.iN && (iPlayers == 1 || !iGhostLast.Finish() || iCar->iFinishSteps < iGhostLast.Finish())) iGhostLast.Copy(iRun);
    if (iRunIsRecord && iRun.iN) { iGhostRecord.Copy(iRun); SaveRecord(); }
    iSeenFinish = EFalse; iRunIsRecord = EFalse; iNextFile = -1; iUnlockedNow = EFalse;
    iCar->Reset(*iTrack);
    iCam->Reset(iCar->iS);
    iDriveStart.UniversalTime();
    iDriveSteps = 0;
    // the countdown first (0.13: as the game shows it: the clock from -0:02.60, "2" to -1 s, "1" to the start), then the race
    iRaceSteps = -130; iSeenCps = 0; iSplitUntil = 0; iSplitHas = EFalse;
    iRun.Begin();
    iView->iNGhosts = 0;
    iSidesDirty = ETrue; iPauseDirty = ETrue;
    GhostRef();
    }

// Enter after the finish: the next run; in the hot seat the player's next attempt, or the next player's turn, after
// the last player the standings, then the series' next track (or the menu)
void CTmView::NextRun()
    {
    if (iPlayers > 1)
        {
        if (iStandings)
            {
            iStandings = EFalse;
            if (++iSeriesPos >= iSeriesN) { BackToMenu(); return; }
            UnloadTrack();
            if (!LoadTrack(iSeries[iSeriesPos])) { BackToMenu(); return; }
            for (TInt k = 0; k < 8; k++) iPlayerBest[k] = 0;
            iPlayer = 0; TurnStart();
            return;
            }
        if (TurnOver()) { EndTurn(); return; }
        }
    else if (iSeenFinish && iNextFile >= 0) { GoNextTrack(); return; }
    DriveReset();
    }

// solo, after a medal: the campaign's next track, against the same ghosts
void CTmView::GoNextTrack()
    {
    const TInt f = iNextFile;
    if (iRunIsRecord && iRun.iN) { iGhostRecord.Copy(iRun); SaveRecord(); iRunIsRecord = EFalse; }
    UnloadTrack();
    if (!LoadTrack(f)) { BackToMenu(); return; }
    iInMenu = EFalse; iKeys = 0;
    iSidesDirty = ETrue; iFirstBlit = ETrue;
    GhostRef();
    }

// the player's turn over: all his attempts used, or his time
TBool CTmView::TurnOver() const
    {
    if (iPlayers <= 1) return EFalse;
    return iHsMode ? iTurnSteps >= iTimeLimit * 50 : iRound >= iAttempts;
    }

// the next player's turn (his message first), or after the last the standings
void CTmView::EndTurn()
    {
    DriveReset();
    if (++iPlayer >= iPlayers) { iPlayer = iPlayers - 1; iStandings = ETrue; iSidesDirty = ETrue; return; }
    TurnStart();
    }

void CTmView::TurnStart()
    {
    iRound = 0; iTurnSteps = 0;
    iGhostLast.Clear(); GhostRef();
    DriveReset();
    PauseOpen(EPmTurn);
    }

// a restart in the hot seat (Backspace, Restart): an attempt used once the car has left the line
void CTmView::HsRestart()
    {
    if (iStandings) return;
    if (iPlayers > 1 && !iHsMode && iRaceSteps > 0 && !iCar->iFinished) iRound++;
    if (TurnOver()) EndTurn(); else DriveReset();
    }

const TmGhost* CTmView::GhostOf(TInt aMode, TInt& aNum, TInt& aDen) const
    {
    aNum = aDen = 1;
    const TmGhost* g = 0;
    switch (aMode)
        {
        case EGhostNadeo: g = &iGhostAuthor; break;
        case EGhostGold: case EGhostSilver: case EGhostBronze:
            {
            // the author's run on a slower clock: at the medal's time it crosses the line
            const TInt medal = iTrack ? iTrack->iMedal[aMode - EGhostNadeo] / 20 : 0;
            if (medal <= 0 || iGhostAuthor.Finish() <= 0) return 0;
            g = &iGhostAuthor; aNum = iGhostAuthor.Finish(); aDen = medal;
            break;
            }
        case EGhostRecord: g = &iGhostRecord; break;
        case EGhostLast: g = &iGhostLast; break;
        default: break;
        }
    return g && g->iN > 0 ? g : 0;
    }

TInt CTmView::GhostFinishOf(TInt aMode) const
    {
    TInt n, d;
    const TmGhost* g = GhostOf(aMode, n, d);
    if (!g) return 0;
    if (aMode >= EGhostNadeo && aMode <= EGhostBronze && iTrack && iTrack->iMedal[aMode - EGhostNadeo] > 0)
        return iTrack->iMedal[aMode - EGhostNadeo] / 20;
    return g->Finish();
    }

TInt CTmView::GhostCp(TInt aK) const
    {
    TInt n, d;
    const TmGhost* g = Ghost(n, d);
    if (!g || !g->CpSteps(aK)) return 0;
    return (TInt) (((TInt64) g->CpSteps(aK) * d) / n).Low();
    }

// the reference among the chosen ghosts: the fastest that has finished (else the first chosen)
void CTmView::GhostRef()
    {
    TInt best = 0, first = EGhostOff;
    iGhostMode = EGhostOff;
    for (TInt m = EGhostNadeo; m < EGhostModes; m++)
        {
        if (!(iGhostMask & (1 << m))) continue;
        if (first == EGhostOff) first = m;
        const TInt f = GhostFinishOf(m);
        if (f > 0 && (!best || f < best)) { best = f; iGhostMode = m; }
        }
    if (iGhostMode == EGhostOff) iGhostMode = first;
    iSidesDirty = ETrue;
    }

const char* CTmView::GhostName(TInt aMode) const
    {
    switch (aMode)
        {
        case EGhostNadeo: return "Nadeo medal";
        case EGhostGold: return "Gold medal";
        case EGhostSilver: return "Silver medal";
        case EGhostBronze: return "Bronze medal";
        case EGhostRecord: return "Your record";
        case EGhostLast: return iPlayers > 1 ? "Your best run" : "Your last run";
        default: return "None";
        }
    }

void CTmView::GhostFile(TDes& aName) const
    {
    aName = KBenchDir; aName.Append(iTrackName); aName.Append(_L(".ghost"));
    }

void CTmView::SaveRecord()
    {
    if (!iGhostRecord.iN) return;
    RFs& fs = iCoeEnv->FsSession();
    fs.MkDirAll(KBenchDir);
    const TInt n = iGhostRecord.Size();
    TUint8* buf = (TUint8*) User::Alloc(n);
    if (!buf) return;
    iGhostRecord.Write(buf);
    TFileName name; GhostFile(name);
    RFile f;
    if (f.Replace(fs, name, EFileWrite) == KErrNone) { f.Write(TPtrC8(buf, n)); f.Close(); }
    User::Free(buf);
    }

void CTmView::LoadRecord()
    {
    iGhostRecord.Clear();
    RFs& fs = iCoeEnv->FsSession();
    TFileName name; GhostFile(name);
    RFile f;
    if (f.Open(fs, name, EFileRead | EFileShareReadersOnly) != KErrNone) return;
    TInt n = 0; f.Size(n);
    if (n > 0 && n < 200000)
        {
        TUint8* buf = (TUint8*) User::Alloc(n);
        if (buf)
            {
            TPtr8 p(buf, 0, n);
            if (f.Read(p) == KErrNone && p.Length() == n) iGhostRecord.Load(buf, n);
            User::Free(buf);
            }
        }
    f.Close();
    }


void CTmView::DriveFrame(TInt& aCx, TInt& aCy, TInt& aCz, TInt& aYaw, TInt& aPitch, TInt& aCarX, TInt& aCarY, TInt& aCarZ, TInt& aCarYaw)
    {
    // the steps the clock asks for (20 ms each), at most 4 a frame (a slow frame does not snowball)
    TTime now; now.UniversalTime();
    TInt ms = (TInt)(now.MicroSecondsFrom(iDriveStart).Int64().Low() / 1000);
    TInt want = ms / 20 - iDriveSteps;
    if (want > 4) { iDriveSteps += want - 4; want = 4; }
    if (iPause || iStandings) want = 0;
    TmCarInput in;
    in.steer = (iKeys & KEY_LEFT ? -256 : 0) + (iKeys & KEY_RIGHT ? 256 : 0);
    in.throttle = (iKeys & KEY_UP) ? 1 : 0;
    in.brake = (iKeys & KEY_DOWN) ? 1 : 0;
    // after the finish the keys do nothing: the car brakes to a stop (and no further: held, it would reverse)
    if (iCar->iFinished) { in.steer = 0; in.throttle = 0; in.brake = iCar->iS.speed > 256 ? 1 : 0; }
    for (TInt k = 0; k < want; k++)
        {
        iDriveSteps++;
        // the countdown: the car waits on the line
        if (iRaceSteps < 0) { iRaceSteps++; iCam->Update(*iGround, iCar->iS, aCx, aCy, aCz, aYaw, aPitch); continue; }
        iCar->Step(*iGround, in); iRaceSteps++;
        if (iPlayers > 1 && iHsMode && !iCar->iFinished)
            {
            // the time mode: the turn's clock (the run on when it runs out may still finish)
            iTurnSteps++;
            if (!(iTurnSteps % 50)) iSidesDirty = ETrue;
            }
        iRun.Record(*iCar);
        iCam->Update(*iGround, iCar->iS, aCx, aCy, aCz, aYaw, aPitch);
        }
    if (want <= 0) iCam->Current(aCx, aCy, aCz, aYaw, aPitch);
    // a checkpoint taken or the finish: its time, and against the ghost's (- ahead, + behind), shown 3 s
    TInt gn, gd;
    const TmGhost* g = Ghost(gn, gd);
    while (iSeenCps < iCar->iCheckpoints)
        {
        const TInt k = iSeenCps++;
        iSplitTime = iRun.CpSteps(k) ? iRun.CpSteps(k) : iCar->iSteps;
        iSplitCp = k + 1;
        iSplitHas = GhostCp(k) > 0;
        if (iSplitHas) iSplitDiff = iSplitTime - GhostCp(k);
        iSplitUntil = iRaceSteps + 150;
        iSidesDirty = ETrue;
        }
    if (iCar->iFinished && !iSeenFinish)
        {
        iSeenFinish = ETrue;
        const TInt t = iCar->iFinishSteps;
        iSplitTime = t; iSplitCp = 0;
        iSplitHas = GhostFinish() > 0;
        if (iSplitHas) iSplitDiff = t - GhostFinish();
        iSplitUntil = 0;
        // a new record: kept and saved (again with its last points at the next start); the menu's medals
        if (!iGhostRecord.Finish() || t < iGhostRecord.Finish()) { iGhostRecord.Copy(iRun); SaveRecord(); iRunIsRecord = ETrue; }
        // the campaign: with a medal the next track is open (it may just have been opened) and Enter goes there
        iNextFile = -1; iUnlockedNow = EFalse;
        const TInt nf = iPlayers == 1 && iUi ? iUi->NextFile(iTrackIndex) : -1;
        const TBool wasOpen = nf >= 0 && iUi->FileUnlocked(nf);
        if (iUi) iUi->SetBest(iTrackIndex, t);
        const TInt bronze = iTrack->iMedal[3];
        if (nf >= 0 && (bronze <= 0 || t * 20 <= bronze) && iUi->FileUnlocked(nf)) { iNextFile = nf; iUnlockedNow = !wasOpen; }
        if (!iPlayerBest[iPlayer] || t < iPlayerBest[iPlayer]) iPlayerBest[iPlayer] = t;
        if (iPlayers > 1 && !iHsMode) iRound++;
        iSidesDirty = ETrue;
        }
    // the ghosts where they were at this step of their runs
    (void) g;
    iView->iNGhosts = 0;
    const TInt gs = iRaceSteps > 0 ? iRaceSteps : 0;
    for (TInt m = EGhostNadeo; m < EGhostModes && iView->iNGhosts < TmView::KMaxGhosts; m++)
        {
        if (!(iGhostMask & (1 << m))) continue;
        const TmGhost* q = GhostOf(m, gn, gd);
        TmView::TGhostCar& c = iView->iGhosts[iView->iNGhosts];
        if (q && q->At((TInt) (((TInt64) gs * gn) / gd).Low(), c.x, c.y, c.z, c.yaw, c.slopeF, c.slopeR)) iView->iNGhosts++;
        }
    aCarX = iCar->iS.x >> 4; aCarY = (iCar->iS.y >> 4) + 3; aCarZ = iCar->iS.z >> 4; aCarYaw = iCar->iS.yaw;
    iCar->iS.Slopes(iView->iCarSlopeF, iView->iCarSlopeR);
    }

void CTmView::Frame()
    {
    // the menus: drawn when they change, nothing a frame otherwise
    if (iInMenu)
        {
        if (iUi && iUi->iDirty) { iUi->Draw(iBack, SCR_W); Blit(iBack, ETrue); }
        else User::After(25000);
        return;
        }
    if (iPause && !iPauseDirty) { User::After(20000); return; }
    TTime t0;
    t0.UniversalTime();
    TInt ms = iPauseAt >= 0 ? iPauseAt : (TInt)(t0.MicroSecondsFrom(iStart).Int64().Low() / 1000);
    TInt pathMs = iTrack->iNPath * 100;
    TInt pos = pathMs > 0 ? (ms * iSpeed / 100) % pathMs : 0;
    TInt sample = pos / 100, frac = ((pos % 100) * 256) / 100;
    if (!iDriving)
        {
        if (iKeys & KEY_LEFT) iYawOff -= 24;
        if (iKeys & KEY_RIGHT) iYawOff += 24;
        if (iKeys & KEY_UP) iPitchOff += 8;
        if (iKeys & KEY_DOWN) iPitchOff -= 8;
        }
    TInt cx, cy, cz, yaw, pitch, carX, carY, carZ, carYaw;
    if (iDriving && iBench < 0) DriveFrame(cx, cy, cz, yaw, pitch, carX, carY, carZ, carYaw);
    else
        {
        iView->iCarSlopeF = iView->iCarSlopeR = 0;
        iView->ChaseCamera(*iTrack, sample, frac, cx, cy, cz, yaw, pitch);
        yaw = (yaw + iYawOff) & 4095;
        pitch += iPitchOff; if (pitch > 900) pitch = 900; if (pitch < -900) pitch = -900;
        iView->CarAt(*iTrack, sample, frac, carX, carY, carZ, carYaw);
        }
    TUint16* view = iBack + VIEW_X;
    iView->Frame(view, SCR_W, *iTrack, cx, cy, cz, yaw, pitch, 200, carX, carY, carZ, carYaw, iDrawCar);
    TTime t1;
    t1.UniversalTime();
    TInt drawUs = t1.MicroSecondsFrom(t0).Int64().Low();
    iDrawSum += drawUs / 1000;
    if (iDriving && iBench < 0)
        {
        if (iHudOn && iSidesDirty) DrawSides();
        DrawOverlay();
        if (iPause) { DrawPause(); iPauseDirty = EFalse; }
        }
    if (iShowInfo)
        {
        for (TInt y = 0; y < 136; y++) { TUint16* q = iBack + y * SCR_W; for (TInt x = 0; x < BAR_W; x++) q[x] = 0; }
        const TUint16 white = 0xFFFF, yel = 0xFFE0;
        DrawNumber(iBack, SCR_W, 8, 8, iFps, white);
        DrawNumber(iBack, SCR_W, 8, 24, iDrawMs, yel);
        DrawNumber(iBack, SCR_W, 8, 40, iBlitMs, yel);
        DrawNumber(iBack, SCR_W, 8, 56, iView->iStats.drawn, white);
        DrawNumber(iBack, SCR_W, 8, 72, iView->iStats.polys, white);
        DrawNumber(iBack, SCR_W, 8, 88, pos / 1000, white);
        DrawNumber(iBack, SCR_W, 8, 104, iFarM, white);
        DrawNumber(iBack, SCR_W, 8, 120, iDsaOn ? 1 : 0, yel);
        if (iSender) DrawNumber(iBack, SCR_W, 8, 136, iSender->iResult == 1 ? 1 : iSender->iResult < 0 ? 2 : 3, yel);
        }
    else if (iBench >= 0)
        {
        for (TInt y = 0; y < 24; y++) { TUint16* q = iBack + y * SCR_W; for (TInt x = 0; x < BAR_W; x++) q[x] = 0; }
        DrawNumber(iBack, SCR_W, 8, 8, iBench + 1, 0xF800);
        DrawNumber(iBack, SCR_W, 32, 8, BENCH_VARIANTS, 0xF800);
        TInt w = pathMs > 0 ? (pos * 56) / pathMs : 0;
        for (TInt y = 20; y < 23; y++) { TUint16* q = iBack + y * SCR_W + 4; for (TInt x = 0; x < w; x++) q[x] = 0xF800; }
        }
    if (!(iBench == 4)) Blit(iBack, EFalse);
    TTime t2;
    t2.UniversalTime();
    TInt blitUs = t2.MicroSecondsFrom(t1).Int64().Low();
    iBlitSum += blitUs / 1000;
    if (iBench >= 0) BenchFrame(pos, drawUs, blitUs);
    iFrames++;
    if (t2.MicroSecondsFrom(iSecond).Int64().Low() >= 1000000)
        {
        iFps = iFrames; iDrawMs = iFrames ? iDrawSum / iFrames : 0; iBlitMs = iFrames ? iBlitSum / iFrames : 0;
        iFrames = 0; iDrawSum = 0; iBlitSum = 0; iSecond = t2;
        }
    User::ResetInactivityTime();
    }

// ---- the race's layout (0.11) ----
using namespace TmDraw;

// the sides: on the left the opponent and the player (the hot seat: every player and his best), on the right the
// track and its author, the checkpoints; drawn when something on them changes
void CTmView::DrawSides()
    {
    iSidesDirty = EFalse; iSidesBlit = ETrue;
    const TmPix bg0 = Rgb(12, 26, 36), bg1 = Rgb(4, 10, 16);
    for (TInt y = 0; y < SCR_H; y++) { const TmPix c = Mix(bg0, bg1, (y * 256) / SCR_H); Fill(iBack, SCR_W, 0, y, SIDE_W, 1, c); Fill(iBack, SCR_W, RIGHT_X, y, SCR_W - RIGHT_X, 1, c); }
    char b[48];
    TInt y = 4;
    // the opponent: its medal, its name, its time
    if (iGhostMode != EGhostOff)
        {
        Round(iBack, SCR_W, 3, y, SIDE_W - 6, 36, 8, Rgb(150, 156, 160), Rgb(96, 102, 108));
        const TInt kind = iGhostMode == EGhostNadeo ? 0 : iGhostMode == EGhostGold ? 1 : iGhostMode == EGhostSilver ? 2 : iGhostMode == EGhostBronze ? 3 : 4;
        Coin(iBack, SCR_W, 18, y + 18, 12, kind);
        TInt more = -1; for (TInt m = EGhostNadeo; m < EGhostModes; m++) if (iGhostMask & (1 << m)) more++;
        Text(iBack, SCR_W, iFText, 34, y + 2, GhostName(iGhostMode), Rgb(255, 255, 255), SIDE_W - 4);
        if (more > 0) { char mm[8] = { '+', (char) ('0' + more), 0 }; Text(iBack, SCR_W, iFSmall, SIDE_W - 12 - iFSmall.Width(mm), y + 4 + iFText.iH, mm, Rgb(255, 255, 255)); }
        const TInt gf = GhostFinish();
        Round(iBack, SCR_W, 34, y + 3 + iFText.iH, SIDE_W - 42, iFSmall.iH + 3, 5, Rgb(70, 76, 82), Rgb(50, 56, 62));
        if (gf) { FormatTime(b, gf * 20); Clock(iBack, SCR_W, iFSmall, 40, y + 4 + iFText.iH, b, Rgb(220, 226, 232)); }
        else Text(iBack, SCR_W, iFSmall, 40, y + 4 + iFText.iH, "not yet", Rgb(200, 200, 200));
        y += 40;
        }
    // the player(s)
    const TInt n = iPlayers > 1 ? iPlayers : 1;
    for (TInt p = 0; p < n; p++)
        {
        const TBool me = p == iPlayer;
        const TInt h = me ? 34 : iFSmall.iH + 6;
        if (y + h > SCR_H - iFSmall.iH - 16) break;
        Round(iBack, SCR_W, 3, y, SIDE_W - 6, h, 8, me ? Rgb(60, 110, 200) : Rgb(40, 60, 80), me ? Rgb(30, 60, 140) : Rgb(26, 40, 54));
        const char* nm = iPlayers > 1 ? iNames[p] : "PLAYER";
        if (me)
            {
            Icon(iBack, SCR_W, 6, y + 5, EIcoFlagBlack);
            Text(iBack, SCR_W, iFText, 34, y + 2, nm, Rgb(255, 255, 255), SIDE_W - 4);
            Round(iBack, SCR_W, 34, y + 3 + iFText.iH, SIDE_W - 42, iFSmall.iH + 3, 5, Rgb(200, 210, 230), Rgb(150, 170, 200));
            if (iPlayerBest[p]) { FormatTime(b, iPlayerBest[p] * 20); Clock(iBack, SCR_W, iFSmall, 40, y + 4 + iFText.iH, b, Rgb(20, 30, 50)); }
            else Text(iBack, SCR_W, iFSmall, 40, y + 4 + iFText.iH, "--:--.--", Rgb(60, 70, 90));
            }
        else
            {
            Text(iBack, SCR_W, iFSmall, 10, y + 3, nm, Rgb(220, 226, 232), SIDE_W - 70);
            if (iPlayerBest[p]) { FormatTime(b, iPlayerBest[p] * 20); Clock(iBack, SCR_W, iFSmall, SIDE_W - 62, y + 3, b, Rgb(220, 226, 232)); }
            }
        y += h + 3;
        }
    if (iPlayers > 1 && !iStandings)
        {
        // the turn left, as the game's orange bar: the attempts, or the time
        TInt left, all;
        if (iHsMode)
            {
            all = iTimeLimit * 50; left = all - iTurnSteps; if (left < 0) left = 0;
            const TInt sec = (left + 49) / 50;
            b[0] = 0; CCat(b, left ? "time left " : "time's up: finish ");
            if (left) { TBuf8<12> d; d.AppendNum(sec / 60); d.Append(':'); if (sec % 60 < 10) d.Append('0'); d.AppendNum(sec % 60); d.Append(0); CCat(b, (const char*) d.Ptr()); }
            }
        else
            {
            all = iAttempts; left = iAttempts - iRound; if (left < 0) left = 0;
            b[0] = 0; CCat(b, "attempt "); TBuf8<8> d; d.AppendNum(iRound < iAttempts ? iRound + 1 : iAttempts); d.Append('/'); d.AppendNum(iAttempts); d.Append(0);
            CCat(b, (const char*) d.Ptr());
            }
        const TInt by = SCR_H - iFSmall.iH - 12;
        Round(iBack, SCR_W, 6, by, SIDE_W - 12, 7, 3, Rgb(60, 50, 40), Rgb(40, 30, 20));
        if (left) Round(iBack, SCR_W, 6, by, ((SIDE_W - 12) * left) / (all > 0 ? all : 1), 7, 3, Rgb(255, 180, 40), Rgb(220, 110, 0));
        Text(iBack, SCR_W, iFSmall, 8, SCR_H - iFSmall.iH - 3, b, Rgb(240, 200, 60));
        }
    // the right: the track and its author, as the game's corner
    Round(iBack, SCR_W, RIGHT_X + 4, 4, SCR_W - RIGHT_X - 8, iFText.iH + iFSmall.iH + 8, 8, Rgb(150, 156, 160), Rgb(96, 102, 108));
    Text(iBack, SCR_W, iFText, SCR_W - 10 - iFText.Width(iTrackC), 7, iTrackC, Rgb(255, 255, 255));
    Text(iBack, SCR_W, iFSmall, SCR_W - 10 - iFSmall.Width("Nadeo"), 8 + iFText.iH, "Nadeo", Rgb(240, 240, 240));
    // the checkpoints
    b[0] = 0; CCat(b, "checkpoints ");
    { TBuf8<12> d; d.AppendNum(iCar->iCheckpoints); d.Append('/'); d.AppendNum(iCar->iTotalCps); d.Append(0); CCat(b, (const char*) d.Ptr()); }
    Text(iBack, SCR_W, iFSmall, RIGHT_X + 10, 16 + iFText.iH + iFSmall.iH, b, Rgb(200, 220, 240));
    Text(iBack, SCR_W, iFSmall, RIGHT_X + 10, SCR_H - iFSmall.iH - 3, "km/h", Rgb(160, 180, 200));
    }

// over the view each frame: the countdown, the timer, the checkpoint's time against the ghost, the result; and the
// speed at the right's foot
void CTmView::DrawOverlay()
    {
    char b[24];
    // the timer on its plate, bottom middle (negative in the countdown, still after the finish)
    const TmImage* plate = iPack.Find("timerbg");
    const TInt pw = plate ? plate->w : 118, ph = plate ? plate->h : 26;
    const TInt px = VIEW_X + (VIEW_W - pw) / 2, py = SCR_H - ph;
    if (plate) TmDraw::Blit(iBack, SCR_W, px, py, plate); else Round(iBack, SCR_W, px, py, pw, ph, 6, Rgb(20, 60, 200), Rgb(0, 20, 90));
    if (iRaceSteps < 0) { b[0] = '-'; FormatTime(b + 1, -iRaceSteps * 20); }
    else FormatTime(b, (iCar->iFinished ? iCar->iFinishSteps : iCar->iSteps) * 20);
    if (iPack.iN) Led(iBack, SCR_W, px + (pw - LedWidth(b)) / 2, py + (ph - 18) / 2 + 1, b);
    else Clock(iBack, SCR_W, iFBig, px + (pw - ClockWidth(iFBig, b)) / 2, py + (ph - iFBig.iH) / 2, b, Rgb(255, 255, 255));
    // the countdown: 2 (from the clock's start at -2.60), 1 (the last second), then Go! for half a second
    if (iRaceSteps < 25 && !iCar->iFinished)
        {
        const TInt k = iRaceSteps < -50 ? 1 : iRaceSteps < 0 ? 2 : 3;
        char nm[4] = { 'g', 'o', (char) ('0' + k), 0 };
        const TmImage* m = iPack.Find(nm);
        if (m) TmDraw::Blit(iBack, SCR_W, VIEW_X + (VIEW_W - m->w) / 2, 50, m);
        else { const char* t[4] = { "3", "2", "1", "Go!" }; TextCentre(iBack, SCR_W, iFBig, VIEW_X, VIEW_X + VIEW_W, 60, t[k], Rgb(80, 160, 255)); }
        }
    // a checkpoint: its time, and against the ghost (blue ahead, red behind), at the top for 3 s
    if (iSplitUntil > iRaceSteps && !iCar->iFinished)
        {
        FormatTime(b, iSplitTime * 20);
        const TInt w = 150, x = VIEW_X + (VIEW_W - w) / 2;
        Shade(iBack, SCR_W, x, 4, w, 22 + (iSplitHas ? iFText.iH + 2 : 0), 150, Rgb(0, 10, 30));
        if (iPack.iN) Led(iBack, SCR_W, x + (w - LedWidth(b)) / 2, 6, b); else TextCentre(iBack, SCR_W, iFBig, x, x + w, 6, b, Rgb(255, 255, 255));
        if (iSplitHas)
            {
            const TInt cs = (iSplitDiff < 0 ? -iSplitDiff : iSplitDiff) * 2;
            char d[16]; d[0] = iSplitDiff <= 0 ? '-' : '+'; d[1] = 0;
            TBuf8<12> e; e.AppendNum(cs / 100); e.Append('.'); if (cs % 100 < 10) e.Append('0'); e.AppendNum(cs % 100); e.Append(0);
            CCat(d, (const char*) e.Ptr());
            TextCentre(iBack, SCR_W, iFText, x, x + w, 26, d, iSplitDiff <= 0 ? Rgb(90, 160, 255) : Rgb(255, 70, 60));
            }
        }
    if (iSeenFinish || iStandings) DrawResult();
    // the speed, bottom right, in the LED digits
    if (iHudOn)
        {
        const TInt kmh = (iCar->iS.speed < 0 ? -iCar->iS.speed : iCar->iS.speed) * 36 / 2560;
        TBuf8<8> d; d.AppendNum(kmh); d.Append(0);
        Fill(iBack, SCR_W, RIGHT_X + 40, SCR_H - 26, SCR_W - RIGHT_X - 44, 22, Rgb(4, 10, 16));
        const char* s = (const char*) d.Ptr();
        if (iPack.iN) Led(iBack, SCR_W, SCR_W - 8 - LedWidth(s), SCR_H - 24, s);
        else Text(iBack, SCR_W, iFBig, SCR_W - 8 - iFBig.Width(s), SCR_H - 4 - iFBig.iH, s, Rgb(255, 255, 255));
        }
    }

// after the finish, over the view: the time in the medal's colour, the medal, against the opponent, what Enter does;
// the hot seat's standings after the last attempt
void CTmView::DrawResult()
    {
    const TInt w = 260, x = VIEW_X + (VIEW_W - w) / 2, y = 8;
    char b[48];
    if (iStandings)
        {
        // the standings: the players by their best
        TInt order[8]; for (TInt p = 0; p < iPlayers; p++) order[p] = p;
        for (TInt i = 0; i < iPlayers; i++) for (TInt j = i + 1; j < iPlayers; j++)
            {
            const TInt a = iPlayerBest[order[i]] ? iPlayerBest[order[i]] : 0x7fffffff, c = iPlayerBest[order[j]] ? iPlayerBest[order[j]] : 0x7fffffff;
            if (c < a) { TInt t = order[i]; order[i] = order[j]; order[j] = t; }
            }
        // the rows in the text font if all fit the 200 lines, else the small one
        const TmFont* rf = &iFText;
        const TInt hh = iFText.iH + 4;
        TInt rh = iFText.iH + 2;
        if (hh + 7 + iPlayers * rh + iFSmall.iH + 8 > SCR_H - 4) { rf = &iFSmall; rh = iFSmall.iH + 1; }
        const TInt h = hh + 7 + iPlayers * rh + iFSmall.iH + 8, sy = (SCR_H - h) / 2;
        Round(iBack, SCR_W, x, sy, w, h, 6, Rgb(40, 42, 44), Rgb(24, 26, 28));
        Round(iBack, SCR_W, x + 4, sy + 3, w - 8, hh, 5, Rgb(12, 12, 14), Rgb(2, 2, 4));
        TextCentre(iBack, SCR_W, iFText, x, x + w, sy + 3 + (hh - iFText.iH) / 2, "Standings", Rgb(60, 220, 60));
        for (TInt i = 0; i < iPlayers; i++)
            {
            const TInt p = order[i], ry = sy + hh + 7 + i * rh;
            b[0] = (char) ('1' + i); b[1] = '.'; b[2] = ' '; b[3] = 0; CCat(b, iNames[p]);
            Text(iBack, SCR_W, *rf, x + 14, ry, b, i == 0 ? Rgb(255, 220, 80) : Rgb(240, 240, 240));
            if (iPlayerBest[p]) { FormatTime(b, iPlayerBest[p] * 20); Clock(iBack, SCR_W, *rf, x + w - 14 - ClockWidth(*rf, b), ry, b, Rgb(240, 240, 240)); }
            }
        TextCentre(iBack, SCR_W, iFSmall, x, x + w, sy + h - iFSmall.iH - 4, iSeriesPos + 1 < iSeriesN ? "Enter: the next track" : "Enter: back to the menu", Rgb(200, 200, 200));
        return;
        }
    const TInt t = iCar->iFinishSteps, ms = t * 20;
    const TInt* md = iTrack->iMedal;
    TInt medal = -1;
    for (TInt k = 3; k >= 0; k--) if (md[k] && ms <= md[k]) medal = k;
    static const char* const KMedal[4] = { "Nadeo medal!", "Gold medal!", "Silver medal!", "Bronze medal" };
    const TmPix mc[4] = { Rgb(90, 220, 90), Rgb(255, 214, 80), Rgb(214, 214, 214), Rgb(206, 160, 100) };
    const TInt h = 32 + 2 * (iFText.iH + 1) + iFSmall.iH + 8;
    Round(iBack, SCR_W, x, y, w, h, 8, Rgb(40, 42, 44), Rgb(24, 26, 28));
    RoundLine(iBack, SCR_W, x, y, w, h, 8, medal >= 0 ? mc[medal] : Rgb(110, 116, 120));
    FormatTime(b, ms);
    if (medal >= 0) Coin(iBack, SCR_W, x + 26, y + 22, 12, medal);
    if (iPack.iN) Led(iBack, SCR_W, x + (w - LedWidth(b)) / 2, y + 8, b); else TextCentre(iBack, SCR_W, iFBig, x, x + w, y + 6, b, Rgb(255, 255, 255));
    TInt ty = y + 32;
    if (medal >= 0) { TextCentre(iBack, SCR_W, iFText, x, x + w, ty, KMedal[medal], mc[medal]); ty += iFText.iH + 1; }
    else if (md[3]) { b[0] = 0; CCat(b, "no medal (bronze "); char tb[16]; FormatTime(tb, md[3]); CCat(b, tb); CCat(b, ")"); TextCentre(iBack, SCR_W, iFText, x, x + w, ty, b, Rgb(200, 200, 200)); ty += iFText.iH + 1; }
    b[0] = 0;
    if (iSplitHas)
        {
        const TInt cs = (iSplitDiff < 0 ? -iSplitDiff : iSplitDiff) * 2;
        b[0] = iSplitDiff <= 0 ? '-' : '+'; b[1] = 0;
        TBuf8<12> e; e.AppendNum(cs / 100); e.Append('.'); if (cs % 100 < 10) e.Append('0'); e.AppendNum(cs % 100); e.Append(0);
        CCat(b, (const char*) e.Ptr()); CCat(b, " against "); CCat(b, GhostName(iGhostMode));
        }
    if (iRunIsRecord) { if (b[0]) CCat(b, ", "); CCat(b, "new record!"); }
    if (b[0]) { const TmFont& f = iFText.Width(b) > w - 8 ? iFSmall : iFText; TextCentre(iBack, SCR_W, f, x, x + w, ty, b, iSplitHas ? (iSplitDiff <= 0 ? Rgb(90, 160, 255) : Rgb(255, 80, 70)) : Rgb(240, 240, 240)); }
    // what Enter does
    b[0] = 0;
    if (iPlayers > 1)
        {
        if (!TurnOver()) CCat(b, "Press Enter: the next attempt");
        else if (iPlayer + 1 >= iPlayers) CCat(b, "Press Enter: the standings");
        else { CCat(b, "Press Enter: "); CCat(b, iNames[iPlayer + 1]); CCat(b, "'s turn"); }
        }
    else if (iNextFile >= 0) CCat(b, iUnlockedNow ? "Next track open! Enter: go on, Backspace: retry" : "Enter: the next track, Backspace: retry");
    else CCat(b, "Press Enter to restart");
    TextCentre(iBack, SCR_W, iFSmall, x, x + w, y + h - iFSmall.iH - 4, b, iUnlockedNow ? Rgb(255, 210, 80) : Rgb(200, 200, 200));
    }

// ---- the pause (Esc): the game's race menus ----
void CTmView::PauseOpen(TInt aKind)
    {
    if (aKind < 0) aKind = iPlayers > 1 ? EPmHotSeat : EPmRace;
    if (!iPause) iPauseSince.UniversalTime();
    iPause = ETrue; iPauseDirty = ETrue; iPauseKind = aKind; iKeys = 0;
    iPauseSel = aKind == EPmRace ? 3 : 0;
    }

void CTmView::PauseClose()
    {
    if (!iPause) return;
    iPause = EFalse; iKeys = 0;
    TTime now; now.UniversalTime();
    const TTimeIntervalMicroSeconds held = now.MicroSecondsFrom(iPauseSince);
    iDriveStart += held; iStart += held;
    iSidesDirty = ETrue;
    }

void CTmView::TogglePanels()
    {
    iHudOn = !iHudOn; iSidesDirty = ETrue; iPauseDirty = ETrue;
    if (!iHudOn) { for (TInt y = 0; y < SCR_H; y++) { Fill(iBack, SCR_W, 0, y, SIDE_W, 1, 0); Fill(iBack, SCR_W, RIGHT_X, y, SCR_W - RIGHT_X, 1, 0); } iSidesBlit = ETrue; }
    }

// the ghost chooser's rows: the ghosts there are, the slowest first (as the game lists them)
TInt CTmView::GhostRows(TmDlg::TRow* aRows, TInt* aModes) const
    {
    TInt n = 0;
    for (TInt m = EGhostNadeo; m < EGhostModes; m++)
        {
        const TInt f = GhostFinishOf(m);
        if (f <= 0) continue;
        TInt i = n++;
        while (i > 0 && GhostFinishOf(aModes[i - 1]) < f) { aModes[i] = aModes[i - 1]; i--; }
        aModes[i] = m;
        }
    for (TInt i = 0; i < n; i++) { aRows[i].iName = GhostName(aModes[i]); aRows[i].iMs = GhostFinishOf(aModes[i]) * 20; aRows[i].iOn = (iGhostMask & (1 << aModes[i])) != 0; }
    return n;
    }

// aKey: EUiUp, EUiDown, EUiLeft, EUiRight, EUiOk, EUiBack
void CTmView::PauseKeyL(TInt aKey)
    {
    iPauseDirty = ETrue;
    switch (iPauseKind)
        {
        case EPmTurn:
            // the next player's turn: OK starts it; Esc: the hot seat's menu
            if (aKey == EUiOk) { PauseClose(); DriveReset(); }
            else if (aKey == EUiBack) { iPauseKind = EPmHotSeat; iPauseSel = 0; }
            return;
        case EPmGhosts:
            {
            TmDlg::TRow rows[EGhostModes]; TInt modes[EGhostModes];
            const TInt n = GhostRows(rows, modes);
            if (aKey == EUiBack) { iPauseKind = EPmRace; iPauseSel = 0; return; }
            if (aKey == EUiUp) iPauseSel = (iPauseSel + n) % (n + 1);
            else if (aKey == EUiDown) iPauseSel = (iPauseSel + 1) % (n + 1);
            else if (aKey == EUiOk || aKey == EUiLeft || aKey == EUiRight)
                {
                if (iPauseSel >= n) { if (aKey == EUiOk) { iPauseKind = EPmRace; iPauseSel = 3; } return; }
                const TInt bit = 1 << modes[iPauseSel];
                TInt on = 0; for (TInt m = 0; m < EGhostModes; m++) if (iGhostMask & (1 << m)) on++;
                if (iGhostMask & bit) iGhostMask &= ~bit;
                else if (on < TmView::KMaxGhosts) iGhostMask |= bit;
                GhostRef();
                iSplitUntil = 0;
                if (iSeenFinish) { iSplitHas = GhostFinish() > 0; iSplitDiff = iCar->iFinishSteps - GhostFinish(); }
                }
            return;
            }
        case EPmHotSeat:
            if (aKey == EUiBack) { PauseClose(); return; }
            if (aKey == EUiUp) iPauseSel = (iPauseSel + 2) % 3;
            else if (aKey == EUiDown) iPauseSel = (iPauseSel + 1) % 3;
            else if (aKey == EUiOk)
                {
                if (iPauseSel == 0) PauseClose();
                else if (iPauseSel == 1) { PauseClose(); if (!iStandings) EndTurn(); }
                else BackToMenu();
                }
            return;
        default:
            // the race menu: 0 Choose opponents, 1 Restart, 2 Race panels, 3 Continue, 4 Quit (3 and 4 side by side)
            if (aKey == EUiBack) { PauseClose(); return; }
            if (aKey == EUiUp) iPauseSel = iPauseSel >= 3 ? 2 : (iPauseSel + 4) % 4;
            else if (aKey == EUiDown) iPauseSel = iPauseSel >= 3 ? 0 : iPauseSel + 1;
            else if ((aKey == EUiLeft || aKey == EUiRight) && iPauseSel >= 3) iPauseSel = aKey == EUiLeft ? 3 : 4;
            else if (aKey == EUiOk || ((aKey == EUiLeft || aKey == EUiRight) && iPauseSel == 2))
                {
                switch (iPauseSel)
                    {
                    case 0: iPauseKind = EPmGhosts; iPauseSel = 0; break;
                    case 1: PauseClose(); DriveReset(); break;
                    case 2: TogglePanels(); break;
                    case 3: PauseClose(); break;
                    default: BackToMenu(); break;
                    }
                }
            return;
        }
    }

void CTmView::DrawPause()
    {
    Shade(iBack, SCR_W, VIEW_X, 0, VIEW_W, VIEW_H, 70, 0);
    const TInt x0 = VIEW_X + 12, x1 = VIEW_X + VIEW_W - 12;
    switch (iPauseKind)
        {
        case EPmTurn:
            {
            char b[48]; b[0] = 0; CCat(b, "Now it's player "); CCat(b, iNames[iPlayer]); CCat(b, "'s turn!");
            TmDlg::Message(iBack, SCR_W, iFText, x0, x1, b);
            break;
            }
        case EPmGhosts:
            {
            TmDlg::TRow rows[EGhostModes]; TInt modes[EGhostModes];
            const TInt n = GhostRows(rows, modes);
            TmDlg::Ghosts(iBack, SCR_W, iFText, iFSmall, x0, x1, rows, n, iPauseSel);
            break;
            }
        case EPmHotSeat:
            {
            const char* rows[3] = { "Continue", "Give up", "Quit" };
            TmDlg::Buttons(iBack, SCR_W, iFText, iFSmall, x0 + 20, x1 - 20, "Hot Seat", rows, 3, iPauseSel, ETrue);
            break;
            }
        default:
            {
            const char* rows[4] = { "Choose opponents", "Restart", iHudOn ? "Race panels: on" : "Race panels: off", "Continue|Quit" };
            TmDlg::Buttons(iBack, SCR_W, iFText, iFSmall, x0, x1, "Race menu", rows, 4, iPauseSel, ETrue);
            break;
            }
        }
    }

// the profiles: one name a line
void CTmView::LoadProfiles()
    {
    if (!iUi) return;
    RFs& fs = iCoeEnv->FsSession();
    TFileName name(KBenchDir); name.Append(_L("profiles.txt"));
    RFile f;
    if (f.Open(fs, name, EFileRead | EFileShareReadersOnly) != KErrNone) return;
    TBuf8<512> d; f.Read(d); f.Close();
    iUi->iNProfiles = 0;
    TInt k = 0; char* p = iUi->iProfiles[0];
    for (TInt i = 0; i <= d.Length() && iUi->iNProfiles < TmMenu::KMaxProfiles; i++)
        {
        const TInt c = i < d.Length() ? d[i] : '\n';
        if (c == '\n' || c == '\r') { if (k) { p[k] = 0; iUi->iNProfiles++; p = iUi->iProfiles[iUi->iNProfiles < TmMenu::KMaxProfiles ? iUi->iNProfiles : 0]; k = 0; } }
        else if (k < TmMenu::KNameLen && c >= 32 && c < 127) p[k++] = (char) c;
        }
    }

void CTmView::SaveProfiles()
    {
    iUi->iProfilesChanged = EFalse;
    RFs& fs = iCoeEnv->FsSession();
    fs.MkDirAll(KBenchDir);
    TFileName name(KBenchDir); name.Append(_L("profiles.txt"));
    TBuf8<512> d;
    for (TInt i = 0; i < iUi->iNProfiles; i++) { d.Append(TPtrC8((const TUint8*) iUi->iProfiles[i])); d.Append(_L8("\r\n")); }
    RFile f;
    if (f.Replace(fs, name, EFileWrite) == KErrNone) { f.Write(d); f.Close(); }
    }

// ---- the menus' side ----
void CTmView::StartRace()
    {
    iPlayers = iUi->iPlayers > 1 ? iUi->iPlayers : 1;
    for (TInt p = 0; p < 8; p++) { iNames[p][0] = 0; if (p < iPlayers) { TInt k = 0; for (; iUi->iNames[p][k] && k < 15; k++) iNames[p][k] = iUi->iNames[p][k]; iNames[p][k] = 0; } iPlayerBest[p] = 0; }
    iAttempts = iUi->iAttempts > 0 ? iUi->iAttempts : 1;
    iHsMode = iUi->iHsMode; iTimeLimit = iUi->iTimeLimit > 0 ? iUi->iTimeLimit : 300;
    iRound = 0; iPlayer = 0; iTurnSteps = 0; iStandings = EFalse;
    iSeriesN = iUi->iSeriesN; for (TInt k = 0; k < iSeriesN && k < 8; k++) iSeries[k] = iUi->iSeriesFiles[k];
    iSeriesPos = 0;
    // the opponent chosen in the menu; the hot seat: the player's own best run of his turn
    switch (iUi->iRaceOpp)
        {
        case EOppNadeo: iGhostMask = 1 << EGhostNadeo; break;
        case EOppGold: iGhostMask = 1 << EGhostGold; break;
        case EOppSilver: iGhostMask = 1 << EGhostSilver; break;
        case EOppBronze: iGhostMask = 1 << EGhostBronze; break;
        case EOppRecord: iGhostMask = 1 << EGhostRecord; break;
        case EOppLast: iGhostMask = 1 << EGhostLast; break;
        default: iGhostMask = 0; break;
        }
    if (iPlayers > 1) iGhostMask = 1 << EGhostLast;
    UnloadTrack();
    if (!LoadTrack(iUi->iRaceFile)) { iUi->iDirty = ETrue; return; }
    iInMenu = EFalse; iKeys = 0;
    iSidesDirty = ETrue; iFirstBlit = ETrue;
    GhostRef();
    if (iPlayers > 1) TurnStart();
    }

void CTmView::BackToMenu()
    {
    if (iRunIsRecord && iRun.iN) { iGhostRecord.Copy(iRun); SaveRecord(); iRunIsRecord = EFalse; }
    UnloadTrack();
    iInMenu = ETrue; iKeys = 0; iPause = EFalse;
    iUi->iDirty = ETrue; iFirstBlit = ETrue;
    }

void CTmView::MenuKeyL(const TKeyEvent& aKey)
    {
    const TInt c = aKey.iCode, sc = aKey.iScanCode;
    TInt key = -1, ch = 0;
    if (c == EKeyUpArrow || sc == EStdKeyUpArrow || sc == EStdKeyDevice8) key = EUiUp;
    else if (c == EKeyDownArrow || sc == EStdKeyDownArrow || sc == EStdKeyDevice9) key = EUiDown;
    else if (c == EKeyLeftArrow || sc == EStdKeyLeftArrow || sc == EStdKeyDevice6) key = EUiLeft;
    else if (c == EKeyRightArrow || sc == EStdKeyRightArrow || sc == EStdKeyDevice7) key = EUiRight;
    else if (c == EKeyEnter || c == EKeyDevice3 || sc == EStdKeyDevice3) key = EUiOk;
    else if (c == EKeyEscape) key = EUiBack;
    else if (c == EKeyBackspace || c == EKeyDelete) key = EUiDel;
    else if (c >= 32 && c < 127) { key = EUiChar; ch = c; }
    if (key < 0) return;
    const TInt a = iUi->Key(key, ch);
    if (iUi->iProfilesChanged) SaveProfiles();
    if (a == EUiQuit) iEikonEnv->EikAppUi()->HandleCommandL(EEikCmdExit);
    else if (a == EUiRace) StartRace();
    }

void CTmView::Blit(const TUint16* aSrc, TBool aAll)
    {
    TBool direct = iFb && StartDirect();
    if (!direct) StopDirect();
    if (direct)
        {
        if (iFbBpp == 12)
            {
            for (TInt y = 0; y < SCR_H; y++)
                {
                const TUint16* src = aSrc + y * SCR_W;
                TUint16* dst = (TUint16*)(iFb + y * iFbStride);
                for (TInt x = 0; x < SCR_W; x++) { TUint16 c = src[x]; dst[x] = (TUint16)(((c >> 4) & 0xF00) | ((c >> 3) & 0xF0) | ((c >> 1) & 0xF)); }
                }
            iDsa->ScreenDevice()->Update();
            return;
            }
        if (aAll || iFirstBlit)
            {
            for (TInt y = 0; y < SCR_H; y++) fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, SCR_W * 2);
            iFirstBlit = EFalse; iSidesBlit = EFalse;
            iDsa->ScreenDevice()->Update();
            return;
            }
        // only what changes: the view; the sides when drawn again, else the speed's corner; the counters
        for (TInt y = 0; y < VIEW_H; y++) fm_memcpy(iFb + y * iFbStride + VIEW_X * 2, aSrc + y * SCR_W + VIEW_X, VIEW_W * 2);
        TRegionFix<4> upd; upd.AddRect(TRect(VIEW_X, 0, VIEW_X + VIEW_W, VIEW_H));
        if (iSidesBlit)
            {
            for (TInt y = 0; y < SCR_H; y++)
                {
                fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, SIDE_W * 2);
                fm_memcpy(iFb + y * iFbStride + RIGHT_X * 2, aSrc + y * SCR_W + RIGHT_X, (SCR_W - RIGHT_X) * 2);
                }
            upd.AddRect(TRect(0, 0, SIDE_W, SCR_H)); upd.AddRect(TRect(RIGHT_X, 0, SCR_W, SCR_H));
            iSidesBlit = EFalse;
            }
        else if (iHudOn)
            {
            for (TInt y = SCR_H - 26; y < SCR_H; y++) fm_memcpy(iFb + y * iFbStride + RIGHT_X * 2, aSrc + y * SCR_W + RIGHT_X, (SCR_W - RIGHT_X) * 2);
            upd.AddRect(TRect(RIGHT_X, SCR_H - 26, SCR_W, SCR_H));
            }
        if (iShowInfo || iBench >= 0)
            {
            for (TInt y = 0; y < BAR_H; y++) fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, BAR_W * 2);
            upd.AddRect(TRect(0, 0, BAR_W, BAR_H));
            }
        iDsa->ScreenDevice()->Update(upd);
        return;
        }
    iBmp->LockHeap();
    TUint16* p = (TUint16*)iBmp->DataAddress();
    TInt stride = CFbsBitmap::ScanLineLength(SCR_W, i4K ? EColor4K : EColor64K) / 2;
    for (TInt y = 0; y < SCR_H; y++)
        {
        const TUint16* src = aSrc + y * SCR_W;
        TUint16* dst = p + y * stride;
        if (i4K) for (TInt x = 0; x < SCR_W; x++) { TUint16 c = src[x]; dst[x] = (TUint16)(((c >> 4) & 0xF00) | ((c >> 3) & 0xF0) | ((c >> 1) & 0xF)); }
        else Mem::Copy(dst, src, SCR_W * 2);
        }
    iBmp->UnlockHeap();
    DrawNow();
    iCoeEnv->WsSession().Flush();
    }

void CTmView::Draw(const TRect& /*aRect*/) const
    {
    CWindowGc& gc = SystemGc();
    if (!iLoaded)
        {
        gc.SetBrushColor(KRgbBlack); gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.Clear();
        const CFont* font = iEikonEnv->NormalFont();
        gc.UseFont(font);
        gc.SetPenColor(KRgbWhite);
        gc.DrawText(iStatus, TPoint(20, 60));
        gc.DiscardFont();
        return;
        }
    if (iDsaOn) return;
    gc.BitBlt(TPoint(0, 0), iBmp);
    }

TKeyResponse CTmView::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
    {
    TUint32 bit = 0;
    switch (aKeyEvent.iScanCode)
        {
        case EStdKeyLeftArrow: case EStdKeyDevice6: bit = KEY_LEFT; break;
        case EStdKeyRightArrow: case EStdKeyDevice7: bit = KEY_RIGHT; break;
        case EStdKeyUpArrow: case EStdKeyDevice8: bit = KEY_UP; break;
        case EStdKeyDownArrow: case EStdKeyDevice9: bit = KEY_DOWN; break;
        case 'W': bit = KEY_UP; break;         // WSAD too (the scan codes are the capitals)
        case 'S': bit = KEY_DOWN; break;
        case 'A': bit = KEY_LEFT; break;
        case 'D': bit = KEY_RIGHT; break;
        }
    if (iInMenu || iPause) bit = 0;
    if (aType == EEventKeyDown) iKeys |= bit;
    else if (aType == EEventKeyUp) iKeys &= ~bit;
    else if (aType == EEventKey)
        {
        TInt c = aKeyEvent.iCode;
        const TInt sc = aKeyEvent.iScanCode;
        if (!iLoaded) { if (c == EKeyEscape) iEikonEnv->EikAppUi()->HandleCommandL(EEikCmdExit); return EKeyWasConsumed; }
        if (iInMenu) { MenuKeyL(aKeyEvent); return EKeyWasConsumed; }
        const TBool joyPress = (c == EKeyDevice3 || sc == EStdKeyDevice3);
        if (iPause)
            {
            TInt key = -1;
            if (c == EKeyEscape) key = EUiBack;
            else if (c == EKeyUpArrow || sc == EStdKeyUpArrow || sc == EStdKeyDevice8) key = EUiUp;
            else if (c == EKeyDownArrow || sc == EStdKeyDownArrow || sc == EStdKeyDevice9) key = EUiDown;
            else if (c == EKeyLeftArrow || sc == EStdKeyLeftArrow || sc == EStdKeyDevice6) key = EUiLeft;
            else if (c == EKeyRightArrow || sc == EStdKeyRightArrow || sc == EStdKeyDevice7) key = EUiRight;
            else if (c == EKeyEnter || joyPress || c == ' ') key = EUiOk;
            else if (c == 'h' || c == 'H') { TogglePanels(); return EKeyWasConsumed; }
            if (key >= 0) PauseKeyL(key);
            return EKeyWasConsumed;
            }
        if (c == EKeyEscape) { if (iDriving && iBench < 0) PauseOpen(); else BackToMenu(); return EKeyWasConsumed; }
        if (iDriving)
            {
            // the joystick's press does nothing while driving; Enter: back to the last checkpoint taken (the start
            // when none), the clock running; after the finish the next run; Backspace: the start
            if (joyPress) return EKeyWasConsumed;
            if (c == EKeyEnter) { if (iCar->iFinished || iStandings) NextRun(); else if (iRaceSteps >= 0) { iCar->Respawn(); iCam->Reset(iCar->iS); } return EKeyWasConsumed; }
            if (c == EKeyBackspace) { if (iPlayers > 1) HsRestart(); else DriveReset(); return EKeyWasConsumed; }
            }
        if (c == 'h' || c == 'H') TogglePanels();
        else if ((c == 'v' || c == 'V') && iGround) { iDriving = !iDriving; if (iDriving) DriveReset(); else { iStart.UniversalTime(); iPauseAt = -1; } }
        else if (c == ' ' && !iDriving)
            {
            if (iPauseAt >= 0) { TTime now; now.UniversalTime(); iStart = now - TTimeIntervalMicroSeconds(iPauseAt * 1000); iPauseAt = -1; }
            else { TTime now; now.UniversalTime(); iPauseAt = (TInt)(now.MicroSecondsFrom(iStart).Int64().Low() / 1000); }
            }
        else if (c == 'f' || c == 'F') { iShowInfo = !iShowInfo; iSidesDirty = ETrue; }
        else if (c == 'm' || c == 'M') iView->iMeshMode = !iView->iMeshMode;
        else if (c == '+' || c == '=') { iFarM += 30; if (iFarM > 480) iFarM = 480; iView->iFarCut = iFarM * 16; }
        else if (c == '-') { iFarM -= 30; if (iFarM < 60) iFarM = 60; iView->iFarCut = iFarM * 16; }
        else if (c == 'r' || c == 'R') { if (iDriving) { if (iPlayers > 1) HsRestart(); else DriveReset(); } else { iStart.UniversalTime(); iPauseAt = -1; iYawOff = 0; iPitchOff = 0; } }
        else if (c == 'b' || c == 'B') { if (iBench < 0) { iDriving = EFalse; BenchStart(); } }
        else if (c >= '1' && c <= '9' && !iDriving) iSpeed = (c - '0') * 25;
        else if (c == '0') { iYawOff = 0; iPitchOff = 0; }
        }
    return EKeyWasConsumed;
    }

// ---------------------------------------------------------------- app framework
class CTmAppUi : public CEikAppUi
    {
public:
    void ConstructL();
    ~CTmAppUi();
    void HandleCommandL(TInt aCommand);
    void HandleForegroundEventL(TBool aForeground);
private:
    CTmView* iView;
    };

void CTmAppUi::ConstructL()
    {
    BaseConstructL();
    CknEnv::Skin().SetAppViewType(ESkinAppViewNoCbaNoToolband);
    iView = new (ELeave) CTmView;
    iView->ConstructL(ApplicationRect());
    AddToStackL(iView);
    }

CTmAppUi::~CTmAppUi()
    {
    if (iView) { RemoveFromStack(iView); delete iView; }
    }

void CTmAppUi::HandleCommandL(TInt aCommand)
    {
    if (aCommand == EEikCmdExit) Exit();
    }

void CTmAppUi::HandleForegroundEventL(TBool aForeground)
    {
    CEikAppUi::HandleForegroundEventL(aForeground);
    if (iView) iView->SetActive(aForeground);
    }

class CTmDocument : public CEikDocument
    {
public:
    CTmDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
    CEikAppUi* CreateAppUiL() { return new (ELeave) CTmAppUi; }
    };

class CTmApplication : public CEikApplication
    {
    CApaDocument* CreateDocumentL() { return new (ELeave) CTmDocument(*this); }
    TUid AppDllUid() const { return KUidTm9300; }
    };

EXPORT_C CApaApplication* NewApplication() { return new CTmApplication; }
GLDEF_C TInt E32Dll(TDllReason) { return KErrNone; }
