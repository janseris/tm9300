// tmcore.h - the track viewer's core for the Nokia 9300 (and the PC / emulator harnesses): a packed track
// (pack.py's .tmb) drawn with Maps 9300's renderer (R3D). Portable C++ for GCC 2.9: no floats, no libc,
// no writable static data; the platform gives it the track's bytes and a frame buffer.
#ifndef TMCORE_H
#define TMCORE_H

#include "r3d.h"

struct TmMesh
    {
    int nv, nf;
    const short* v;             // nv * 3, 1/16 m, local to the block's origin corner
    const unsigned short* idx;  // nf * 3
    const unsigned short* col;  // nf: RGB565, the light baked in
    short bb[6];
    };

struct TmInst
    {
    int mesh, flags;            // flags: 1 a base plate tile
    int far;                    // the mesh drawn beyond the track's far distance (-1: the same)
    int x, y, z;                // the origin, world, 1/16 m (the mesh's corners are from it, already turned)
    int bb[6];                  // world box
    };

struct TmPath { int x, y, z; short yaw, pitch; };    // the car every 100 ms

// a collision mesh (Nadeo's collision solid, simplified): faces with a unit normal (* 8192) and a surface class
struct TmColl
    {
    int nv, nf;
    const short* v;             // nv * 3, 1/16 m from the instance origin
    const unsigned short* idx;  // nf * 3
    const short* nrm;           // nf * 3, * 8192, pointing up (ny >= 0)
    const unsigned char* surf;  // nf: 0 road, 1 dirt, 2 grass, 3 rock, 4 water, 6 turbo, 7 ice, 8 snow
    short bb[6];
    };
struct TmCInst
    {
    int coll, kind;             // kind: 0 plain, 1 start, 2 checkpoint, 3 finish, 4 multilap
    int x, y, z;
    int bb[6];                  // world, 1/16 m
    };

class TmTrack
    {
public:
    TmTrack();
    ~TmTrack();
    // aData stays owned by the caller and must outlive the track (the meshes point into it)
    bool Load(const unsigned char* aData, int aSize);
    int iNMesh, iNInst, iNPath;
    TmMesh* iMesh;
    TmInst* iInst;
    const TmPath* iPath;
    int iSky, iGround, iFog, iFogStart, iFogEnd;
    int iCar;                   // the car's mesh (-1: none)
    int iCarClass;              // 0 Rally, 1 Snow, 2 Desert, 3 Bay, 4 Island (the physics tables)
    int iTris;                  // placed triangles in all
    // TMB4: the collision (none in a TMB3)
    int iNColl, iNCInst;
    TmColl* iColl;
    TmCInst* iCInst;
    int iStartX, iStartY, iStartZ, iStartYaw;   // 1/16 m
    int iFarDist;               // 1/16 m: instances further than this draw their far form (TMB5; 0: none)
    // TMB6: the potentially visible set: per 32 m cell of the map a bit per instance (0: not drawn from there)
    int iPvsX0, iPvsZ0, iPvsW, iPvsH, iPvsWords;    // cells (1/16 m >> 9), words per cell
    const unsigned char* iPvs;
    const unsigned int* PvsCell(int aCamX, int aCamZ) const;   // the camera's cell's bits (0: none)
    int iMedal[4];              // 0.8: ms: author ("Nadeo"), gold, silver, bronze (0: not known; the file's "MEDL" trailer)
    };

struct TmFrameStats { int insts, visible, polys, drawn, culled, hidden, pixels, full; };

class TmView
    {
public:
    TmView();
    ~TmView();
    bool Init(int aW, int aH, int aMaxPolys);
    // the camera (1/16 m, yaw 0..4095, pitch -1024..1024); aFocal pixels; the car at (aCarX, aCarY, aCarZ) turned aCarYaw (iCar >= 0)
    void Frame(R3Pix* aFrame, int aStride, const TmTrack& aT, int aCamX, int aCamY, int aCamZ, int aYaw, int aPitch, int aFocal,
        int aCarX = 0, int aCarY = 0, int aCarZ = 0, int aCarYaw = 0, int aDrawCar = 0);
    // the car's tilt for the next Frame: the ground's slope along its heading and to its right (* 4096; 0 level)
    int iCarSlopeF, iCarSlopeR;
    // 0.18: on a loop's road the car is drawn turned in 3D: its up and its forward (* 8192) when iCar3D
    int iCar3D, iCarUp[3], iCarFwd[3];
    // 0.8: ghost cars for the next Frame (0.14: up to four): each one's place (1/16 m), heading and tilt; drawn in the
    // car's colours paled, in its place among the instances by distance, not when within 3 m of the car
    enum { KMaxGhosts = 4 };
    struct TGhostCar { int x, y, z, yaw, slopeF, slopeR; };
    TGhostCar iGhosts[KMaxGhosts];
    int iNGhosts;
    int iIdMode;                // 1: every face in the colour (instance index + 1), no fog, no car (the PVS tool)
    int iUsePvs;                // 1: instances the track's PVS rules out from the camera's cell are skipped
    // the car's place and heading at a path sample (interpolated)
    void CarAt(const TmTrack& aT, int aSample, int aFrac256, int& aX, int& aY, int& aZ, int& aYaw) const;
    // a chase camera 7 m behind and 2.5 m above the car's path sample (interpolated: aT100 in 1/100 of a sample)
    void ChaseCamera(const TmTrack& aT, int aSample, int aFrac256, int& aX, int& aY, int& aZ, int& aYaw, int& aPitch) const;
    TmFrameStats iStats;
    int iFarCut;                // 1/16 m: instances beyond are not drawn (0: all)
    int iFlipWinding;           // 1: faces submitted clockwise (if the kit's winding turns out inverted)
    int iMeshMode;              // 1: MeshBegin / MeshTris (the corners once a mesh, in assembly), 0: Poly a triangle
    int iSkipEnd;               // 1: the polygons submitted but not drawn (the benchmark: the geometry's share)
    R3D* iR;
private:
    void Reserve(int aNv, int aNf);
    void DrawCar(const TmMesh& aM, short* aXyz, const R3Pix* aCol, int aX, int aY, int aZ, int aYaw, int aSlopeF, int aSlopeR);
    short* iGhostXyz;           // the ghost's corners turned
    unsigned short* iGhostCol;  // its colours
    int iGhostN;                // corners / faces those hold
    int iW, iH, iMaxPolys;
    int* iOrder;                // instance indices sorted by distance (scratch)
    int* iKey;
    unsigned char* iFar;        // per instance: 1 when its far form is drawn this frame
    R3Vtx* iVtx;                // the current mesh's corners in world space (Poly mode)
    short* iXyz;                // the car's corners turned (mesh mode)
    unsigned short* iUv0;       // zero texture coordinates (mesh mode needs 6 a face)
    unsigned short* iFlipIdx;   // the faces' corners swapped (iFlipWinding in mesh mode)
    unsigned short* iIdCol;     // a colour per face for the ID mode
    int iVtxCap, iUvCap;
    };

#endif
