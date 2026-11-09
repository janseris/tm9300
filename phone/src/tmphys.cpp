// tmphys.cpp - see tmphys.h
#include "tmphys.h"

static inline int Abs(int a) { return a < 0 ? -a : a; }
static inline int Clamp(int a, int lo, int hi) { return a < lo ? lo : a > hi ? hi : a; }
static inline int ILen(int a, int b) { a = Abs(a); b = Abs(b); return a > b ? a + (b >> 1) : b + (a >> 1); }

// the yaw (0..4095, R3D's) of the direction (aDx, aDz): a table search in the first quadrant, then the quadrant
int TmYawTo(int aDx, int aDz)
    {
    int ax = Abs(aDx), az = Abs(aDz);
    if (ax == 0 && az == 0) return 0;
    while (ax > 0x7fff || az > 0x7fff) { ax >>= 1; az >>= 1; }
    // the yaw in the first quadrant where sin(y) * az - cos(y) * ax changes sign: a bisection to the unit
    int lo = 0, hi = 1024;
    while (hi - lo > 1)
        {
        int mid = (lo + hi) >> 1;
        int e = R3D::Sin(mid) * az - R3D::Cos(mid) * ax;
        if (e < 0) lo = mid; else hi = mid;
        }
    int yaw = (R3D::Sin(hi) * az - R3D::Cos(hi) * ax) + (R3D::Sin(lo) * az - R3D::Cos(lo) * ax) < 0 ? hi : lo;
    if (aDx >= 0 && aDz < 0) yaw = 2048 - yaw;
    else if (aDx < 0 && aDz < 0) yaw = 2048 + yaw;
    else if (aDx < 0 && aDz >= 0) yaw = 4096 - yaw;
    return yaw & 4095;
    }

// ---- the ground --------------------------------------------------------------------------------------------

TmGround::TmGround() : iProbes(0), iTests(0), iT(0), iCellX0(0), iCellZ0(0), iCellsX(0), iCellsZ(0), iFirst(0), iList(0), iGrid(0), iGridList(0) {}
TmGround::~TmGround() { delete[] iFirst; delete[] iList; delete[] iGrid; delete[] iGridList; }

bool TmGround::Init(const TmTrack& aT)
    {
    iT = &aT;
    delete[] iFirst; delete[] iList; iFirst = 0; iList = 0;
    if (aT.iNCInst == 0) { iCellsX = iCellsZ = 0; return true; }
    int x0 = 0x7fffffff, z0 = 0x7fffffff, x1 = -0x7fffffff, z1 = -0x7fffffff;
    for (int i = 0; i < aT.iNCInst; i++)
        {
        const TmCInst& c = aT.iCInst[i];
        if (c.bb[0] < x0) x0 = c.bb[0]; if (c.bb[2] < z0) z0 = c.bb[2];
        if (c.bb[3] > x1) x1 = c.bb[3]; if (c.bb[5] > z1) z1 = c.bb[5];
        }
    iCellX0 = x0 >> 9; iCellZ0 = z0 >> 9;
    iCellsX = (x1 >> 9) - iCellX0 + 1; iCellsZ = (z1 >> 9) - iCellZ0 + 1;
    int cells = iCellsX * iCellsZ;
    iFirst = new int[cells + 1];
    if (!iFirst) return false;
    for (int k = 0; k <= cells; k++) iFirst[k] = 0;
    // count, then place (an instance in every cell its box touches)
    int total = 0;
    for (int pass = 0; pass < 2; pass++)
        {
        if (pass == 1)
            {
            int sum = 0;
            for (int k = 0; k < cells; k++) { int n = iFirst[k]; iFirst[k] = sum; sum += n; }
            iFirst[cells] = sum; total = sum;
            iList = new int[total > 0 ? total : 1];
            if (!iList) return false;
            }
        for (int i = 0; i < aT.iNCInst; i++)
            {
            const TmCInst& c = aT.iCInst[i];
            int cx0 = (c.bb[0] >> 9) - iCellX0, cx1 = (c.bb[3] >> 9) - iCellX0, cz0 = (c.bb[2] >> 9) - iCellZ0, cz1 = (c.bb[5] >> 9) - iCellZ0;
            for (int cz = cz0; cz <= cz1; cz++)
                for (int cx = cx0; cx <= cx1; cx++)
                    {
                    int k = cz * iCellsX + cx;
                    if (pass == 0) iFirst[k]++;
                    else iList[iFirst[k]++] = i;
                    }
            }
        }
    // iFirst was advanced by the placing: back to the starts
    for (int k = cells; k > 0; k--) iFirst[k] = iFirst[k - 1];
    iFirst[0] = 0;
    // the meshes' face grids: a face in every bucket its box touches
    delete[] iGrid; delete[] iGridList; iGrid = 0; iGridList = 0;
    iGrid = new TMeshGrid[aT.iNColl > 0 ? aT.iNColl : 1];
    if (!iGrid) return false;
    for (int pass = 0; pass < 2; pass++)
        {
        if (pass == 1)
            {
            // the counts into offsets, the list allocated
            int pos = 0;
            for (int mi = 0; mi < aT.iNColl; mi++)
                {
                TMeshGrid& g = iGrid[mi];
                for (int k = 0; k < KGrid * KGrid; k++) { int n = g.first[k]; g.first[k] = pos; pos += n; }
                g.first[KGrid * KGrid] = pos;
                }
            iGridList = new unsigned short[pos > 0 ? pos : 1];
            if (!iGridList) return false;
            }
        for (int mi = 0; mi < aT.iNColl; mi++)
            {
            const TmColl& m = aT.iColl[mi];
            TMeshGrid& g = iGrid[mi];
            g.x0 = m.bb[0]; g.z0 = m.bb[2];
            g.dx = (m.bb[3] - m.bb[0]) / KGrid + 1; g.dz = (m.bb[5] - m.bb[2]) / KGrid + 1;
            g.list = iGridList;
            const int start = g.first[0];
            if (pass == 0) for (int k = 0; k <= KGrid * KGrid; k++) g.first[k] = 0;
            for (int f = 0; f < m.nf; f++)
                {
                const unsigned short* idx = m.idx + f * 3;
                int fx0 = 0x7fff, fx1 = -0x7fff, fz0 = 0x7fff, fz1 = -0x7fff;
                for (int k = 0; k < 3; k++)
                    {
                    const short* v = m.v + idx[k] * 3;
                    if (v[0] < fx0) fx0 = v[0]; if (v[0] > fx1) fx1 = v[0]; if (v[2] < fz0) fz0 = v[2]; if (v[2] > fz1) fz1 = v[2];
                    }
                int bx0 = (fx0 - g.x0) / g.dx, bx1 = (fx1 - g.x0) / g.dx, bz0 = (fz0 - g.z0) / g.dz, bz1 = (fz1 - g.z0) / g.dz;
                if (bx0 < 0) bx0 = 0; if (bz0 < 0) bz0 = 0; if (bx1 >= KGrid) bx1 = KGrid - 1; if (bz1 >= KGrid) bz1 = KGrid - 1;
                for (int bz = bz0; bz <= bz1; bz++)
                    for (int bx = bx0; bx <= bx1; bx++)
                        {
                        int b = bz * KGrid + bx;
                        if (pass == 0) g.first[b]++;
                        else iGridList[g.first[b]++] = (unsigned short) f;
                        }
                }
            // the cursors were advanced by the placing: back to the starts
            if (pass == 1) { for (int k = KGrid * KGrid; k > 0; k--) g.first[k] = g.first[k - 1]; g.first[0] = start; }
            }
        }
    return true;
    }

