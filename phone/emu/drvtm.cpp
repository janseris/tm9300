// the emulator driver for the track viewer (armemu.py): no Symbian, a bump heap
#include "tmcore.h"
static char* gHeap = (char*) 0x400000;
void* operator new(unsigned int n) { void* p = gHeap; gHeap += (n + 7) & ~7; return p; }
void* operator new[](unsigned int n) { return operator new(n); }
void operator delete(void*) {}
void operator delete[](void*) {}
extern "C" void __pure_virtual() {}
extern "C" void* memset(void* d, int c, unsigned int n) { char* p = (char*) d; while (n--) *p++ = (char) c; return d; }
extern "C" void* memcpy(void* d, const void* s, unsigned int n) { char* p = (char*) d; const char* q = (const char*) s; while (n--) *p++ = *q++; return d; }
static TmTrack* gT; static TmView* gV; static int gW, gH, gFocal;
extern "C" int emu_init() { return 0; }
extern "C" int emu_load(const unsigned char* aData, int aSize, int aW, int aH)
    {
    gT = new TmTrack; gV = new TmView; gW = aW; gH = aH; gFocal = aW * 5 / 8;
    if (!gT->Load(aData, aSize)) return -1;
    gV->Init(aW, aH, 2500); gV->iFarCut = 180 * 16;
    return gT->iNPath;
    }
extern "C" int emu_setup(int aMaxPolys, int aFarCutM, int aFocal) { gV->iR->SetMax(aMaxPolys); gV->iFarCut = aFarCutM * 16; gFocal = aFocal; return 0; }
extern "C" int emu_znear(int aMetres) { gV->iR->iZNear = (aMetres % 1000) * 16; return gV->iR->iZNear; }
// draws the frame at path sample aSample into aFrame; aStats gets 8 ints (TmFrameStats)
static int gGhost = 0;   // 0.8: a ghost aGhost path samples ahead of the car (0: none)
extern "C" int emu_ghost(int aAhead) { gGhost = aAhead; return 0; }
extern "C" int emu_frame(R3Pix* aFrame, int aSample, int* aStats)
    {
    int cx, cy, cz, yaw, pitch;
    gV->ChaseCamera(*gT, aSample, 0, cx, cy, cz, yaw, pitch);
    int carx, cary, carz, caryaw;
    gV->CarAt(*gT, aSample, 0, carx, cary, carz, caryaw);
    gV->iNGhosts = 0;
    if (gGhost) { int gs = aSample + gGhost; if (gs >= gT->iNPath) gs = gT->iNPath - 1; TmView::TGhostCar& c = gV->iGhosts[0]; gV->CarAt(*gT, gs, 0, c.x, c.y, c.z, c.yaw); c.slopeF = c.slopeR = 0; gV->iNGhosts = 1; }
    gV->Frame(aFrame, gW, *gT, cx, cy, cz, yaw, pitch, gFocal, carx, cary, carz, caryaw, 1);
    const int* s = (const int*) &gV->iStats; for (int i = 0; i < 8; i++) aStats[i] = s[i];
    return gV->iStats.drawn;
    }

// the physics: the car reset, then aSteps steps with a scripted input (throttle on, steering by a pattern), the
// state written to aOut (x y z vx vy vz yaw onGround surf speed) and the follow camera's frame drawn
#include "tmphys.h"
static TmGround* gG; static TmCar* gCar; static TmFollowCam* gCam;
extern "C" int emu_drive_reset() { if (!gG) { gG = new TmGround; gCar = new TmCar; gCam = new TmFollowCam; if (!gG->Init(*gT)) return -1; } gCar->Reset(*gT); gCam->Reset(gCar->iS); return gT->iNCInst; }
extern "C" int emu_drive(int aSteps, int aStep0, int* aOut, R3Pix* aFrame)
    {
    for (int k = 0; k < aSteps; k++)
        {
        int t = aStep0 + k;
        TmCarInput in; in.throttle = 1; in.brake = (t % 200) > 180; in.steer = ((t / 50) % 4 == 1) ? -200 : ((t / 50) % 4 == 3) ? 180 : 0;
        gCar->Step(*gG, in);
        }
    const TmCarState& s = gCar->iS;
    aOut[0] = s.x; aOut[1] = s.y; aOut[2] = s.z; aOut[3] = s.vx; aOut[4] = s.vy; aOut[5] = s.vz; aOut[6] = s.yaw; aOut[7] = s.onGround; aOut[8] = s.surf; aOut[9] = s.speed;
    aOut[10] = gG->iProbes; aOut[11] = gG->iTests;
    if (aFrame)
        {
        int cx, cy, cz, yaw, pitch;
        gCam->Update(*gG, s, cx, cy, cz, yaw, pitch);
        s.Slopes(gV->iCarSlopeF, gV->iCarSlopeR);
        gV->Frame(aFrame, gW, *gT, cx, cy, cz, yaw, pitch, gFocal, s.x >> 4, s.y >> 4, s.z >> 4, s.yaw, 1);
        }
    return gCar->iSteps;
    }
