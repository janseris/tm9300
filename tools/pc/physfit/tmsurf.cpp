// the surface class under each path sample of a packed track (one line per sample: index surface ground_y_m)
#include <stdio.h>
#include <stdlib.h>
#include "tmphys.h"
int main(int argc, char** argv)
    {
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = new unsigned char[n]; fread(d, 1, n, f); fclose(f);
    TmTrack t; if (!t.Load(d, (int) n)) return 1;
    TmGround g; g.Init(t);
    for (int i = 0; i < t.iNPath; i++)
        {
        TmGroundHit h;
        const TmPath& p = t.iPath[i];
        if (g.Probe(p.x * 16, p.z * 16, p.y * 16 + 256, h) && p.y * 16 - h.y < 512) printf("%d %d %.2f\n", i, h.surf, (p.y * 16 - h.y) / 256.0);
        else printf("%d -1 0\n", i);
        }
    return 0;
    }
