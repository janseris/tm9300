# TrackMania United Forever on the Nokia 9300: feasibility analysis (7 Nov 2026; 02:30: block map, LODs, collision, per-track counts)

Scope the user set: **Snow, Rally, Desert, Bay, Island — no Stadium. Snow or Rally first.**
Facts below were measured on the user's own install (`C:\Games\TmUnitedForever`, TMUF 2.11.26) with the
two scripts in `tm_tools/` (own code; no Nadeo data is copied anywhere). Phone facts are from
`symbian-s80v2-dev-tips-and-tricks/performance.md` and Hrimfell's `docs/WORKPLAN.md`.

## 1. What is portable and what is not

- `TmForever.exe` is x86 / Direct3D 9: nothing of it runs on the ARM925T. **The port is a new engine**
  (like Hrimfell), fed by PC tools that read the user's install. Nadeo's car physics is closed and has to
  be written anew and tuned against the shipped replays (`*.Replay.gbx`, one per campaign track).
- **Tracks are readable now.** `tm_tools/gbxtrack.py` reads a `*.Challenge.Gbx` (plain Gbx: header XML +
  LZO1X body, chunk `0x2400301F` in this old-ID TMUF install) and lists its blocks: name, direction
  (0-3), grid cell, flags (bit 12 = ground variant, bit 15 = has skin). All 105 campaign race tracks
  (21 per environment) parse.
