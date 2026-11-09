// tm9300_app.cpp - Series 80 (Nokia 9300) application shell for the TrackMania 9300 track viewer (phase 3:
// a flythrough of a packed track along its replay path, to measure the frame rate on the phone).
// After Hrimfell's shell: direct screen access when the phone allows it, a CIdle loop, keys by scan code.
#include <eikapp.h>
#include <eikdoc.h>
#include <eikappui.h>
#include <eikenv.h>
#include <coecntrl.h>
#include <fbs.h>
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
    iLog.AppendFormat(_L8("tm9300 bench v0.7: %S, %d meshes %d instances %d triangles, display %d bpp direct %d, RAM free %d KB of %d KB\r\n"),
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
    iCar->Reset(*iTrack);
    iCam->Reset(iCar->iS);
    iDriveStart.UniversalTime();
    iDriveSteps = 0;
    }

void CTmView::DriveFrame(TInt& aCx, TInt& aCy, TInt& aCz, TInt& aYaw, TInt& aPitch, TInt& aCarX, TInt& aCarY, TInt& aCarZ, TInt& aCarYaw)
    {
    // the steps the clock asks for (20 ms each), at most 4 a frame (a slow frame does not snowball)
    TTime now; now.UniversalTime();
    TInt ms = (TInt)(now.MicroSecondsFrom(iDriveStart).Int64().Low() / 1000);
    TInt want = ms / 20 - iDriveSteps;
    if (want > 4) { iDriveSteps += want - 4; want = 4; }
    TmCarInput in;
    in.steer = (iKeys & KEY_LEFT ? -256 : 0) + (iKeys & KEY_RIGHT ? 256 : 0);
    in.throttle = (iKeys & KEY_UP) ? 1 : 0;
    in.brake = (iKeys & KEY_DOWN) ? 1 : 0;
    // 0.6: the camera after each step (its trail and smoothing go by steps, not frames)
    for (TInt k = 0; k < want; k++) { iCar->Step(*iGround, in); iDriveSteps++; iCam->Update(*iGround, iCar->iS, aCx, aCy, aCz, aYaw, aPitch); }
    if (want <= 0) iCam->Current(aCx, aCy, aCz, aYaw, aPitch);
    // drawn from the highest of the wheels' ground, 0.15 m up: a crowned or coarse road no longer cuts into the body
    aCarX = iCar->iS.x >> 4; aCarY = (iCar->iS.y >> 4) + 3; aCarZ = iCar->iS.z >> 4; aCarYaw = iCar->iS.yaw;     // 0.6: the contact point + 0.19 m (the clipping was the depth test, not the height)
    iCar->iS.Slopes(iView->iCarSlopeF, iView->iCarSlopeR);
    }

void CTmView::Frame()
    {
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
    if (iDriving && iBench < 0)
        {
        // the race on the right: km/h, the time (s), checkpoints taken / in all; when finished the time stays (and
        // a 1 below it); the bar cleared first
        const TInt bx = (SCR_W + VIEW_W) / 2 + 8;
        for (TInt y = 0; y < 96; y++) { TUint16* q = iBack + y * SCR_W + bx - 8; for (TInt x = 0; x < 80; x++) q[x] = 0; }
        const TUint16 white = 0xFFFF, yel = 0xFFE0, green = 0x07E0;
        TInt kmh = (iCar->iS.speed < 0 ? -iCar->iS.speed : iCar->iS.speed) * 36 / 2560;
        DrawNumber(iBack, SCR_W, bx, 8, kmh, white);
        TInt steps = iCar->iFinished ? iCar->iFinishSteps : iCar->iSteps;
        DrawNumber(iBack, SCR_W, bx, 24, steps / 50, yel);
        DrawNumber(iBack, SCR_W, bx + 40, 24, (steps % 50) * 2, yel);
        DrawNumber(iBack, SCR_W, bx, 40, iCar->iCheckpoints, green);
        DrawNumber(iBack, SCR_W, bx + 24, 40, iCar->iTotalCps, green);
        if (iCar->iFinished) DrawNumber(iBack, SCR_W, bx, 56, 1, green);
        // the track's number of the ones installed (N: the next), in white under the race
        DrawNumber(iBack, SCR_W, bx + 24, 56, iTrackIndex + 1, white); DrawNumber(iBack, SCR_W, bx + 48, 56, iTrackCount, white);
        DrawWords(iBack, SCR_W, bx, 72, iTrackName, white);
        // the depth test's state, in words, under the track's name: what it is, and its key with on (green) / off (red)
        DrawWords(iBack, SCR_W, bx, 82, _L("depth test"), white);
        if (iView->iR->iZNear) DrawWords(iBack, SCR_W, bx, 89, _L("z key: on"), green);
        else DrawWords(iBack, SCR_W, bx, 89, _L("z key: off"), 0xF800);
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
            for (TInt y = 0; y < 96; y++) fm_memcpy(iFb + y * iFbStride + rx * 2, aSrc + y * SCR_W + rx, 80 * 2);
        if (iFirstBlit) { for (TInt y = 0; y < SCR_H; y++) fm_memcpy(iFb + y * iFbStride, aSrc + y * SCR_W, SCR_W * 2); iFirstBlit = EFalse; }
        TRegionFix<3> upd; upd.AddRect(rv); upd.AddRect(rb); upd.AddRect(TRect(rx, 0, rx + 80, 96));
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
        if (c == EKeyEscape) { iEikonEnv->EikAppUi()->HandleCommandL(EEikCmdExit); return EKeyWasConsumed; }
        if (!iLoaded) return EKeyWasConsumed;
        // Enter: back to the last checkpoint taken (the start when none), the clock running; Backspace: the start
        if ((c == EKeyEnter || c == EKeyDevice3) && iDriving) { iCar->Respawn(); iCam->Reset(iCar->iS); return EKeyWasConsumed; }
        if (c == EKeyBackspace && iDriving) { DriveReset(); return EKeyWasConsumed; }
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
