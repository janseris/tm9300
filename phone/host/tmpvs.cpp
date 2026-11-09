// The potentially visible set of a packed track: from points in every 32 m cell of the map, at the heights a
// camera has there, the frame is drawn with each face in its instance's number (TmView::iIdMode) and the numbers
// that show are the cell's set; appended to the track (TMB6). The phone then skips what cannot show from the
// camera's cell: in Snow the Great Wall hides most of the map.
//   g++ -O2 -I../inc tmpvs.cpp ../src/tmphys.cpp ../src/tmcore.cpp ../src/r3d.cpp ../src/r3dgeo.c ../src/r3dspan.c ../src/fastmem.c ../src/fastmem_ref.c -o tmpvs
//   ./tmpvs in.tmb out.tmb
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tmphys.h"

int main(int argc, char** argv)
    {
    if (argc < 3) { fprintf(stderr, "usage: tmpvs in.tmb out.tmb\n"); return 1; }
    FILE* fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
    fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* data = new unsigned char[sz]; if (fread(data, 1, sz, fi) != (size_t) sz) return 1; fclose(fi);
    if (data[3] != '5') { fprintf(stderr, "a TMB5 is needed (the packer's output)\n"); return 1; }
    TmTrack t; if (!t.Load(data, (int) sz)) { fprintf(stderr, "bad track\n"); return 1; }
    TmGround g; g.Init(t);
    // the cells: the blocks' extent plus one
    int x0 = 0x7fffffff, z0 = 0x7fffffff, x1 = -0x7fffffff, z1 = -0x7fffffff;
    for (int i = 0; i < t.iNCInst; i++) { const TmCInst& c = t.iCInst[i]; if (c.bb[0] < x0) x0 = c.bb[0]; if (c.bb[2] < z0) z0 = c.bb[2]; if (c.bb[3] > x1) x1 = c.bb[3]; if (c.bb[5] > z1) z1 = c.bb[5]; }
    int cx0 = (x0 >> 9) - 1, cz0 = (z0 >> 9) - 1, cw = (x1 >> 9) - cx0 + 2, ch = (z1 >> 9) - cz0 + 2;
    int words = (t.iNInst + 31) >> 5;
    unsigned int* pvs = new unsigned int[cw * ch * words]; memset(pvs, 0, cw * ch * words * 4);
    const int W = 320, H = 200;
    TmView v; v.Init(W, H, 6000); v.iFarCut = 180 * 16; v.iIdMode = 1; v.iUsePvs = 0;
    R3Pix* frame = new R3Pix[W * H];
    const int pts[5][2] = { { 0, 0 }, { -14, -14 }, { 14, -14 }, { -14, 14 }, { 14, 14 } };
    long renders = 0, cellsWithGround = 0; clock_t t0 = clock();
    for (int cz = 0; cz < ch; cz++)
        for (int cx = 0; cx < cw; cx++)
            {
            unsigned int* bits = pvs + (cz * cw + cx) * words;
            int found = 0;
            for (int pi = 0; pi < 5; pi++)
                {
                int px = ((cx0 + cx) * 32 + 16 + pts[pi][0]) * 256, pz = ((cz0 + cz) * 32 + 16 + pts[pi][1]) * 256;
                // the surfaces there, the top one and the one under it (a road under a bridge)
                int ymax = 1000 * 256;
                for (int layer = 0; layer < 2; layer++)
                    {
                    TmGroundHit h;
                    if (!g.Probe(px, pz, ymax, h)) break;
                    found = 1; ymax = h.y - 256;
                    for (int hi = 0; hi < 2; hi++)
                        {
                        int cy = h.y + (hi ? 10 : 4) * 256;
                        for (int yaw = 0; yaw < 4096; yaw += 512)
                            {
                            v.Frame(frame, W, t, px >> 4, cy >> 4, pz >> 4, yaw, hi ? -200 : -80, 200, 0, 0, 0, 0, 0);
                            renders++;
                            for (int k = 0; k < W * H; k++) { int id = frame[k]; if (id > 0 && id <= t.iNInst) bits[(id - 1) >> 5] |= 1u << ((id - 1) & 31); }
                            }
                        }
                    }
                }
            if (!found) memset(bits, 0xff, words * 4);        // nothing to stand on: everything may show
            else cellsWithGround++;
            }
    // the sets of the 8 neighbours joined in: the camera is up to 9 m behind the car, and a wall's shadow is sharp
    unsigned int* wide = new unsigned int[cw * ch * words];
    for (int cz = 0; cz < ch; cz++)
        for (int cx = 0; cx < cw; cx++)
            {
            unsigned int* o = wide + (cz * cw + cx) * words; memset(o, 0, words * 4);
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
                {
                int nx = cx + dx, nz = cz + dz; if (nx < 0 || nz < 0 || nx >= cw || nz >= ch) continue;
                const unsigned int* b = pvs + (nz * cw + nx) * words; for (int w = 0; w < words; w++) o[w] |= b[w];
                }
            }
    long tot = 0; for (int k = 0; k < cw * ch * words; k++) for (unsigned int b = wide[k]; b; b &= b - 1) tot++;
    printf("%d x %d cells (%ld with ground), %ld renders in %.1f s; %.0f instances visible a cell on average of %d\n",
        cw, ch, cellsWithGround, renders, (double) (clock() - t0) / CLOCKS_PER_SEC, (double) tot / (cw * ch), t.iNInst);
    FILE* fo = fopen(argv[2], "wb"); if (!fo) { perror(argv[2]); return 1; }
    data[3] = '6';
    fwrite(data, 1, sz, fo);
    int hdr[4] = { cx0, cz0, cw, ch }; fwrite(hdr, 4, 4, fo);
    fwrite(wide, 4, cw * ch * words, fo);
    fclose(fo);
    printf("wrote %s: + %d bytes of PVS\n", argv[2], 16 + cw * ch * words * 4);
    return 0;
    }
