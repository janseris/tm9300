// tmcore.cpp - see tmcore.h
#include "tmcore.h"

static inline int RdI32(const unsigned char* p) { return (int) ((unsigned) p[0] | (unsigned) p[1] << 8 | (unsigned) p[2] << 16 | (unsigned) p[3] << 24); }
static inline int RdU16(const unsigned char* p) { return p[0] | p[1] << 8; }
static inline int RdI16(const unsigned char* p) { return (short) (p[0] | p[1] << 8); }

TmTrack::TmTrack() : iNMesh(0), iNInst(0), iNPath(0), iMesh(0), iInst(0), iPath(0), iCar(-1), iCarClass(0), iTris(0),
    iNColl(0), iNCInst(0), iColl(0), iCInst(0), iStartX(0), iStartY(0), iStartZ(0), iStartYaw(0), iFarDist(0), iPvsX0(0), iPvsZ0(0), iPvsW(0), iPvsH(0), iPvsWords(0), iPvs(0) {}
TmTrack::~TmTrack() { delete[] iMesh; delete[] iInst; delete[] iColl; delete[] iCInst; }

bool TmTrack::Load(const unsigned char* aData, int aSize)
    {
    // 0.8: the medal times in the last 20 bytes ("MEDL" + 4 * i32), when the packer put them there
    iMedal[0] = iMedal[1] = iMedal[2] = iMedal[3] = 0;
    if (aSize >= 84 && aData[aSize - 20] == 'M' && aData[aSize - 19] == 'E' && aData[aSize - 18] == 'D' && aData[aSize - 17] == 'L')
        for (int k = 0; k < 4; k++) { int v = RdI32(aData + aSize - 16 + k * 4); iMedal[k] = v > 0 && v < 3600000 ? v : 0; }
    const unsigned char* p = aData;
    const unsigned char* end = aData + aSize;
    if (aSize < 40 || p[0] != 'T' || p[1] != 'M' || p[2] != 'B' || p[3] < '3' || p[3] > '6') return false;
    const int v4 = p[3] >= '4', v5 = p[3] >= '5', v6 = p[3] >= '6';
    iNMesh = RdI32(p + 4); iNInst = RdI32(p + 8); iNPath = RdI32(p + 12); iCar = RdI32(p + 16);
    iCarClass = 0;
    if (iCar >= 0) { iCarClass = (iCar >> 24) & 0xff; iCar &= 0xffffff; }
    iSky = RdI32(p + 20); iGround = RdI32(p + 24); iFog = RdI32(p + 28); iFogStart = RdI32(p + 32); iFogEnd = RdI32(p + 36);
    p += 40;
    if (v4)
        {
        if (aSize < 64) return false;
        iNColl = RdI32(p); iNCInst = RdI32(p + 4);
        iStartX = RdI32(p + 8); iStartY = RdI32(p + 12); iStartZ = RdI32(p + 16); iStartYaw = RdI32(p + 20);
        p += 24;
        }
    iMesh = new TmMesh[iNMesh];
    iInst = new TmInst[iNInst];
    if (!iMesh || !iInst) return false;
    for (int i = 0; i < iNMesh; i++)
        {
        if (p + 16 > end) return false;
        TmMesh& m = iMesh[i];
        m.nv = RdU16(p); m.nf = RdU16(p + 2);
        for (int k = 0; k < 6; k++) m.bb[k] = (short) RdI16(p + 4 + k * 2);
        p += 16;
        // the arrays are 2-byte aligned in the file (the header is 40 bytes, a mesh header 16, corners 6 bytes, faces 8)
        m.v = (const short*) p; p += m.nv * 6;
        m.idx = (const unsigned short*) p; p += m.nf * 6;
        m.col = (const unsigned short*) p; p += m.nf * 2;
        if ((p - aData) & 2) p += 2;
        if (p > end) return false;
        }
    iTris = 0;
    for (int i = 0; i < iNInst; i++)
        {
        if (p + 40 > end) return false;
        TmInst& t = iInst[i];
        t.mesh = RdU16(p); t.flags = RdU16(p + 2); t.far = -1;
        t.x = RdI32(p + 4); t.y = RdI32(p + 8); t.z = RdI32(p + 12);
        for (int k = 0; k < 6; k++) t.bb[k] = RdI32(p + 16 + k * 4);
        p += 40;
        if (t.mesh >= iNMesh) return false;
        iTris += iMesh[t.mesh].nf;
        }
    iPath = (const TmPath*) p;
    p += iNPath * 16;
    if (p > end) return false;
    if (!v4) return true;
    if (iNPath == 0) { iStartX = iStartY = iStartZ = 0; }
    iColl = new TmColl[iNColl];
    iCInst = new TmCInst[iNCInst];
    if (!iColl || !iCInst) return false;
    for (int i = 0; i < iNColl; i++)
        {
        if (p + 16 > end) return false;
        TmColl& c = iColl[i];
        c.nv = RdU16(p); c.nf = RdU16(p + 2);
        for (int k = 0; k < 6; k++) c.bb[k] = (short) RdI16(p + 4 + k * 2);
        p += 16;
        c.v = (const short*) p; p += c.nv * 6;
        c.idx = (const unsigned short*) p; p += c.nf * 6;
        c.nrm = (const short*) p; p += c.nf * 6;
        c.surf = p; p += c.nf;
        p += (4 - ((p - aData) & 3)) & 3;
        if (p > end) return false;
        }
    for (int i = 0; i < iNCInst; i++)
        {
        if (p + 40 > end) return false;
        TmCInst& t = iCInst[i];
        t.coll = RdU16(p); t.kind = RdU16(p + 2);
        t.x = RdI32(p + 4); t.y = RdI32(p + 8); t.z = RdI32(p + 12);
        for (int k = 0; k < 6; k++) t.bb[k] = RdI32(p + 16 + k * 4);
        p += 40;
        if (t.coll >= iNColl) return false;
        }
    if (!v5) return p <= end;
    if (p + 4 + iNInst * 2 > end) return false;
    iFarDist = RdI32(p); p += 4;
    for (int i = 0; i < iNInst; i++, p += 2) { int f = RdU16(p); iInst[i].far = f == 0xffff || f >= iNMesh ? -1 : f; }
    p += (4 - ((p - aData) & 3)) & 3;
    if (!v6) return p <= end;
    if (p + 16 > end) return false;
    iPvsX0 = RdI32(p); iPvsZ0 = RdI32(p + 4); iPvsW = RdI32(p + 8); iPvsH = RdI32(p + 12); p += 16;
    iPvsWords = (iNInst + 31) >> 5;
    iPvs = p; p += iPvsW * iPvsH * iPvsWords * 4;
    if (p > end) { iPvs = 0; return false; }
    return true;
    }

