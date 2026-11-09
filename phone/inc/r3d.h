// Maps 9300: a small 3D renderer for the 3D view (first: the 3D test, 0.40). Portable C++ (tested
// on a PC: test/r3dtest.cpp), integers only (the ARM9 has no FPU), into a 16-bit RGB565 frame.
//
// World: x east, y up, z north, in 1/16 m units (int). Camera: position, yaw (0..4095 = 0..360°,
// 0 = looking north, growing clockwise), pitch (-1024..1024 = -90..90°, up positive).
// Polygons: convex, up to 4 vertices before clipping (quads), drawn far to near (painter's
// algorithm, no z-buffer), back faces skipped, clipped at the near plane and the screen edges.
// Fill: a flat colour (with distance fog) or a texture (RGB565, power-of-two sizes), affine or
// perspective-correct (every 16 pixels).

#ifndef R3D_H
#define R3D_H

typedef unsigned short R3Pix;
// 1.60: a polygon's plane as 1/z over the screen (ResolveLayers): 1/z * 2^28 = A * (px - cx) + B * (py - cy) + C
struct R3Plane { int A, B, C; };

struct R3Tex
    {
    const R3Pix* pix;
    int wShift, hShift;         // width = 1 << wShift, height = 1 << hShift
    int stride;                 // pixels per row
    };

struct R3Vtx
    {
    int x, y, z;                // world, 1/16 m
    int u, v;                   // texture coordinates, texels * 256 (8 fractional bits), |u|, |v| < 8192 texels
    };

struct R3Stats
    {
    int polys, drawn, culled, clipped, pixels;
    int msTransform, msRaster;  // filled by the caller (timing is platform code)
    int hidden, lost;           // 1.59: front to back: polygons wholly behind what was drawn; runs not drawn (more than 4 in a row)
    };

