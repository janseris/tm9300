#include <stdio.h>
#include <stdlib.h>
#include "tmphys.h"
// every checkpoint / finish box against the replay's path (10 Hz, interpolated to 20 points a sample): passed or not
int main(int argc, char** argv)
    {
    FILE* fi = fopen(argv[1], "rb"); fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* data = new unsigned char[sz]; if (fread(data, 1, sz, fi) != (size_t) sz) return 1; fclose(fi);
    TmTrack t; if (!t.Load(data, (int) sz)) return 1;
    TmGround g; g.Init(t);
    int cps = 0, missed = 0, fin = 0, finHit = 0;
    for (int i = 0; i < t.iNCInst; i++)
        {
        const TmCInst& c = t.iCInst[i];
        if (c.kind != 2 && c.kind != 3) continue;
        bool hitIt = false;
        for (int s = 0; s + 1 < t.iNPath && !hitIt; s++)
            for (int k = 0; k < 20 && !hitIt; k++)
                {
                const TmPath& a = t.iPath[s]; const TmPath& b = t.iPath[s + 1];
                int x = (a.x + (b.x - a.x) * k / 20) * 16, y = (a.y + (b.y - a.y) * k / 20) * 16, z = (a.z + (b.z - a.z) * k / 20) * 16;
                if (g.KindAt(c.kind, x, y, z) == i) hitIt = true;
                }
        if (c.kind == 2) { cps++; if (!hitIt) { missed++;
            // the replay's nearest pass to the box (horizontal), and its height there
            int best = 1 << 30, bx = 0, by = 0, bz = 0;
            for (int s = 0; s < t.iNPath; s++) { const TmPath& a = t.iPath[s]; int dx = a.x < c.bb[0] ? c.bb[0] - a.x : a.x > c.bb[3] ? a.x - c.bb[3] : 0, dz = a.z < c.bb[2] ? c.bb[2] - a.z : a.z > c.bb[5] ? a.z - c.bb[5] : 0; int d = dx > dz ? dx : dz; if (d < best) { best = d; bx = a.x; by = a.y; bz = a.z; } }
            printf("  checkpoint box %d (x %d..%d, y %d..%d, z %d..%d m, coll mesh %d) not passed: the replay comes within %d m, at (%d, %d, %d)\n", i, c.bb[0] / 16, c.bb[3] / 16, c.bb[1] / 16, c.bb[4] / 16, c.bb[2] / 16, c.bb[5] / 16, c.coll, best / 16, bx / 16, by / 16, bz / 16); } }
        else { fin++; if (hitIt) finHit++; }
        }
    // as the car counts them: the replay driven through by a car's race logic, point by point
    TmCar car; car.Reset(t);
    int taken = 0; unsigned mask = 0;
    for (int s = 0; s + 1 < t.iNPath; s++)
        for (int k = 0; k < 20; k++)
            {
            const TmPath& a = t.iPath[s]; const TmPath& b = t.iPath[s + 1];
            int x = (a.x + (b.x - a.x) * k / 20) * 16, y = (a.y + (b.y - a.y) * k / 20) * 16, z = (a.z + (b.z - a.z) * k / 20) * 16;
            int cp = g.KindAt(2, x, y, z);
            if (cp >= 0) for (int q = 0; q < car.iNCp; q++) if (car.iCpInst[q] == cp && !(mask & (1u << car.iCpGroup[q]))) { mask |= 1u << car.iCpGroup[q]; taken++; }
            }
    printf("%s: %d checkpoint boxes (%d checkpoints), the replay takes %d of them; %d finish boxes, %d passed\n", argv[1], cps, car.iTotalCps, taken, fin, finHit);
    return 0;
    }