const unsigned int* TmTrack::PvsCell(int aCamX, int aCamZ) const
    {
    if (!iPvs) return 0;
    int cx = (aCamX >> 9) - iPvsX0, cz = (aCamZ >> 9) - iPvsZ0;
    if (cx < 0 || cz < 0 || cx >= iPvsW || cz >= iPvsH) return 0;
    return (const unsigned int*) (iPvs + (cz * iPvsW + cx) * iPvsWords * 4);
    }

TmView::TmView() : iCarSlopeF(0), iCarSlopeR(0), iGhostOn(0), iGhostX(0), iGhostY(0), iGhostZ(0), iGhostYaw(0), iGhostSlopeF(0), iGhostSlopeR(0), iIdMode(0), iUsePvs(1), iFarCut(0), iFlipWinding(0), iMeshMode(1), iSkipEnd(0), iR(0), iGhostXyz(0), iGhostCol(0), iGhostN(0), iW(0), iH(0), iMaxPolys(0), iOrder(0), iKey(0), iFar(0), iVtx(0), iXyz(0), iUv0(0), iFlipIdx(0), iIdCol(0), iVtxCap(0), iUvCap(0) {}
TmView::~TmView() { delete iR; delete[] iOrder; delete[] iKey; delete[] iFar; delete[] iVtx; delete[] iXyz; delete[] iUv0; delete[] iFlipIdx; delete[] iIdCol; delete[] iGhostXyz; delete[] iGhostCol; }

bool TmView::Init(int aW, int aH, int aMaxPolys)
    {
    iW = aW; iH = aH; iMaxPolys = aMaxPolys;
    iR = new R3D;
    if (!iR) return false;
    iR->SetMax(aMaxPolys);
    iR->iCover = 1;
    return true;
    }

