// the ground along a line: x0 z0 x1 z1 (m), the highest surface under y (m), every 0.25 m
#include <stdio.h>
#include <stdlib.h>
#include "tmphys.h"
int main(int argc, char** argv)
    {
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = new unsigned char[n]; fread(d, 1, n, f); fclose(f);
    TmTrack t; t.Load(d, (int) n); TmGround g; g.Init(t);
    double x0 = atof(argv[2]), z0 = atof(argv[3]), x1 = atof(argv[4]), z1 = atof(argv[5]), y = atof(argv[6]);
    for (int i = 0; i <= 40; i++)
        {
        double x = x0 + (x1 - x0) * i / 40, z = z0 + (z1 - z0) * i / 40;
        TmGroundHit h;
        if (g.Probe((int) (x * 256), (int) (z * 256), (int) (y * 256), h)) printf("%.2f %.2f: y %.2f n (%d %d %d) surf %d inst %d face %d\n", x, z, h.y / 256.0, h.nx, h.ny, h.nz, h.surf, h.cinst, h.face);
        else printf("%.2f %.2f: none\n", x, z);
        }
    }
