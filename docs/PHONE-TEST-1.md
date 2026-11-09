# Phone test 1: the Rally A1 flythrough (tm9300.sis, 7 Nov 2026)

**Result (11:27, v0.1.2's automatic benchmark, `uploads/2026-11-07T10-27-10-982Z_tm9300_bench.txt`):
29.1 fps with the mesh path at 180 m (draw 24 ms, copy 9 ms, slowest frame 46 ms), 22.3 with the polygon
path, 44.1 at 90 m, 45.5 with nothing drawn (geometry 12 ms), 40.7 without the screen copy. Direct screen
access, 16 bpp, 22.7 MB RAM free.** v0.1.3 only fixes the digits 3, 6, 7, 9 of the counters.


The first thing to run on the real Nokia 9300. It is a **viewer**, not a game: the camera follows the
author's replay of RallyA1 (18.95 s, looped) 7 m behind and 2.5 m above the car, through the track built
from the TrackMania data by the exporter and packer, drawn with Maps 9300's renderer (flat colours with the
light baked in, nearest first with the coverage bits, 320 x 200 in the middle of the screen).

## Install

`localserver/tm9300.sis` (119 KB: the program and `rallyA1.tmb`, 193 KB uncompressed). Install as usual
(the OTA server). It goes to `C:\system\apps\tm9300\` (or the card: `!:`). Start **TM9300**.

## What to look at

The counters at the top left (white / yellow), each a number:

1. **fps** (frames in the last second) - the figure this test is for
2. draw ms (the renderer, average over the second)
3. copy ms (the back buffer to the screen)
4. polygons drawn
5. polygons submitted
6. the second of the replay (0-18)
7. the far cut in metres (180)

Expected from the emulator: ~1.9 M instructions a frame -> **~38 ms drawing, ~25 fps** if the phone runs
3 cycles an instruction as it did for Hrimfell; plus the copy (~5 ms). If the phone shows 15-25 fps the
estimate holds and the plan in `ANALYSIS.md` is confirmed. Please note the fps at a few places (the start,
the hill at ~4 s, the castle gate at ~12 s, the finish) and whether it is direct screen access
(copy ms ~5) or the slow path (~8+: a note was up, or the region check failed).

Keys: **Esc** quit, **Space** pause, **F** counters on/off, **M** mesh / polygon mode (A/B: the polygon
mode should be ~1.5x slower), **+ / -** the far cut 60-480 m, **arrows** look round (**0** straight
again), **1-9** speed (4 = real time), **R** restart.

What the picture should look like: `docs/flythrough_pc_frames.png` (the PC harness; the phone draws the
same pixels). Trees are dark cones (the lowest LOD without its sprite foliage: billboards are next),
the car is not drawn yet, the sky is a flat colour.

If it crashes (KERN-EXEC 3) or shows a black view, the likely spots: the `.tmb` not found beside the
program (the status screen says so), the 8 KB stack, or the DSA region. Tell me what the screen showed.

## Where things are

- `phone/` - the viewer: `src/tm9300_app.cpp` (the Symbian shell, after Hrimfell's), `src/tmcore.*` (the
  track format and the frame: portable, no Symbian), Maps 9300's `r3d.cpp` / `r3dgeo.c` / `r3dspan.c` /
  `fastmem` as they are; `host/tmhost.cpp` (PC frames to PPM: `g++ ... -o tmhost; ./tmhost track.tmb out`);
  `emu/` (the ARM emulator driver and `runtm.py`: instructions a frame, profiles, pictures);
  `build.sh` (needs the kit from `s80_DP_2_0_SDK/prebuilt`: `. ~/sym/env.sh`) -> `bin/tm9300.sis`.
- `tools/pc/` - `nadeopak.py` (pak directories), `gbxtrack.py` (track block lists), `assemble.py` (block
  placement: `rot_xz`, rotsign +1 proven on 21 tracks), `pack.py` (kit + track + replay -> `.tmb`),
  `trackstats.py`, `avgcolors.json` (the textures' average colours), `ReplayDump.cs` (replay -> JSON).
- `tools/KitExport` (C#, GBX.NET): a pak dump -> `rally_kit.json` (every block's mobil solids, LODs,
  collision with surface ids, materials -> textures); `BlockMap`, `SolidStats`, `Shared/TreeWalk.cs`.
- The kits and dumps are Nadeo's data: not in git (`dump/`).

## Next (in order), after the phone's number

1. Packer: real simplification (terrain tiles decimated, trees as billboards with a texture, the 400-cap
   replaced by far forms), the base plate, the car at ~500 triangles from `RallyCar`.
2. 640 x 200 full width (the emulator says what it costs), or Hrimfell's doubled layout.
3. Physics on the PC host against the replay (the collision triangles carry the surface ids: Dirt,
   WetDirtRoad, WetPavement, Wood, Turbo ...), then the game loop on the phone.
4. Snow on the same pipeline (`alpine_kit.json` is already exported).