void TmView::CarAt(const TmTrack& aT, int aSample, int aFrac256, int& aX, int& aY, int& aZ, int& aYaw) const
    {
    if (aT.iNPath == 0) { aX = aY = aZ = 0; aYaw = 0; return; }
    if (aSample < 0) aSample = 0;
    if (aSample >= aT.iNPath - 1) { aSample = aT.iNPath - 1; aFrac256 = 0; }
    const TmPath& a = aT.iPath[aSample];
    const TmPath& b = aT.iPath[aSample + (aFrac256 ? 1 : 0)];
    aX = a.x + (((b.x - a.x) * aFrac256) >> 8);
    aY = a.y + (((b.y - a.y) * aFrac256) >> 8);
    aZ = a.z + (((b.z - a.z) * aFrac256) >> 8);
    int dy = ((int) b.yaw - (int) a.yaw);
    if (dy > 2048) dy -= 4096; else if (dy < -2048) dy += 4096;
    aYaw = (a.yaw + ((dy * aFrac256) >> 8)) & 4095;
    }

static inline int ILen(int a, int b) { if (a < 0) a = -a; if (b < 0) b = -b; return a > b ? a + (b >> 1) : b + (a >> 1); }

void TmView::ChaseCamera(const TmTrack& aT, int aSample, int aFrac256, int& aX, int& aY, int& aZ, int& aYaw, int& aPitch) const
    {
    if (aT.iNPath == 0) { aX = aY = aZ = 0; aYaw = 0; aPitch = 0; return; }
    int cx, cy, cz, cyaw;
    CarAt(aT, aSample, aFrac256, cx, cy, cz, cyaw);
    // the camera trails the car along its own path: the point ~8 m back along it (never inside the ground the car
    // drove over), 3 m higher, looking at the car
    // the trailing point: back along the path from the car until the distance (over the ground) reaches 8 m,
    // interpolated inside the sample it lands in (the camera glides; snapping to the 10 Hz samples shook)
    int s = aSample < aT.iNPath - 1 ? aSample : aT.iNPath - 1;
    int px = cx, py = cy, pz = cz;
    const int want = 8 * 16;
    int wx = cx, wy = cy, wz = cz, acc = 0;          // the point walked back to, and the distance so far
    bool got = false;
    for (int k = s; k >= 0 && !got; k--)
        {
        const TmPath& p = aT.iPath[k];
        int seg = ILen(wx - p.x, wz - p.z);
        if (acc + seg >= want && seg > 0)
            {
            int f = ((want - acc) << 8) / seg;        // 0..256 along the segment from a towards p
            px = wx + (((p.x - wx) * f) >> 8); py = wy + (((p.y - wy) * f) >> 8); pz = wz + (((p.z - wz) * f) >> 8);
            got = true;
            }
        else { acc += seg; wx = p.x; wy = p.y; wz = p.z; }
        }
    if (!got) { px = wx; py = wy; pz = wz; }
    int dx = cx - px, dz = cz - pz;
    int d = ILen(dx, dz);
    if (d < 2 * 16)
        {
        // standing still (the start): behind the car by its heading
        int sn = R3D::Sin(cyaw), co = R3D::Cos(cyaw);
        px = cx - ((sn * want) >> 14); pz = cz - ((co * want) >> 14); py = cy; dx = cx - px; dz = cz - pz; d = want;
        }
    aX = px; aZ = pz; aY = py + 48;
    // the yaw towards the car (R3D: 0 = +z, clockwise = +x): a small atan2 from the octant
    int ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz, ang;
    if (ax > az) { ang = 1024 - ((az << 10) / (ax + 1)) * 1 / 1 ; ang = 1024 - (az * 1024) / (ax + 1) / 2; }     // 1024 * (1 - az/ax/2) ~ 45..90 deg
    else ang = (ax * 1024) / (az + 1) / 2;                                                                      // 0..45 deg
    // refine with the table: pick the yaw whose sin/cos best matches (dx, dz) near ang (a few steps)
    int best = ang, bestErr = 0x7fffffff;
    for (int y = ang - 160; y <= ang + 160; y += 8)
        {
        int yy = y & 4095;
        int ex = ((R3D::Sin(yy) * az) >> 14) - ((R3D::Cos(yy) * ax) >> 14); if (ex < 0) ex = -ex;
        // for the first octant mapping we compare sin/cos ratio: sin(yaw)/cos(yaw) = ax/az
        int e2 = ((R3D::Sin(yy) * az) >> 14) - ((R3D::Cos(yy) * ax) >> 14); if (e2 < 0) e2 = -e2;
        if (e2 < bestErr) { bestErr = e2; best = yy; }
        }
    int yaw = best;                             // in the first quadrant (dx >= 0, dz >= 0)
    if (dx >= 0 && dz < 0) yaw = 2048 - yaw;
    else if (dx < 0 && dz < 0) yaw = 2048 + yaw;
    else if (dx < 0 && dz >= 0) yaw = 4096 - yaw;
    aYaw = yaw & 4095;
    // the pitch towards the car (a point 1 m above it), a little below it: 1024 = 90 degrees, ~652 a radian
    int dy = (cy + 16) - aY;
    int dist = d > 16 ? d : 16;
    int pitch = (dy * 652) / dist - 60;
    if (pitch > 600) pitch = 600; if (pitch < -600) pitch = -600;
    aPitch = pitch;
    }