bool TmGround::FaceAt(int aCInst, int aFace, int aX, int aZ, int aYMax, TmGroundHit& aHit) const
    {
    const TmCInst& c = iT->iCInst[aCInst];
    const TmColl& m = iT->iColl[c.coll];
    const int px = aX - c.x * 16, pz = aZ - c.z * 16;
    const unsigned short* idx = m.idx + aFace * 3; const short* nrm = m.nrm + aFace * 3;
    const int ny = nrm[1];
    if (ny < 2048) return false;                                    // steeper than ~75 degrees: a wall, not ground
    const short* a = m.v + idx[0] * 3; const short* b = m.v + idx[1] * 3; const short* cc = m.v + idx[2] * 3;
    const int ax = a[0] * 16, az = a[2] * 16, bx = b[0] * 16, bz = b[2] * 16, cx2 = cc[0] * 16, cz2 = cc[2] * 16;
    // inside in x, z: the three edge sides agree
    const int s1 = (bx - ax) * (pz - az) - (bz - az) * (px - ax);
    const int s2 = (cx2 - bx) * (pz - bz) - (cz2 - bz) * (px - bx);
    if ((s1 ^ s2) < 0 && s1 != 0 && s2 != 0) return false;
    const int s3 = (ax - cx2) * (pz - cz2) - (az - cz2) * (px - cx2);
    if (((s1 ^ s3) < 0 && s1 != 0 && s3 != 0) || ((s2 ^ s3) < 0 && s2 != 0 && s3 != 0)) return false;
    const int h = c.y * 16 + a[1] * 16 + (nrm[0] * (ax - px) + nrm[2] * (az - pz)) / ny;
    if (h > aYMax) return false;
    aHit.y = h; aHit.nx = nrm[0]; aHit.ny = ny; aHit.nz = nrm[2]; aHit.surf = m.surf[aFace]; aHit.cinst = aCInst; aHit.face = aFace;
    return true;
    }

bool TmGround::Probe(int aX, int aZ, int aYMax, int aLastY, TmGroundHit& aHit) const
    {
    if (aHit.cinst >= 0 && aHit.cinst < iT->iNCInst && aHit.face >= 0 && aHit.face < iT->iColl[iT->iCInst[aHit.cinst].coll].nf)
        {
        TmGroundHit h;
        if (FaceAt(aHit.cinst, aHit.face, aX, aZ, aYMax, h) && h.y - aLastY <= 128 && h.y - aLastY >= -128) { aHit = h; ((TmGround*) this)->iProbes++; return true; }
        }
    return Probe(aX, aZ, aYMax, aHit);
    }

bool TmGround::Probe(int aX, int aZ, int aYMax, TmGroundHit& aHit) const
    {
    if (!iT || !iFirst) return false;
    int cx = ((aX >> 4) >> 9) - iCellX0, cz = ((aZ >> 4) >> 9) - iCellZ0;
    if (cx < 0 || cz < 0 || cx >= iCellsX || cz >= iCellsZ) return false;
    const int x16 = aX >> 4, z16 = aZ >> 4;
    int best = -0x7fffffff; bool found = false;
    ((TmGround*) this)->iProbes++;
    for (int k = iFirst[cz * iCellsX + cx]; k < iFirst[cz * iCellsX + cx + 1]; k++)
        {
        const TmCInst& c = iT->iCInst[iList[k]];
        if (x16 < c.bb[0] || x16 > c.bb[3] || z16 < c.bb[2] || z16 > c.bb[5] || c.bb[1] * 16 > aYMax) continue;
        const TmColl& m = iT->iColl[c.coll];
        // the point from the instance's origin, 1/256 m
        const int px = aX - c.x * 16, pz = aZ - c.z * 16, py0 = c.y * 16;
        // the bucket of the mesh's grid the point is in
        const TMeshGrid& g = iGrid[c.coll];
        int bx = ((px >> 4) - g.x0) / g.dx, bz = ((pz >> 4) - g.z0) / g.dz;
        if (bx < 0 || bz < 0 || bx >= KGrid || bz >= KGrid) continue;
        const int b = bz * KGrid + bx;
        ((TmGround*) this)->iTests += g.first[b + 1] - g.first[b];
        for (int li = g.first[b]; li < g.first[b + 1]; li++)
            {
            const int f = g.list[li];
            const unsigned short* idx = m.idx + f * 3; const short* nrm = m.nrm + f * 3;
            const int ny = nrm[1];
            if (ny < 3686 || m.surf[f] == 9) continue;              // steeper than ~63 degrees or a wall: not ground
            const short* a = m.v + idx[0] * 3; const short* b = m.v + idx[1] * 3; const short* cc = m.v + idx[2] * 3;
            const int ax = a[0] * 16, az = a[2] * 16, bx = b[0] * 16, bz = b[2] * 16, cx2 = cc[0] * 16, cz2 = cc[2] * 16;
            // inside in x, z: the three edge sides agree
            const int s1 = (bx - ax) * (pz - az) - (bz - az) * (px - ax);
            const int s2 = (cx2 - bx) * (pz - bz) - (cz2 - bz) * (px - bx);
            if ((s1 ^ s2) < 0 && s1 != 0 && s2 != 0) continue;
            const int s3 = (ax - cx2) * (pz - cz2) - (az - cz2) * (px - cx2);
            if (((s1 ^ s3) < 0 && s1 != 0 && s3 != 0) || ((s2 ^ s3) < 0 && s2 != 0 && s3 != 0)) continue;
            // the plane's height there
            const int h = py0 + a[1] * 16 + (nrm[0] * (ax - px) + nrm[2] * (az - pz)) / ny;
            if (h > aYMax || h <= best) continue;
            best = h; found = true;
            aHit.y = h; aHit.nx = nrm[0]; aHit.ny = ny; aHit.nz = nrm[2]; aHit.surf = m.surf[f]; aHit.cinst = iList[k]; aHit.face = f;
            }
        }
    if (!found) { aHit.cinst = -1; aHit.face = -1; }
    return found;
    }

bool TmGround::WallHit(int aX0, int aZ0, int aX1, int aZ1, int aY, TmGroundHit& aHit, int aDY) const
    {
    if (!iT || !iFirst) return false;
    const int dx = aX1 - aX0, dz = aZ1 - aZ0;
    if (dx == 0 && dz == 0 && aDY == 0) return false;
    int bestT = 4097; bool found = false;
    const int y16 = aY >> 4;
    // the cells of the move's two ends (a move of a metre can cross into the next)
    int cells[2]; int ncells = 0;
    for (int e = 0; e < 2; e++)
        {
        const int x = e ? aX1 : aX0, z = e ? aZ1 : aZ0;
        int cx = ((x >> 4) >> 9) - iCellX0, cz = ((z >> 4) >> 9) - iCellZ0;
        if (cx < 0 || cz < 0 || cx >= iCellsX || cz >= iCellsZ) continue;
        const int cell = cz * iCellsX + cx;
        if (ncells == 1 && cells[0] == cell) continue;
        cells[ncells++] = cell;
        }
    for (int ci = 0; ci < ncells; ci++)
        for (int k = iFirst[cells[ci]]; k < iFirst[cells[ci] + 1]; k++)
        {
        const TmCInst& c = iT->iCInst[iList[k]];
        const int x16 = aX1 >> 4, z16 = aZ1 >> 4;
        if (x16 < c.bb[0] - 64 || x16 > c.bb[3] + 64 || z16 < c.bb[2] - 64 || z16 > c.bb[5] + 64 || y16 < c.bb[1] || y16 > c.bb[4]) continue;
        // the same instance from the other cell: once
        bool dup = false;
        for (int cj = 0; cj < ci && !dup; cj++) for (int k2 = iFirst[cells[cj]]; k2 < iFirst[cells[cj] + 1]; k2++) if (iList[k2] == iList[k]) { dup = true; break; }
        if (dup) continue;
        const TmColl& m = iT->iColl[c.coll];
        const int px0 = aX0 - c.x * 16, pz0 = aZ0 - c.z * 16, py = aY - c.y * 16;
        const TMeshGrid& g = iGrid[c.coll];
        // the mesh's buckets at both ends of the move
        int bk[2]; int nb = 0;
        for (int e = 0; e < 2; e++)
            {
            int bx = (((e ? aX1 : aX0) - c.x * 16) >> 4); bx = (bx - g.x0) / g.dx; int bz = (((e ? aZ1 : aZ0) - c.z * 16) >> 4); bz = (bz - g.z0) / g.dz;
            if (bx < 0 || bz < 0 || bx >= KGrid || bz >= KGrid) continue;
            const int b = bz * KGrid + bx;
            if (nb == 1 && bk[0] == b) continue;
            bk[nb++] = b;
            }
        for (int bi = 0; bi < nb; bi++)
        for (int li = g.first[bk[bi]]; li < g.first[bk[bi] + 1]; li++)
            {
            const int f = g.list[li];
            if (m.surf[f] != 9) continue;
            const short* nrm = m.nrm + f * 3;
            const unsigned short* idx = m.idx + f * 3;
            const short* a = m.v + idx[0] * 3;
            // the segment against the plane n . (p - a) = 0: n . (p0 - a) + t * n . d = 0
            const int ax = a[0] * 16, ay = a[1] * 16, az = a[2] * 16;
            // 0.6: the move's height change counts (a car falling past a leaning face crosses it going down)
            const int nd = (nrm[0] * (dx >> 4) + nrm[1] * (aDY >> 4) + nrm[2] * (dz >> 4));     // * 16 less (dx up to a few m)
            if (nd >= 0) continue;                                              // moving away from or along the face's front
            const int n0 = (nrm[0] * ((px0 - ax) >> 4) + nrm[1] * ((py - ay) >> 4) + nrm[2] * ((pz0 - az) >> 4));
            if (n0 < 0) continue;                                               // already behind it
            // t = -n0 / nd in 0..1, * 4096
            int t = (int) (((long long) n0 << 12) / -nd);
            if (t > 4096 || t >= bestT) continue;
            // the crossing point, inside the triangle? by the axis the normal is largest in (x or z: a wall)
            const int hx = px0 + (int) (((long long) dx * t) >> 12), hz = pz0 + (int) (((long long) dz * t) >> 12), hy = py + (int) (((long long) aDY * t) >> 12);
            const short* bb2 = m.v + idx[1] * 3; const short* cc = m.v + idx[2] * 3;
            int u0, v0, u1, v1, u2, v2, pu, pv;
            if (Abs(nrm[0]) > Abs(nrm[2])) { u0 = az; v0 = ay; u1 = bb2[2] * 16; v1 = bb2[1] * 16; u2 = cc[2] * 16; v2 = cc[1] * 16; pu = hz; pv = hy; }
            else { u0 = ax; v0 = ay; u1 = bb2[0] * 16; v1 = bb2[1] * 16; u2 = cc[0] * 16; v2 = cc[1] * 16; pu = hx; pv = hy; }
            const int s1 = ((u1 - u0) >> 2) * ((pv - v0) >> 2) - ((v1 - v0) >> 2) * ((pu - u0) >> 2);
            const int s2 = ((u2 - u1) >> 2) * ((pv - v1) >> 2) - ((v2 - v1) >> 2) * ((pu - u1) >> 2);
            if ((s1 ^ s2) < 0 && s1 != 0 && s2 != 0) continue;
            const int s3 = ((u0 - u2) >> 2) * ((pv - v2) >> 2) - ((v0 - v2) >> 2) * ((pu - u2) >> 2);
            if (((s1 ^ s3) < 0 && s1 != 0 && s3 != 0) || ((s2 ^ s3) < 0 && s2 != 0 && s3 != 0)) continue;
            bestT = t; found = true;
            aHit.y = t; aHit.nx = nrm[0]; aHit.ny = nrm[1]; aHit.nz = nrm[2]; aHit.surf = 9; aHit.cinst = iList[k]; aHit.face = f;
            }
        }
    return found;
    }

