// the menu's picture of a track (0.11): an overview from high over the replay's path, drawn with the phone's own
// renderer at twice the size and averaged down, with the medal times, into <name>.tmt beside the track:
// "TMT1", w, h, the medals (ms: author, gold, silver, bronze), w*h RGB565
// g++ -O2 -I../inc tmthumb.cpp ../src/tmcore.cpp ../src/r3d.cpp ../src/r3dgeo.c ../src/r3dspan.c ../src/fastmem.c ../src/fastmem_ref.c -o tmthumb
//   ./tmthumb track.tmb out.tmt
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tmcore.h"

int main(int argc, char** argv)
    {
    if (argc < 3) { fprintf(stderr, "usage: tmthumb track.tmb out.tmt\n"); return 1; }
    FILE* fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
    fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* data = new unsigned char[sz]; if (fread(data, 1, sz, fi) != (size_t) sz) return 1; fclose(fi);
    TmTrack t; if (!t.Load(data, (int) sz)) { fprintf(stderr, "bad track\n"); return 1; }
    const int W = 74, H = 46, W2 = W * 2, H2 = H * 2;
    TmView v; v.Init(W2, H2, 4000); v.iFarCut = 0; v.iUsePvs = 0;
    t.iFogStart = 20000 * 16; t.iFogEnd = 20001 * 16;      // no fog: the camera is hundreds of metres off
    // the path's bounds (1/16 m)
    int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30), ysum = 0;
    for (int i = 0; i < t.iNPath; i++) { const TmPath& p = t.iPath[i]; if (p.x < x0) x0 = p.x; if (p.x > x1) x1 = p.x; if (p.z < z0) z0 = p.z; if (p.z > z1) z1 = p.z; ysum += p.y; }
    if (!t.iNPath) { x0 = z0 = 0; x1 = z1 = 512 * 16; }
    const double cx = (x0 + x1) / 32.0, cz = (z0 + z1) / 32.0, cy = t.iNPath ? ysum / (16.0 * t.iNPath) : 0;
    const double size = fmax((x1 - x0) / 16.0, (z1 - z0) / 16.0) + 40;
    // from the south-west, high: 40 degrees down, the whole path in the picture
    const double yaw = 0.6, pitch = -0.7, dist = size * 0.95;
    const double camx = cx - sin(yaw) * cos(pitch) * dist, camz = cz - cos(yaw) * cos(pitch) * dist, camy = cy - sin(pitch) * dist;
    R3Pix* frame = new R3Pix[W2 * H2];
    const int focal = (int) (W2 * 0.9);
    v.Frame(frame, W2, t, (int) (camx * 16), (int) (camy * 16), (int) (camz * 16), (int) (yaw * 4096 / 6.2831853) & 4095, (int) (pitch * 4096 / 6.2831853), focal);
    FILE* fo = fopen(argv[2], "wb");
    int hdr[7] = { 0x31544d54, W, H, t.iMedal[0], t.iMedal[1], t.iMedal[2], t.iMedal[3] };
    fwrite(hdr, 4, 7, fo);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            {
            int r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) { int c = frame[(y * 2 + (k >> 1)) * W2 + x * 2 + (k & 1)]; r += c >> 11; g += (c >> 5) & 63; b += c & 31; }
            unsigned short c = (unsigned short) (((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
            fwrite(&c, 2, 1, fo);
            }
    fclose(fo);
    printf("%s: %dx%d, medals %d %d %d %d\n", argv[2], W, H, t.iMedal[0], t.iMedal[1], t.iMedal[2], t.iMedal[3]);
    return 0;
    }