static int Dist2(const TmInst& t, int cx, int cz)
    {
    int mx = (t.bb[0] + t.bb[3]) >> 1, mz = (t.bb[2] + t.bb[5]) >> 1;
    int dx = (mx - cx) >> 4, dz = (mz - cz) >> 4;      // metres: no overflow within 46 km
    return dx * dx + dz * dz;
    }

// the scratch for a mesh of aNv corners and aNf faces (grown, never shrunk)
// a car mesh at (aX, aY, aZ) (1/16 m: its contact point) turned aYaw, tilted by the slopes (* 4096)
void TmView::DrawCar(const TmMesh& aM, short* aXyz, const R3Pix* aCol, int aX, int aY, int aZ, int aYaw, int aSlopeF, int aSlopeR)
    {
    R3D& r = *iR;
    // the car's model points along +z; its yaw as the camera's (0 = +z, clockwise)
    const int s = R3D::Sin(aYaw), co = R3D::Cos(aYaw);
    const short* v = aM.v; short* o = aXyz;
    // tilted with the ground first (a shear: fine for the slopes a car stays on), then turned
    for (int k = 0; k < aM.nv; k++, v += 3, o += 3)
        {
        int x = v[0], z = v[2], y = v[1] + ((x * aSlopeR + z * aSlopeF) >> 12);
        o[0] = (short) ((x * co + z * s) >> 14); o[1] = (short) y; o[2] = (short) ((z * co - x * s) >> 14);
        }
    if (r.MeshBegin(aXyz, aM.nv, 1, aX, aY + 1, aZ)) r.MeshTris(aM.idx, iUv0, aCol, aM.nf, 0, 0, 0, 0);
    }

void TmView::Reserve(int aNv, int aNf)
    {
    if (aNv > iVtxCap)
        {
        delete[] iVtx; delete[] iXyz; iVtxCap = aNv + 64;
        iVtx = new R3Vtx[iVtxCap]; iXyz = new short[iVtxCap * 3];
        if (!iVtx || !iXyz) { delete[] iVtx; delete[] iXyz; iVtx = 0; iXyz = 0; iVtxCap = 0; }
        }
    if (aNf > iUvCap)
        {
        delete[] iUv0; delete[] iFlipIdx; delete[] iIdCol; iUvCap = aNf + 64;
        iUv0 = new unsigned short[iUvCap * 6]; iFlipIdx = new unsigned short[iUvCap * 3]; iIdCol = new unsigned short[iUvCap];
        if (!iUv0 || !iFlipIdx || !iIdCol) { delete[] iUv0; delete[] iFlipIdx; delete[] iIdCol; iUv0 = 0; iFlipIdx = 0; iIdCol = 0; iUvCap = 0; }
        else for (int k = 0; k < iUvCap * 6; k++) iUv0[k] = 0;
        }
    }

