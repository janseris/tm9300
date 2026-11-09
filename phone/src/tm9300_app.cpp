// tm9300_app.cpp - Series 80 (Nokia 9300) application shell for the TrackMania 9300 track viewer (phase 3:
// a flythrough of a packed track along its replay path, to measure the frame rate on the phone).
// After Hrimfell's shell: direct screen access when the phone allows it, a CIdle loop, keys by scan code.
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
#include "fastmem.h"

const TUid KUidTm9300 = { 0x0E3D9310 };
enum { SCR_W = 640, SCR_H = 200 };
enum { VIEW_W = 320, VIEW_H = 200 };        // the 3D view, in the middle of the screen (Maps 9300's layout)
_LIT(KTrackDir, "?:\\system\\apps\\tm9300\\");
_LIT(KTrackPattern, "*.tmb");
_LIT(KBenchDir, "C:\\Data\\TM9300\\");
_LIT(KBenchFile, "C:\\Data\\TM9300\\bench.txt");
_LIT8(KBenchHost, "192.168.137.1");
const TInt KBenchPort = 8000;
enum { BENCH_VARIANTS = 5, BENCH_LOG = 6144 };
enum { BAR_W = 64, BAR_H = 152 };           // the counters, top left
enum { HUD_X = (SCR_W + VIEW_W) / 2, HUD_W = SCR_W - HUD_X };     // 0.8: the race's panel on the right, in the phone's fonts
enum { RES_W = 260, RES_H = 100, RES_X = (SCR_W - RES_W) / 2, RES_Y = 24 };
enum { MENU_W = 280, MENU_H = 184, MENU_X = (SCR_W - MENU_W) / 2, MENU_Y = (SCR_H - MENU_H) / 2 };
_LIT(KVersion, "0.9");

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