- **Block geometry is out of the paks.** `tools/nadeopak.py` decrypts the pak directories (Blowfish,
  keys from `packlist.dat`) and lists every file with its class and size; the public hash list resolves
  every hashed name (1039/1039 Alpine, 1240/1240 Rally). The full dump (meshes included) was made with
  GBX.NET's `Tools/PakToFolder` (commit aed16ae, .NET 9/10, 6 s per pak):
  `dump/pak_dump_rally_alpine_game.tgz` (Rally 45 MB, Alpine 47 MB, Game 27 MB, decrypted and
  decompressed). `tools/SolidStats/` (C#, against GBX.NET) reads every `*.Solid.Gbx`, counts trees,
  vertices and triangles and can export OBJ+MTL (`SolidStats <dumpdir> [objdir]`); results in
  `docs/pak_inventory/{rally,alpine}_solids.tsv`. Checked visually (`docs/tm_meshes_check.png`).
- Loose files: textures (`GameData/<Env>/Media/**/*.dds|tga`), WAVs (engine, surfaces, impacts in
  `GameData/Vehicles/Media/Audio`), per-track lightmaps (`GameData/LightmapsCache/*.lightmap.zip`),
  campaign replays. All usable from the PC side.

## 2. The data (Alpine = Snow, Rally)

| | Alpine.pak | Rally.pak |
|---|---|---|
| Files | 1,293 | 1,475 |
| Solid meshes (CPlugSolid) | 585, 31.8 MB uncompressed | 721, 28.2 MB |
| Block definitions (BlockInfo*, all kinds) | 209 | 189 |
| Bitmaps / materials | 103 / 96 | 117 / ~100 |
| Decorations (moods, map sizes) | 20 | ~20 |

**Measured triangle counts (LOD 0, as shipped):** Rally kit 664 meshes, 212 k triangles in all; Alpine
571 meshes, 242 k. Per piece:

| Piece | Triangles | | Piece | Triangles |
|---|---|---|---|---|
| RallyRoadCastleStraightAir | 34 | | AlpineRoadWoodStraightGround | 70 |
| RallyRoadCastleStraightPillar | ~10 | | AlpineRoadChaletStraightGround | 124 |
| Zone RockyGrassHill StraightA (terrain tile) | 56 | | Zone Hills StraightA (terrain tile) | 158 |
| RallyHouseGround | 247 | | AlpineMetalTubeStraightGround | 272 |
| RallyTreesGround1 | 416 + sprites | | AlpineTempleGround | 480 |
| RallyFieldFenceTreeStraightA | 421 + sprites | | AlpineTrees | 672 |
| RallyTunnelGTCurve3 | 2,820 | | AlpineTunnelGTCurve3 | ~3,200 |
| RallyFieldTetris U-road pieces | 3,000-4,000 | | AlpineSnowBase (flat snow tile) | 2 |
| Base plate 30x30 (one per map) | 6,636 | | Base plate 30x30 | 4,592 |
| RallyCar | 5,028 | | SnowCar | 6,886 |

So: road pieces and terrain tiles are already small (30-160); decor (houses, temples, trees) 250-700;
the few big items are tunnels, Tetris field pieces and the per-map base plate. Trees carry sprite
foliage (CPlugVisualSprite) on top of the counted triangles: billboards on the phone. The cars need a
~10x reduction (a 400-600 triangle car is plenty at 320 x 200).

A whole track at LOD 0, by matching block names to meshes (rough; terrain zones excluded): Rally
tracks 5-30 k triangles (RallyE 53 k), Snow tracks 7-120 k (the Hills zone pieces and trees dominate).
With ~150 m of view distance a frame sees maybe a sixth of a track: 1-5 k raw triangles for Rally,
more for Snow, so **a 2-4x simplification pass (far forms, billboards, merged pillars, no back
faces of road sides) gets Rally into the 1,000-1,500 budget**, and Snow needs the trees billboarded
and the Hills zone tiles decimated to get there.

Campaign race tracks (21 each):

| Env | Blocks per track min / median / max | Distinct models per track (median) | Distinct models over all 21 |
|---|---|---|---|
| Snow (Alpine) | 171 / 581 / 856 | 33 | 142 |
| Rally | 136 / 441 / 2,631 | 40 | 139 |
| Desert (Speed) | 181 / 739 / 1,308 | 30 | 117 |
| Bay | 159 / 872 / 1,242 | 52 | 161 |
| Island | 329 / 727 / 1,206 | 45 | 169 |

Most blocks in a track are **terrain zone pieces** (SnowC3: 323 of 665 are `SnowToSnow`, SnowA1: 232
`SnowToIce` + 108 `Hills` + 88 `TundraHills`; RallyC3: 251 `HillsToHills` + 121 `RallyFlat`), which are
auto-placed ground tiles: flat or gently shaped quads, cheap. The track proper is 100-250 pieces
(roads, pillars, lamps, checkpoints, signs). Snow tracks additionally have the Great Wall / temple
pieces, metal tubes and chalets; Rally has castles, fields with fences and trees, houses, windmills.

Map grid: Alpine 30x18x30 (or 45x45, 20x60) cells; a block is 32 x 8 x 32 m (Alpine/Rally 32 m;
Bay/Island 64 m). Decorations name the mood (Day/Sunrise/Sunset/Night) and map size.

## 3. Fit to the phone (from Hrimfell's and Maps 9300's measurements)

- Hrimfell: ~2,250 triangles → 5-6 fps; light scenes 12-13 fps; the emulator's fps estimate holds
  within a few % of the phone (3 cycles per instruction, misses ~40 cycles).
- Maps 9300 1.59: with small triangles the frame follows the triangle count (~10 us each), not pixels;
  nearest-first with a coverage bit per pixel rejects the hidden ones before setup.
- A racing game wants **~15 fps** (at 150 km/h and 10 fps the car jumps 4 m a frame). Budget:
  **~1,000-1,500 triangles drawn per frame** at 320 x 200 doubled to 640.

Snow and Rally against that budget:
- **Rally is the easier first target.** Open rolling terrain, few pillars, little vertical stacking,
  the road is mostly on the ground; the costly items (fence-trees, houses, windmill) are decor that can
  be billboarded or drawn as far-form boxes beyond 100 m. Its tracks are also the smallest (median 441
  blocks). Weakness: the grass/field terrain wants texture or at least vertex shading over large areas.
- **Snow is a close second.** Snow terrain is flat white (cheap, even untextured), but Snow tracks lean
  on elevated wooden roads with pillars, chalets, the Great Wall and metal tubes: more pieces in view,
  more overdraw, more vertical. Ice and snow surfaces also mean the physics has two grips to tune from
  the start. Fog (`30x30Sunset` etc. moods) is natural here and hides the far cut-off.
- Order the user asked for: start with **Rally**, Snow right after on the same engine (same block grid,
  same pipeline; only the kit and the car differ). Desert (Speed) next: flat desert, few objects, but
  huge map sizes (45x45) and long straights where view distance matters. Bay and Island last: 64 m
  blocks, water, lots of city/harbour decor, the heaviest campaigns (872 / 727 blocks median).

Per-frame estimate for a Rally track at street level, after offline simplification (no foliage,
pillars as 4-6 quads, roads as their drivable surface plus a side quad each, terrain tiles as 2-8
triangles): 300-600 triangles for the track within ~150 m, 100-300 for terrain, 50-150 for the car,
a sky gradient. Inside the budget with headroom for 15 fps; to be proven on the phone with a replay
flythrough (phase 3 below) before any gameplay code.

Physics: one car, 4 wheel contacts against the drivable triangles of the few blocks around the car
(the block grid gives the lookup for free), fixed-point at 100 Hz as the original. Well under 10 % of
the CPU. The feel (drift on dirt, grip on castle road, turbo pads, respawn) is tuned against the
replays; medal times are recalibrated by driving the replay paths with the new physics.

Controls: Hrimfell reads key down/up by scan code (`hrimfell_app.cpp` ~845): simultaneous accelerate +
steer works; steering is digital like the PC keyboard, with a steering rate ramp for feel.

Data: per environment a shared kit of ~140 simplified block models (~1-2 MB) plus 21 tracks of a few KB
each (blocks, not meshes); textures 16-bit, few. A couple of minutes over Bluetooth at ~35 kB/s.
Program and data as two packages like Hrimfell (`hrimfell.pkg` / `hrimfell_data.pkg`).

## 3a. Phase 3 reached (7 Nov 2026, 02:40): the flythrough viewer built, waiting for the phone

The whole pipeline runs: `KitExport` (pak dump -> kit JSON) -> `pack.py` (kit + RallyA1 + its replay ->
`rallyA1.tmb`, 193 KB: 70 meshes at Nadeo's lowest LOD, a 400-triangle cap, 498 instances, 29 k placed
triangles, the car's path at 10 Hz) -> `tmcore` on Maps 9300's renderer (flat colours with the light
baked in, nearest first). Checked: block placement (`rotsign +1`: road exits match on all 21 Rally tracks),
winding and lighting on the PC harness (`docs/flythrough_pc_frames.png`: the dirt road between hills,
the castle gate, fences, the field), then the ARM build in the Unicorn emulator:

| RallyA1 along the replay, 320 x 200, far cut 180 m | instructions a frame | at 3 cycles / 150 MHz |
|---|---|---|
| a triangle at a time (`Poly`) | 2.9 M (2.0-4.6 M) | 58 ms, ~17 fps |
| the mesh path (`MeshBegin` / `MeshTris`, the corners once, in assembly) | **1.9 M (1.4-2.9 M)** | **38 ms, ~26 fps** |

Drawing only, before any simplification: the kit is still Nadeo's geometry. Where it goes: r3_verts 14 %,
the flat rasteriser 14 %, the coverage bits 12 %, the per-instance loop 10 %, MeshTris 9 %, r3_edges 8 %,
BoxVisible 8 %. 60-75 % of the submitted faces are back faces (a mesh's faces are all submitted).
`tm9300.sis` is on the local server; `docs/PHONE-TEST-1.md` says what to look at.

**The phone's own figures (7 Nov 2026, 11:27, tm9300 bench v0.1.2, direct screen access, 16 bpp):**

| RallyA1 along the replay, 320 x 200 | fps | draw ms | copy ms | slowest frame |
|---|---|---|---|---|
| mesh path, far cut 180 m | **29.1** | 24 | 9 | 46 |
| a triangle at a time (Poly), 180 m | 22.3 | 34 | 9 | 78 |
| mesh path, 90 m | 44.1 | 13 | 9 | 46 |
| geometry only (nothing drawn) | 45.5 | 12 | 9 | 46 |
| mesh path, no copy to the screen | 40.7 | 24 | 0 | 46 |

The phone beats the emulator's estimate (24 ms drawn against 38 predicted: ~2 cycles an instruction on this
code, not Hrimfell's 3). Geometry and rasterising are 12 ms each; the copy of the 640 x 200 screen is
9 ms (Hrimfell's measured 5: to look at). **The 15 fps target is met with a 2x margin before any
simplification**, which leaves room for the car, billboarded trees, a wider view and the physics.

## 4. Phases

1. **Mesh extraction, block map, LODs, collision, per-track counts: DONE** (§5).
2. **PC exporter** (Python, like `hrimfell/tools`): block kit → simplified near/far meshes, drivable
   surface triangles with surface type, collision hull; tracks → block lists; textures → 16-bit; replay
   paths → tuning data. Original names everywhere (as Hrimfell does); no Nadeo data in the repo.
3. **Flythrough on the phone**: Hrimfell's `r3d.cpp` + `spans.s` with Maps' nearest-first, a chase
   camera following a replay path through RallyA1. Measure in `emu/rungame` and with the Benchmark
   menu. Go / no-go for 15 fps here.
4. **Physics** on the PC host first (`host/` style tests driving the replay paths), then the phone.
5. **Game**: start/checkpoints/finish triggers (the `*Trigger*` mobils in the pak), timer, respawn,
   medal times, a ghost from the replay, engine sound (WAV → PCM stream, `audio.md`), menus.
6. Snow on the same engine, then Desert, Bay, Island.

## 5. The data pipeline, as far as it is proven

Pak-internal Gbx files decrypt only with a class-aware reader (the game XORs the parent class id of
every node into the Blowfish IV at the next 256-byte boundary); GBX.NET (`Src/GBX.NET.PAK`) does this.
Built here with the .NET 10 SDK; `PakToFolder -h Resources/FileHashes.txt X.pak` dumps a pak in ~6 s
with real file names.

**Block → mesh (`tools/BlockMap`, output `docs/pak_inventory/{rally,alpine}_blocks.json`).** A
`*.TMED<Kind>.Gbx` block info (Classic / Road / Flat / Frontier / Slope / Clip / Pylon) holds
`GroundMobils[variant][subvariant]` and `AirMobils[...]`: embedded CSceneMobil → CHmsItem → a wrapper
CPlugSolid whose `Model` is the external `Media/Solid/X.Solid.Gbx`. A placed block in a track picks the
row by its flags: bit 12 ground/air, `flags & 63` the variant (Road: base / deadend / corner / straight /
T / cross by connectivity; Frontier: 14 connectivity shapes; Slope: 2), `(flags >> 6) & 63` the
sub-variant (the Bis / Ter / A-D alternatives). Example: `RallyRoadCastle` air straight = 34 triangles,
cross = 564; `HillsToHills` has 14 shapes x 3 alternatives. Clip blocks (auto-placed edges) have no
mobils in some rows: ignore them.

**LODs (`CPlugTreeVisualMip`).** 161 of the 721 Rally solids and 39 of 585 Alpine ones carry 2-3 LOD
levels, switching at 16 / 20 / 40 / 64 / 128 m. The flat ground tile `RallyBase` is 460 triangles within
20 m (grass tufts) and 2 beyond 40 m; a hill tile 238 → 42 at 40 m; trees 416 → 123 at 128 m. The
phone will use Nadeo's lowest LOD as the starting mesh everywhere. Alpine has few LODs, so its snow
tiles (`SnowToSnow` ~108 triangles, bumpy snow) need the exporter's own decimation.

**Collision.** Every solid has a `Collision` child tree with `CPlugSurface → Geom → Mesh`: vertices and
*cooked* triangles, each with its plane equation and a **surface material index** into the solid's
material list (`WetPavement`, `Rock`, `WetGrass`, `Snow`, `Ice`, ...): the physics input, ready-made.
Rally kit 213 k collision triangles, Alpine 275 k; a straight castle road 34, a hill tile 238. The
exporter keeps only the triangles a car can touch (up-facing, near the road) and simplifies the rest.

**Per-track triangles (`tools/trackstats.py`, exact block → mesh assembly), 21 race tracks each:**

| | Rally, LOD 0 | Rally, lowest LOD | Snow, LOD 0 | Snow, lowest LOD |
|---|---|---|---|---|
| Whole track, median | 80 k | 41 k | 77 k | 73 k |
| Whole track, range | 36-292 k | 16-157 k | 31-133 k | 31-124 k |
| 150 m disc around the start | 2.5-35 k | 1.9-17 k | 1.2-26 k | 1.2-23 k |
| Densest 150 m disc | 12-41 k | 3-21 k | 8.5-27 k | 8.5-24 k |

Terrain is the bulk: `HillsToHills` + `RallyFlat` are 40-60 % of a Rally track at LOD 0, `SnowToSnow` +
`Hills` + `TundraHills` the same for Snow; trees next (`RallyTreeGround1SpeedUp` 10-44 k per track).

**Exporter model** (what a simplification pass leaves: flat tiles 2 triangles, shaped terrain tiles
≤ 24, trees as 8-triangle billboards, decor capped at 400 with far forms, everything else at its lowest
LOD; a frame = the densest 150 m disc / 3 for a 90° field of view):

| | Rally | Snow |
|---|---|---|
| Whole track | 10-51 k (RallyE 103 k) | 9-74 k |
| Frame, typical place | **500-1,900** | **600-2,200** (SnowA3 3,000) |
| Frame, densest place | 1,200-4,700 | 1,500-5,100 |

Typical frames land inside the 1,000-1,500 budget for both; the dense spots (RallyE's village, SnowA3's
Great Wall, SnowC3) need what Maps 9300 already does: nearest-first with a triangle budget, so the far
end is what gets dropped, plus a fog cut at ~120-150 m. **Rally stays the first target**; Snow is the
same engine with a terrain decimator.

## 6. Files (in `trackmania9300/`)

- `tools/nadeopak.py` — pak directory + non-Gbx extraction. `python3 nadeopak.py packlist.dat X.pak`.
- `tools/gbxtrack.py` — track block lists. `python3 gbxtrack.py track.Challenge.Gbx -v` or a `.tgz`.
- `tools/SolidStats/` — C# triangle counter (per LOD, collision, sprites, materials) / OBJ exporter over a
  pak dump; `tools/BlockMap/` — block info → mobil → solid map as JSON; `tools/Shared/TreeWalk.cs` — the
  tree walker both use (handles `CPlugTreeVisualMip` levels, which GBX.NET's own walker skips when the
  level node itself carries the visual). Both need the gbx-net checkout as `../gbx-net`
  (https://github.com/BigBang1112/gbx-net at aed16ae) and the .NET 10 SDK.
- `tools/trackstats.py` — assembles the campaign tracks from a block map, counts triangles per track,
  per 150 m disc, and under the exporter model (`-model`).
- `docs/pak_inventory/{alpine,rally,game}.txt` — the pak inventories (class id, size, path);
  `{rally,alpine}_solids.tsv` — triangles per mesh and LOD, collision, materials; `{rally,alpine}_blocks.json` — the block map; `campaign_tracks.txt` — blocks per track.
- `dump/pak_dump_rally_alpine_game.tgz` — the decrypted paks (Nadeo's data: never commit; `.gitignore`).
- `docs/tm_meshes_check.png` — six extracted meshes rendered as a check.