void TmView::Frame(R3Pix* aFrame, int aStride, const TmTrack& aT, int aCamX, int aCamY, int aCamZ, int aYaw, int aPitch, int aFocal,
    int aCarX, int aCarY, int aCarZ, int aCarYaw, int aDrawCar)
    {
    R3D& r = *iR;
    iStats.insts = aT.iNInst; iStats.visible = 0; iStats.full = 0;
    if (!r.Begin(aFrame, iW, iH, aStride, aFocal)) return;
    r.SetCamera(aCamX, aCamY, aCamZ, aYaw, aPitch);
    if (iIdMode) { r.SetFog(30000, 30001, 0); r.Background(0, 0); }
    else { r.SetFog(aT.iFogStart, aT.iFogEnd, (R3Pix) aT.iFog); r.Background((R3Pix) aT.iSky, (R3Pix) aT.iGround); }
    r.SetPerspective(false);
    const unsigned int* pvs = iUsePvs && !iIdMode ? aT.PvsCell(aCamX, aCamZ) : 0;
    // the instances nearest first (the renderer keeps the first ones it is given, and front to back
    // lets it skip what is hidden)
    if (!iOrder) { iOrder = new int[aT.iNInst]; iKey = new int[aT.iNInst]; iFar = new unsigned char[aT.iNInst]; }
    if (!iOrder || !iKey || !iFar) return;
    int n = 0;
    int far2 = iFarCut ? (iFarCut >> 4) * (iFarCut >> 4) : 0x7fffffff;
    const int fs = R3D::Sin(aYaw) >> 4, fc = R3D::Cos(aYaw) >> 4;      // the view's direction * 1024
    // |(focal, W/2)|: the side planes' normal length (integer square root)
    int sideNorm = 1; { const int q = aFocal * aFocal + (iW >> 1) * (iW >> 1); while ((sideNorm + 1) * (sideNorm + 1) <= q) sideNorm++; }
    for (int i = 0; i < aT.iNInst; i++)
        {
        const TmInst& t = aT.iInst[i];
        if (pvs && !(pvs[i >> 5] & (1u << (i & 31)))) continue;
        // the far cut on the box's nearest corner (a wide tile counts by its near edge)
        int dx = aCamX < t.bb[0] ? t.bb[0] - aCamX : aCamX > t.bb[3] ? aCamX - t.bb[3] : 0;
        int dz = aCamZ < t.bb[2] ? t.bb[2] - aCamZ : aCamZ > t.bb[5] ? aCamZ - t.bb[5] : 0;
        dx >>= 4; dz >>= 4;
        if (dx * dx + dz * dz > far2) continue;
        // wholly behind the camera: its middle's forward distance less its half size (in the view's direction) < -2 m
        int mx = ((t.bb[0] + t.bb[3]) >> 1) - aCamX, mz = ((t.bb[2] + t.bb[5]) >> 1) - aCamZ;
        int half = ((t.bb[3] - t.bb[0]) + (t.bb[5] - t.bb[2])) >> 1;          // >= the half diagonal's forward part
        const int fwd = (mx >> 4) * fs + (mz >> 4) * fc;                      // m * 1024
        if (fwd + (half >> 4) * 1024 < -2 * 1024) continue;
        // 0.6: wholly beside the view: the box's circle (radius <= half) outside a side plane of the view (the
        // plane's normal (focal, -W/2) in (sideways, forward)); BoxVisible was 7 % of a frame on what this drops
        {
        const int lat = (mx >> 4) * fc - (mz >> 4) * fs;
        const long long out = (long long) (lat < 0 ? -lat : lat) * aFocal - (long long) fwd * (iW >> 1);
        if (out > (long long) ((half >> 4) + 3) * 1024 * sideNorm) continue;
        }
        iOrder[n] = i; iKey[n] = Dist2(t, aCamX, aCamZ); n++;
        iFar[i] = (t.far >= 0 && aT.iFarDist > 0 && (dx * dx + dz * dz) > (aT.iFarDist >> 4) * (aT.iFarDist >> 4)) ? 1 : 0;
        }
    // shell sort by key
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++)
            {
            int k = iKey[i], o = iOrder[i], j = i;
            while (j >= gap && iKey[j - gap] > k) { iKey[j] = iKey[j - gap]; iOrder[j] = iOrder[j - gap]; j -= gap; }
            iKey[j] = k; iOrder[j] = o;
            }
    // the car first: nearest, and the only thing turned per frame (its corners into iXyz)
    const bool car = aDrawCar && !iIdMode && aT.iCar >= 0 && aT.iCar < aT.iNMesh;
    if (car)
        {
        const TmMesh& m = aT.iMesh[aT.iCar];
        Reserve(m.nv, m.nf);
        if (iXyz && iUv0) DrawCar(m, iXyz, (const R3Pix*) m.col, aCarX, aCarY, aCarZ, aCarYaw, iCarSlopeF, iCarSlopeR);
        }
    // 0.8: the ghost: its distance as the instances' key; drawn before the first instance further away
    int ghostKey = -1;
    if (car && iGhostOn)
        {
        const int gdx = (iGhostX - aCarX) >> 4, gdz = (iGhostZ - aCarZ) >> 4, gdy = (iGhostY - aCarY) >> 4;
        const int cdx = (iGhostX - aCamX) >> 4, cdz = (iGhostZ - aCamZ) >> 4;
        const int gfwd = cdx * fs + cdz * fc;                  // m * 1024 along the view
        if ((gdx * gdx + gdz * gdz > 9 || gdy * gdy > 9) && gfwd > -3 * 1024 && cdx * cdx + cdz * cdz <= far2)
            {
            const TmMesh& m = aT.iMesh[aT.iCar];
            if (iGhostN < m.nv || iGhostN < m.nf)
                {
                delete[] iGhostXyz; delete[] iGhostCol; iGhostN = m.nv > m.nf ? m.nv : m.nf;
                iGhostXyz = new short[iGhostN * 3]; iGhostCol = new unsigned short[iGhostN];
                if (!iGhostXyz || !iGhostCol) { delete[] iGhostXyz; delete[] iGhostCol; iGhostXyz = 0; iGhostCol = 0; iGhostN = 0; }
                // the car's colours halfway to a pale blue
                else for (int k = 0; k < m.nf; k++) iGhostCol[k] = (unsigned short) (((((const unsigned short*) m.col)[k] >> 1) & 0x7BEF) + ((0xA5DF >> 1) & 0x7BEF));
                }
            if (iGhostXyz) ghostKey = cdx * cdx + cdz * cdz;
            }
        }
    for (int oi = 0; oi <= n; oi++)
        {
        if (ghostKey >= 0 && (oi == n || iKey[oi] >= ghostKey))
            {
            DrawCar(aT.iMesh[aT.iCar], iGhostXyz, (const R3Pix*) iGhostCol, iGhostX, iGhostY + 3, iGhostZ, iGhostYaw, iGhostSlopeF, iGhostSlopeR);
            ghostKey = -1;
            }
        if (oi == n) break;
        const TmInst& t = aT.iInst[iOrder[oi]];
        if (r.Full()) { iStats.full = 1; break; }
        if (!r.BoxVisible(t.bb[0], t.bb[1], t.bb[2], t.bb[3], t.bb[4], t.bb[5])) continue;
        iStats.visible++;
        const TmMesh& m = aT.iMesh[iFar[iOrder[oi]] ? t.far : t.mesh];
        Reserve(m.nv, m.nf);
        if (!iUv0 || !iFlipIdx || !iVtx || !iXyz) return;
        if (iMeshMode)
            {
            const unsigned short* idx = m.idx;
            if (iFlipWinding)
                {
                for (int k = 0; k < m.nf * 3; k += 3) { iFlipIdx[k] = idx[k]; iFlipIdx[k + 1] = idx[k + 2]; iFlipIdx[k + 2] = idx[k + 1]; }
                idx = iFlipIdx;
                }
            // the corners are from the instance's origin; base 1 with the origin's y + 1: world y as it is, and no
            // corner counts as "the ground" (a world y of 0 never happens)
            const R3Pix* col = (const R3Pix*) m.col;
            if (iIdMode) { for (int k = 0; k < m.nf; k++) iIdCol[k] = (unsigned short) (iOrder[oi] + 1); col = (const R3Pix*) iIdCol; }
            if (r.MeshBegin(m.v, m.nv, 1, t.x, t.y + 1, t.z))
                {
                r.MeshTris(idx, iUv0, col, m.nf, 0, 0, 0, 0);
                continue;
                }
            }
        const short* v = m.v;
        for (int k = 0; k < m.nv; k++, v += 3)
            {
            iVtx[k].x = t.x + v[0]; iVtx[k].y = t.y + v[1]; iVtx[k].z = t.z + v[2];
            iVtx[k].u = 0; iVtx[k].v = 0;
            }
        const unsigned short* f = m.idx;
        R3Vtx tri[3];
        for (int k = 0; k < m.nf; k++, f += 3)
            {
            tri[0] = iVtx[f[0]];
            if (iFlipWinding) { tri[1] = iVtx[f[2]]; tri[2] = iVtx[f[1]]; }
            else { tri[1] = iVtx[f[1]]; tri[2] = iVtx[f[2]]; }
            r.Poly(tri, 3, 0, iIdMode ? (R3Pix) (iOrder[oi] + 1) : (R3Pix) m.col[k], false);
            }
        }
    if (!iSkipEnd) r.End();
    iStats.polys = r.iStats.polys; iStats.drawn = r.iStats.drawn; iStats.culled = r.iStats.culled;
    iStats.hidden = r.iStats.hidden; iStats.pixels = r.iStats.pixels;
    }
