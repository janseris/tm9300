// gbx2tmb kit.tmk track.Challenge.Gbx out.tmb [path.bin] : a track built on the PC as the phone builds it (0.19:
// tmkit.cpp); path.bin: a replay path (5 int32 a sample: x y z 1/16 m, yaw, pitch); prints the build's stats
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "tmgbx.h"
#include "tmkit.h"
static unsigned char* Load(const char* f, int& n) { FILE* fp = fopen(f, "rb"); if (!fp) return 0; fseek(fp, 0, 2); n = ftell(fp); fseek(fp, 0, 0); unsigned char* d = (unsigned char*)malloc(n); if (fread(d, 1, n, fp)) {} fclose(fp); return d; }
struct FOut : MTmOut { FILE* f; int Write(const void* a, int n) { return fwrite(a, 1, n, f) == (size_t)n ? 0 : -1; } };
int main(int argc, char** argv)
{
    int kn, gn; unsigned char* k = Load(argv[1], kn); unsigned char* g = Load(argv[2], gn);
    TmKit kit; int r = kit.Open(k, kn); if (r) { printf("kit open %d\n", r); return 1; }
    TmGbxTrack t; r = t.Read(g, gn); if (r) { printf("gbx %d\n", r); return 1; }
    FOut o; o.f = fopen(argv[3], "wb");
    int* path = 0; int np = 0;
    if (argc > 4) { int pn; unsigned char* pd = Load(argv[4], pn); path = (int*)pd; np = pn / 20; }
    clock_t c0 = clock();
    r = kit.Build(t, o, path, np); fclose(o.f);
    printf("%s: build %d env %s: %d inst %d mesh %d coll %d cinst %d tris, unknown %d dropped %d chunks %d (%d KB) start %d %d %d %d (%.2fs)\n", argv[2], r, t.iEnv, kit.iNInst, kit.iNMesh, kit.iNColl, kit.iNCInst, kit.iTris, kit.iUnknown, kit.iDropped, kit.iChunks, kit.iUnpacked / 1024, kit.iStart[0], kit.iStart[1], kit.iStart[2], kit.iStart[3], (double)(clock() - c0) / CLOCKS_PER_SEC);
    return r ? 1 : 0;
}
