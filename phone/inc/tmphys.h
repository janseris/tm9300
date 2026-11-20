// tmphys.h - the car on the track: an arcade model fitted to TrackMania's replays (integer, 50 steps a second),
// the ground from the packed track's collision meshes (TMB4). Portable C++ for GCC 2.9: no floats, no libc,
// no writable static data.
//
// Units: positions and speeds in 1/256 m (and 1/256 m/s); yaw 0..4095 (0 = +z, clockwise = towards +x, as R3D);
// a step is 20 ms. Nadeo's replays gave the figures: a Rally car launches at ~30 m/s^2, pulls ~6 m/s^2 at 50 m/s
// and tops out near 80 m/s; it turns at up to ~1.5 rad/s while sliding ~10 degrees; it falls at ~27 m/s^2.
#ifndef TMPHYS_H
#define TMPHYS_H

#include "tmcore.h"

// the yaw (0..4095, R3D's: 0 = +z, clockwise = towards +x) of a direction
int TmYawTo(int aDx, int aDz);

struct TmGroundHit
    {
    int y;                      // 1/256 m
    int nx, ny, nz;             // * 8192
    int surf;                   // the surface class (TmColl::surf)
    int cinst, face;            // the collision instance and face hit (-1: none)
    };

// the collision instances on a grid of cells, and a probe for the ground under a point
class TmGround
    {
public:
    TmGround();
    ~TmGround();
    bool Init(const TmTrack& aT);
    // the highest surface at (aX, aZ) no higher than aYMax (all 1/256 m); false: nothing there
    bool Probe(int aX, int aZ, int aYMax, TmGroundHit& aHit) const;
    // the same with a hint: the face hit last time is tried first, and taken when the point is still over it
    // and its height is within 0.5 m of aLastY (the car rolls along a face for many steps)
    bool Probe(int aX, int aZ, int aYMax, int aLastY, TmGroundHit& aHit) const;
    // one face: true when the point is over it and its height there is no more than aYMax (aHit filled)
    bool FaceAt(int aCInst, int aFace, int aX, int aZ, int aYMax, TmGroundHit& aHit) const;
    // a wall between (aX0, aY, aZ0) and (aX1, aY, aZ1) (1/256 m): the nearest wall face the segment crosses;
    // aHit gets the crossing point (x, z in aHit.y? no: aHit.nx/ny/nz the wall's normal, aHit.y the fraction * 4096)
    bool WallHit(int aX0, int aZ0, int aX1, int aZ1, int aY, TmGroundHit& aHit, int aDY = 0) const;     // aDY: the move's height change
    // the collision instances of a kind (checkpoint, finish) whose box holds the point (1/256 m): the first one's index, or -1
    bool Covers(int aX, int aZ) const;
    int KindAt(int aKind, int aX, int aY, int aZ) const;
    int iProbes, iTests;        // statistics: probes, triangles tested
    const TmTrack& Track() const { return *iT; }
private:
    const TmTrack* iT;
    int iCellX0, iCellZ0, iCellsX, iCellsZ;     // the grid: 32 m cells (1/16 m: 512), from the instances' boxes
    int* iFirst;                // per cell: the first index into iList (iCellsX * iCellsZ + 1)
    int* iList;                 // instance indices, grouped by cell
    // each collision mesh's faces bucketed on an 8 x 8 grid over its box (a probe scans one bucket)
    enum { KGrid = 8 };
    struct TMeshGrid { int x0, z0, dx, dz; int first[KGrid * KGrid + 1]; unsigned short* list; };
    TMeshGrid* iGrid;
    unsigned short* iGridList;
    };

struct TmCarInput { int steer; int throttle; int brake; };    // steer -256..256 (left negative), throttle / brake 0 or 1

struct TmCarState
    {
    int x, y, z;                // 1/256 m: the car's contact point (under its middle)
    int vx, vy, vz;             // 1/256 m/s
    int yaw;                    // 0..4095 (* 16 inside: iYaw16)
    int yaw16;
    int onGround, surf;
    int nx, ny, nz;             // the ground's normal under the car (* 8192; (0, 8192, 0) in the air)
    int speed;                  // 1/256 m/s along the heading (signed)
    // the ground's slope along the heading and to the right (* 4096), for the car's tilt when drawn
    void Slopes(int& aAlong, int& aRight) const;
    int steerPos;               // the steering's position -256..256 (ramped from the keys)
    };

