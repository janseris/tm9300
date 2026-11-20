// loop test (0.18): tmloop track.tmb x y z yawdeg v steps every : straight on at full throttle from there, traced (surface mode, up, walls)
#include <stdio.h>
#include <stdlib.h>
#include "tmphys.h"
int main(int argc, char** argv)
    {
    FILE* fi = fopen(argv[1], "rb"); fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* d = new unsigned char[sz]; if (fread(d, 1, sz, fi)) {} fclose(fi);
    TmTrack t; t.Load(d, sz); TmGround g; g.Init(t);
    double x = atof(argv[2]), y = atof(argv[3]), z = atof(argv[4]), yawd = atof(argv[5]), v = atof(argv[6]); int steps = atoi(argv[7]), every = atoi(argv[8]);
    TmCar car; car.iClass = t.iCarClass; car.Reset(t);
    TmGroundHit h;
    if (!g.Probe((int) (x * 256), (int) (z * 256), (int) ((y + 1) * 256), h)) { printf("noground\n"); return 1; }
    car.iS.x = (int) (x * 256); car.iS.z = (int) (z * 256); car.iS.y = h.y; car.iS.yaw = ((int) (yawd * 4096 / 360)) & 4095; car.iS.yaw16 = car.iS.yaw << 4;
    int v0 = (int) (v * 256); car.iS.vx = (v0 * R3D::Sin(car.iS.yaw)) >> 14; car.iS.vz = (v0 * R3D::Cos(car.iS.yaw)) >> 14; car.iS.vy = 0; car.iS.speed = v0; car.iS.onGround = 1;
    TmCarInput in; in.steer = 0; in.throttle = 1; in.brake = 0;
    for (int i = 0; i < steps; i++)
        {
        car.Step(g, in);
        if (i % every == 0) printf("%4d (%7.1f %6.1f %7.1f) v (%6.1f %6.1f %6.1f) %s%s up (%5d %5d %5d) surf %d walls %d\n", i, car.iS.x / 256.0, car.iS.y / 256.0, car.iS.z / 256.0, car.iS.vx / 256.0, car.iS.vy / 256.0, car.iS.vz / 256.0,
            car.iS.surfMode ? "SURF " : "", car.iS.onGround ? "G" : "A", car.iS.ux, car.iS.uy, car.iS.uz, car.iS.surf, car.iWallHits); if (i % every == 0) printf("      n (%d %d %d) speed %.1f\n", car.iS.nx, car.iS.ny, car.iS.nz, car.iS.speed / 256.0);
        }
    }
