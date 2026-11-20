// Building a track from a .Challenge.Gbx on the phone (0.19): an environment's block kit (blockkit.py: every
// piece of its blocks, simplified on the PC) placed as the Gbx's block list says, into a .tmb (TMB5) the app loads.
// The steps mirror tools/pc/assemble.py and pack.py.
#ifndef TMKIT_H
#define TMKIT_H

class TmGbxTrack;

// where the built track goes, in order (a file on the phone); 0 = written
class MTmOut
    {
public:
    virtual int Write(const void* aData, int aLen) = 0;
    };

struct TmKMesh;
struct TmKColl;
struct TmKInst;

class TmKit
    {
public:
    TmKit();
    ~TmKit();
    // the kit file (TMK2) in memory: kept (not copied) while the kit is open; 0 = ok
    int Open(const unsigned char* aFile, int aSize);
    void Close();
    // the environment (the car class: 0 Rally 1 Snow 2 Desert 3 Bay 4 Island), -1 not open
    int Env() const { return iDir ? iEnv : -1; }
    // the track built into aOut; 0 = ok, else negative (where it failed). aPath: a replay's samples (x, y, z 1/16 m,
    // yaw, pitch; 5 ints each, as pack.py's), the start its first; none: the start from the start block
    int Build(const TmGbxTrack& aT, MTmOut& aOut, const int* aPath = 0, int aNPath = 0);
public:
    // what the last Build did
    int iNInst, iNMesh, iNColl, iNCInst, iTris, iUnknown, iDropped, iChunks, iUnpacked;
    int iStart[4];
private:
    int Rd16(const unsigned char* p) const { return p[0] | (p[1] << 8); }
    int Rs16(const unsigned char* p) const { return (short)(p[0] | (p[1] << 8)); }
    int Rd32(const unsigned char* p) const { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
    int FindBlock(const char* aName) const;
    int Unpack(int aPart, unsigned char*& aOut);
    void Free();
private:
    const unsigned char* iFile; int iFileSize; int iProps; int iNParts;
    unsigned char* iDir; int iDirSize;
    int iEnv, iBlk, iBlkH, iBaseCell, iDefZone, iSky, iGround, iFog, iFogStart, iFogEnd, iFarDist, iCar;
    int iNB, iNS, iNP, iNM, iNC, iNPl, iNPt, iNCov, iNCell, iNRow, iNUnit, iNameBytes, iNChunk;
    const unsigned char *iBlocks, *iRows, *iUnits, *iSolids, *iParts, *iCovers, *iCells, *iPlates, *iPTiles, *iMeshTab, *iCollTab, *iChunkTab, *iNames;
    int* iMeshV0; int* iMeshF0; int* iCollV0; int* iCollF0;    // each mesh's first corner / face in its chunk
    int* iChunkNV; int* iChunkNF; int* iChunkCV; int* iChunkCF;
    };

#endif
