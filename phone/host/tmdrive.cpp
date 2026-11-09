// PC harness for the physics: an automatic driver follows the replay's path with the car model, the frames
// from the follow camera saved every so often, the lap compared with the replay's time.
// g++ -O2 -I../inc tmdrive.cpp ../src/tmphys.cpp ../src/tmcore.cpp ../src/r3d.cpp ../src/r3dgeo.c ../src/r3dspan.c ../src/fastmem.c ../src/fastmem_ref.c -o tmdrive
//   ./tmdrive track.tmb outprefix [frame every n steps (0 none)] [max seconds]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tmphys.h"

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


// brute force: does the segment p0-p1 (metres) cross the triangle a b c (metres)?
static bool SegTri(const double* p0, const double* p1, const double* a, const double* b, const double* c)
    {
    double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
    double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (fabs(det) < 1e-9) return false;
    double f = 1 / det, s[3] = { p0[0] - a[0], p0[1] - a[1], p0[2] - a[2] };
    double u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]); if (u < 0 || u > 1) return false;
    double q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    double v = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]); if (v < 0 || u + v > 1) return false;
    double t = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
    return t >= 0 && t <= 1;
    }

int main(int argc, char** argv)
    {
    if (argc < 3) { fprintf(stderr, "usage: tmdrive track.tmb outprefix [every] [maxsec]\n"); return 1; }
    FILE* fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
    fseek(fi, 0, SEEK_END); long sz = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char* data = new unsigned char[sz]; if (fread(data, 1, sz, fi) != (size_t) sz) return 1; fclose(fi);
    int every = argc > 3 ? atoi(argv[3]) : 100;
    int maxsec = argc > 4 ? atoi(argv[4]) : 120;
    TmTrack t;
    if (!t.Load(data, (int) sz)) { fprintf(stderr, "bad track\n"); return 1; }
    printf("track: %d meshes, %d instances, %d collision meshes, %d collision instances, start (%d,%d,%d) yaw %d, %d path samples (%.1f s)\n",
        t.iNMesh, t.iNInst, t.iNColl, t.iNCInst, t.iStartX / 16, t.iStartY / 16, t.iStartZ / 16, t.iStartYaw, t.iNPath, t.iNPath / 10.0);
    TmGround g; if (!g.Init(t)) { fprintf(stderr, "no ground\n"); return 1; }
    TmCar car; car.Reset(t);
    TmFollowCam cam;
    if (getenv("CAM")) { int b, u, l, a; if (sscanf(getenv("CAM"), "%d,%d,%d,%d", &b, &u, &l, &a) == 4) { cam.iBack = b * 256 / 10; cam.iUp = u * 256 / 10; cam.iLookUp = l * 256 / 10; cam.iLookAhead = a * 256 / 10; } }
    cam.Reset(car.iS);
    TmView v; v.Init(320, 200, 2500); v.iFarCut = 180 * 16;
    R3Pix* frame = new R3Pix[320 * 200];
    int prog = 0;                               // the path sample reached
    // 0.8: GHOST=1: the replay as a ghost (its checkpoints and finish printed; drawn in the frames); GHOST=2 also
    // records this run and checks it back (At at every 5th step = the car's place, the file form round trip)
    TmGhost ghost, run;
    if (getenv("GHOST"))
        {
        ghost.FromPath(t, g, car);
        printf("ghost from the replay: %d points, finish %d steps (%.2f s), checkpoints:", ghost.iN, ghost.Finish(), ghost.Finish() / 50.0);
        for (int k = 0; k < car.iTotalCps; k++) printf(" %.2f", ghost.CpSteps(k) / 50.0);
        printf("\n");
        // the replay's height against the ground under it
        double sum = 0; int cnt = 0;
        for (int i = 0; i < t.iNPath; i += 10) { TmGroundHit h; if (g.Probe(t.iPath[i].x * 16, t.iPath[i].z * 16, t.iPath[i].y * 16 + 512, h)) { sum += (t.iPath[i].y * 16 - h.y) / 256.0; cnt++; } }
        printf("medals: author %d gold %d silver %d bronze %d ms\n", t.iMedal[0], t.iMedal[1], t.iMedal[2], t.iMedal[3]);
        printf("replay km/h by second:");
        for (int i = 0; i + 10 < t.iNPath; i += 10) { double dx = t.iPath[i + 10].x - t.iPath[i].x, dz = t.iPath[i + 10].z - t.iPath[i].z, dy = t.iPath[i + 10].y - t.iPath[i].y; printf(" %.0f", sqrt(dx * dx + dy * dy + dz * dz) / 16 * 3.6); }
        printf("\n");
        printf("replay over the ground: %.2f m on average (%d samples)\n", cnt ? sum / cnt : 0.0, cnt);
        run.Begin();
        }
    int frames = 0;
    static int reach[20000]; for (int i = 0; i < 20000; i++) reach[i] = -1;    // 0.8: the step the car reached each path sample
    const int lookTarget = getenv("LOOK") ? atoi(getenv("LOOK")) : 8;
    for (int step = 0; step < maxsec * 50 && !car.iFinished; step++)
        {
        const TmCarState& s = car.iS;
        if (prog < 20000 && reach[prog] < 0) reach[prog] = step;
        // the driver: the nearest path sample from here on, then a target some metres further along
        for (;;)
            {
            if (prog + 1 >= t.iNPath) break;
            const TmPath& a = t.iPath[prog]; const TmPath& b = t.iPath[prog + 1];
            long da = (long) (a.x * 16 - s.x) * (a.x * 16 - s.x) + (long) (a.z * 16 - s.z) * (a.z * 16 - s.z);
            long db = (long) (b.x * 16 - s.x) * (b.x * 16 - s.x) + (long) (b.z * 16 - s.z) * (b.z * 16 - s.z);
            if (db < da) prog++; else break;
            }
        int look = lookTarget * 256 + ((s.speed > 0 ? s.speed : 0) * 1) / 4;
        if (getenv("OFFSET")) look += atoi(getenv("OFFSET")) * 256;     // aims farther ahead: cuts corners across the terrain     // 12 m + 0.25 s of travel
        int ti = prog;
        while (ti + 1 < t.iNPath)
            {
            int dx = t.iPath[ti].x * 16 - s.x, dz = t.iPath[ti].z * 16 - s.z;
            if (abs(dx) + abs(dz) >= look) break;
            ti++;
            }
        int dx = t.iPath[ti].x * 16 - s.x, dz = t.iPath[ti].z * 16 - s.z;
        int wantYaw = TmYawTo(dx, dz);
        int err = ((wantYaw - s.yaw + 2048) & 4095) - 2048;        // -2048..2047, positive: turn right
        TmCarInput in;
        in.steer = err * (getenv("GAIN") ? atoi(getenv("GAIN")) : 4) / 2; if (in.steer > 256) in.steer = 256; if (in.steer < -256) in.steer = -256;
        in.throttle = 1; in.brake = 0;
        // a sharp turn ahead at speed: brake
        int ti2 = ti; while (ti2 + 1 < t.iNPath && ti2 < ti + 8) ti2++;
        int far2 = ((TmYawTo(t.iPath[ti2].x * 16 - s.x, t.iPath[ti2].z * 16 - s.z) - s.yaw + 2048) & 4095) - 2048;
        if (!getenv("NOLIMIT") && abs(far2) > 400 && s.speed > 25 * 256) { in.brake = 1; in.throttle = 0; }
        if (abs(err) > 700 && s.speed > 15 * 256) { in.brake = 1; in.throttle = 0; }
        // the replay's own speed a little ahead (over 2 samples) as the limit: the driver mimics the replay's pace
        {
        int k = ti < t.iNPath - 2 ? ti : t.iNPath - 3;
        int rx = t.iPath[k + 2].x - t.iPath[k].x, rz = t.iPath[k + 2].z - t.iPath[k].z, ry = t.iPath[k + 2].y - t.iPath[k].y;
        double rv = sqrt((double) rx * rx + (double) rz * rz + (double) ry * ry) / 16 / 0.2;     // m/s
        if (!getenv("NOLIMIT") && s.speed > (rv * 1.08 + 1.0) * 256) { in.throttle = 0; if (s.speed > (rv * 1.2 + 2.0) * 256) in.brake = 1; }
        }
        // 0.8: DRIVER=2: a driver that knows the car: pure pursuit on the replay's line (a point 5 m + 0.3 s ahead,
        // the turning rate it needs over the car's full-lock rate), and the speed each bend of the line allows
        // (its sideways limit ACAP m/s^2 and turning limit RCAP rad/s), braked for at BDEC m/s^2 in time
        if (getenv("DRIVER") && atoi(getenv("DRIVER")) == 2)
            {
            const bool snow = t.iCarClass == 1;
            const double acap = getenv("ACAP") ? atof(getenv("ACAP")) : (snow ? 120.0 : 52.0);
            const double rcap = getenv("RCAP") ? atof(getenv("RCAP")) : (snow ? 2.2 : 1.3);
            const double bdec = getenv("BDEC") ? atof(getenv("BDEC")) : 14.0;
            const double v = s.speed / 256.0, cx = s.x / 256.0, cz = s.z / 256.0;
            const double hy = s.yaw * 6.283185307 / 4096, fx = sin(hy), fz = cos(hy);
            const double L = 5 + 0.3 * (v > 0 ? v : 0);
            int k = prog;
            while (k + 1 < t.iNPath && hypot(t.iPath[k].x / 16.0 - cx, t.iPath[k].z / 16.0 - cz) < L) k++;
            const double tx = t.iPath[k].x / 16.0 - cx, tz = t.iPath[k].z / 16.0 - cz;
            const double lat = tx * fz - tz * fx, fwd = tx * fx + tz * fz;           // right of the heading, ahead
            const double kap = 2 * lat / (lat * lat + fwd * fwd + 0.01);
            // the full-lock turning rate at this speed (the car's own table, roughly): Snow 2 m + 0.4 v radius
            const double vv = v > 1 ? v : 1;
            const double rfull = snow ? vv / (2 + 0.4 * vv) : (vv < 30 ? 1.9 : vv < 40 ? 1.65 : vv < 50 ? 1.5 : 1.35);
            double st = (v * kap) / rfull * 256;
            in.steer = (int) (st > 256 ? 256 : st < -256 ? -256 : st);
            // the speed: every bend ahead (150 m along the line) by its curvature, with the braking room to it
            double vmax = 1e9, dist = hypot(t.iPath[prog].x / 16.0 - cx, t.iPath[prog].z / 16.0 - cz);
            for (int i = prog + 2; i + 2 < t.iNPath && dist < 150; i++)
                {
                const TmPath& a = t.iPath[i - 2]; const TmPath& b = t.iPath[i]; const TmPath& c = t.iPath[i + 2];
                const double ax = a.x / 16.0, az = a.z / 16.0, bx = b.x / 16.0, bz = b.z / 16.0, cx2 = c.x / 16.0, cz2 = c.z / 16.0;
                const double ab = hypot(bx - ax, bz - az), bc = hypot(cx2 - bx, cz2 - bz), ca = hypot(ax - cx2, az - cz2);
                const double area2 = fabs((bx - ax) * (cz2 - az) - (bz - az) * (cx2 - ax));
                const double kk = ab * bc * ca > 1e-6 ? 2 * area2 / (ab * bc * ca) : 0;
                dist += hypot(t.iPath[i].x / 16.0 - t.iPath[i - 1].x / 16.0, t.iPath[i].z / 16.0 - t.iPath[i - 1].z / 16.0);
                if (kk < 1e-4) continue;
                double vl = sqrt(acap / kk); if (rcap / kk < vl) vl = rcap / kk;
                const double now = sqrt(vl * vl + 2 * bdec * dist);
                if (now < vmax) vmax = now;
                }
            in.throttle = v < vmax - 0.5 ? 1 : 0;
            in.brake = v > vmax + 1.5 ? 1 : 0;
            }
        if (getenv("STRAIGHT")) { in.steer = 0; in.throttle = 1; in.brake = 0; if (step % 50 == 49) printf("straight %d s: %.0f km/h %s\n", (step + 1) / 50, car.iS.speed * 3.6 / 256, car.iS.onGround ? "" : "(air)"); }
        if (getenv("STEERBIAS")) { in.steer += atoi(getenv("STEERBIAS")); if (in.steer > 256) in.steer = 256; if (in.steer < -256) in.steer = -256; }
        const int px0 = car.iS.x, py0 = car.iS.y, pz0 = car.iS.z;
        car.Step(g, in);
        if (getenv("GHOST")) run.Record(car);
        if (getenv("WALLCHECK"))
            {
            // the move against every wall face (surface 9) of the track, at 0.5 and 1 m up
            static int crossings = 0;
            for (int hh = 128; hh <= 256; hh += 128)
                {
                double p0[3] = { px0 / 256.0, (py0 + hh) / 256.0, pz0 / 256.0 }, p1[3] = { car.iS.x / 256.0, (car.iS.y + hh) / 256.0, car.iS.z / 256.0 };
                for (int ci = 0; ci < t.iNCInst; ci++)
                    {
                    const TmCInst& c = t.iCInst[ci];
                    const double mx = fmin(p0[0], p1[0]) * 16, Mx = fmax(p0[0], p1[0]) * 16, mz = fmin(p0[2], p1[2]) * 16, Mz = fmax(p0[2], p1[2]) * 16;
                    if (Mx < c.bb[0] || mx > c.bb[3] || Mz < c.bb[2] || mz > c.bb[5]) continue;
                    const TmColl& m = t.iColl[c.coll];
                    for (int f = 0; f < m.nf; f++)
                        {
                        if (m.surf[f] != 9) continue;
                        double v3[3][3];
                        for (int k = 0; k < 3; k++) { const short* v = m.v + m.idx[f * 3 + k] * 3; v3[k][0] = (c.x + v[0]) / 16.0; v3[k][1] = (c.y + v[1]) / 16.0; v3[k][2] = (c.z + v[2]) / 16.0; }
                        if (SegTri(p0, p1, v3[0], v3[1], v3[2])) { crossings++; if (crossings <= 10) printf("WALL CROSSED step %d at (%.1f, %.1f, %.1f) height +%.1f m, inst %d face %d n (%d %d %d)\n", step, p1[0], p1[1], p1[2], hh / 256.0, ci, f, m.nrm[f * 3], m.nrm[f * 3 + 1], m.nrm[f * 3 + 2]); }
                        }
                    }
                }
            if (car.iFinished || step == maxsec * 50 - 1) printf("wall crossings: %d\n", crossings);
            }
        if (getenv("BURY"))
            {
            // the car inside the ground: a surface 0.3 .. 1.5 m over its contact point (at its middle or a wheel)
            static int buried = 0, firstBuried = -1, steps = 0;
            TmGroundHit h; int worst = 0;
            const int sn = R3D::Sin(car.iS.yaw), co = R3D::Cos(car.iS.yaw);
            for (int k = 0; k < 1; k++)
                {
                const int fx = k == 0 ? 0 : (k & 1 ? 435 : -384), rx = k == 0 ? 0 : (k & 2 ? 230 : -230);
                const int px = car.iS.x + ((fx * sn + rx * co) >> 14), pz = car.iS.z + ((fx * co - rx * sn) >> 14);
                if (g.Probe(px, pz, car.iS.y + 384, h) && h.y > car.iS.y + 77 && h.y - car.iS.y > worst) worst = h.y - car.iS.y;
                }
            steps++;
            if (worst) { buried++; if (firstBuried < 0) firstBuried = step; if (getenv("BURYLOG")) printf("buried step %d at (%.1f, %.1f, %.1f) by %.2f m, speed %.1f\n", step, car.iS.x / 256.0, car.iS.y / 256.0, car.iS.z / 256.0, worst / 256.0, car.iS.speed / 256.0); }
            if (car.iFinished || step == maxsec * 50 - 1) printf("buried: %d of %d steps (first at step %d)\n", buried, steps, firstBuried);
            }
        if (getenv("AIR"))
            {
            static int air = 0, total = 0, hops = 0, wasAir = 0;
            static int spell = 0, jumps = 0, small = 0;
            total++; if (!car.iS.onGround) { air++; spell++; if (!wasAir) hops++; } else { if (spell >= 15) jumps++; else if (spell) small++; spell = 0; } wasAir = !car.iS.onGround;
            if (car.iFinished || step == maxsec * 50 - 1) printf("air: %.1f %% of the time, %d hops under 0.3 s, %d jumps of 0.3 s or more\n", 100.0 * air / total, small, jumps);
            }
        if (getenv("SLIP") && car.iS.onGround)
            {
            static int hist[10][91]; static int cnt[10];
            const int sn = R3D::Sin(car.iS.yaw), co = R3D::Cos(car.iS.yaw);
            const double v = (car.iS.vx * sn + car.iS.vz * co) / 16384.0, l = (car.iS.vx * co - car.iS.vz * sn) / 16384.0;
            const double sp = sqrt(v * v + l * l) / 256;
            if (sp >= 8) { int b = (int) (sp / 10); if (b > 9) b = 9; int a = (int) (fabs(atan2(l, v)) * 57.3); if (a > 90) a = 90; hist[b][a]++; cnt[b]++; }
            if (car.iFinished || step == maxsec * 50 - 1)
                {
                printf("slip deg 50/90/98 by band:");
                for (int bb = 0; bb < 10; bb++) if (cnt[bb] > 30) { int acc = 0, p50 = -1, p90 = -1, p98 = -1; for (int a = 0; a <= 90; a++) { acc += hist[bb][a]; if (p50 < 0 && acc * 2 >= cnt[bb]) p50 = a; if (p90 < 0 && acc * 10 >= cnt[bb] * 9) p90 = a; if (p98 < 0 && acc * 50 >= cnt[bb] * 49) p98 = a; } printf(" (%d: %d %d %d)", bb * 10, p50, p90, p98); }
                printf("\n");
                }
            }
        if (getenv("TRACE") && step >= atoi(getenv("TRACE")) && step < atoi(getenv("TRACE")) + 60)
            printf("step %d: (%.2f,%.2f,%.2f) v (%.1f,%.1f,%.1f) speed %.1f %s n (%d,%d,%d) surf %d steer %d\n", step, s.x / 256.0, s.y / 256.0, s.z / 256.0,
                s.vx / 256.0, s.vy / 256.0, s.vz / 256.0, s.speed / 256.0, s.onGround ? "G" : "A", s.nx, s.ny, s.nz, s.surf, s.steerPos);
        if (getenv("CLIP") && t.iCar >= 0)
            {
            // the car as drawn (tilted, turned, at y + 3/16 m) against the visible ground under each of its
            // corners: the drivable faces drawn are the collision faces, so the probe sees the road as drawn
            static int steps = 0, clipSteps = 0, worst = 0, worstStep = 0; static double sumDepth = 0;
            const TmMesh& m = t.iMesh[t.iCar];
            int sf, sr; s.Slopes(sf, sr);
            const int sn = R3D::Sin(s.yaw), co = R3D::Cos(s.yaw);
            int ymin = 1 << 30; for (int k = 0; k < m.nv; k++) if (m.v[k * 3 + 1] < ymin) ymin = m.v[k * 3 + 1];
            int deepest = 0;
            for (int k = 0; k < m.nv; k++)
                {
                const short* v = m.v + k * 3;
                if (v[1] > ymin + 8) continue;             // the corners within 0.5 m of the car's bottom
                int x = v[0], z = v[2], y = v[1] + ((x * sr + z * sf) >> 12);
                int wx = (s.x >> 4) + ((x * co + z * sn) >> 14), wz = (s.z >> 4) + ((z * co - x * sn) >> 14), wy = (s.y >> 4) + 3 + y;
                TmGroundHit h;
                if (g.Probe(wx * 16, wz * 16, s.y + 320, h))
                    {
                    const int d = h.y - wy * 16;           // 1/256 m: the road over the corner
                    if (d > deepest) deepest = d;
                    }
                }
            steps++;
            if (deepest > 5) { clipSteps++; sumDepth += deepest / 256.0; if (deepest > worst) { worst = deepest; worstStep = step; } }
            if (car.iFinished || step == maxsec * 50 - 1 || getenv("CLIPALL")) {}
            if (step % 500 == 499) printf("clip: %d of %d steps with the road over a corner of the car (mean %.3f m), worst %.3f m at step %d; car bottom %d/16 m\n", clipSteps, steps, clipSteps ? sumDepth / clipSteps : 0.0, worst / 256.0, worstStep, ymin);
            }
        int cx, cy, cz, yaw, pitch;
        cam.Update(g, car.iS, cx, cy, cz, yaw, pitch);
        if (getenv("CAMLOG")) printf("cam %d: %d %d %d yaw %d pitch %d car y %d speed %d g %d\n", step, cx, cy, cz, yaw, pitch, s.y, s.speed, s.onGround);
        if (step % 50 == 0)
            {
            // the replay's place at this time, for the comparison
            int rs = step / 5; if (rs >= t.iNPath) rs = t.iNPath - 1;
            const TmPath& r = t.iPath[rs];
            double off = sqrt((double) (r.x * 16 - s.x) * (r.x * 16 - s.x) + (double) (r.z * 16 - s.z) * (r.z * 16 - s.z)) / 256;
            printf("t %3d s: car (%6.1f,%5.1f,%6.1f) v %5.1f m/s %s surf %d steer %4d%s%s | path sample %d (%.0f,%.0f) replay is %.0f m away | cps %d\n",
                step / 50, s.x / 256.0, s.y / 256.0, s.z / 256.0, s.speed / 256.0, s.onGround ? "ground" : "AIR   ", s.surf, in.steer,
                in.brake ? " BRAKE" : "", in.throttle ? "" : " coast", prog, t.iPath[prog].x / 16.0, t.iPath[prog].z / 16.0, off, car.iCheckpoints);
            }
        if (every && step % every == 0)
            {
            s.Slopes(v.iCarSlopeF, v.iCarSlopeR);
            if (getenv("GHOST")) { v.iGhostOn = ghost.At(step + 1, v.iGhostX, v.iGhostY, v.iGhostZ, v.iGhostYaw, v.iGhostSlopeF, v.iGhostSlopeR) ? 1 : 0; }
            v.Frame(frame, 320, t, cx, cy, cz, yaw, pitch, 200, s.x >> 4, (s.y >> 4) + 3, s.z >> 4, s.yaw, 1);
            char name[256]; sprintf(name, "%s_%04d.ppm", argv[2], step);
            Save(name, frame, 320, 200); frames++;
#ifdef R3_REFERENCE
            if (getenv("REF"))
                {
                static R3Pix ref[320 * 200];
                for (int i = 0; i < 320 * 200; i++) ref[i] = frame[i];
                v.iR->ReferenceFrame(ref, 320);
                // the car's neighbourhood: the middle third of the lower half
                int diff = 0, carDiff = 0;
                for (int i = 0; i < 320 * 200; i++) if (ref[i] != frame[i]) { diff++; int x = i % 320, y = i / 320; if (x > 100 && x < 220 && y > 90 && y < 170) carDiff++; }
                printf("ref %d: %d pixels differ, %d around the car\n", step, diff, carDiff);
                sprintf(name, "%s_%04d_ref.ppm", argv[2], step); Save(name, ref, 320, 200);
                if (getenv("PIX") && step == atoi(getenv("PIXSTEP"))) { int px, py; sscanf(getenv("PIX"), "%d,%d", &px, &py); v.iR->DebugPixel(px, py); }
                }
#endif
            }
        }
    if (getenv("GAP"))
        {
        // where the time went: at every 10th sample of the replay, our time there against the replay's, and the speeds
        printf("gap by replay second (our time - replay time, s; our km/h / replay km/h):\n");
        for (int i = 10; i < t.iNPath && i < 20000; i += 10)
            if (reach[i] >= 0)
                {
                double dx = t.iPath[i].x - t.iPath[i - 1].x, dz = t.iPath[i].z - t.iPath[i - 1].z;
                printf("  %4.1f s: %+5.2f\n", i / 10.0, reach[i] / 50.0 - i / 10.0);
                (void) dx; (void) dz;
                }
        }
    if (getenv("GHOST"))
        {
        // the recorded run: its points where the car was (checked by replaying the same drive is not possible here: the
        // first point and the finish), the file form back and forth
        printf("run recorded: %d points, finish %d steps, checkpoints:", run.iN, run.Finish());
        for (int k = 0; k < car.iCheckpoints; k++) printf(" %.2f", run.CpSteps(k) / 50.0);
        int x, y, z, yaw, sf, sr; run.At(run.Finish(), x, y, z, yaw, sf, sr);
        printf("\nat the finish step the ghost is %.2f m from the car's finish place\n", sqrt((double) (x * 16 - car.iS.x) * (x * 16 - car.iS.x) + (double) (z * 16 - car.iS.z) * (z * 16 - car.iS.z)) / 256);
        unsigned char* buf = new unsigned char[run.Size()]; run.Write(buf);
        TmGhost back; bool ok = back.Load(buf, run.Size());
        int x2, y2, z2, yaw2, sf2, sr2; back.At(777, x, y, z, yaw, sf, sr); run.At(777, x2, y2, z2, yaw2, sf2, sr2);
        printf("file round trip: %s, %d bytes, same at step 777: %s\n", ok ? "ok" : "FAILED", run.Size(), (x == x2 && y == y2 && z == z2 && yaw == yaw2 && sf == sf2 && back.Finish() == run.Finish()) ? "yes" : "NO");
        delete[] buf;
        }
    printf("%s in %.2f s (the replay: %.1f s); %d checkpoints of %d; %d walls hit; %d probes, %d triangle tests (%.0f a probe); %d frames\n",
        car.iFinished ? "FINISHED" : "not finished", car.iFinishSteps / 50.0, t.iNPath / 10.0, car.iCheckpoints, car.iTotalCps, car.iWallHits, g.iProbes, g.iTests, g.iProbes ? (double) g.iTests / g.iProbes : 0.0, frames);
    return 0;
    }
