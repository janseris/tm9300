// PC harness: renders frames of a packed track along its replay path to PPM files and prints the stats.
// g++ -O2 -I../inc tmhost.cpp ../src/tmcore.cpp ../src/r3d.cpp ../src/r3dgeo.c ../src/r3dspan.c ../src/fastmem.c ../src/fastmem_ref.c -o tmhost
//   ./tmhost track.tmb outprefix [every_nth_sample] [maxpolys] [W H focal]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tmcore.h"

static void Save(const char* name, const R3Pix* f, int w, int h)
    {
    FILE* o = fopen(name, "wb");
    fprintf(o, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++)
        {
        int c = f[i];
        unsigned char rgb[3] = { (unsigned char) (((c >> 11) & 31) << 3), (unsigned char) (((c >> 5) & 63) << 2), (unsigned char) ((c & 31) << 3) };
        fwrite(rgb, 1, 3, o);
        }
    fclose(o);
    }

int main(int argc, char** argv)
    {
    if (argc < 3) { fprintf(stderr, "usage: tmhost track.tmb outprefix [every] [maxpolys] [W H focal] [farcut m] [flip]\n"); return 1; }
    FILE* fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
    fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* data = new unsigned char[sz]; fread(data, 1, sz, fi); fclose(fi);
    int every = argc > 3 ? atoi(argv[3]) : 20;
    int maxp = argc > 4 ? atoi(argv[4]) : 2500;
    int W = argc > 5 ? atoi(argv[5]) : 320, H = argc > 6 ? atoi(argv[6]) : 200, focal = argc > 7 ? atoi(argv[7]) : 200;
    int farcut = argc > 8 ? atoi(argv[8]) : 180;
    int flip = argc > 9 ? atoi(argv[9]) : 0;
    TmTrack t;
    if (!t.Load(data, (int) sz)) { fprintf(stderr, "bad track\n"); return 1; }
    printf("track: %d meshes, %d instances, %d triangles placed, %d path samples\n", t.iNMesh, t.iNInst, t.iTris, t.iNPath);
    TmView v;
    v.Init(W, H, maxp);
    v.iFarCut = farcut * 16;
    v.iFlipWinding = flip; v.iMeshMode = getenv("MESH") ? atoi(getenv("MESH")) : 1; if (getenv("COVER")) v.iR->iCover = atoi(getenv("COVER")); if (getenv("ZNEAR")) v.iR->iZNear = atoi(getenv("ZNEAR")) * 16;
    R3Pix* frame = new R3Pix[W * H];
    long tot = 0; int frames = 0;
    for (int s = 0; s < t.iNPath; s += every)
        {
        int cx, cy, cz, yaw, pitch;
        v.ChaseCamera(t, s, 0, cx, cy, cz, yaw, pitch);
        if (getenv("CAM")) sscanf(getenv("CAM"), "%d,%d,%d,%d,%d", &cx, &cy, &cz, &yaw, &pitch), cx *= 16, cy *= 16, cz *= 16;
        int carx, cary, carz, caryaw;
        v.CarAt(t, s, 0, carx, cary, carz, caryaw);
        clock_t t0 = clock();
        v.Frame(frame, W, t, cx, cy, cz, yaw, pitch, focal, carx, cary, carz, caryaw, 1);
        clock_t t1 = clock();
        tot += t1 - t0; frames++;
        char name[256]; sprintf(name, "%s_%03d.ppm", argv[2], s);
        Save(name, frame, W, H);
        printf("sample %3d cam (%d,%d,%d) yaw %d: visible %d/%d, polys %d drawn %d culled %d hidden %d pixels %d%s\n",
            s, cx / 16, cy / 16, cz / 16, yaw, v.iStats.visible, v.iStats.insts, v.iStats.polys, v.iStats.drawn, v.iStats.culled, v.iStats.hidden, v.iStats.pixels, v.iStats.full ? " FULL" : ""); printf("   depth test: %d polys, %d rows (%d whole, %d with several runs, %d clamped), %d pixels looked at, %d written\n", v.iR->iZPolys, v.iR->iZRows, v.iR->iZFullRows, v.iR->iZRowsNp, v.iR->iZRowsSlow, v.iR->iZVisited, v.iR->iZWritten);
        if (getenv("REF"))
            {
            // the z-buffer reference of the same polygons and the count of pixels the sorted frame gets wrong
            R3Pix* ref = new R3Pix[W * H];
            for (int i = 0; i < W * H; i++) ref[i] = frame[i];
            v.iR->ReferenceFrame(ref, W);
            int diff = 0; for (int i = 0; i < W * H; i++) if (ref[i] != frame[i]) diff++;
            printf("   reference: %d pixels differ (%.2f%%)\n", diff, 100.0 * diff / (W * H));
            char name[256]; sprintf(name, "%s_%03d_ref.ppm", argv[2], s);
            Save(name, ref, W, H);
            delete[] ref;
            }
        if (getenv("PIX"))
            {
            // the polygons under a pixel in the order drawn (the frame's buckets after End)
            int px, py; sscanf(getenv("PIX"), "%d,%d", &px, &py);
            static int out[24 * 8000]; const R3Tex* tex[4]; int nt;
            int n = v.iR->ScenePolys(out, 8000, tex, 4, &nt); printf("   %d polys in the buckets\n", n);
            v.iR->DebugPixel(px, py); n = 0;
            for (int i = 0; i < n; i++)
                {
                const int* p = out + i * 24; int c = p[0] < 4 ? p[0] : 4, pos = 0, neg = 0;
                for (int k = 0; k < c; k++) { int j = (k + 1) % c; long d = (long) (p[5 + j] - p[5 + k]) * (py - p[9 + k]) - (long) (p[9 + j] - p[9 + k]) * (px - p[5 + k]); if (d > 0) pos++; else if (d < 0) neg++; }
                if (pos && neg) continue;
                printf("   #%d n %d col %04x depth %d: (%d,%d) (%d,%d) (%d,%d) (%d,%d) sz %d %d %d\n", i, p[0], p[3], p[4], p[5], p[9], p[6], p[10], p[7], p[11], p[8], p[12], p[21], p[22], p[23]);
                }
            }
        }
    printf("%d frames, %.2f ms each on this PC\n", frames, 1000.0 * tot / CLOCKS_PER_SEC / (frames ? frames : 1));
    return 0;
    }
