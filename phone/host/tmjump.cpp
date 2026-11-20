// jump test (0.18): stdin lines "x y z yawdeg v steps" (metres, m/s); per line prints "takeoff_step land_step land_x land_z apex_y" (tools/pc/jumptest.py drives it)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tmphys.h"
int main(int argc, char** argv)
    {
    FILE* fi = fopen(argv[1], "rb"); fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* d = new unsigned char[sz]; fread(d, 1, sz, fi); fclose(fi);
    TmTrack t; t.Load(d, sz); TmGround g; g.Init(t);
    double x, y, z, yawd, v; int steps;
    while (scanf("%lf %lf %lf %lf %lf %d", &x, &y, &z, &yawd, &v, &steps) == 6)
        {
        TmCar car; car.iClass = t.iCarClass; car.Reset(t);
        TmGroundHit h;
        if (!g.Probe((int) (x * 256), (int) (z * 256), (int) ((y + 1) * 256), h)) { printf("noground\n"); continue; }
        car.iS.x = (int) (x * 256); car.iS.z = (int) (z * 256); car.iS.y = h.y; car.iS.yaw = ((int) (yawd * 4096 / 360)) & 4095; car.iS.yaw16 = car.iS.yaw << 4;
        int v0 = (int) (v * 256); car.iS.vx = (v0 * R3D::Sin(car.iS.yaw)) >> 14; car.iS.vz = (v0 * R3D::Cos(car.iS.yaw)) >> 14; car.iS.vy = 0; car.iS.speed = v0; car.iS.onGround = 1;
        TmCarInput in; in.steer = 0; in.throttle = 1; in.brake = 0;
        int to = -1, ld = -1, air = 0; double apex = -1e9;
        for (int i = 0; i < steps; i++)
            {
            car.Step(g, in);
            if (!car.iS.onGround) { air++; if (air == 1 && to < 0) fprintf(stderr, "launch vy %.1f vh %.1f y %.1f\n", car.iS.vy / 256.0, sqrt((double) car.iS.vx * car.iS.vx + (double) car.iS.vz * car.iS.vz) / 256.0, car.iS.y / 256.0); if (air == 5 && to < 0) to = i - 4; if (car.iS.y / 256.0 > apex) apex = car.iS.y / 256.0; }
            else { if (to >= 0 && ld < 0) { ld = i; break; } air = 0; }
            }
        printf("%d %d %.2f %.2f %.2f\n", to, ld, car.iS.x / 256.0, car.iS.z / 256.0, apex);
        }
    return 0;
    }
