// Reading a TMF .Challenge.Gbx on the phone: the header (environment, name) and the block list.
// Own code; the LZO1X decompressor follows the published algorithm.
#ifndef TMGBX_H
#define TMGBX_H

// LZO1X: aSrc (aSrcLen bytes) into aDst (room for aDstLen); the bytes written, or -1 on bad data
int TmLzoDecompress(const unsigned char* aSrc, int aSrcLen, unsigned char* aDst, int aDstLen);

// the header only (quick, for the track list; 0.19: the map's uid and author too): the environment ("Rally", "Alpine", "Speed", "Bay", "Island" ...),
// the map's name without TrackMania's $ codes (plain ASCII), the medal times (ms: author, gold, silver, bronze);
// 0 = a Challenge.Gbx
int TmGbxHeader(const unsigned char* aData, int aSize, char* aEnv, int aEnvCap, char* aName, int aNameCap, int* aMedals,
    char* aUid = 0, int aUidCap = 0, char* aAuthor = 0, int aAuthorCap = 0);
// the bytes a header takes (the start of the file: 17 + its size; read that much for TmGbxHeader); -1 not a Gbx
int TmGbxHeaderSize(const unsigned char* aStart, int aLen);

struct TmGbxBlock
    {
    unsigned short iName;       // index into the track's name table
    unsigned char iDir, iX, iY, iZ;
    unsigned int iFlags;        // 0x1000 ground, &63 variant, (>>6)&63 sub-variant
    };

enum { KGbxMaxBlocks = 8192, KGbxMaxNames = 512, KGbxNamePool = 16384 };

class TmGbxTrack
    {
public:
    TmGbxTrack();
    ~TmGbxTrack();
    // parse a whole file in memory; 0 on success, else a negative step number (where it failed)
    int Read(const unsigned char* aData, int aSize);
    const char* Name(int aIndex) const { return iPool + iNameOff[aIndex]; }
    // the block name's index, -1 if the track does not use it (case-insensitive)
    int FindName(const char* aName) const;
    void Free();
public:
    char iEnv[16];              // "Rally", "Alpine", "Speed", "Bay", "Island", ...
    char iTitle[64];            // the track's name (Nadeo formatting codes left in)
    char iUid[32];
    int iSize[3];
    int iMedals[4];             // ms: author, gold, silver, bronze (0: not in the file)
    int iVersion;
    int iNBlocks;
    TmGbxBlock* iBlocks;        // heap, iNBlocks
    int iNNames;
    int iNameOff[KGbxMaxNames];
    char* iPool;                // heap, KGbxNamePool
    int iPoolUsed;
    int AddName(const unsigned char* aS, int aLen);
private:
    int ParseBlocks(const unsigned char* aB, int aLen);
    };

#endif