class R3D
    {
public:
    R3D();
    ~R3D();
    // the frame (RGB565, aStride pixels per row), the field of view (focal length in pixels)
    bool Begin(R3Pix* aFrame, int aW, int aH, int aStride, int aFocal);
    void SetCamera(int aX, int aY, int aZ, int aYaw, int aPitch);
    void SetFog(int aStart, int aEnd, R3Pix aColor);      // distances in 1/16 m
    void SetPerspective(bool aOn) { iPersp = aOn; }
    // at most this many polygons a frame (the first ones submitted are kept: submit near to far)
    void SetMax(int aMax);
    bool Full() const { return iCount >= iMax; }   // 0.53: nothing more is kept this frame
    // 1.13: false if the box (y from the ground's base, as Poly's) is wholly off the screen (behind
    // the near plane, or beyond one of the view's 4 sides, with a few pixels to spare): none of it drawn
    bool BoxVisible(int aX0, int aY0, int aZ0, int aX1, int aY1, int aZ1) const;
    // 1.17: which of a mesh's 64 chunks (8 x 8) can be on the screen: aVis[k] 1 or 0. aB: 81 boxes (the
    // chunks, then the 16 of 2 x 2 chunks (4 x 4), then the whole): the middle x, y, z, then the half sizes
    // x | z << 16 (-1: no faces); aHy: y's; from (aOx, aOy, aOz) (VtsChunkSet). Returns how many.
    int ChunksVisible(const int (*aB)[4], const int* aHy, int aOx, int aOy, int aOz, unsigned char* aVis);
    // a polygon of 3 or 4 vertices, counter-clockwise when seen from its front; aTex NULL: aColor
    void Poly(const R3Vtx* aV, int aN, const R3Tex* aTex, R3Pix aColor, bool aTwoSided = false);   // aTex is copied
    // 0.92: a mesh: its corners into camera space and onto the screen once each (MeshBegin; false:
    // no room, Poly instead), then its triangles (MeshTris): the polygons Poly would make (a corner
    // behind the camera, out of range or 512 m away: Poly itself), at ~1/4 of the corners' work
    // (1.13: the corners 16 bits from the mesh's origin aOx, aOy, aOz)
    // 1.17: aChV (NULL: all the corners): only the corners of the aNg groups with aGroups[h] set (corners
    // aChV[h] .. aChV[h + 1] - 1)
    bool MeshBegin(const short* aXyz, int aN, int aBase, int aOx, int aOy, int aOz, const unsigned short* aChV = 0, const unsigned char* aGroups = 0, int aNg = 0);
    int Bytes() const;          // 1.03: its buffers (the polygon list, buckets, rows, reciprocals, mesh corners)
    // the mesh's triangles at once (aIdx 3 a triangle, aUv 6, aShade 1; texture coordinates >> aUvShift,
    // or << 1 for -1); a triangle whose middle is in one of the aNsk squares of aSkip (x0, z0, x1, z1,
    // each * 3) is left out. Returns the triangles submitted. Stops when the renderer is full.
    int MeshTris(const unsigned short* aIdx, const unsigned short* aUv, const R3Pix* aShade, int aNf, int aUvShift,
        const R3Tex* aTex, const int* aSkip, int aNsk, const unsigned char* aFch = 0, const unsigned char* aVis = 0,
        const int* aFr = 0);   // 1.17: aFch: face f only if aVis[aFch[f]]; 1.59: aFr (with aFch): the faces sorted by chunk, chunk h's aFr[h] .. aFr[h + 1] - 1: the chunks off the screen skipped whole
    // 1.60: the DSP's triangles (dsp/echo_node.c cmd 47, faces4: 16 halfwords a record: the 3 corners'
    // x, y, the 6 u, v as they're drawn, the depth's low and high halves, (flags & 2) | the tile's number
    // << 8 (0x8000: a face for Poly to clip, its number in the last word), the face) into R3_PACKED2
    // polygons and their depth buckets, as r3_build would make them. aTiles: the tiles by number (the
    // texture; for the clipped ones the mesh, as MeshBegin's). Returns the records taken.
    struct DspTile { const R3Tex* tex; const short* xyz; const unsigned short* idx; const unsigned short* uv;
        const R3Pix* shade; int nf, ox, oy, oz, base, uvShift; };
    int DspRecords(const unsigned short* aRec, int aN, const DspTile* aTiles, int aNt);
    // the camera as r3_verts takes it: x, y, z, the yaw's cos, sin, the pitch's cos, sin (* 16384)
    void Camera(int* aC) const { aC[0] = iCamX; aC[1] = iCamY; aC[2] = iCamZ; aC[3] = iYawC; aC[4] = iYawS; aC[5] = iPitchC; aC[6] = iPitchS; }
    void View(int& aFocal, int& aCx, int& aCy, int& aW, int& aH) const { aFocal = iFocal; aCx = iCx; aCy = iCy; aW = iW; aH = iH; }
    // the sky above the horizon and aGround below it (what the ground polygons don't cover)
    void Background(R3Pix aSky, R3Pix aGround);
    // draws the collected polygons, far to near
    void End();
    // 1.60: the depth test (RasterNear) for the flat polygons nearer than this (1/16 m; 0: off; 40 m): what
    // the bucket order gets wrong between surfaces close together (a deck over water, a road over its terrain,
    // the car over the road) is put right per pixel. Set before Begin (the buffer is made there)
    int iZNear;
    int iZHidden;               // 0.6: a near polygon all hidden already is tested only nearer than this (16 m)
    int iZVisited, iZWritten, iZPolys, iZRows, iZFullRows, iZRowsNp, iZRowsSlow;      // statistics (the PC build counts the pixels)
    // 1.59: the frame's polygons as End draws them (the depth buckets' order), for a scene dump (the
    // DSP's 3D tests): 24 ints each: n, down, the texture's index in aTex (-1: flat), color, depth, sx[4],
    // sy[4], su[4], sv[4], sz[3] (packed triangles unpacked; the floor's quads with their 4th corner;
    // more corners than 4: the first 4, n tells). Distinct textures into aTex (up to aMaxTex; more:
    // index -2). Call before End.
    // 1.59: focal, fog start, fog end, fog colour, perspective (for the scene dump)
    void DumpParams(int* aOut) const { aOut[0] = iFocal; aOut[1] = iFogStart; aOut[2] = iFogEnd; aOut[3] = iFogColor; aOut[4] = iPersp ? 1 : 0; }
    int ScenePolys(int* aOut, int aMax, const R3Tex** aTex, int aMaxTex, int* aNTex) const;
#ifdef R3_REFERENCE
    int ReferenceFrame(R3Pix* aFrame, int aStride) const;     // 1.60: the PC harness: a z-buffer reference of the frame's polygons
    void DebugPixel(int aPx, int aPy) const;
#endif
    R3Stats iStats;
    bool iPackedUV;             // 1.22: textures with u and v packed in one register (faster, not the same picture to the bit)
    int iSpanMode;              // 0.75: 1 the rows and 4-pixel spans in assembly, 0 the 0.59 span (the drawing test compares)
    int iGeoMode;               // 0.75: 1 the corners' geometry in assembly (r3dgeo.c), 0 in C
    int iCover;                 // 1.59: front to back, the pixels drawn already skipped (r3_cover; 7 in the 3D view switches it, for A/B)
    int iFullRecords;           // 1.59: R3_PACKED2 with its colour (the scene dump: Shift+L); else not read from shade[] (a miss each)
    int iPacked2;               // 1.59: R3_PACKED2 for r3_build's triangles (8 in the 3D view switches it, for A/B)
    int iMeshMode;              // 0.92: 1 VtsDraw's corners projected once a mesh (MeshBegin), 0 per triangle (Poly)
    // sine and cosine of an angle (0..4095), * 16384
    static int Sin(int aAngle);
    static int Cos(int aAngle) { return Sin(aAngle + 1024); }
private:
    struct SPv { int x, y, z, u, v; };          // after the camera: x right, y up, z forward
    // 0.58: the texture's description is copied (a caller's R3Tex may be gone before End: the
    // KERN-EXEC 3 crashes of 0.43-0.50 and of the 0.57 benchmark)
    // 1.13: 208 bytes, 16-byte aligned (iPolys): what drawing reads first (n, down, tex, next) in the
    // first 16, the corners from 48 (a triangle's sx[0..2], and sy, su, sv, each in one 16-byte cache line
    // of the 9300's data cache): 5 lines read when it's drawn, not 10. A mesh's polygons share one copy
    // of its texture (iMeshTex), others have their own (texCopy).
    struct SPoly { int n, down; const R3Tex* tex; int next; int depth; R3Pix color; R3Tex texCopy; int pad[2]; int sx[8], sy[8], sz[8], su[8], sv[8]; };
    // 1.24: r3_build's triangles packed in the first 48 bytes (3 cache lines, one miss each when drawn,
    // were 5): n = 3 | KPacked, 4 sx | sy << 16 of corner 0, tex 8, next 12, 16 / 20 corners 1 / 2,
    // 24 su[3], 36 sv[3]; 48 depth, 52 color, 56 sz[3]. r3_raster_tex unpacks them; C: UnpackPoly.
    enum { KPacked = 0x20 };
    // 1.59: R3_PACKED2 (n's low byte 0x63, r3_build when the uv need no shift left): 2 cache lines, all that
    // drawing a hidden one reads in the first: n and next (n's high half) at 0, the corners' x | y << 16 at
    // 4, 8, 12; tex at 16, u | v << 16 at 20, 24, 28; depth, color, sz as KPacked's
    enum { KPacked2 = 0x40 };
    static bool IsPacked2(const SPoly& aP) { return (aP.n & 0xff) == (3 | KPacked | KPacked2); }
    static int NextOf(const SPoly& aP) { return IsPacked2(aP) ? aP.n >> 16 : aP.next; }
    int NextOf(int aI) const { return NextOf(iPolys[aI]); }
    void UnpackPoly(const SPoly& aP, SPoly& aT) const;
    static int PolyCorners(const int* aW, int* aSx, int* aSy, int* aSz);
    bool PlaneOfPoly(const SPoly& aP, const int* aSx, const int* aSy, const int* aSz, int aN, R3Plane& aPlane) const;
    void StorePlane(SPoly& aP, int aNx, int aNy, int aNz, int aD) const;
    SPoly iUnpacked;
    void Raster(const SPoly& aP);
    void RasterNear(const SPoly& aP, int aNp, int aY0, int aY1, int aZA, int aZB, int aZC);
    void ZGapSlow(R3Pix* aRow, unsigned short* aZ, int aXa, int aXb, int aW0, int aZA, R3Pix aColor);
    unsigned short* iZ;         // 1.60: the depth buffer (2^18 / z a cell of two pixels), iZW cells a row, iZSize cells
    int iZSize, iZW;
    int* iRowSpan;              // 1.60: a near polygon's rows' full spans (r3_cover clips iLeft's)
    void RasterPersp(const SPoly& aP);
    R3Pix Fog(R3Pix aColor, int aDepth) const;
    R3Pix* iFrame;
    int iW, iH, iStride, iFocal, iCx, iCy;
    int iCamX, iCamY, iCamZ, iYawS, iYawC, iPitchS, iPitchC;
    int iWn[5][6];              // 1.17: the view's planes in world space for ChunksVisible (normals * 4096, their sizes)
    bool iWnSet;                // worked out for this camera and frame
    int iFogStart, iFogEnd;
    R3Pix iFogColor;
    bool iPersp;
    SPoly* iPolys;              // 1.13: in iPolyMem, 16-byte aligned
    char* iPolyMem;
    int iCount, iMax;
    int iCap;                   // 1.10: the polygon list's size (SetMax)
    int* iBucket;               // depth buckets: the first polygon of each, -1 none
    int* iLeft;                 // per scanline: span limits and attributes (scratch)
    int* iRecip;                // 0.59: 2^30 / n (no divide instruction on the ARM9)
    short* iMV;                 // 0.92: the mesh's corners: screen x, y, camera z, flags (1.13: shorts)
    int iMVCap;
    const short* iMXyz;
    int iMBase;
    int iMOx, iMOy, iMOz;       // 1.13
    int* iMT;                   // the triangles kept (r3_tris)
    int iMTCap;
    enum { KMeshTex = 128 };
    R3Tex iMeshTex[KMeshTex];   // 1.13: the meshes' textures this frame (MeshTris), shared by their polygons
    int iMeshTexN;
    struct R3Cov* iCov;         // 1.59: r3_cover's bits
    struct R3Cov* iCovNow;      // this frame's (End), or NULL
    struct R3Cov* Cover();
    // 1.59: front to back: the background only where nothing was drawn (End, from the bits; Background
    // leaves it pending): ~90 % of a street-level frame is covered
    bool iBgPend;
    R3Pix iBgSky, iBgGround;
    int iBgHy;
    void FillUncovered();
    };

#endif