int TmGround::KindAt(int aKind, int aX, int aY, int aZ) const
    {
    if (!iT) return -1;
    const int x16 = aX >> 4, y16 = aY >> 4, z16 = aZ >> 4;
    for (int i = 0; i < iT->iNCInst; i++)
        {
        const TmCInst& c = iT->iCInst[i];
        if (c.kind != aKind) continue;
        if (x16 >= c.bb[0] && x16 <= c.bb[3] && z16 >= c.bb[2] && z16 <= c.bb[5] && y16 >= c.bb[1] - 32 && y16 <= c.bb[4] + 64) return i;
        }
    return -1;
    }

// ---- the car -----------------------------------------------------------------------------------------------

static int ISqrt(unsigned a)
    {
    unsigned r = 0, bit = 1u << 30;
    while (bit > a) bit >>= 2;
    while (bit) { if (a >= r + bit) { a -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (int) r;
    }

// the ground under the four wheels (1/256 m; the car 1.8 m wide, the axles 1.7 m before and 1.5 m behind the
// middle): the middle's height (their mean), the normal from the slopes between them, the surface under the
// rear wheels; false when none of them is over anything
static bool WheelsGround(const TmGround& aG, int aX, int aZ, int aYMax, int aSn, int aCo, TmGroundHit& aOut, TmGroundHit* aLast, int aLastY, int& aTop, int aYMin)
    {
    // the right vector (co, -sn) and the heading (sn, co), * 16384
    const int off[4][2] = { { 435, 230 }, { 435, -230 }, { -384, 230 }, { -384, -230 } };    // (along, right) 1/256 m
    int hy[4]; int n = 0, sum = 0, surfR = -1, surfF = -1, bestNy = 8192;
    for (int w = 0; w < 4; w++)
        {
        int px = aX + ((off[w][0] * aSn + off[w][1] * aCo) >> 14);
        int pz = aZ + ((off[w][0] * aCo - off[w][1] * aSn) >> 14);
        TmGroundHit& h = aLast[w];
        // 0.8: a wheel over an edge finds what is far below (the water under a bridge): that is "over nothing", not
        // ground (the mean dropped the car off a narrow road with a wheel over its side)
        if (aG.Probe(px, pz, aYMax, aLastY, h) && h.y >= aYMin) { hy[w] = h.y; n++; sum += h.y; if (w >= 2) surfR = h.surf; else surfF = h.surf; if (h.ny < bestNy) bestNy = h.ny; }
        else hy[w] = 0x7fffffff;
        }
    if (n == 0) return false;
    int mean = sum / n;
    for (int w = 0; w < 4; w++) if (hy[w] == 0x7fffffff) hy[w] = mean;    // a wheel over nothing: level with the rest
    aOut.y = (hy[0] + hy[1] + hy[2] + hy[3]) >> 2;
    aTop = hy[0]; for (int w = 1; w < 4; w++) if (hy[w] > aTop) aTop = hy[w];
    // the slopes (* 4096): along from the axles 3.2 m apart, across from the wheels 1.8 m apart
    int sF = (((hy[0] + hy[1]) - (hy[2] + hy[3])) * 4096) / (2 * 819);
    int sR = (((hy[0] + hy[2]) - (hy[1] + hy[3])) * 4096) / (2 * 460);
    if (sF > 4096) sF = 4096; if (sF < -4096) sF = -4096; if (sR > 4096) sR = 4096; if (sR < -4096) sR = -4096;
    // n = up - sF * heading - sR * right, normalised to 8192
    int nx = -((sF * aSn + sR * aCo) >> 14), nz = -((sF * aCo - sR * aSn) >> 14), ny = 4096;
    int len = ISqrt((unsigned) (nx * nx + ny * ny + nz * nz)); if (len < 1) len = 1;
    aOut.nx = (nx * 8192) / len; aOut.ny = (ny * 8192) / len; aOut.nz = (nz * 8192) / len;
    aOut.surf = surfR >= 0 ? surfR : surfF;
    aOut.cinst = -1; aOut.face = -1;
    (void) bestNy;
    return true;
    }

// the throttle's pull by speed (10 m/s bands), 1/256 m/s a step (m/s^2 * 5.12): the replays' 80th percentile on
// level ground, a row per car class (Rally, Snow, Desert, Bay, Island; the last three provisional). Negative past
// the car's top speed on the flat: Rally's is ~60 m/s (the replays: 0-100 km/h in a second, ~215 km/h at most on
// the flat; the 80th percentile kept it climbing to 80 m/s on downhills)
static const int KPull[5][10] =
    {
    { 230, 135, 89, 30, 30, 24, 20, 20, 20, 18 },       // 0.8: Rally: the replays' median tyre force along the car on straights (slope out)
    { 153, 127, 46, 25, 12, 12, 12, 8, 8, 8 },          // 0.8: Snow, the same way
    { 150, 150, 132, 50, 50, 45, 45, 45, 46, 36 },      // 0.15: Desert, the replays' medians on the road (sand: a fifth)
    { 200, 253, 223, 61, 61, 60, 45, 45, 45, 0 },
    { 96, 92, 87, 190, 195, 248, 222, 230, 229, 171 },
    };
// the full-lock turning rate by speed band, in 1/16 yaw units a step (rad/s * 208.6): the replays' 95th percentile
static const int KTurn[5][10] =
    {
    { 396, 396, 396, 344, 313, 292, 280, 280, 280, 280 },   // 0.8: on at high speed (Nadeo: radius 5 m + 0.7 s * speed)
    { 261, 392, 434, 455, 467, 477, 484, 488, 492, 494 },   // 0.8: Snow, Nadeo's Steer02 radius 2 m + 0.4 s * speed
    { 300, 330, 370, 412, 381, 362, 339, 336, 364, 331 },   // 0.15: Desert, the replays' 98th percentile
    { 288, 359, 263, 204, 192, 236, 298, 323, 363, 302 },
    { 81, 229, 315, 503, 421, 330, 321, 194, 186, 165 },
    };
// by surface: the pull (/256), the extra drag (/256), the lateral grip's keep factor a step (/256), the turning (/256)
static const int KSurfPull[9] = { 256, 250, 150, 200, 80, 256, 256, 110, 190 };
static const int KSurfDrag[9] = { 256, 300, 600, 300, 1200, 256, 256, 200, 500 };
static const int KSurfKeep[9] = { 218, 228, 240, 218, 243, 256, 218, 248, 232 };
// 0.6: the sideways grip by class, 1/256 m/s a step (m/s^2 * 5.12), fitted to the replays' slip angles; by surface
// (/256: road, dirt, grass, rock, water, none, turbo, ice, snow); the widest slide (tan * 256); the speed a slide
// costs (/4096 of the sideways speed a step)
static const int KGrip[5] = { 297, 740, 340, 300, 300 };      // 0.15: Desert 66 m/s^2 (the replays' 90th percentile, flat)
static const int KSurfGrip[9] = { 256, 238, 170, 240, 120, 256, 256, 8, 50 };
static const int KSlipMax[5] = { 160, 80, 128, 128, 110 };
static const int KScrub[5] = { 40, 64, 48, 48, 48 };
static const int KSurfTurn[9] = { 256, 240, 205, 256, 180, 256, 256, 128, 210 };

void TmCarState::Slopes(int& aAlong, int& aRight) const
    {
    const int sn = R3D::Sin(yaw), co = R3D::Cos(yaw);
    int ny2 = ny < 1024 ? 1024 : ny;
    aAlong = -(((sn * nx + co * nz) >> 14) * 4096) / ny2;
    aRight = -(((co * nx - sn * nz) >> 14) * 4096) / ny2;
    if (aAlong > 4096) aAlong = 4096; if (aAlong < -4096) aAlong = -4096;
    if (aRight > 4096) aRight = 4096; if (aRight < -4096) aRight = -4096;
    }

TmCar::TmCar() : iClass(0), iCheckpoints(0), iCheckMask(0), iFinished(0), iSteps(0), iFinishSteps(0), iLastKindInst(-1), iTotalCps(0), iWallHits(0), iTopY(0), iCpX(0), iCpY(0), iCpZ(0), iCpYaw(0), iGravity(40 * 256), iBumpY(0), iAirSteps(0), iClimbVy(0), iClimbRaw(0), iTurbo(0)
    {
    iS.x = iS.y = iS.z = 0; iS.vx = iS.vy = iS.vz = 0; iS.yaw = iS.yaw16 = 0; iS.onGround = 1; iS.surf = 0;
    iS.nx = 0; iS.ny = 8192; iS.nz = 0; iS.speed = 0; iS.steerPos = 0;
    }

void TmCar::Reset(const TmTrack& aT)
    {
    iClass = aT.iCarClass >= 0 && aT.iCarClass < 5 ? aT.iCarClass : 0;
    iS.x = aT.iStartX * 16; iS.y = aT.iStartY * 16; iS.z = aT.iStartZ * 16;
    iS.vx = iS.vy = iS.vz = 0;
    iS.yaw = aT.iStartYaw & 4095; iS.yaw16 = iS.yaw * 16;
    iS.onGround = 1; iS.surf = 0; iS.nx = 0; iS.ny = 8192; iS.nz = 0; iS.speed = 0; iS.steerPos = 0;
    iCheckpoints = 0; iCheckMask = 0; iFinished = 0; iSteps = 0; iFinishSteps = 0; iLastKindInst = -1;
    // 0.6: the checkpoints: the checkpoint boxes on one spot (footprints overlapping by more than a metre each way:
    // an elevated checkpoint and the pillars under it) are one checkpoint
    iTotalCps = 0; iNCp = 0;
    for (int i = 0; i < aT.iNCInst && iNCp < KMaxCpBoxes; i++)
        {
        const TmCInst& c = aT.iCInst[i];
        if (c.kind != 2) continue;
        int grp = -1;
        for (int k = 0; k < iNCp && grp < 0; k++)
            {
            const TmCInst& o = aT.iCInst[iCpInst[k]];
            const int ox = (c.bb[3] < o.bb[3] ? c.bb[3] : o.bb[3]) - (c.bb[0] > o.bb[0] ? c.bb[0] : o.bb[0]);
            const int oz = (c.bb[5] < o.bb[5] ? c.bb[5] : o.bb[5]) - (c.bb[2] > o.bb[2] ? c.bb[2] : o.bb[2]);
            if (ox > 16 && oz > 16) grp = iCpGroup[k];
            }
        if (grp < 0) { if (iTotalCps >= 32) continue; grp = iTotalCps++; }
        iCpInst[iNCp] = i; iCpGroup[iNCp] = grp; iNCp++;
        }
    iBumpY = iS.y;
    for (int w = 0; w < 5; w++) { iLast[w].cinst = -1; iLast[w].face = -1; }
    iCpX = iS.x; iCpY = iS.y; iCpZ = iS.z; iCpYaw = iS.yaw; iTopY = iS.y; iAirSteps = 0; iClimbVy = 0; iClimbRaw = 0; iTurbo = 0;
    }

void TmCar::Respawn()
    {
    // standing at the last checkpoint taken (the start when none), the race going on
    iS.x = iCpX; iS.y = iCpY; iS.z = iCpZ; iS.yaw = iCpYaw & 4095; iS.yaw16 = iS.yaw * 16;
    iS.vx = iS.vy = iS.vz = 0; iS.onGround = 1; iS.surf = 0; iS.nx = 0; iS.ny = 8192; iS.nz = 0; iS.speed = 0; iS.steerPos = 0;
    iBumpY = iS.y; iTopY = iS.y; iAirSteps = 0; iClimbVy = 0; iClimbRaw = 0; iTurbo = 0;
    for (int w = 0; w < 5; w++) { iLast[w].cinst = -1; iLast[w].face = -1; }
    }

void TmCar::Step(const TmGround& aG, const TmCarInput& aIn)
    {
    TmCarState& s = iS;
    if (!iFinished) iSteps++;
    // the steering wheel: to the key's side in ~100 ms, back to the middle faster
    int want = Clamp(aIn.steer, -256, 256);
    if (want > s.steerPos) s.steerPos = want - s.steerPos > 48 ? s.steerPos + 48 : want;
    else if (want < s.steerPos) s.steerPos = s.steerPos - want > 48 ? s.steerPos - 48 : want;
    const int sn = R3D::Sin(s.yaw), co = R3D::Cos(s.yaw);         // the heading (* 16384)
    if (s.onGround)
        {
        int surf = s.surf; if (surf < 0 || surf > 8) surf = 0;
        // the speed along the heading and across it
        int v = (s.vx * sn + s.vz * co) >> 14;
        int l = (s.vx * co - s.vz * sn) >> 14;
        int band = Abs(v) >> 8; band /= 10; if (band > 9) band = 9;
        int a = 0;
        // rolling and air drag: 2.5 m/s^2 + 0.0005 v^2 (v m/s); the pull table is the net gain on a road at
        // full throttle (the replays' figures have the drag in them), so the drag counts when coasting, and
        // soft ground adds its share
        int vm = Abs(v) >> 8;
        int drag = 13 + ((vm * vm * 26) >> 10);
        int soft = (drag * (KSurfDrag[surf] - 256)) >> 8;
        if (aIn.throttle && !aIn.brake) a = ((KPull[iClass][band] * KSurfPull[surf]) >> 8) - soft;
        else drag += soft;
        if (aIn.brake)
            {
            if (v > 128) a -= 82;                                   // 16 m/s^2
            else if (v > -15 * 256) a -= 41;                        // reverse, to 15 m/s
            }
        else if (!aIn.throttle) { if (v > 0) a -= drag < v ? drag : v; else if (v < 0) a += drag < -v ? drag : -v; }
        // a turbo pad: 0.16: Desert's a boost of 17.6 m/s^2 for a second from the pad on (its replays: 71 -> 97 m/s over
        // the pad and the 40 m after it; was 58 m/s^2 on the pad only)
        // (Rally and Snow as before: their replays fit the pad's push)
        if (iClass != 2) { if (surf == 6) a += 300; }
        else { if (surf == 6) iTurbo = 50; if (iTurbo > 0) { a += 90; iTurbo--; } }
        // the slope: g along the heading (f . n) * ny. 0.8: the game's gravity on the ground (29.4 m/s^2: GroundGravityCoef 3;
        // the replays' climbs fit 29.6 for Rally, 28.9 for Snow), 150 a step; was 9.8 m/s^2 (the pull table now leaves the slope
        // out, measured with 27)
        int fn = (sn * s.nx + co * s.nz) >> 14;
        a += ((((fn * s.ny) >> 13) * 150) >> 13);
        v = Clamp(v + a, -20 * 256, (iClass == 2 ? 140 : 100) * 256);       // 0.16: Desert's 140 m/s (its car reaches 120 in the replays; the others 100)
        // the turning, and the grip: the sideways speed dies away (a slide on soft ground), scrubbing some speed
        int turn = (KTurn[iClass][band] * KSurfTurn[surf]) >> 8;
        if (band == 0) { int t = (Abs(v) * 42) >> 8; if (t < turn) turn = t; }
        int dyaw = (s.steerPos * turn) >> 8;
        if (v < 0) dyaw = -dyaw;
        s.yaw16 = (s.yaw16 + dyaw) & 0xffff; s.yaw = s.yaw16 >> 4;
        // 0.6: the car turns, its momentum does not: the velocity (the old heading's v and l) is taken as it is and
        // split along the new heading; the tyres take out at most the class's grip of the sideways speed a step
        // (m/s^2, by surface), so a hard turn at speed builds a slide that dies away when the grip catches up. The
        // Rally car's grip is a quarter of the Snow car's, as Nadeo's tunings (MaxSideFriction 25 against 100) and
        // the replays' slip angles (Rally 10-30 degrees in a turn, Snow 2-8)
        const int sn2 = R3D::Sin(s.yaw), co2 = R3D::Cos(s.yaw);
        {
        const int wx = (v * sn + l * co) >> 14, wz = (v * co - l * sn) >> 14;
        v = (wx * sn2 + wz * co2) >> 14;
        l = (wx * co2 - wz * sn2) >> 14;
        }
        const int grip = (KGrip[iClass] * KSurfGrip[surf]) >> 8;
        if (l > grip) l -= grip; else if (l < -grip) l += grip; else l = 0;
        // the slide no wider than the class's angle (TrackMania's cars do not spin out on their own)
        const int lmax = (Abs(v) * KSlipMax[iClass]) >> 8;
        if (l > lmax) l = lmax; else if (l < -lmax) l = -lmax;
        // sliding costs speed: the tyres' sideways work
        int scrub = (Abs(l) * KScrub[iClass]) >> 12;
        if (v > 0) v -= scrub < v ? scrub : v; else if (v < 0) v += scrub < -v ? scrub : -v;
        s.vx = (v * sn2 + l * co2) >> 14;
        s.vz = (v * co2 - l * sn2) >> 14;
        s.vy = -((s.vx * s.nx + s.vz * s.nz) / s.ny);               // along the slope
        s.speed = v;
        }
    else
        {
        s.vy -= (iGravity * 256 / 50) >> 8;                         // 1/256 m/s a step
        s.speed = (s.vx * sn + s.vz * co) >> 14;
        }
    // the move, then the ground there
    int nx = s.x + s.vx / 50, ny = s.y + s.vy / 50, nz = s.z + s.vz / 50;
    // a wall across the way (at 0.25, 0.6 and 1.4 m up: a road's rail, a bank, a wall): stopped just short of it,
    // the speed into it taken out and a little given back (a bounce), the rest slides along
    for (int pass = 0; pass < 4; pass++)
        {
        TmGroundHit w;
        int hy = s.y + (pass == 0 ? 64 : pass == 2 ? 358 : 154);
        if (!aG.WallHit(s.x, s.z, nx, nz, hy, w, ny - s.y)) { if (pass == 2) break; continue; }
        // to the wall (a hair short), then the rest of the move along it
        int t = w.y > 128 ? w.y - 128 : 0;
        int mx = nx - s.x, mz = nz - s.z;
        int hx = s.x + (int) (((long long) mx * t) >> 12), hz = s.z + (int) (((long long) mz * t) >> 12);
        int rx = mx - (int) (((long long) mx * t) >> 12), rz = mz - (int) (((long long) mz * t) >> 12);
        int rn = (rx * w.nx + rz * w.nz) >> 13;
        if (rn < 0) { rx -= (rn * w.nx) >> 13; rz -= (rn * w.nz) >> 13; }
        nx = hx + rx; nz = hz + rz;
        int vn = (s.vx * w.nx + s.vz * w.nz) >> 13;                       // the speed into the wall taken out, a fifth back
        if (vn < 0) { s.vx -= (vn * w.nx * 6 / 5) >> 13; s.vz -= (vn * w.nz * 6 / 5) >> 13; }
        s.speed = (s.vx * R3D::Sin(s.yaw) + s.vz * R3D::Cos(s.yaw)) >> 14;
        iWallHits++;
        }
    // 0.15: the car's body for the walls: its front corners (0.85 m to each side, 1.9 m out) at 0.35 m and 1 m up (the
    // rear ones in reverse), and in a slide (sideways faster than 2 m/s) the leading side's middle at 0.5 m (its rear corner
    // too snagged the castle roads in a drift): a post, a pole or a log the middle's line misses still stops the car where its body meets it (the
    // checkpoints' and the start's poles, the logs were driven through). A whole box at all times snagged the narrow
    // wooden roads' rails (the walls are the drawn shapes' steep faces, rougher than the game's collision)
    // (not on a bank or a wall ride: there the road itself rises past the corners)
    if (s.ny >= 7000)
    {
    const int lat = (s.vx * co - s.vz * sn) >> 14;
    const int lside = lat > 512 ? 218 : lat < -512 ? -218 : 0;
    for (int pass = 0; pass < 5; pass++)
        {
        int fwd = s.speed >= 0 ? 486 : -486, side = (pass & 1) ? 218 : -218, up = pass < 2 ? 90 : 256;
        if (pass >= 4)
            {
            if (!lside || s.ny < 7700) break;
            side = lside; up = 128; fwd = pass == 4 ? 0 : -fwd;
            }
        const int ox = (fwd * sn + side * co) >> 14, oz = (fwd * co - side * sn) >> 14;
        TmGroundHit w;
        const int px = s.x + ox, pz = s.z + oz;
        // the corner's height over the ground's plane under the car (a gentle slope rises under a corner too)
        const int py = s.y - (s.nx * ox + s.nz * oz) / s.ny;
        if (!aG.WallHit(px, pz, px + nx - s.x, pz + nz - s.z, py + up, w, ny - s.y)) continue;
        int t = w.y > 128 ? w.y - 128 : 0;
        int mx = nx - s.x, mz = nz - s.z;
        int hx = s.x + (int) (((long long) mx * t) >> 12), hz = s.z + (int) (((long long) mz * t) >> 12);
        int rx = mx - (int) (((long long) mx * t) >> 12), rz = mz - (int) (((long long) mz * t) >> 12);
        int rn = (rx * w.nx + rz * w.nz) >> 13;
        if (rn < 0) { rx -= (rn * w.nx) >> 13; rz -= (rn * w.nz) >> 13; }
        nx = hx + rx; nz = hz + rz;
        int vn = (s.vx * w.nx + s.vz * w.nz) >> 13;
        if (vn < 0) { s.vx -= (vn * w.nx * 6 / 5) >> 13; s.vz -= (vn * w.nz * 6 / 5) >> 13; }
        s.speed = (s.vx * R3D::Sin(s.yaw) + s.vz * R3D::Cos(s.yaw)) >> 14;
        iWallHits++;
        }
    }
    // 0.6: and the move as it now is crosses no wall (a slide along one face can run into the next at a corner):
    // else the car stays where it is this step (its speed into the walls was taken out above)
    {
    TmGroundHit w;
    if (aG.WallHit(s.x, s.z, nx, nz, s.y + 154, w, ny - s.y) || aG.WallHit(s.x, s.z, nx, nz, s.y + 64, w, ny - s.y)) { nx = s.x; nz = s.z; }
    }
    TmGroundHit hit;
    // 0.6: the middle's ground. The highest surface up to the car's height (1.2 m) plus the move is the candidate (higher
    // is overhead: a bridge, a roof); it is climbed when its rise is within 0.45 m plus the move times the surface's
    // OWN slope (a 45-degree bank met at speed is climbed; a rail's flat top 0.6 m up is not: a wall). 0.5 was judging
    // by the slope the car stood on, so a bank met from flat ground was a wall, the car bounced and a step later stood
    // inside the bank (the uphill sinking, the stones, Snow's banks)
    const int move = ILen(nx - s.x, nz - s.z);
    TmGroundHit& mid = iLast[4];
    bool found = s.onGround ? aG.Probe(nx, nz, ny + 307 + move, s.y, mid) : aG.Probe(nx, nz, ny + 115 + 102, s.y, mid);
    int tan = 0;
    if (found && s.onGround && mid.y - ny > 115)
        {
        tan = (ILen(mid.nx, mid.nz) * 256) / (mid.ny > 1024 ? mid.ny : 1024);           // the surface's slope, * 256
        const int allowed = 115 + ((move * tan) >> 8) + 26;
        // or the surface's plane passes through the car's own place (a ramp's foot under the car)
        const int hAtCar = mid.y - (mid.nx * ((s.x - nx) >> 4) + mid.nz * ((s.z - nz) >> 4)) / (mid.ny > 1024 ? mid.ny : 1024) * 16;
        const bool ramp = hAtCar - s.y < 77 && hAtCar - s.y > -77;
        // 0.8: a surface over 1 m above with a drivable one under it at the car's level: the car goes on under it
        // (a terrain tile left over a road, the replay's car drives under it; a solid thing over the road has its
        // steep sides as walls now, which stop the car before this)
        if (mid.y - ny > allowed && !ramp && mid.y - ny > 256)
            {
            TmGroundHit low;
            if (aG.Probe(nx, nz, mid.y - 64, s.y, low))
                {
                const int ltan = (ILen(low.nx, low.nz) * 256) / (low.ny > 1024 ? low.ny : 1024);
                if (low.y - ny <= 115 + ((move * ltan) >> 8) + 26) mid = low;
                }
            }
        if (mid.y - ny > allowed && !ramp)
            {
            // a wall: the car stops and bounces back a quarter of its speed
            int v = s.speed;
            s.vx = -(s.vx >> 2); s.vz = -(s.vz >> 2); s.speed = -(v >> 2);
            nx = s.x; nz = s.z; ny = s.y;
            found = aG.Probe(nx, nz, ny + 115 + 102, s.y, mid);
            iWallHits++;
            }
        }
    if (!found && s.onGround)
        {
        // nothing under the car within reach: inside a bank (rounding, a landing) or over an edge. A drivable surface
        // (up to 45 degrees) within 2 m above: the car is put on it; else it falls (the edge)
        TmGroundHit high;
        if (aG.Probe(nx, nz, ny + 512, high) && high.y > ny && high.ny >= 5734) { found = true; mid = high; }
        }
    // the wheels: within 0.45 m of the middle's ground plus its slope's rise over the car's length (2.4 m at most)
    int reach = 115;
    if (found) { tan = (ILen(mid.nx, mid.nz) * 256) / (mid.ny > 1024 ? mid.ny : 1024); reach += (435 * tan) >> 8; if (reach > 614) reach = 614; }
    const int wheelMax = (found ? mid.y : ny) + reach;
    const int sn3 = R3D::Sin(s.yaw), co3 = R3D::Cos(s.yaw);
    int top = ny;
    if (found && !WheelsGround(aG, nx, nz, wheelMax, sn3, co3, hit, iLast, s.y, top, mid.y - reach - 128)) { hit = mid; top = mid.y; }
    else if (found && mid.y > hit.y && mid.y - hit.y < 256) hit.y = mid.y;        // a crest under the middle: the body sits on it
    if (found)
        {
        int dh = hit.y - ny;
        if (s.onGround)
            {
            // the ground is left only when it falls away faster than a throw would, by more than 0.25 m and for two
            // steps running: the car's y a step on, by its upward speed and gravity, against the ground there (glued
            // over crests and seams otherwise, as TrackMania's cars)
            // 0.16: no grace off a ramp (going up faster than 2 m/s: the launch kept). 0.15: the one step's grace is for a seam (0.5 m at most): a platform further below is fallen to, by the
            // air's gravity, no longer put under the car at once
            // (0.16: the upward speed the ground itself gave the last step, not the slope's under the smoothed normal,
            // which lags behind a ramp's kicker: the launches were a third too low)
            if (iClimbVy > 8 * 256 && iClimbVy > s.vy) s.vy = iClimbVy;
            int throwY = s.y + s.vy / 50 - ((iGravity * 256 / 50) >> 8) / 50;
            if (hit.y >= throwY - 24 || (iAirSteps < 1 && hit.y >= throwY - 128 && s.vy < 2 * 256))
                {
                if (hit.y < throwY - 24) iAirSteps++; else iAirSteps = 0;
                // on it: the climb rate from the ground itself (a ramp's end throws the car at that rate)
                ny = hit.y; s.surf = hit.surf;
                int vy = (hit.y - s.y) * 50;
                if (vy > 40 * 256) vy = 40 * 256; if (vy < -40 * 256) vy = -40 * 256;
                // a ramp's climb is the smaller of the last two steps' (a kerb's one step up launches nothing)
                s.vy = vy; iClimbVy = vy < iClimbRaw ? vy : iClimbRaw; iClimbRaw = vy;
                // the normal smoothed over a few steps: a kerb under one wheel is not a ramp
                s.nx += (hit.nx - s.nx) >> 2; s.ny += (hit.ny - s.ny) >> 2; s.nz += (hit.nz - s.nz) >> 2;
                if (s.ny < 2048) s.ny = 2048;
                }
            else { s.onGround = 0; s.nx = 0; s.ny = 8192; s.nz = 0; iAirSteps = 0; ny = s.y + s.vy / 50; iClimbVy = 0; iClimbRaw = 0; }
            }
        else if (dh >= 0)
            {
            // landed: the speed into the ground taken out
            ny = hit.y; s.onGround = 1; s.nx = hit.nx; s.ny = hit.ny; s.nz = hit.nz; s.surf = hit.surf;
            int vn = (s.vx * s.nx + s.vy * s.ny + s.vz * s.nz) >> 13;
            if (vn < 0) { s.vx -= (vn * s.nx) >> 13; s.vy -= (vn * s.ny) >> 13; s.vz -= (vn * s.nz) >> 13; }
            }
        iBumpY = hit.y;
        }
    else if (!s.onGround)
        {
        // in the air with nothing below within reach: flown into a hill or a bank? the ground far above the car
        // (and nothing between) is hit: the car lands on it, the speed into it taken out
        TmGroundHit high;
        if (aG.Probe(nx, nz, ny + 12 * 256, high) && high.y > ny)
            {
            ny = high.y; s.onGround = 1; s.nx = high.nx; s.ny = high.ny; s.nz = high.nz; s.surf = high.surf; hit = high; top = high.y;
            int vn = (s.vx * s.nx + s.vy * s.ny + s.vz * s.nz) >> 13;
            if (vn < 0) { s.vx -= (vn * s.nx) >> 13; s.vy -= (vn * s.ny) >> 13; s.vz -= (vn * s.nz) >> 13; }
            iBumpY = ny;
            }
        else if (ny < iBumpY - 200 * 256) { ny = iBumpY; s.onGround = 1; s.vy = 0; s.nx = 0; s.ny = 8192; s.nz = 0; }
        }
    else
        {
        // off the collision meshes (the map's edge): flat ground at the last height
        ny = iBumpY; s.nx = 0; s.ny = 8192; s.nz = 0;
        }
    iTopY = top > ny ? top : ny;
    s.x = nx; s.y = ny; s.z = nz;
    Race(aG);
    }

void TmCar::Race(const TmGround& aG)
    {
    if (iFinished) return;
    int cp = aG.KindAt(2, iS.x, iS.y, iS.z);
    if (cp >= 0)
        {
        // its checkpoint (a bit each, 32 at most)
        int ord = 32;
        for (int k = 0; k < iNCp; k++) if (iCpInst[k] == cp) { ord = iCpGroup[k]; break; }
        if (ord < 32 && !(iCheckMask & (1 << ord))) { iCheckMask |= 1 << ord; iCheckpoints++; iLastKindInst = cp; iCpX = iS.x; iCpY = iS.y; iCpZ = iS.z; iCpYaw = iS.yaw; }
        }
    // the finish counts once every checkpoint is taken (and not in the first second: a start/finish line)
    int fin = aG.KindAt(3, iS.x, iS.y, iS.z);
    if (fin >= 0 && iSteps > 50 && iCheckpoints >= iTotalCps) { iFinished = 1; iFinishSteps = iSteps; iLastKindInst = fin; }
    }

// ---- the camera --------------------------------------------------------------------------------------------

// 0.15: 11 m back, 4 m up, looking at a point 30 m on and 1.5 m up: lower, the road ahead in view, as the game's
TmFollowCam::TmFollowCam() : iBack(11 * 256), iUp(4 * 256), iLookUp(384), iLookAhead(30 * 256), iX(0), iY(0), iZ(0), iYaw16(0), iViewYaw(0), iViewPitch(0), iTn(0), iTh(0) { iLast.cinst = -1; iLast.face = -1; iLast.y = 0; }

// 0.6: the camera rides on the car's own trail: a point every 0.4 m of the car's way (KTrail of them: 25 m), the camera
// 8 m back along it and 3 m over where the car was, looking at a point 1 m over the car. 0.8: 6.5 m back and 5.2 m up
// (Nadeo's cameras: 3-5.9 m up, 6-9 m back), looking 7 m past the car, and kept under a ceiling. 0.10: 7.8 m back, 5 m up. It goes where the car went
// (under a bridge or a track overhead, through a gate or a building, along a jump) and never searches for ground; a
// slide shows as a slide. Smoothed a little each step (not each frame: the same at any frame rate)
void TmFollowCam::Reset(const TmCarState& aS)
    {
    const int sn = R3D::Sin(aS.yaw), co = R3D::Cos(aS.yaw);
    // the trail seeded behind the car along its heading
    iTn = 0; iTh = 0;
    for (int k = KTrail - 1; k >= 0; k--) Push(aS.x - ((sn * k * 102) >> 14), aS.y, aS.z - ((co * k * 102) >> 14));
    int tx, ty, tz; Target(aS, tx, ty, tz);
    iX = tx; iY = ty; iZ = tz;
    iYaw16 = aS.yaw * 16; iViewYaw = aS.yaw; iViewPitch = -60;
    iLast.cinst = -1; iLast.face = -1; iLast.y = aS.y;
    }

void TmFollowCam::Push(int aX, int aY, int aZ)
    {
    iTh = (iTh + 1) % KTrail;
    iTx[iTh] = aX; iTy[iTh] = aY; iTz[iTh] = aZ;
    if (iTn < KTrail) iTn++;
    }

// the place 8 m back along the trail from the car, 3 m up (1/256 m); with too short a trail, behind the oldest
// point along the way it went
void TmFollowCam::Target(const TmCarState& aS, int& aX, int& aY, int& aZ) const
    {
    const int back = iBack;
    int px = aS.x, py = aS.y, pz = aS.z, left = back;
    int i = iTh;
    for (int n = 0; n < iTn; n++)
        {
        const int qx = iTx[i], qy = iTy[i], qz = iTz[i];
        const int d = ILen(qx - px, qz - pz);
        if (d >= left && d > 0)
            {
            // between p and q
            px += (int) (((long long) (qx - px) * left) / d); pz += (int) (((long long) (qz - pz) * left) / d); py += (int) (((long long) (qy - py) * left) / d);
            left = 0; break;
            }
        left -= d; px = qx; py = qy; pz = qz;
        i = (i + KTrail - 1) % KTrail;
        }
    if (left > 0)
        {
        // the trail ran out: on along the last piece's direction (or the car's heading)
        int dx, dz;
        if (iTn >= 2) { const int j = (i + 1) % KTrail; dx = px - iTx[j]; dz = pz - iTz[j]; } else { dx = -R3D::Sin(aS.yaw); dz = -R3D::Cos(aS.yaw); }
        const int d = ILen(dx, dz);
        if (d > 0) { px += (int) (((long long) dx * left) / d); pz += (int) (((long long) dz * left) / d); }
        }
    aX = px; aY = py + iUp; aZ = pz;
    }

void TmFollowCam::Update(const TmGround& aG, const TmCarState& aS, int& aX, int& aY, int& aZ, int& aYaw, int& aPitch)
    {
    // the trail: a point when the car is 0.4 m from the last (backing up adds none: the camera then stays behind)
    if (aS.speed >= 0 && ILen(aS.x - iTx[iTh], aS.z - iTz[iTh]) >= 102) Push(aS.x, aS.y, aS.z);
    int tx, ty, tz;
    if (aS.speed < -2 * 256)
        {
        // reversing: behind the heading
        const int sn = R3D::Sin(aS.yaw), co = R3D::Cos(aS.yaw);
        tx = aS.x - ((sn * iBack) >> 14); tz = aS.z - ((co * iBack) >> 14); ty = aS.y + iUp;
        }
    else Target(aS, tx, ty, tz);
    // 0.8: under a roof, a bridge or a track overhead the camera stays 1.5 m under it (and at least 1.2 m over the
    // trail): the first surface over the trail point, up to the camera's height + 0.5 m
    {
    TmGroundHit h;
    const int base = ty - iUp;
    // 0.16: a surface well under the camera is ground it is above (a bank, the hill behind a start), not a roof: the
    // camera stays at its height, a metre over that ground at least (it was held 1.2 m over the road at the starts)
    if (aG.Probe(tx, tz, ty + 128, h) && h.y > base + 256 && h.y < ty - 128) { if (ty < h.y + 256) ty = h.y + 256; }
    else if (aG.Probe(tx, tz, ty + 128, h) && h.y > base + 256)
        {
        int cap = h.y - 384; if (cap < base + 307) cap = base + 307;
        if (ty > cap) ty = cap;
        }
    }
    // a little smoothing a step (the trail is smooth already; the height softens the bumps)
    iX += (tx - iX) / 2; iZ += (tz - iZ) / 2; iY += (ty - iY) / 4;
    aX = iX >> 4; aY = iY >> 4; aZ = iZ >> 4;
    // the look: at a point 1 m over the car
    const int wantYaw = TmYawTo(aS.x - iX, aS.z - iZ);
    const int dyaw = ((wantYaw - iViewYaw + 2048) & 4095) - 2048;
    iViewYaw = (iViewYaw + (dyaw + (dyaw > 0 ? 1 : -1)) / 2) & 4095;
    // 0.8: the pitch to a point iLookAhead beyond the car (on the camera-to-car line) and iLookUp over it: the car
    // sits lower in the view and the road ahead shows
    int dist = ILen(aS.x - iX, aS.z - iZ) + iLookAhead; if (dist < 256) dist = 256;
    int dy = (aS.y + iLookUp) - iY;
    while (dist > 0x7fff) { dist >>= 2; dy >>= 2; }
    const int wantPitch = Clamp((dy * 652) / dist - 30, -600, 600);
    iViewPitch += (wantPitch - iViewPitch) / 2;
    aYaw = iViewYaw; aPitch = iViewPitch;
    }

// ---- the ghost (0.8) ---------------------------------------------------------------------------------------

TmGhost::TmGhost() : iN(0), iPt(0), iFinish(0), iNCp(0), iSteps(0), iLastCps(0) {}
TmGhost::~TmGhost() { delete[] iPt; }

void TmGhost::Clear() { iN = 0; iFinish = 0; iNCp = 0; iSteps = 0; iLastCps = 0; }

static void GhostPoint(TmGhostPt& aP, const TmCarState& aS)
    {
    int f, r; aS.Slopes(f, r);
    f >>= 5; r >>= 5;
    aP.x = aS.x >> 4; aP.y = aS.y >> 4; aP.z = aS.z >> 4; aP.yaw = (short) (aS.yaw & 4095);
    aP.sf = (signed char) Clamp(f, -127, 127); aP.sr = (signed char) Clamp(r, -127, 127);
    }

bool TmGhost::Begin()
    {
    Clear();
    if (!iPt) iPt = new TmGhostPt[KMaxPts];
    return iPt != 0;
    }

void TmGhost::Record(const TmCar& aCar)
    {
    if (!iPt) return;
    // the start's point (step 0) with the first record: the car stands still the first step
    if (iN == 0 && iSteps == 0) { GhostPoint(iPt[0], aCar.iS); iN = 1; }
    iSteps++;
    if (aCar.iCheckpoints > iLastCps) { while (iLastCps < aCar.iCheckpoints && iNCp < KMaxCps) { iCp[iNCp++] = iSteps; iLastCps++; } iLastCps = aCar.iCheckpoints; }
    if (aCar.iFinished && !iFinish) iFinish = aCar.iFinishSteps > 0 ? aCar.iFinishSteps : iSteps;
    // points on to 3 s past the finish (the ghost drives through the line)
    if (iFinish && iSteps > iFinish + 150) return;
    if (iSteps % 5 == 0 && iN < KMaxPts) GhostPoint(iPt[iN++], aCar.iS);
    }

bool TmGhost::FromPath(const TmTrack& aT, const TmGround& aG, const TmCar& aCarForCps)
    {
    Clear();
    if (aT.iNPath < 2) return false;
    if (!iPt) iPt = new TmGhostPt[KMaxPts];
    if (!iPt) return false;
    const int n = aT.iNPath < KMaxPts ? aT.iNPath : KMaxPts;
    for (int i = 0; i < n; i++)
        {
        const TmPath& p = aT.iPath[i];
        TmGhostPt& g = iPt[i];
        g.x = p.x; g.y = p.y; g.z = p.z; g.yaw = (short) (p.yaw & 4095);
        // the tilt along the way: the climb between the neighbours over their distance (the path has no pitch)
        const TmPath& a = aT.iPath[i > 0 ? i - 1 : i];
        const TmPath& b = aT.iPath[i + 1 < n ? i + 1 : i];
        int dx = b.x - a.x, dz = b.z - a.z, d = ILen(dx, dz);
        int f = d > 8 ? ((b.y - a.y) * 4096) / d : 0;
        g.sf = (signed char) Clamp(f >> 5, -127, 127); g.sr = 0;
        }
    iN = n;
    // the checkpoints on the way: the path every 20 ms (5 points between samples) against the checkpoints' boxes,
    // counted as the car counts them (a checkpoint's boxes are one), the finish once all are taken
    unsigned int mask = 0; int cps = 0;
    for (int i = 0; i + 1 < n && !iFinish; i++)
        {
        const TmPath& a = aT.iPath[i]; const TmPath& b = aT.iPath[i + 1];
        for (int k = 1; k <= 5 && !iFinish; k++)
            {
            const int x = (a.x + ((b.x - a.x) * k) / 5) * 16, y = (a.y + ((b.y - a.y) * k) / 5) * 16, z = (a.z + ((b.z - a.z) * k) / 5) * 16;
            const int step = i * 5 + k;
            int cp = aG.KindAt(2, x, y, z);
            if (cp >= 0)
                {
                int ord = 32;
                for (int q = 0; q < aCarForCps.iNCp; q++) if (aCarForCps.iCpInst[q] == cp) { ord = aCarForCps.iCpGroup[q]; break; }
                if (ord < 32 && !(mask & (1u << ord))) { mask |= 1u << ord; cps++; if (iNCp < KMaxCps) iCp[iNCp++] = step; }
                }
            if (step > 50 && cps >= aCarForCps.iTotalCps && aG.KindAt(3, x, y, z) >= 0) iFinish = step;
            }
        }
    iLastCps = cps;
    return true;
    }

bool TmGhost::Copy(const TmGhost& aFrom)
    {
    if (!iPt) iPt = new TmGhostPt[KMaxPts];
    if (!iPt) return false;
    iN = aFrom.iN; iFinish = aFrom.iFinish; iNCp = aFrom.iNCp; iSteps = aFrom.iSteps; iLastCps = aFrom.iLastCps;
    for (int i = 0; i < iNCp; i++) iCp[i] = aFrom.iCp[i];
    for (int i = 0; i < iN; i++) iPt[i] = aFrom.iPt[i];
    return true;
    }

bool TmGhost::At(int aStep, int& aX, int& aY, int& aZ, int& aYaw, int& aSlopeF, int& aSlopeR) const
    {
    if (!iPt || iN == 0) return false;
    if (aStep < 0) aStep = 0;
    int i = aStep / 5, f = aStep % 5;
    if (i >= iN - 1) { i = iN - 1; f = 0; }
    const TmGhostPt& a = iPt[i];
    const TmGhostPt& b = iPt[f ? i + 1 : i];
    aX = a.x + ((b.x - a.x) * f) / 5; aY = a.y + ((b.y - a.y) * f) / 5; aZ = a.z + ((b.z - a.z) * f) / 5;
    int dy = (int) b.yaw - (int) a.yaw; if (dy > 2048) dy -= 4096; else if (dy < -2048) dy += 4096;
    aYaw = (a.yaw + (dy * f) / 5) & 4095;
    aSlopeF = (a.sf + ((b.sf - a.sf) * f) / 5) << 5; aSlopeR = (a.sr + ((b.sr - a.sr) * f) / 5) << 5;
    return true;
    }

// the file: "TMG1", points, finish, checkpoints, the checkpoints' steps, the points (little endian, as the phone)
static void PutI(unsigned char*& aP, int aV) { aP[0] = (unsigned char) aV; aP[1] = (unsigned char) (aV >> 8); aP[2] = (unsigned char) (aV >> 16); aP[3] = (unsigned char) (aV >> 24); aP += 4; }
static int GetI(const unsigned char*& aP) { int v = aP[0] | (aP[1] << 8) | (aP[2] << 16) | (aP[3] << 24); aP += 4; return v; }

int TmGhost::Size() const { return 16 + KMaxCps * 4 + iN * 16; }

void TmGhost::Write(unsigned char* aBuf) const
    {
    unsigned char* p = aBuf;
    p[0] = 'T'; p[1] = 'M'; p[2] = 'G'; p[3] = '1'; p += 4;
    PutI(p, iN); PutI(p, iFinish); PutI(p, iNCp);
    for (int i = 0; i < KMaxCps; i++) PutI(p, i < iNCp ? iCp[i] : 0);
    for (int i = 0; i < iN; i++)
        {
        const TmGhostPt& g = iPt[i];
        PutI(p, g.x); PutI(p, g.y); PutI(p, g.z);
        p[0] = (unsigned char) g.yaw; p[1] = (unsigned char) (g.yaw >> 8); p[2] = (unsigned char) g.sf; p[3] = (unsigned char) g.sr; p += 4;
        }
    }

bool TmGhost::Load(const unsigned char* aBuf, int aLen)
    {
    Clear();
    if (aLen < 16 + KMaxCps * 4 || aBuf[0] != 'T' || aBuf[1] != 'M' || aBuf[2] != 'G' || aBuf[3] != '1') return false;
    const unsigned char* p = aBuf + 4;
    int n = GetI(p), fin = GetI(p), ncp = GetI(p);
    if (n < 1 || n > KMaxPts || ncp < 0 || ncp > KMaxCps || aLen < 16 + KMaxCps * 4 + n * 16) return false;
    if (!iPt) iPt = new TmGhostPt[KMaxPts];
    if (!iPt) return false;
    iFinish = fin; iNCp = ncp;
    for (int i = 0; i < KMaxCps; i++) { int v = GetI(p); if (i < ncp) iCp[i] = v; }
    for (int i = 0; i < n; i++)
        {
        TmGhostPt& g = iPt[i];
        g.x = GetI(p); g.y = GetI(p); g.z = GetI(p);
        g.yaw = (short) ((p[0] | (p[1] << 8)) & 4095); g.sf = (signed char) p[2]; g.sr = (signed char) p[3]; p += 4;
        }
    iN = n; iLastCps = ncp;
    return true;
    }