class TmCar
    {
public:
    TmCar();
    void Reset(const TmTrack& aT);                  // at the start, standing
    void Respawn();                                 // standing at the last checkpoint taken (the start when none), the race going on
    void Step(const TmGround& aG, const TmCarInput& aIn);
    TmCarState iS;
    int iClass;                 // the car's class (TmTrack::iCarClass)
    // the race: checkpoints taken, finished, the time in steps
    int iCheckpoints, iCheckMask, iFinished, iSteps, iFinishSteps;
    int iLastKindInst, iTotalCps;
    int iWallHits;              // walls hit so far (statistics)
    int iTopY;                  // the highest of the wheels' ground this step (1/256 m): where to draw the body so a crowned road does not cut it
    int iCpX, iCpY, iCpZ, iCpYaw;   // the respawn pose: the last checkpoint taken (the start)
    enum { KMaxCpBoxes = 96 };
    int iCpInst[KMaxCpBoxes], iCpGroup[KMaxCpBoxes], iNCp;     // 0.6: the checkpoint boxes and their checkpoint
    // tuning (set before Reset; 1/256 m/s^2 etc. are computed inside)
    int iGravity;               // m/s^2 * 256 (0.16: 40 * 256, the replays' vertical acceleration in the air in all environments)
    int iSusB, iSusV;           // 0.17: the body on its springs: its height over its rest (1/256 m), its speed (1/256 m/s)
    int Load() const;           // the wheels' load from the body (256: standing)
    int FullTurn(int aSpeed) const;     // the full-lock turning rate (rad/s * 208.6) at a speed; Grip: the sideways grip (m/s^2 * 5.12)
    int Grip() const;
private:
    void Race(const TmGround& aG);
    int iBumpY;                 // the ground's last y (when the probe finds nothing)
    TmGroundHit iLast[5];       // the faces under the wheels and the middle last time (the probes' hints)
    int iAirSteps;              // steps running the ground fell away under the car (the car leaves it at the second)
    int iClimbVy;               // 0.16: the upward speed the ground gave the last steps (1/256 m/s)
    int iClimbRaw;              //   the last step's alone
    int iShadow, iShY, iShVy;    // 0.18: the throw's shadow over a ramp's lip: on, its height (1/256 m), its upward speed
    int iTurbo;                 // 0.16: the turbo's steps left
    void Suspension(int aVy0);
    };

// the camera that follows a driven car: behind it along its heading, smoothed, kept above the ground
class TmFollowCam
    {
public:
    TmFollowCam();
    void Reset(const TmCarState& aS);
    // after EACH step (0.6: the trail and the smoothing are per step): the camera (1/16 m, yaw, pitch) for TmView::Frame
    void Update(const TmGround& aG, const TmCarState& aS, int& aX, int& aY, int& aZ, int& aYaw, int& aPitch);
    // the camera as the last Update left it (a frame with no step)
    void Current(int& aX, int& aY, int& aZ, int& aYaw, int& aPitch) const { aX = iX >> 4; aY = iY >> 4; aZ = iZ >> 4; aYaw = iViewYaw; aPitch = iViewPitch; }
    // 0.8: the placing (1/256 m): back along the trail, up over it, the look's point over the car and ahead of it
    int iBack, iUp, iLookUp, iLookAhead;
private:
    void Push(int aX, int aY, int aZ);
    void Target(const TmCarState& aS, int& aX, int& aY, int& aZ) const;
    int iX, iY, iZ;             // 1/256 m
    int iYaw16;
    int iViewYaw, iViewPitch;   // the look, smoothed
    TmGroundHit iLast;
    enum { KTrail = 64 };       // the car's trail: a point every 0.4 m (25 m)
    int iTx[KTrail], iTy[KTrail], iTz[KTrail];
    int iTn, iTh;               // points held, the newest's index
    };

// 0.8: a ghost: a run as the car's place every 5 steps (100 ms), the steps it took each checkpoint and the finish.
// Made from a driven run (Record each step) or from the track's replay (FromPath); played back by the race's step.
struct TmGhostPt { int x, y, z; short yaw; signed char sf, sr; };     // 1/16 m, 0..4095, the tilt (* 4096 >> 5)
class TmGhost
    {
public:
    enum { KMaxPts = 6000, KMaxCps = 32 };          // 10 minutes
    TmGhost();
    ~TmGhost();
    void Clear();                                   // empty (no run)
    bool Begin();                                   // a new run to record (false: no memory)
    void Record(const TmCar& aCar);                 // after each Step: a point every 5 steps, the checkpoints, the finish
    bool FromPath(const TmTrack& aT, const TmGround& aG, const TmCar& aCarForCps);    // the replay (its checkpoints found on the way)
    bool Copy(const TmGhost& aFrom);
    // the place at a step of the race (false: no points); past the end the last point
    bool At(int aStep, int& aX, int& aY, int& aZ, int& aYaw, int& aSlopeF, int& aSlopeR) const;
    int Finish() const { return iFinish; }          // steps (0: not finished)
    int CpSteps(int aK) const { return aK >= 0 && aK < iNCp ? iCp[aK] : 0; }    // the step it took its aK-th checkpoint (0: none)
    // the file form (a flat buffer): Size bytes; Load false when it is not one
    int Size() const;
    void Write(unsigned char* aBuf) const;
    bool Load(const unsigned char* aBuf, int aLen);
    int iN;
private:
    TmGhostPt* iPt;
    int iFinish, iNCp, iCp[KMaxCps];
    int iSteps, iLastCps;
    };

#endif