// keys: driving: the arrows (up throttle, down brake / reverse, left / right steer), Enter restart; V the flythrough
// (then: Space pause, arrows look round, R restart, 1-9 speed); F the counters, M mesh / polygon mode, +/- the far cut, B benchmark
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
static const TUint16 KLetters[26] = { 0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x2B6A, 0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492, 0x5B6B, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7 };   // A-Z, as KFont
static const TUint16 KLower[26] = { 0x076B, 0x4D6E, 0x0723, 0x176B, 0x2BE3, 0x15D2, 0x075E, 0x4D6D, 0x2092, 0x106A, 0x4BAD, 0x6497, 0x0DED, 0x0D6D, 0x056A, 0x0D74, 0x0759, 0x0724, 0x388E, 0x2E93, 0x0B6B, 0x0B7A, 0x0B7F, 0x0A95, 0x5ACE, 0x0EF7 };   // 0.6: a-z, 3 x 5 (the HUD's words)
// 0.7: words in single size letters (3 x 5 pixels, 4 a letter: 18 in the bar), small and capital letters, digits, ':'
static void DrawWords(TUint16* aFrame, TInt aStride, TInt aX, TInt aY, const TDesC& aText, TUint16 aCol)
    {
    for (TInt i = 0; i < aText.Length() && i < 18; i++, aX += 4)
        {
        TInt c = aText[i];
        TUint16 g;
        if (c >= 'a' && c <= 'z') g = KLower[c - 'a'];
        else if (c >= 'A' && c <= 'Z') g = KLetters[c - 'A'];
        else if (c >= '0' && c <= '9') g = KFont[c - '0'];
        else if (c == ':') g = 0x0410;
        else if (c == '-' || c == '_') g = 0x01C0;
        else continue;
        for (TInt r = 0; r < 5; r++)
            {
            TUint16* p = aFrame + (aY + r) * aStride + aX;
            for (TInt k = 0; k < 3; k++) if (g & (1 << (14 - r * 3 - k))) p[k] = aCol;
            }
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

// times as the game shows them: m:ss.cc (a step is 20 ms); a difference with its sign (+ slower, - faster)
static void AppendTime(TDes& aOut, TInt aSteps)
    {
    if (aSteps < 0) aSteps = 0;
    const TInt cs = aSteps * 2;
    aOut.AppendFormat(_L("%d:%02d.%02d"), cs / 6000, (cs / 100) % 60, cs % 100);
    }
static void AppendDiff(TDes& aOut, TInt aSteps)
    {
    aOut.Append(aSteps < 0 ? '-' : '+');
    const TInt cs = (aSteps < 0 ? -aSteps : aSteps) * 2;
    if (cs >= 6000) aOut.AppendFormat(_L("%d:%02d.%02d"), cs / 6000, (cs / 100) % 60, cs % 100);
    else aOut.AppendFormat(_L("%d.%02d"), cs / 100, cs % 100);
    }

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
    TBool LoadTrack(TInt aIndex);
    void UnloadTrack();
    TInt iTrackIndex, iTrackCount;
    TBuf<16> iTrackName;        // the file's name without .tmb
    TBool StartDirect();
    void StopDirect();
    void Blit(const TUint16* aSrc);
    CFbsBitmap* iBmp;
    CIdle* iIdle;
    RChunk iChunk;
    TUint8* iData; TInt iDataSize;
    TUint16* iBack;
    TmTrack* iTrack;
    TmView* iView;
    TBool iLoaded, iRunning, i4K, iDsaOn, iFirstBlit;
    CDirectScreenAccess* iDsa;
    TUint8* iFb;
    TInt iFbStride, iFbBpp;
    TTime iStart, iSecond;
    TInt iPauseAt, iSpeed;      // ms into the path when paused (-1 running); speed % (100 = real time)
    TInt iFrames, iFps, iDrawMs, iBlitMs, iDrawSum, iBlitSum, iTris;
    TUint32 iKeys;
    TInt iYawOff, iPitchOff;
    TBool iShowInfo;
    TInt iFarM, iDrawCar;
    TBuf<64> iStatus;
    // driving: the car on the track (TmPhys), stepped 50 times a second by the clock
    TBool iDriving;
    TmGround* iGround;
    TmCar* iCar;
    TmFollowCam* iCam;
    TTime iDriveStart;          // the clock when the car was reset
    TInt iDriveSteps;           // steps done since
    void DriveReset();
    void DriveFrame(TInt& aCx, TInt& aCy, TInt& aCz, TInt& aYaw, TInt& aPitch, TInt& aCarX, TInt& aCarY, TInt& aCarZ, TInt& aCarYaw);
    // the benchmark: the replay once per variant, the figures into iLog, then the file and the PC
    void BenchStart();
    void BenchVariant(TInt aV);
    void BenchFrame(TInt aPos, TInt aDrawUs, TInt aBlitUs);
    void BenchEnd();
    TInt iBench;                // the variant running (-1: off)
    TInt iBenchFrames, iBenchDrawUs, iBenchBlitUs, iBenchMaxUs, iBenchDrawn, iBenchPolys, iBenchVis, iBenchLastPos, iBenchSecFrames, iBenchSec;
    TBuf8<BENCH_LOG> iLog;
    TBuf8<160> iSecs;
    CBenchSender* iSender;
    TInt iSendShown;
    // 0.8: the race's panel and the menu, in the phone's fonts
    enum { KHudLines = 13 };
    CTextCanvas iHud, iMenuCv;
    const CFont* iFontBig;
    const CFont* iFontText;
    const CFont* iFontSmall;
    TBuf<40> iHudText[KHudLines];
    TUint16 iHudCol[KHudLines];
    TBool iHudValid;
    void DrawHud();
    void HudLine(TInt aLine, const TDesC& aText, TUint16 aCol, TInt& aY, TInt& aDirty0, TInt& aDirty1);
    // the menu (Esc): the race stands still while it is open
    enum { EMContinue, EMRestart, EMGhost, EMPlayers, EMDepth, EMNext, EMPrev, EMQuit, EMItems };
    TBool iMenu, iMenuDirty;
    TInt iMenuSel;
    TTime iMenuAt;
    void MenuOpen();
    void MenuClose();
    void MenuKeyL(TInt aDir, TBool aSelect);
    void DrawMenu();
    // ghosts: the track's replay (the author), the record (saved per track), the last finished run; the run being driven
    // 0.8: the medals' ghosts are the author's run (the replay) slowed to the medal's time
    enum { EGhostOff, EGhostNadeo, EGhostGold, EGhostSilver, EGhostBronze, EGhostRecord, EGhostLast, EGhostModes };
    TmGhost iGhostAuthor, iGhostRecord, iGhostLast, iRun;
    TInt iGhostMode;
    const TmGhost* Ghost() const { TInt n, d; return Ghost(n, d); }
    const TmGhost* Ghost(TInt& aNum, TInt& aDen) const;     // the run, and its clock: its step = the race's * aNum / aDen
    TInt GhostFinish() const;                               // its finish (steps; 0 none): a medal's own time
    TInt GhostCp(TInt aK) const;                            // when it took its aK-th checkpoint (steps; 0 none)
    // after the finish: the result over the view (the time, the medal, against the ghost, what Enter does)
    CTextCanvas iResultCv;
    TBool iResultDirty;
    void DrawResult();
    void GhostName(TDes& aOut, TInt aMode) const;
    TInt iRaceSteps;            // steps since the start (the ghost's clock; the race clock stops at the finish)
    TInt iSeenCps;              // checkpoints already shown
    TBool iSeenFinish;
    TInt iSplitUntil, iSplitDiff, iSplitTime, iSplitCp, iSplitHas;     // the last checkpoint's (or the finish's) time and difference, shown 3 s
    void SaveRecord();
    void LoadRecord();
    void GhostFile(TDes& aName) const;
    // hot seat: players take turns (Enter after the finish), each one's best kept
    TInt iPlayers, iPlayer;
    TInt iPlayerBest[4];
    TBool iRunIsRecord;         // this run set the record (saved again with its last points when the next starts)
    void NextRun();
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
    iSpeed = 100; iPauseAt = -1; iShowInfo = ETrue; iFarM = 180; iDrawCar = 1;
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
    // 0.8: the panel's and the menu's fonts (the phone's): the clock big, the rest as the phone's text
    iFontBig = iEikonEnv->TitleFont(); iFontText = iEikonEnv->NormalFont(); iFontSmall = iEikonEnv->DenseFont();
    if (!iFontBig || !iFontText || !iFontSmall || !iHud.Create(HUD_W, SCR_H) || !iMenuCv.Create(MENU_W, MENU_H)) iFontBig = 0;
    if (iFontBig && !iResultCv.Create(RES_W, RES_H)) iFontBig = 0;
    iPlayers = 1; iGhostMode = EGhostNadeo;
    iLoaded = LoadTrack(0);
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
    iChunk.Close();
    }

void CTmView::BenchStart()
    {
    iLog.Zero();
    TInt f = 0, t = 0; HAL::Get(HALData::EMemoryRAMFree, f); HAL::Get(HALData::EMemoryRAM, t);
    TBuf8<64> name8; name8.Copy(iStatus);
    iLog.AppendFormat(_L8("tm9300 bench v0.9: %S, %d meshes %d instances %d triangles, display %d bpp direct %d, RAM free %d KB of %d KB\r\n"),
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
    iChunk.Close(); iBack = 0; iData = 0;
    iLoaded = EFalse; iDriving = EFalse; iBench = -1;
    iSeenFinish = EFalse; iRunIsRecord = EFalse; iMenu = EFalse;
    }

TBool CTmView::LoadTrack(TInt aIndex)
    {
    RFs& fs = iCoeEnv->FsSession();
    // the first .tmb beside the program, on the drive it was installed to (C:, the card)
    TFileName dir(KTrackDir);
    TFileName app = iEikonEnv->EikAppUi()->Application()->AppFullName();
    dir[0] = app.Length() ? app[0] : (TText)'C';
    CDir* list = 0;
    TFileName pat(dir); pat.Append(KTrackPattern);
    if (fs.GetDir(pat, KEntryAttNormal, ESortByName, list) != KErrNone || !list || list->Count() == 0)
        {
        delete list;
        iStatus = _L("No .tmb track beside tm9300.app");
        return EFalse;
        }
    iTrackCount = list->Count();
    iTrackIndex = ((aIndex % iTrackCount) + iTrackCount) % iTrackCount;
    TFileName name(dir); name.Append((*list)[iTrackIndex].iName);
    iStatus = (*list)[iTrackIndex].iName.Left(60);
    iTrackName = (*list)[iTrackIndex].iName.Left(16);
    TInt dot = iTrackName.LocateReverse('.'); if (dot > 0) iTrackName.SetLength(dot);
    delete list;
    RFile f;
    if (f.Open(fs, name, EFileRead | EFileShareReadersOnly) != KErrNone) { iStatus = _L("Cannot open the track"); return EFalse; }
    TInt size = 0;
    f.Size(size);
    TInt need = size + 64 + SCR_W * SCR_H * 2 + 64;
    if (iChunk.CreateLocal(need, need) != KErrNone) { f.Close(); iStatus = _L("No memory for the track"); return EFalse; }
    TUint8* base = iChunk.Base();
    iBack = (TUint16*)base;
    iData = base + ((SCR_W * SCR_H * 2 + 63) & ~63);
    iDataSize = size;
    TPtr8 p(iData, 0, size);
    TInt err = f.Read(p);
    f.Close();
    if (err != KErrNone || p.Length() != size) { iStatus = _L("Cannot read the track"); return EFalse; }
    iTrack = new TmTrack;
    iView = new TmView;
    if (!iTrack || !iView || !iTrack->Load(iData, size) || !iView->Init(VIEW_W, VIEW_H, 2500)) { iStatus = _L("Bad track file"); return EFalse; }
    iView->iFarCut = iFarM * 16;
    iView->iMeshMode = 1;
    // the side bars once
    for (TInt i = 0; i < SCR_W * SCR_H; i++) iBack[i] = 0;
    iFirstBlit = ETrue;
    iBench = -1;
    // the car, when the track carries its collision (TMB4): driving is the default, V the flythrough
    iDriving = EFalse;
    if (iTrack->iNCInst > 0)
        {
        iGround = new TmGround; iCar = new TmCar; iCam = new TmFollowCam;
        if (iGround && iCar && iCam && iGround->Init(*iTrack)) { iDriving = ETrue; DriveReset(); }
        }
    // 0.8: the ghosts: the replay's (its checkpoints found by the car's rules), the record from its file; the hot
    // seat starts over
    iGhostAuthor.Clear(); iGhostRecord.Clear(); iGhostLast.Clear();
    if (iDriving) { iGhostAuthor.FromPath(*iTrack, *iGround, *iCar); LoadRecord(); }
    iPlayer = 0; for (TInt k = 0; k < 4; k++) iPlayerBest[k] = 0;
    iHudValid = EFalse;
    if (!iDriving) BenchStart();    // a viewer-only track: the benchmark runs by itself (B runs it again)
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
    // 0.8: the run before: the last ghost when it finished, the record again with its points past the line
    if (iSeenFinish && iRun.iN) iGhostLast.Copy(iRun);
    if (iRunIsRecord && iRun.iN) { iGhostRecord.Copy(iRun); SaveRecord(); }
    iSeenFinish = EFalse; iRunIsRecord = EFalse;
    iCar->Reset(*iTrack);
    iCam->Reset(iCar->iS);
    iDriveStart.UniversalTime();
    iDriveSteps = 0;
    iRaceSteps = 0; iSeenCps = 0; iSplitUntil = 0; iSplitHas = EFalse;
    iRun.Begin();
    iView->iGhostOn = 0;
    }

// Enter after the finish: the next run (in the hot seat the next player's)
void CTmView::NextRun()
    {
    if (iPlayers > 1) iPlayer = (iPlayer + 1) % iPlayers;
    DriveReset();
    }

const TmGhost* CTmView::Ghost(TInt& aNum, TInt& aDen) const
    {
    aNum = aDen = 1;
    const TmGhost* g = 0;
    switch (iGhostMode)
        {
        case EGhostNadeo: g = &iGhostAuthor; break;
        case EGhostGold: case EGhostSilver: case EGhostBronze:
            {
            // the author's run on a slower clock: at the medal's time it crosses the line
            const TInt medal = iTrack ? iTrack->iMedal[iGhostMode - EGhostNadeo] / 20 : 0;
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

TInt CTmView::GhostFinish() const
    {
    TInt n, d;
    const TmGhost* g = Ghost(n, d);
    if (!g) return 0;
    if (iGhostMode >= EGhostNadeo && iGhostMode <= EGhostBronze && iTrack && iTrack->iMedal[iGhostMode - EGhostNadeo] > 0)
        return iTrack->iMedal[iGhostMode - EGhostNadeo] / 20;
    return g->Finish();
    }

TInt CTmView::GhostCp(TInt aK) const
    {
    TInt n, d;
    const TmGhost* g = Ghost(n, d);
    if (!g || !g->CpSteps(aK)) return 0;
    return (TInt) (((TInt64) g->CpSteps(aK) * d) / n).Low();
    }

void CTmView::GhostName(TDes& aOut, TInt aMode) const
    {
    switch (aMode)
        {
        case EGhostNadeo: aOut.Append(_L("Nadeo")); break;
        case EGhostGold: aOut.Append(_L("gold")); break;
        case EGhostSilver: aOut.Append(_L("silver")); break;
        case EGhostBronze: aOut.Append(_L("bronze")); break;
        case EGhostRecord: aOut.Append(_L("record")); break;
        case EGhostLast: aOut.Append(_L("last run")); break;
        default: aOut.Append(_L("off")); break;
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
    if (iMenu) want = 0;                        // 0.8: the menu holds the race
    TmCarInput in;
    in.steer = (iKeys & KEY_LEFT ? -256 : 0) + (iKeys & KEY_RIGHT ? 256 : 0);
    in.throttle = (iKeys & KEY_UP) ? 1 : 0;
    in.brake = (iKeys & KEY_DOWN) ? 1 : 0;
    // 0.8: after the finish the keys do nothing: the car brakes to a stop (and no further: held, it would reverse)
    if (iCar->iFinished) { in.steer = 0; in.throttle = 0; in.brake = iCar->iS.speed > 256 ? 1 : 0; }
    // 0.6: the camera after each step (its trail and smoothing go by steps, not frames)
    for (TInt k = 0; k < want; k++)
        {
        iCar->Step(*iGround, in); iDriveSteps++; iRaceSteps++;
        iRun.Record(*iCar);
        iCam->Update(*iGround, iCar->iS, aCx, aCy, aCz, aYaw, aPitch);
        }
    if (want <= 0) iCam->Current(aCx, aCy, aCz, aYaw, aPitch);
    // 0.8: a checkpoint taken or the finish: its time, and against the ghost's (- ahead, + behind), shown 3 s
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
        }
    if (iCar->iFinished && !iSeenFinish)
        {
        iSeenFinish = ETrue;
        const TInt t = iCar->iFinishSteps;
        iSplitTime = t; iSplitCp = 0;
        iSplitHas = GhostFinish() > 0;
        if (iSplitHas) iSplitDiff = t - GhostFinish();
        iSplitUntil = 0x7fffffff;
        // a new record: kept and saved (again with its last points at the next start)
        if (!iGhostRecord.Finish() || t < iGhostRecord.Finish()) { iGhostRecord.Copy(iRun); SaveRecord(); iRunIsRecord = ETrue; }
        if (!iPlayerBest[iPlayer] || t < iPlayerBest[iPlayer]) iPlayerBest[iPlayer] = t;
        iResultDirty = ETrue;
        }
    // the ghost where it was at this step of its run
    iView->iGhostOn = 0;
    if (g && g->At((TInt) (((TInt64) iRaceSteps * gn) / gd).Low(), iView->iGhostX, iView->iGhostY, iView->iGhostZ, iView->iGhostYaw, iView->iGhostSlopeF, iView->iGhostSlopeR)) iView->iGhostOn = 1;
    // drawn from the highest of the wheels' ground, 0.15 m up: a crowned or coarse road no longer cuts into the body
    aCarX = iCar->iS.x >> 4; aCarY = (iCar->iS.y >> 4) + 3; aCarZ = iCar->iS.z >> 4; aCarYaw = iCar->iS.yaw;     // 0.6: the contact point + 0.19 m (the clipping was the depth test, not the height)
    iCar->iS.Slopes(iView->iCarSlopeF, iView->iCarSlopeR);
    }

void CTmView::Frame()
    {
    // 0.8: the menu: drawn again only when it changes (the scene stands still under it)
    if (iMenu && !iMenuDirty) { User::After(20000); return; }
    TTime t0;
    t0.UniversalTime();
    // where on the path: real time * speed; the path loops
    TInt ms = iPauseAt >= 0 ? iPauseAt : (TInt)(t0.MicroSecondsFrom(iStart).Int64().Low() / 1000);
    TInt pathMs = iTrack->iNPath * 100;
    TInt pos = pathMs > 0 ? (ms * iSpeed / 100) % pathMs : 0;
    TInt sample = pos / 100, frac = ((pos % 100) * 256) / 100;
    // looking round with the arrows (held)
    if (iKeys & KEY_LEFT) iYawOff -= 24;
    if (iKeys & KEY_RIGHT) iYawOff += 24;
    if (iKeys & KEY_UP) iPitchOff += 8;
    if (iKeys & KEY_DOWN) iPitchOff -= 8;
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
    TUint16* view = iBack + (SCR_W - VIEW_W) / 2;
    iView->Frame(view, SCR_W, *iTrack, cx, cy, cz, yaw, pitch, 200, carX, carY, carZ, carYaw, iDrawCar);
    TTime t1;
    t1.UniversalTime();
    TInt drawUs = t1.MicroSecondsFrom(t0).Int64().Low();
    iDrawSum += drawUs / 1000;
    if (iShowInfo)
        {
        // the left bar cleared (the digits would pile up), then: fps, draw ms, copy ms, polygons drawn,
        // submitted, the path's second, the far cut, 1 when the screen is written directly
        for (TInt y = 0; y < 136; y++) { TUint16* q = iBack + y * SCR_W; for (TInt x = 0; x < 64; x++) q[x] = 0; }
        const TUint16 white = 0xFFFF, yel = 0xFFE0;
        DrawNumber(iBack, SCR_W, 8, 8, iFps, white);
        DrawNumber(iBack, SCR_W, 8, 24, iDrawMs, yel);
        DrawNumber(iBack, SCR_W, 8, 40, iBlitMs, yel);
        DrawNumber(iBack, SCR_W, 8, 56, iView->iStats.drawn, white);
        DrawNumber(iBack, SCR_W, 8, 72, iView->iStats.polys, white);
        DrawNumber(iBack, SCR_W, 8, 88, pos / 1000, white);
        DrawNumber(iBack, SCR_W, 8, 104, iFarM, white);
        DrawNumber(iBack, SCR_W, 8, 120, iDsaOn ? 1 : 0, yel);
        // the benchmark's result: 1 sent, 2 saved only (the PC did not answer), 0 not run
        if (iSender) DrawNumber(iBack, SCR_W, 8, 136, iSender->iResult == 1 ? 1 : iSender->iResult < 0 ? 2 : 3, yel);
        }
    else if (iBench >= 0)
        {
        for (TInt y = 0; y < 24; y++) { TUint16* q = iBack + y * SCR_W; for (TInt x = 0; x < 64; x++) q[x] = 0; }
        DrawNumber(iBack, SCR_W, 8, 8, iBench + 1, 0xF800);     // which variant runs (red), of BENCH_VARIANTS
        DrawNumber(iBack, SCR_W, 32, 8, BENCH_VARIANTS, 0xF800);
        // the lap's progress as a bar
        TInt w = pathMs > 0 ? (pos * 56) / pathMs : 0;
        for (TInt y = 20; y < 23; y++) { TUint16* q = iBack + y * SCR_W + 4; for (TInt x = 0; x < w; x++) q[x] = 0xF800; }
        }
    if (iDriving && iBench < 0) DrawHud();
    if (iDriving && iBench < 0 && iSeenFinish && iResultCv.Ok())
        {
        if (iResultDirty) { DrawResult(); iResultDirty = EFalse; }
        iResultCv.CopyTo(iBack, SCR_W, RES_X, RES_Y, 0, RES_H);
        }
    if (iMenu)
        {
        DrawMenu();
        if (iMenuCv.Ok()) iMenuCv.CopyTo(iBack, SCR_W, MENU_X, MENU_Y, 0, MENU_H);
        iMenuDirty = EFalse;
        }
    if (!(iBench == 4)) Blit(iBack);
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

// 0.8: the result after the finish, over the view as the game shows it: the time in the medal's colour, the medal,
// against the ghost, and what Enter does (the car no longer answers the keys)
void CTmView::DrawResult()
    {
    CTextCanvas& m = iResultCv;
    const TUint16 bg = 0x1082, white = 0xFFFF, blue = 0x5D1F, red = 0xF904, grey = 0xC618;
    const TUint16 cNadeo = 0x2FE6, cGold = 0xFEA0, cSilver = 0xCE79, cBronze = 0xCC27;
    m.Fill(TRect(0, 0, RES_W, RES_H), bg);
    const TInt t = iCar->iFinishSteps, ms = t * 20;
    const TInt* md = iTrack->iMedal;
    TBuf<48> s;
    TUint16 col = white;
    if (md[0] && ms <= md[0]) { s.Append(_L("Nadeo medal!")); col = cNadeo; }
    else if (md[1] && ms <= md[1]) { s.Append(_L("gold medal!")); col = cGold; }
    else if (md[2] && ms <= md[2]) { s.Append(_L("silver medal!")); col = cSilver; }
    else if (md[3] && ms <= md[3]) { s.Append(_L("bronze medal")); col = cBronze; }
    else if (md[3]) { s.Append(_L("no medal (bronze ")); AppendTime(s, md[3] / 20); s.Append(')'); }
    m.Frame(TRect(0, 0, RES_W, RES_H), col);
    TInt y = 4;
    TBuf<16> tt; AppendTime(tt, t);
    m.Clock(iFontBig, tt, (RES_W - iFontBig->TextWidthInPixels(tt)) / 2, y, col);
    y += iFontBig->HeightInPixels() + 4;
    if (s.Length()) { m.Text(iFontText, s, (RES_W - iFontText->TextWidthInPixels(s)) / 2, y, col); y += iFontText->HeightInPixels() + 3; }
    s.Zero();
    // against the ghost (as it was at the finish: a new record replaces the record's ghost; the menu's ghost choice
    // works it out again)
    const TInt gf = iSplitHas ? 1 : 0, diff = iSplitDiff;
    if (gf > 0) { AppendDiff(s, diff); s.Append(_L(" against ")); GhostName(s, iGhostMode); }
    if (iRunIsRecord) { if (s.Length()) s.Append(_L(", ")); s.Append(_L("new record!")); }
    if (s.Length())
        {
        const CFont* f = iFontText->TextWidthInPixels(s) > RES_W - 8 ? iFontSmall : iFontText;
        m.Text(f, s, (RES_W - f->TextWidthInPixels(s)) / 2, y, gf > 0 ? (diff <= 0 ? blue : red) : white);
        y += f->HeightInPixels() + 3;
        }
    s.Zero();
    if (iPlayers > 1) s.AppendFormat(_L("press Enter: player %d"), (iPlayer + 1) % iPlayers + 1);
    else s.Append(_L("press Enter to restart"));
    m.Text(iFontText, s, (RES_W - iFontText->TextWidthInPixels(s)) / 2, RES_H - iFontText->HeightInPixels() - 5, grey);
    }

// 0.8: the race's panel on the right, a line at a time in the phone's fonts, a line drawn again only when it changes:
// the time (as the game: m:ss.cc), km/h, checkpoints, the last checkpoint's difference to the ghost, the ghost, the
// player (hot seat), the track, the depth fix, what Enter / Esc do, the players' bests
void CTmView::HudLine(TInt aLine, const TDesC& aText, TUint16 aCol, TInt& aY, TInt& aDirty0, TInt& aDirty1)
    {
    const CFont* f = aLine == 0 ? iFontBig : iFontText;
    if (aLine != 0 && f->TextWidthInPixels(aText) > HUD_W - 8) f = iFontSmall;
    const TInt h = f->HeightInPixels() + (aLine == 0 ? 6 : 3);
    if (!iHudValid || iHudCol[aLine] != aCol || iHudText[aLine] != aText.Left(40))
        {
        iHud.Fill(TRect(0, aY, HUD_W, aY + h), 0);
        if (aText.Length())
            {
            if (aLine == 0) iHud.Clock(f, aText, 6, aY + 2, aCol);
            else iHud.Text(f, aText, 6, aY + 1, aCol);
            }
        iHudText[aLine] = aText.Left(40); iHudCol[aLine] = aCol;
        if (aY < aDirty0) aDirty0 = aY;
        if (aY + h > aDirty1) aDirty1 = aY + h;
        }
    aY += h;
    }

void CTmView::DrawHud()
    {
    const TUint16 white = 0xFFFF, yel = 0xFFE0, green = 0x07E0, blue = 0x5D1F, red = 0xF904, grey = 0xA514, pale = 0xA5DF, orange = 0xFC00;
    const TInt kmh = (iCar->iS.speed < 0 ? -iCar->iS.speed : iCar->iS.speed) * 36 / 2560;
    if (!iFontBig)
        {
        // no fonts: the old digits
        const TInt bx = HUD_X + 8;
        for (TInt y = 0; y < 96; y++) { TUint16* q = iBack + y * SCR_W + HUD_X; for (TInt x = 0; x < HUD_W; x++) q[x] = 0; }
        DrawNumber(iBack, SCR_W, bx, 8, kmh, white);
        TInt steps = iCar->iFinished ? iCar->iFinishSteps : iCar->iSteps;
        DrawNumber(iBack, SCR_W, bx, 24, steps / 50, yel);
        DrawNumber(iBack, SCR_W, bx + 40, 24, (steps % 50) * 2, yel);
        DrawNumber(iBack, SCR_W, bx, 40, iCar->iCheckpoints, green);
        DrawNumber(iBack, SCR_W, bx + 24, 40, iCar->iTotalCps, green);
        DrawWords(iBack, SCR_W, bx, 72, iTrackName, white);
        DrawWords(iBack, SCR_W, bx, 82, iView->iR->iZNear ? _L("depth fix on (Z)") : _L("depth fix off (Z)"), iView->iR->iZNear ? green : orange);
        return;
        }
    TInt y = 4, d0 = SCR_H, d1 = 0;
    if (!iHudValid) { iHud.Fill(TRect(0, 0, HUD_W, SCR_H), 0); d0 = 0; d1 = SCR_H; }
    TBuf<40> t;
    // the time: the race's clock, green once finished
    AppendTime(t, iCar->iFinished ? iCar->iFinishSteps : iCar->iSteps);
    HudLine(0, t, iCar->iFinished ? green : white, y, d0, d1);
    t.Zero(); t.AppendFormat(_L("%d km/h"), kmh);
    HudLine(1, t, white, y, d0, d1);
    t.Zero();
    if (iCar->iFinished) t.Append(_L("finished!")); else t.AppendFormat(_L("checkpoint %d/%d"), iCar->iCheckpoints, iCar->iTotalCps);
    HudLine(2, t, iCar->iFinished ? green : yel, y, d0, d1);
    // the last checkpoint (or the finish): against the ghost (blue ahead, red behind), or its time without one
    t.Zero(); TUint16 col = white;
    if (iSplitUntil > iRaceSteps)
        {
        if (iSplitCp) t.AppendFormat(_L("cp %d: "), iSplitCp); else t.Append(_L("finish: "));
        if (iSplitHas) { AppendDiff(t, iSplitDiff); col = iSplitDiff <= 0 ? blue : red; } else AppendTime(t, iSplitTime);
        }
    HudLine(3, t, col, y, d0, d1);
    const TmGhost* g = Ghost();
    t.Zero(); t.Append(_L("ghost: ")); GhostName(t, iGhostMode);
    if (g && GhostFinish()) { t.Append(' '); AppendTime(t, GhostFinish()); }
    else if (iGhostMode != EGhostOff && !g) t.Append(_L(" (none yet)"));
    HudLine(4, t, pale, y, d0, d1);
    t.Zero(); if (iPlayers > 1) t.AppendFormat(_L("player %d of %d"), iPlayer + 1, iPlayers);
    HudLine(5, t, yel, y, d0, d1);
    t.Zero(); t.Append(iTrackName); t.AppendFormat(_L(" (%d/%d)"), iTrackIndex + 1, iTrackCount);
    HudLine(6, t, white, y, d0, d1);
    t.Zero(); t.Append(iView->iR->iZNear ? _L("depth fix on (toggle: Z)") : _L("depth fix off (toggle: Z)"));
    HudLine(7, t, iView->iR->iZNear ? green : orange, y, d0, d1);
    t.Zero();
    if (iCar->iFinished) t.Append(iPlayers > 1 ? _L("Enter: next player") : _L("Enter: race again"));
    else t.Append(_L("Esc: menu"));
    HudLine(8, t, grey, y, d0, d1);
    for (TInt k = 0; k < 4; k++)
        {
        t.Zero();
        if (iPlayers > 1 && k < iPlayers)
            {
            t.AppendFormat(_L("P%d  "), k + 1);
            if (iPlayerBest[k]) AppendTime(t, iPlayerBest[k]); else t.Append(_L("-"));
            }
        HudLine(9 + k, t, k == iPlayer ? yel : white, y, d0, d1);
        }
    if (!iHudValid && y < SCR_H) { d1 = SCR_H; }
    iHudValid = ETrue;
    if (d1 > d0) iHud.CopyTo(iBack, SCR_W, HUD_X, 0, d0, d1);
    }

// ---- the menu (Esc) ----
void CTmView::MenuOpen()
    {
    if (iMenu) return;
    iMenu = ETrue; iMenuDirty = ETrue; iMenuSel = EMContinue; iKeys = 0;
    iMenuAt.UniversalTime();
    }

void CTmView::MenuClose()
    {
    if (!iMenu) return;
    iMenu = EFalse; iKeys = 0;
    // the race's clock (and the flythrough's) on from where it stood
    TTime now; now.UniversalTime();
    const TTimeIntervalMicroSeconds held = now.MicroSecondsFrom(iMenuAt);
    iDriveStart += held; iStart += held;
    iHudValid = EFalse;
    }

// aDir: 1 up, 2 down, 3 left, 4 right; aSelect: Enter / the joystick's press / Space
void CTmView::MenuKeyL(TInt aDir, TBool aSelect)
    {
    iMenuDirty = ETrue;
    if (aDir == 1) { iMenuSel = (iMenuSel + EMItems - 1) % EMItems; return; }
    if (aDir == 2) { iMenuSel = (iMenuSel + 1) % EMItems; return; }
    if (!aSelect && aDir != 3 && aDir != 4) return;
    const TInt step = aDir == 3 ? -1 : 1;
    switch (iMenuSel)
        {
        case EMContinue: if (aSelect) MenuClose(); break;
        case EMRestart: if (aSelect) { MenuClose(); if (iDriving) DriveReset(); } break;
        case EMGhost:
            iGhostMode = (iGhostMode + step + EGhostModes) % EGhostModes; iSplitUntil = 0; iResultDirty = ETrue;
            if (iSeenFinish) { iSplitHas = GhostFinish() > 0; iSplitDiff = iCar->iFinishSteps - GhostFinish(); }
            break;
        case EMPlayers:
            iPlayers = (iPlayers - 1 + step + 4) % 4 + 1; iPlayer = 0;
            for (TInt k = 0; k < 4; k++) iPlayerBest[k] = 0;
            // the hot seat races the one before: the last run's ghost
            if (iPlayers > 1 && iGhostMode != EGhostOff) iGhostMode = EGhostLast;
            iResultDirty = ETrue;
            break;
        case EMDepth: iView->iR->iZNear = iView->iR->iZNear ? 0 : 40 * 16; break;
        case EMNext: case EMPrev:
            if (aSelect || aDir == 3 || aDir == 4)
                {
                const TInt i = iTrackIndex + (iMenuSel == EMPrev ? -1 : 1);
                MenuClose(); UnloadTrack(); iLoaded = LoadTrack(i); if (!iLoaded) DrawNow();
                }
            break;
        case EMQuit: if (aSelect) iEikonEnv->EikAppUi()->HandleCommandL(EEikCmdExit); break;
        default: break;
        }
    }

void CTmView::DrawMenu()
    {
    CTextCanvas& m = iMenuCv;
    if (!m.Ok() || !iFontText) return;
    const TUint16 bg = 0x1082, sel = 0x31A6, yel = 0xFFE0, white = 0xFFFF, grey = 0xA514, pale = 0xA5DF;
    m.Fill(TRect(0, 0, MENU_W, MENU_H), bg);
    m.Frame(TRect(0, 0, MENU_W, MENU_H), yel);
    const CFont* items = iFontText;
    const TInt need = iFontText->HeightInPixels() + 8 + EMItems * (iFontText->HeightInPixels() + 3) + iFontSmall->HeightInPixels() + 12;
    if (need > MENU_H) items = iFontSmall;
    TInt y = 5;
    TBuf<48> t;
    t.Append(_L("TrackMania 9300  ")); t.Append(KVersion);
    m.Text(iFontText, t, 10, y, yel);
    y += iFontText->HeightInPixels() + 6;
    const TInt h = items->HeightInPixels();
    for (TInt i = 0; i < EMItems; i++)
        {
        t.Zero();
        switch (i)
            {
            case EMContinue: t.Append(_L("continue (Esc)")); break;
            case EMRestart: t.Append(_L("restart (Backspace)")); break;
            case EMGhost: t.Append(_L("ghost: ")); GhostName(t, iGhostMode); break;
            case EMPlayers: t.AppendFormat(_L("hot seat players: %d"), iPlayers); break;
            case EMDepth: t.Append(iView->iR->iZNear ? _L("depth fix: on (Z)") : _L("depth fix: off (Z)")); break;
            case EMNext: t.Append(_L("next track (N)")); break;
            case EMPrev: t.Append(_L("previous track")); break;
            case EMQuit: t.Append(_L("quit")); break;
            }
        const TBool on = i == iMenuSel;
        const TBool lr = i == EMGhost || i == EMPlayers || i == EMDepth;
        if (on) m.Fill(TRect(4, y - 1, MENU_W - 4, y + h + 2), sel);
        if (on && lr) { TBuf<48> u; u.Append(_L("< ")); u.Append(t.Left(44)); u.Append(_L(" >")); t = u; }
        m.Text(items, t, on && lr ? 10 : 22, y, on ? yel : (lr ? pale : white));
        y += h + 3;
        }
    m.Text(iFontSmall, _L("up/down: choose  left/right: change  Enter: ok"), 10, MENU_H - iFontSmall->HeightInPixels() - 6, grey);
    }

void CTmView::Blit(const TUint16* aSrc)
    {
    TBool direct = iFb && StartDirect();
    if (!direct) StopDirect();
    if (direct)
        {
        // only what changes: the view in the middle and the counters' bar at the left (Maps 9300 copies the
        // dirty rectangle the same way); the side bars stay black from the first frame
        const TInt vx = (SCR_W - VIEW_W) / 2;
        TRect rv(vx, 0, vx + VIEW_W, VIEW_H), rb(0, 0, BAR_W, BAR_H);
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
        for (TInt y = 0; y < VIEW_H; y++)
            fm_memcpy(iFb + y * iFbStride + vx * 2, aSrc + y * SCR_W + vx, VIEW_W * 2);
        if (iShowInfo || iBench >= 0)
            for (TInt y = 0; y < BAR_H; y++) fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, BAR_W * 2);
        const TInt rx = (SCR_W + VIEW_W) / 2;
        if (iDriving && iBench < 0)
            for (TInt y = 0; y < SCR_H; y++) fm_memcpy(iFb + y * iFbStride + rx * 2, aSrc + y * SCR_W + rx, HUD_W * 2);
        if (iFirstBlit) { for (TInt y = 0; y < SCR_H; y++) fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, SCR_W * 2); iFirstBlit = EFalse; }
        TRegionFix<3> upd; upd.AddRect(rv); upd.AddRect(rb); upd.AddRect(TRect(rx, 0, rx + HUD_W, SCR_H));
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
        gc.DrawText(_L("Copy a .tmb to C:\\system\\apps\\tm9300\\ and restart"), TPoint(20, 90));
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
    if (aType == EEventKeyDown) iKeys |= bit;
    else if (aType == EEventKeyUp) iKeys &= ~bit;
    else if (aType == EEventKey)
        {
        TInt c = aKeyEvent.iCode;
        const TInt sc = aKeyEvent.iScanCode;
        if (!iLoaded) { if (c == EKeyEscape) iEikonEnv->EikAppUi()->HandleCommandL(EEikCmdExit); return EKeyWasConsumed; }
        const TBool joyPress = (c == EKeyDevice3 || sc == EStdKeyDevice3);
        // 0.8: Esc opens the menu (and closes it); in the menu the arrows / joystick choose, Enter / its press select
        if (iMenu)
            {
            if (c == EKeyEscape) { MenuClose(); return EKeyWasConsumed; }
            TInt dir = 0;
            if (c == EKeyUpArrow || sc == EStdKeyUpArrow || sc == EStdKeyDevice8) dir = 1;
            else if (c == EKeyDownArrow || sc == EStdKeyDownArrow || sc == EStdKeyDevice9) dir = 2;
            else if (c == EKeyLeftArrow || sc == EStdKeyLeftArrow || sc == EStdKeyDevice6) dir = 3;
            else if (c == EKeyRightArrow || sc == EStdKeyRightArrow || sc == EStdKeyDevice7) dir = 4;
            const TBool select = c == EKeyEnter || joyPress || c == ' ';
            if (dir || select) MenuKeyL(dir, select);
            else if (c == 'z' || c == 'Z') { iView->iR->iZNear = iView->iR->iZNear ? 0 : 40 * 16; iMenuDirty = ETrue; }
            return EKeyWasConsumed;
            }
        if (c == EKeyEscape) { MenuOpen(); return EKeyWasConsumed; }
        if (iDriving)
            {
            // the joystick's press does nothing while driving (0.8: it respawned by accident when steering)
            if (joyPress) return EKeyWasConsumed;
            // Enter: back to the last checkpoint taken (the start when none), the clock running; after the finish
            // the next run (the hot seat's next player); Backspace: the start
            if (c == EKeyEnter) { if (iCar->iFinished) NextRun(); else { iCar->Respawn(); iCam->Reset(iCar->iS); } return EKeyWasConsumed; }
            if (c == EKeyBackspace) { DriveReset(); return EKeyWasConsumed; }
            }
        if (c == 'n' || c == 'N') { TInt i = iTrackIndex + 1; UnloadTrack(); iLoaded = LoadTrack(i); if (!iLoaded) DrawNow(); return EKeyWasConsumed; }
        if ((c == 'v' || c == 'V') && iGround) { iDriving = !iDriving; if (iDriving) DriveReset(); else { iStart.UniversalTime(); iPauseAt = -1; } }
        else if (c == ' ')
            {
            if (iPauseAt >= 0) { TTime now; now.UniversalTime(); iStart = now - TTimeIntervalMicroSeconds(iPauseAt * 1000); iPauseAt = -1; }
            else { TTime now; now.UniversalTime(); iPauseAt = (TInt)(now.MicroSecondsFrom(iStart).Int64().Low() / 1000); }
            }
        else if (c == 'f' || c == 'F') iShowInfo = !iShowInfo;
        else if (c == 'm' || c == 'M') iView->iMeshMode = !iView->iMeshMode;
        else if (c == 'z' || c == 'Z') iView->iR->iZNear = iView->iR->iZNear ? 0 : 40 * 16;     // 0.5: the depth test on/off (A/B)
        else if (c == '+' || c == '=') { iFarM += 30; if (iFarM > 480) iFarM = 480; iView->iFarCut = iFarM * 16; }
        else if (c == '-') { iFarM -= 30; if (iFarM < 60) iFarM = 60; iView->iFarCut = iFarM * 16; }
        else if (c == 'r' || c == 'R') { if (iDriving) DriveReset(); else { iStart.UniversalTime(); iPauseAt = -1; iYawOff = 0; iPitchOff = 0; } }
        else if (c == 'b' || c == 'B') { if (iBench < 0) { iDriving = EFalse; BenchStart(); } }
        else if (c >= '1' && c <= '9') iSpeed = (c - '0') * 25;
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
