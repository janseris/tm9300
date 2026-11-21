# The car on the track: physics, collision, the drive mode, the depth order (9 Nov 2026)

State: **tm9300 v0.3.1 is a playable game** on two tracks (RallyA1, SnowA1): the car drives on Nadeo's
own collision geometry with an arcade model fitted to the game's replays; checkpoints and the finish count;
a follow camera; the car tilts with the ground. Everything is integer, 50 steps a second, ~1.5–2 % of the
phone's CPU (ARM emulator: 22–24 k instructions a step), bit-exact between the PC and the ARM build.

## What the phone test said (v0.2.0, the flythrough, 320×200 in the middle of the screen)

| variant | fps | draw | copy |
|---|---|---|---|
| mesh path, 180 m, with the car | **26.5** | 31 ms | 5 ms |
| the same without the car | 29.5 | 27 ms | 6 ms |
| 90 m far cut | 38.8 | 20 ms | 5 ms |
| geometry only (nothing rasterised) | 49.7 | 14 ms | 5 ms |

The copy to the screen dropped from 9 ms to 5 ms by copying only the view and the counter bar
(`fm_memcpy` a row, `Update(region)`). The car (542 triangles) costs ~4 ms. The geometry share (14 ms of 31)
is the next thing to attack (see "Next").

The shaking the user saw was the trailing camera snapping to the replay's 10 Hz samples; it now glides
(the trailing point is interpolated inside the sample it lands in). The benchmark pauses with the counters
when it is done and shows its progress (variant n of 5 and a bar) while it runs.

## Three placement bugs the flythrough had hidden

The ground probe compared against the replay's car heights (`tools/pc/groundcheck.py` over
`phone/host/tmgrounddiag.cpp`: the ground under every replay sample) found:

1. **The rotation sense was inverted.** `rot_xz` turned blocks the wrong way round; the earlier "proof"
   on road exits was on pieces symmetric under the swap. Now `rotsign = -1` (assemble.py), checked on both
   tracks: every road sample sits on its road within 0.3 m.
2. **Multi-cell blocks pivoted about the wrong corner.** The Gbx cell is the lowest corner of the *turned*
   footprint: the mesh is turned about its first unit's middle and then shifted by the turned footprint's
   lowest unit. Blocks without a unit list in the kit (Alpine's Great Wall pieces: 62 of 209) get their
   footprint from the solid's box, rounded to whole cells.
3. **The zone tile under every ground block was still drawn** (the grass flat at 48 m over a road sunk to
   46.3 m: the car looked buried, and which one won was luck in the sorter). Every Rally ground block carries
   its own terrain over its whole footprint (checked: footprint ≥ 1.0 cell for all of them except the Clip),
   so `drop_covered_zones()` removes a zone tile whose cell middle a ground block's collision mesh covers.
   RallyA1: 88 tiles dropped.

Also found: **Alpine's cells are 16 m high** (Rally 8 m): `ENV_BLK_H`. And the KitExport keyed solids by
their file name, which Alpine reuses per block folder (`GreatWallGTCurve3/Air.Solid.Gbx`): the key is now
`folder/name`, and the big Alpine pieces keep their mobils in the newer `GroundBlockInfoMobils` rows
(chunk 0x01E), which the export now reads too (Alpine: 454 → 539 solids).

## TMB4 (pack.py v3)

TMB3 plus, after the path: the collision meshes and their instances, the start.

```
'TMB4' u32 nMesh u32 nInst u32 nPath i32 carMesh (top byte: the car's class 0 Rally 1 Snow 2 Desert 3 Bay 4 Island)
i32 sky ground fog fogStart fogEnd
u32 nColl u32 nCInst  i32 startX startY startZ (1/16 m) i32 startYaw
meshes, instances, path as TMB3
coll mesh: u16 nv u16 nf i16 bbox[6]; i16 v[nv*3]; u16 idx[nf*3]; i16 normal[nf*3] (*8192, up); u8 surface[nf]; pad 4
cinst: u16 coll u16 kind (0 plain, 1 start, 2 checkpoint, 3 finish, 4 multilap); i32 x y z; i32 bbox[6]
```

Surfaces: 0 road (pavement, wood, metal), 1 dirt, 2 grass, 3 rock, 4 water, 6 turbo, 7 ice, 8 snow
(5 "not collidable" is dropped). The collision comes from Nadeo's `CPlugSurface` meshes, **as they are**:
only the faces the car can stand on (|ny| ≥ 0.25) are kept, two-sided faces turned up. Decimating them by
vertex clustering was tried and dropped: it opened seams between pieces (the car fell through or hit a
0.76 m "wall") and merged road edges with their shoulders (a kerb under one wheel every piece). RallyA1
carries 70 k collision triangles (989 KB file), SnowA1 35 k (768 KB). The phone has the RAM; the probe cost
is kept down by (a) a 32 m cell grid over the instances, (b) an 8×8 grid over each mesh's faces, built at
load, (c) a hint: each wheel tries the face it stood on last step first and keeps it when the point is still
over it and the height within 0.5 m (on a road that is 90 % of the probes).

A loader that sees `TMB3` still works (no collision: the viewer only).

## The car model (tmphys.cpp)

Units: positions and speeds in 1/256 m, yaw 0..4095 (×16 inside), 20 ms steps. No floats, no libc.

The figures were fitted to the campaign replays (10 Hz samples with velocity and orientation,
`tools/pc/ReplayDump.cs`): per 10 m/s speed band the 80th percentile of the acceleration on level ground
(= the pull at full throttle, drag included), the 95th percentile of the yaw rate (= full lock), the slip
angle (10–15° in a Rally drift), and the vertical acceleration in the air (the mode is **27 m/s²**: the game's
gravity is heavier than Earth's).

- longitudinal: `v += pull[class][band] * surf` at full throttle; coasting loses 2.5 m/s² + 0.0005 v²,
  soft ground adds its share; brake −16 m/s², reverse to 15 m/s; the slope adds g·(f·n)·ny; a turbo
  pad +60 m/s².
- steering: the wheel ramps to full lock in ~100 ms; yaw rate = lock · turn[class][band] · surf; below 10 m/s
  the rate is bounded by a 5 m turning radius.
- grip: the sideways speed decays by a per-surface factor a step (road 0.85, dirt 0.88, grass 0.93, ice 0.97)
  and scrubs ~0.8 of itself a second off the speed.
- the ground: four wheel probes (±0.9 m, +1.7/−1.5 m); the height is their mean, the normal from the axle and
  track slopes (smoothed over ~4 steps so a kerb under one wheel is not a ramp), the surface the rear wheels'.
  The car stays on the ground unless the ground falls away faster than a throw would (y + vy·dt − ½g·dt²):
  TrackMania's cars stick over crests but fly off real ramps. The climb rate is measured from the ground itself
  so a ramp's end throws the car at that rate. A step up of more than 0.75 m is a wall: the car stops and
  bounces back a quarter of its speed. Off the collision meshes the ground is flat at the last height.
- the race: a checkpoint counts once per instance (a bit each), the finish only with all checkpoints and
  after the first second (start/finish lines).

Per-class tables exist for all five cars; Rally's and Snow's are from the replays and driven, the other three
are first fits from the same analysis (`Desert`, `Bay`, `Island` rows) and untested.

### Validation: an automatic driver on the PC

`phone/host/tmdrive.cpp` steers the car toward the replay's path (12 m + 0.4 s ahead), brakes for sharp
turns and keeps to the replay's own pace, and renders the follow camera. It **finishes RallyA1 in 20.4 s
(the replay: 22.0 s) and SnowA1 in 17.9 s (21.0 s)**, taking both checkpoints, within a few metres of the
replay's line throughout. It is the regression test for anything touching the physics or the packer.

## The drive mode on the phone (tm9300_app.cpp)

- starts in the car at the start line when the track carries collision; **↑ throttle, ↓ brake / reverse,
  ←/→ steer, Enter (or R) restart, N the next track, V the flythrough, B the benchmark** (pauses with the
  counters when done, then back in the car), F the counters, +/− the far cut.
- the physics is stepped by the clock (20 ms a step, at most 4 steps a frame so a slow frame does not
  snowball), the frame drawn from `TmFollowCam` (8 m behind the car along the way it moves, 2.5 m up,
  smoothed, never under the ground: it probes).
- the right-hand counters: km/h, the time (s and 1/100), checkpoints taken / in all, a 1 under them when
  finished (the time then stays).
- the car tilts with the ground: `TmView::iCarSlopeF/R` shear the car's corners before the yaw turn.

## The depth order (0.5): a depth test for the near polygons, in the engine

R3D draws polygons sorted by the depth of their middle into 1 m buckets, nearest first, skipping pixels
already drawn (the coverage bits). Two surfaces close together fight in that order: a bridge deck 0.34 m
over its water, a road over the terrain tile, the car's underside over the road. Whichever middle sorts
nearer covers the other's pixels: the "triangles" on the RallyB1/C1 bridges, the car's wheels in the road.
v0.3/0.4 patched the data (water under decks and faces under low pieces dropped in the importer); the
rule now is **the engine draws what the data says**, so those work-arounds are off (`pack.py --depthfix`
turns them back on for comparison) and the fix is in R3D 1.60:

- **Every flat polygon nearer than 40 m at a corner carries its plane** (`StorePlane`, 1/z affine over the
  screen: `w·2^8 = A·(x−cx) + B·(y−cy) + C`, 32-bit, from the world geometry turned to the camera, exact to
  1e-4; Poly's clipped polygons from their camera-space corners, pieces without one fitted from the
  rounded corners). ~100 instructions a polygon.
- **A depth buffer of 2^18/z per cell of two pixels** (320×200: 32 kB). On a near polygon's runs (what
  r3_cover found not drawn yet) the pixels and the plane's depth are written; in the **gaps** between the
  runs (drawn already by something sorted nearer) the pixel and its depth are replaced where this polygon
  is nearer. Far polygons (all corners beyond 40 m) write their middle's depth on their runs and are never
  tested: beyond 40 m a misorder is a pixel. `RasterNear`, `ZGap` in r3d.cpp; `iZNear` sets the reach
  (0: off; the **Z key** on the phone toggles it for A/B).
- Measured against a z-buffer reference of the same polygons (`tmhost` with `REF=1`, `R3_REFERENCE`
  build: `ReferenceFrame`, `DebugPixel`): the RallyB1 bridge frame had 4.6 % of its pixels wrong in
  blobs before (the deck, the banks), 0.47 % after (all beyond 40 m or shoreline pixels where the bank
  meets the water within a centimetre).
- Cost in the ARM emulator (320×200, 5 frames each): RallyA1 2.29 → 3.03 M instructions a frame,
  SnowA1 3.69 → 4.79 M, RallyB1 2.17 → 3.15 M: about +0.7–1.1 M, ~30 %. Half of it is the per-row
  bookkeeping in C (`RasterNear` ~95 instructions a row with ~19 pixels a row); an assembly version
  would halve that. If the phone shows the drop matters, that is the next step; `Z` turns it off
  meanwhile.

What was tried first and dropped: re-sorting the polygons by their planes (Newell's test on
overlapping screen boxes within a window of depth buckets, big polygons cut into depth slabs first). It
worked on the bridge (0.65 % wrong) but cost 2.3 M instructions a frame however it was arranged (the
pair search and the topological sort), more than drawing the frame; the depth test is both cheaper and
exact.

## 0.6: what the second phone test found, and what changed (9 Nov 2026)

The phone test of 0.5: 19 fps on Rally (26 before the depth test), the road still cutting into the car, the car
jumping on RallyA1, Rally driving "like Island", the car sinking into uphills at speed (B1, D2) and into stones,
the D2 start platform clipping through the car at 12 fps, the camera leaping over bridges, buildings, gates and
tracks overhead, the Snow car climbing the road's rails and passing castle walls, no visible depth-test state,
no checkpoint respawn. Every fix below was measured on the PC first (the auto-driver, a z-buffer reference of the
frame, the replays) and checked in the ARM emulator (physics and frames identical to the PC bit for bit).

### Drawing
- **The depth test had a hole**: a near polygon that `r3_cover` found completely drawn over already was skipped
  before the depth test ran. The car's triangles under a big road triangle whose middle sorted nearer were all in
  that case: the reference frame showed the whole car, the engine half of it. Fixed (`Raster`: such a polygon is
  all gaps); the car now draws as the z-buffer reference does.
- **A depth value per pixel** (was one per two pixels: neighbouring faces of the car shared a cell and the one
  judged a hair farther lost its pixel to the road: dots inside the car). The routines in assembly
  (`fm_zrows`, `fm_zrun`, `fm_zgap`), identical to their C references. A tolerance instead was tried: it let the
  car's own far faces show through. 128 kB of RAM.
- Completely hidden polygons are tested only within 16 m (the car and what it stands on): testing every pixel of
  every hidden polygon to 40 m was the test's biggest cost.
- **Side culling** of instances (their bounding circles against the view's side planes) before the sort and
  `BoxVisible`: ~80 k instructions a frame back, frames unchanged.
- Cost now (emulator, 5 frames each): RallyA1 2.20 → 2.91 M instructions with the test, SnowA1 3.60 → 4.70,
  RallyB1 2.09 → 2.95, RallyD2 2.48 → 3.33 (0.5 was 3.02 on RallyA1 with the test, i.e. ~19 fps; expect ~20). The
  cost is per polygon and per row more than per pixel: a car-only mode (the test only in the car's screen box)
  was built and measured at +0.61 M against +0.70 M for the full test, and dropped. A 250-triangle car was tried:
  vertex clustering wrecks its shape below ~500.
- The **Z** key toggles the test; the bar shows **Z ON** (green) / **Z OFF** (red).

### The car
- **It could not drift.** The step turned the car's velocity together with its heading, so turning never made any
  sideways speed: zero slip on every track (measured), which is what felt like the Island car. Now the velocity
  keeps its direction when the car turns and the tyres take out at most a grip (m/s², by class and surface) of
  the sideways speed per step; the slide is capped (Rally 32°, Snow 17°) and costs speed. Fitted to the replays'
  slip angles: Rally 90th percentile ~8–20° at 20–40 m/s (replays 5–11°, hardest 20–30°), Snow 0–3° (replays
  2–3°). Nadeo's tunings agree on the ratio (Rally `MaxSideFriction` 25, Snow 100). With it **the Rally lap
  times match the replays**: A1 22.5 s (replay 22.0), B1 20.4 (20.2), B2 21.6 (22.1), A3 20.3 (20.4), A5 22.7
  (22.3). The straight-line pace was not too fast: the replays do 0–100 km/h in 1.0 s and ours 1.1 s.
- **Climbing**: a surface is judged by its own slope, not the slope the car stands on: it is climbed when its rise
  over the step is within 0.45 m plus the move times its own steepness, so a 45° bank met from flat ground at
  speed is climbed (0.5 took it for a wall, the car bounced and a step later stood inside the bank: the uphill
  sinking, the stones), and a rail's flat top 0.6 m up is not. Surfaces more than 1.2 m plus the move above the
  car are overhead (bridges, roofs) and ignored. Nothing under the car but a drivable surface within 2 m above:
  the car is put on it.
- **Walls**: a bevelled face (26–63°) is a wall where the game's own collision mesh has a steep wall right there
  facing the same way (pack.py `mark_walls`): the snow road's rails and the castle walls are drawn bevelled and
  were slopes the car climbed. The wall test follows the move's vertical part (a falling car passed leaning castle
  faces), and the final move is checked once more (a slide along one face could cross the next at a corner).
  Checked by brute force (every wall face of the track against every step, steering into the sides): SnowA1 0
  crossings driving normally, 4 in a run spent pressed into the walls (a wedge between two leaning faces).
- **Jumps**: the car leaves the ground when it falls away faster than a throw by more than 0.1 m, two steps
  running: the real jumps happen (A1 1 of the replay's 3 jumps of 0.3 s or more, B1 3 of 5, C1 3 of 7, D2 6 of 8)
  and the small hops (14 a lap on the replay of A1) do not — 0.5 bounced on every seam, last night's 0.25 m never
  left the ground at all.
- **Checkpoints**: an elevated checkpoint's pillars carried the checkpoint flag too and counted as checkpoints of
  their own, so 10 of the 42 tracks could not be finished (RallyE: 33 "checkpoints" for 22). The boxes on one
  spot are now one checkpoint: on all 42 tracks the replay takes every checkpoint (`cpcheck`). **Enter** puts the
  car at the last checkpoint taken (the start when none), **Backspace** at the start.

### The camera
- It rides on the car's own trail (a point every 0.4 m, 25 m kept): 8 m back along it, 3 m over where the car
  was, looking at a point 1 m over the car. It goes where the car went — under the Rally E building, under a
  bridge or a track overhead, through a gate — and never searches for ground, so it cannot leap onto what is
  above. Smoothed per physics step (it was per frame: at 19 fps the heading lagged ~0.2 s).

### Regression (all 42 campaign tracks, `packall.sh`)
20 finish under the auto-driver (0.5: 15), every Rally A/B track among them. The failures are mostly Snow C/D
(wall rides and loops the physics does not model) and long E tracks.

## 0.8: ghosts, medals, the menu, a higher camera, walls on everything (9 Nov 2026)

### The camera
6.5 m back along the trail and 5.2 m up (was 8 m, 3 m), looking at a point 7 m past the car, so the car sits in
the lower third and the road ahead shows over it (Nadeo's own cameras, `Vehicles/ControlCamera/*.CtrlCamTmRace`:
"Close" 3 m up / 6 m back / 75°, "Behind" 5.9 m / 7.5 m / 75°, "Behind 2" 5.5 m / 9 m / 70°). Under a roof,
a bridge or a track overhead it stays 1.5 m under the first surface over the trail (at least 1.2 m up).

### Ghosts (`TmGhost`, tmphys)
- A run is the car's place every 5 steps (100 ms, 16 bytes a point, 10 minutes at most), the step it took each
  checkpoint and the finish. Recorded while driving; played back by the race's step (interpolated).
- The author's ghost is the track's replay (its checkpoints found by running the path every 20 ms through the
  checkpoint boxes with the car's own grouping). Gold, silver and bronze are the same run on a slower clock:
  it crosses the line at the medal's time. The medal times come from each track file's header (chunk
  0x03043002 / 0x24003002) into a `MEDL` trailer at the end of the .tmb.
- The record (best finished run) is saved per track to `C:\Data\TM9300\<track>.ghost` ("TMG1"); "last run" is
  the previous finished run: in the hot seat that is the previous player.
- Drawn in the car's colours halved towards pale blue, in its place among the instances by distance, not
  within 3 m of the car. Cost on the ARM emulator: +0.06 M instructions a frame far off, +0.25 M close (2-8 %).

### What the replays really say (a correction)
The replay files run on past the finish: their length (22.0 s on Rally A1) is not the author's time (18.95 s).
Against the real times the auto-driver is ~19 % slower on Rally A1/B1 (22.5 vs 18.95, 20.4 vs 17.1) — it
keeps ~105 km/h through corners the author takes at ~175 — and about even on Snow A1 (17.5 vs 18.0). So the
author ghost will pull away on Rally; the Rally car's cornering grip is the next thing to look at.

### Collision
Every steep face of a piece that has any collision in the game is now a wall (was: only where Nadeo's
collision had a wall within 0.6 m facing the same way, which the simplified shapes of fences, posts, houses
and rocks often missed). The car checks walls 0.25 m and 0.6 m over its wheels, so kerbs and painted edges
still let it over. Regression unchanged (20/42 finish, the same times); up to 14 % more triangle tests.

### The app
- Esc opens a menu (continue, restart, ghost: off / Nadeo / gold / silver / bronze / record / last run,
  hot seat players 1-4, depth fix, next / previous track, quit); the race stands still while it is open.
- The joystick's press no longer respawns (Enter does).
- After the finish the clock stops, the keys do nothing (the car brakes to a stop), and a box shows the time
  in the medal's colour, the medal, the difference to the ghost and "press Enter to restart" (in the hot seat:
  the next player). The panel on the right is in the phone's own fonts: the time as the game shows it
  (m:ss.cc), km/h, checkpoints, each checkpoint's difference to the ghost (blue ahead, red behind) for 3 s,
  the ghost, the player, the track, "depth fix on/off (toggle: Z)", and the players' bests.

## 0.9: the cars' numbers from the replays and Nadeo's tunings (9 Nov 2026)

The author replays carry position, rotation and velocity every 100 ms; joined with the ground under each sample
(`tmsurf`), they give the tyre forces per mass (acceleration minus gravity, in the car's frame) by surface and
speed (`tools/pc/physfit/`). What they say, and what changed:

| | the replays | Nadeo's tuning | 0.8 | 0.9 |
|---|---|---|---|---|
| Rally sideways grip, road | 58 m/s² (90th %, flat ground, 30-80 m/s; a hard ceiling: 98th % 61-64) | steer radius 5 m + 0.7 s × v, MaxSideFriction 25 | 32 | 58 (dirt 54, grass 39) |
| Rally pull, straight (median) | 45 / 26 / 17 / 6 / 6 / 5 / 4 / 4 m/s² by 10 m/s | AccelCurve 60@0, 25@50, 8@100, 4@200 km/h | 26 → −10 past 60 m/s | the replay's medians |
| Ground gravity | climbs fit 29.6 (Rally), 28.9 (Snow) | GroundGravityCoef 3 | 9.8 | 29.4 |
| Snow turning | 2.1-2.5 rad/s (98th %) at 50-130 m/s; a flat U-turn at 228 km/h, 32 m radius | radius 2 m + 0.4 s × v | 1.2-1.4 rad/s | Nadeo's radius |
| Snow sideways grip | road 100-150 m/s² (slip 1°), snow 25, ice 1-4 | MaxSideFriction 100 | 49 | 145 / 28 / 4 |

Two physics bugs the faster car found: a wheel over a road's edge took the ground far below (the water under a
bridge) into the car's height and dropped it off narrow roads (C2); and a terrain tile left over a road 1-5 m up
was a wall to climb (A2, A4) — now the car keeps to the surface at its level when the one above is over 1 m up.

The auto-driver was the other half of the gap: it braked for any bend over ~35° at speed. `DRIVER=2` knows the
car: pure pursuit on the replay's line and the speed each bend allows (its curvature against 52 m/s² sideways
and 1.3 rad/s for Rally, 120 and 2.2 for Snow), braking in time. Times against the medals (all 42 tracks):

- Rally: gold on A1 (19.46), A2, B1, B2, C5; silver on A3, A5, B3, B5, C1, D2; slower on A4 (a missing hill
  road piece in our import: the replay drives 3-15 m above our ground), C2, C4; 7 do not finish.
- Snow: faster than the author on A1 and A4, gold on A5, silver on B1 and B3; the C/D/E tracks need wall rides
  and loops the model does not have.
- The physics costs ~74k instructions a step on Rally A1 (was ~37k: the walls on everything): ~5 % of the CPU.

## 0.10 (9 Nov 2026)
- The depth fix is always on (no Z key, no menu item): without it the near polygons clip through the car.
- The race panel costs nothing a frame unless something on it changes: the clock and the speed are copied from
  glyphs drawn once per font (no font calls a frame), the other lines are drawn only when they change, and only
  the panel's changed rows go to the screen. H (or the menu's "race panel") turns it off.
- No "Esc: menu" line (the panel did not fit 200 pixels).
- The camera 7.8 m back and 5 m up (0.8: 6.5 m and 5.2 m, too close).

## 0.11: the game's menus (9 Nov 2026)
- The app opens on the game's start-up (its loading picture, the orange "Press any key" bar) and its menus:
  Welcome (Play Solo, Party Mode, Quit; Play Online, Editors and Profile greyed as the phone has none), Play
  Solo: United, Races, the five colours (White A .. Black E) with the tracks as the game's grid (pictures, a
  padlock for what is not installed, the medal won), then "Choose an opponent" (the Nadeo, gold, silver and
  bronze medals with their times, your record, none). Party Mode: Hot Seat (the attempts, up to eight named
  players), then a series.
- The pictures are the game's own: the icons (Icons128x128), the medals (MedalsBig), the race timer's LED digits
  and plate, 3-2-1-Go, the United logo (`tools/pc/uipack.py` cuts them from GameData into `tm9300.ui`), and the
  globe and the loading picture from the user's screenshots. They go to the phone only, never to git.
- Each track's picture is drawn on the PC by our own renderer from high over the replay's line (`host/tmthumb`,
  `<track>.tmt`, with the medal times).
- The menus are plain C++ (`tmui.cpp`): the same code draws them on the PC (`host/tmuiprev`) and on the phone;
  the ARM emulator's pixels equal the PC's.
- The race as the game's: a 3-2-1-Go countdown with the clock running up from -0:01.50, the LED timer on its blue
  plate under the view, the opponent's medal and time and the player(s) on the left, the track and "Nadeo" top
  right, the speed in LED digits bottom right, a checkpoint's time with the gap to the opponent at the top, and
  the result (the time in the medal's colour, the medal, the gap) with "Press Enter to restart". Esc pauses
  (continue, restart, opponent, the race panels, back to the menu).

## 0.14: the race menus and the hot seat as the game's, the game's font (9 Nov 2026)
- Every dialog is measured against the 200 lines before it is drawn: the text font if all of it fits, else the
  small one, and a list longer than that scrolls to keep its selection in view (`TmDlg` in `tmui.cpp`). The menus'
  left column takes its row and title heights from the fonts (`TmMenu::RowY`). The PC preview is checked with
  fonts bigger than the phone's (24/18/17) as well.
- Esc in a solo race: "Race menu" (Choose opponents, Restart, Race panels, Continue | Quit), small, over the
  paused race (only lightly darkened). "Choose opponents" is the game's ghost list: the medals, your record and
  your last run, slowest first, with their times; up to four at once (`TmView::iGhosts`), the fastest chosen one
  the reference for the checkpoint times.
- The hot seat as the game's: settings Game mode (Attempts or Time) and its value (1..10 attempts, or 1:00..15:00
  a player), eight player slots; a slot opens "Choose a profile" (the saved profiles, "Create a new profile",
  "Cancel"; Del removes a profile; kept in `C:\Data\TM9300\profiles.txt`). In the race a player's turn is all his
  attempts (or his time: the run on when it runs out may still finish), after "Now it's player X's turn! OK";
  his best run of the turn is his ghost; the orange bar under the players shows what is left of the turn. Esc:
  "Hot Seat" (Continue, Give up, Quit). After the last player the standings, then the series' next track.
- The game's own font (Gill Sans from `GameData/Menu/Media/Font/United_Textures/United_00.dds`): `tools/pc/tmfont.py`
  cuts ASCII out of the sheet into `tm9300.fnt` (never in git); the phone scales it at start-up to the game's
  proportions against the menus' rows (titles 15 lines in bold, text 11, small 10; the phone's own fonts filled the
  rows). Without the file the phone's fonts are used as before.
- The campaign as the game's: each series' first track is open, the next opens with a medal on the one before it in
  the same series (bronze on White, silver on Green and Blue, gold on Red; a track before it that is not installed
  does not hold it shut); Black opens with gold on every track of White to Red. A locked track shows a padlock and "(conditions)"; Enter on it says which medal
  opens it. After a medal the result offers the next track (Enter; Backspace retries), "Next track open!" when the
  run has just opened it.
- The camera further from the car: 9.5 m back and 5.6 m up (0.10: 7.8 m and 5 m, still too close).
- The menus' picture pack is no longer a writable static (the phone's programs may not have one: petran had
  refused it), it sits in the thread's slot.

## 0.15: falls, the car's body against posts, the Snow forests, the hot seat's order, the game's pictures (9 Nov 2026)
- Off a ledge the car falls (the air's 27 m/s^2) to the platform below: the one step's grace that kept it glued over
  seams now covers 0.5 m at most (it put the car on a platform any distance below at once). SnowD3 now finishes
  (it got stuck before), RallyE takes 19 checkpoints of 22 instead of 7, SnowA1 and B1 are 0.24 s slower (the falls
  take their time).
- Walls are checked at the car's front corners too (0.85 m to each side, 1.9 m out, at 0.35 m and 1 m over the
  ground's plane; the rear ones in reverse), and in a slide (sideways over 2 m/s) at the leading side's middle,
  not only on its middle's line: a checkpoint's or the start's pole, a log the middle passes by stops the car.
  Not on banks and wall rides (the road rises past the corners there). A whole box at all times (the rear corners
  and both sides too) snagged the narrow wooden roads' rails and the castle roads in a drift: the walls are the
  drawn shapes' steep faces, rougher than the game's collision.
- The Alpine hills' forest cards (crossed quads 17 x 28 m, the trees in the texture's alpha) were drawn as whole
  dark rectangles: each is now two fir shapes (`tree_cards` in `pack.py`), and the tree textures' colour is the
  average of their opaque pixels; the ice's colour (its texture is a reflection table) is a light blue.
- The camera 11 m back, 4 m up, looking at a point 30 m on and 1.5 m up: lower and further, the road ahead in view.
- The hot seat as the game's: after every attempt the player without a time, else the one with the slowest best,
  drives next (a player stays on while he is the slowest); "Now it's player X's turn!" when it changes; the
  standings when nobody has attempts (time) left. The ghost is the best run of the track so far.
- The track pictures are the game's own (the thumbnail in the Challenge.Gbx header, `tools/pc/tmthumbgbx.py`).
- The game's loading screens (Snow, Rally; Desert's for later) while a track loads, then "Press any key".
- Esc on the start-up screen leaves the game.

## 0.16: Desert (9 Nov 2026)
- Speed.pak (Desert) dumped with GBX.NET's PakToFolder, exported by `KitExport` (now every solid of
  `Vehicles/Media/Solid`: Desert's car is `American.Solid.Gbx`, 12,210 triangles, 437 on the phone), its textures'
  colours averaged on the PC (`avgcolors.json`). Desert's cells are 16 m high, as Alpine's (`assemble.py`; at 8 m
  the replays drove 32 m over the road).
- Its sand tiles (`RiverBeach/*`, 350-900 triangles each) are simplified coarser than the other environments' ground
  (`D_GRID=3 R_GRID=2 pack.py --budget 150`): ~1,000-1,300 triangles drawn a frame, between Rally and Snow.
- The car's tables from the 21 Desert replays (`physfit/pull.py Desert`, `yaw.py Desert`, `replaygrip.py Desert`):
  the pull 29 m/s^2 to 30 m/s, then 9-10 to 110 m/s (its top speed is ~130 m/s: 470 km/h), the turning 1.6-2.0 rad/s,
  the sideways grip 66 m/s^2.
- The body on springs (drawn only): a bump or a landing throws it, the spring brings it back; the Desert car's
  soft (1.4 Hz, damping 0.12: it rings, as the game's bouncy American car), Rally's and Snow's stiff (3 Hz, 0.6).
- Installed: DesertA1, B3, C1 (the auto-driver finishes them; the others need work: the garage starts' long slopes
  have a sand tile left over them, A2/A3's jumps, the D and E tracks). The menu's grid and the hot seat's series
  list every installed environment (the grid three a page in the game's order).

- The camera at the start (and wherever the ground behind the car rises: a hill, a bank) stays at its height: a
  surface well under it is ground it is above, not a roof (the Rally starts held it 1.2 m over the road). Tried a
  whole box for the car's body (its size from the car's mesh) against walls from Nadeo's own collision meshes
  (`NADEOWALLS=1 pack.py`): the narrow roads' rails and the hills' sides still snag it (RallyE stops at a hill's foot,
  SnowE at 2 checkpoints of 27 instead of 6): not shipped.

## 0.17: Desert, the jumps (9 Nov 2026)
- **The air's gravity is 40 m/s^2**, in all three environments: the replays' vertical acceleration in the air, from
  the positions' second differences (the mode, sharply: Desert, Rally and Snow alike; 27 was a wrong fit). With it
  SnowE goes 26 checkpoints of 27 (was 6), RallyE 19 of 22 (was 13).
- A ramp's launch: the car leaves it at the climb rate the ground itself gave it (the smaller of the last two steps',
  so a kerb's one step launches nothing), not at the slope under the smoothed normal, which lags behind a kicker
  (DesertD3's big jump flew a third too low); no one step's grace off a ramp.
- Desert's turbo: a boost of 17.6 m/s^2 for a second from the pad on (its replays: 71 -> 97 m/s); Rally's and Snow's
  as before. Desert's speed cap 140 m/s (its replays reach 120; the others stay at 100).
- A tunnel's cell has no terrain tile over it (`assemble.py`: a tunnel block, air variant too, is dug into the
  terrain): the Desert garage starts' tunnel slopes were covered by a sand tile (DesertD1 got stuck at the start).
- Desert's roads, ramps and garages are simplified as finely as the other environments' (`COARSE` in `pack.py`: the
  coarse grids only for the sand, cliffs and rocks; `ROAD_BUDGET=300`): a ramp clustered at 3 m lost its slope.
  Still ~1,100-1,400 triangles drawn a frame on average (Rally 900, Snow 1,600). `tools/pc/packdesert.sh` packs
  and drives all 21.
- The body on its springs is the car's own now (`TmCar::Suspension`, was the app's, drawn only), and on the Desert car
  it drives: the load the body puts on the wheels (the spring's and the damper's push over 20 m/s^2, 0.5 .. 1.3 of
  standing) scales the grip and the turning, so after a bump or a landing, while the body rebounds, the car holds and
  turns less (the game's American car); Rally's and Snow's stiff cars bite always.
- The auto-driver finishes Desert A1, A4, A5, B1, B3, B5, C1, C2, C3 (D1 too, before the turbo's change); the others
  fail on its line-keeping after jumps and at the E start (the author turns on the spot). Installed: A1, B3, C1, D1.

## 0.18: Bay and Island

- Two more environments, each its own package (`tm9300_bay.sis`, `tm9300_island.sis`, installed beside the tracks
  package): Bay A1, B1, C4, D2 (the BayCar) and Island A1, B2, C2, D1 (the SportCar, 250 m/s). Bay's cells are
  32 m (its replays), Island's 64 m; a 64 m checkpoint block is cut into four tiles that count as one checkpoint.
- The kit's solid names: Bay keeps `Road/Turbo/Ground`, `Bridge/Turbo/Ground` and `Platform/Turbo/Ground` (79 such
  pairs in Bay, 2 in Island, 1 in Alpine, none in Rally and Speed); the kit had kept the first one's shape for all of
  them, so BayC1's bridge turbo stood 1.3 m low with a turbo platform's gantry across the road. `KitExport` puts the
  grandparent folder in front of a name that collides now.
- The turbo: a timed push per car from the pad on (Desert 17.6 m/s^2 for 1 s, Bay 29 for 0.6 s, Island 29 for
  1.2 s); the red pads (`turbo2`, surface 11) push 215 m/s^2 while on them. The ground's climb rate is held to 80 m/s (was 40: Island's
  ramps throw its car at 52).
- Bay's car falls at 29 m/s^2 (its replays; the others at 40): BayD1's big drop landed short.
- The launch: thrown up off a ramp, the upward speed is taken from the flat speed (BayD2's kicker: 96 m/s flat ->
  86 flat and 33 up; the car flew a fifth too far), and a ramp's rounded lip throws the car where it starts to round
  (the throw's shadow: once the ground falls away under the throw, the car's flight is followed from there and it is
  in the air when the ground is 0.25 m under it; re-based each step on the ground, the throw alone never saw a slow
  rounding, and the car rode the lip down to a third of its climb).
  Jump test (`jt`: each replay take-off, the car started 0.8 s before at the replay's speed, straight on; the
  landing distance against the replay's): mean |log(sim/replay)| Rally 0.131 -> 0.110, Snow 0.197 -> 0.172,
  Desert 0.258 -> 0.207, Bay 0.254 -> 0.178, Island 0.199 -> 0.186; Bay's apex -5.4 m -> +0.1 m.
- Island and Bay: off a platform's end over the sea the car falls (was: flat ground at its height in thin air, kept
  for Rally, Snow and Desert, whose terrain covers the map in the game). Flown into the ground from below, the car is
  put on the lowest surface over it (the road under a tunnel's roof, not the roof). Never faster on the flat than the
  car's top speed.
- Island's car springs off a kicker at 1.08 times the ramp's climb (its replays' take-offs average 1.19 times; the
  full figure flew it over checkpoints), its climb held to 120 m/s (others 80): Island's apex error -3.1 m -> -0.3 m.
- Loops' friction per car (`KLoopFric`): Island 0.47 of the press, Bay 0.07 (BayC2's loop: 111 -> 85 m/s, gravity
  alone 93), the others 0.12 (unmeasured).
- Island and Bay: fallen into the sea (nothing under the car, 40 m under the last ground) the car is put back at its
  last checkpoint, the race going on (the app puts the camera behind it again).
- Snow's tracks and Desert's repacked with surface 12 (Snow's metal tubes, Desert's pipelines and loops): the
  respawn-assisted driver needs 31 respawns over Snow's 21 tracks (35 before; SnowC2 finishes now), Desert's 47 (54).
- Every campaign track of the five environments is installed (105): a package per environment (`tm9300_tracks.sis`
  Rally, `tm9300_snow.sis`, `tm9300_desert.sis`, `tm9300_bay.sis`, `tm9300_island.sis`), each on the phone or the card:
  the app finds tracks on its own drive, C: and E:. The tracks are packed with LZMA for the phone (`tools/tmbz.py`:
  "TMBL", the size, the properties byte, the raw stream; preset 9 extreme, lc 0 lp 1 pb 1, a 16 MB dictionary):
  171 MB -> 31 MB installed (zlib: 68 MB). Filtered first ("TMBF": the corners' and the normals' x, y, z split into
  low and high byte planes, each triangle's corner index from the highest so far + 1): 27 MB. The app's own decoder (`tmlzma.cpp`, the LZMA specification, decoding
  straight into the track's memory: 40 ms for the biggest track on the PC) unpacks them on loading; a zlib-packed
  ("TMBZ", the phone's ezlib) or a plain .tmb still loads. The menus' pictures (`tm9300.ui`, with Bay's and Island's loading screens now) go with the program.
  The heap may grow to 12 MB (a big track's tables take ~2.5 MB beside its data).
- Fan tracks: `tools/pc/fantrack.py kitdir outdir X.Challenge.Gbx ...` turns any track of the five environments
  (TMX's, the editor's) into `f_<name>.tmb` (no replay: the start from the start block, as the campaign's replays
  stand on each start block; LZMA-packed) and `f_<name>.tmt` (the map's thumbnail, its medal times, a trailer with
  the environment and the map's name). `build.sh` makes `tm9300_fan.sis` of the `data/f_*` files. On the phone:
  Play Solo > Browse lists them by name (environment, medals, record, the picture); the opponents are your record or
  none (no author's run to make the medals' ghosts from). The waypoints come from the kit's blocks (Start, Finish,
  Checkpoint, StartFinish); a multilap track with no finish block finishes on its start-finish.
- The auto-driver: `RESPAWN=1` puts the car back on the replay's line when it is 15 m off it or stuck for a second
  (as a player's respawn) and counts it: Bay A1-A5, B1 and Island A1, A3, A4, B2, B4 finish with none; the installed
  C and D tracks with one or two. `TRACE=a,b` prints every step between two times.
- Loading screens for Bay and Island wait for their `LoadScreen.dds` (on the PC, outside the paks): black until then.
- Loops, quarter pipes and wall rides (surface mode): the steep faces of the loop, wall-ride, pipe and tube pieces
  (`STEEPROAD` in `pack.py`: `Loop`, `Wall/`, `Circular/`, `MetalTube`, `Pipeline`, ...) are surface 12: a wall to
  plain driving, road to the car that meets them at speed. On ground steeper than 41 degrees going up it faster
  than 20 m/s, or meeting a surface-12 face head on, the car leaves the heightfield: it is a point on the road with
  the road's normal for its up; a step adds gravity, the pull or the brake along the speed and the steering about
  the up, moves, and finds the road again along the up (`TmGround::Ray`: the nearest face crossed from 1.5 m over
  to 1.5 m under the car), turning the speed onto it. It holds while the road's curve presses the car on harder than
  gravity pulls it off (the press averaged over ~8 steps: a faceted loop turns at the edges; a seam between two
  pieces' roads or a gap in a loop's top is flown across for up to 12 steps), else it flies; back to
  plain driving where the road is flatter than 37 degrees. The press costs speed (0.47 of it: IslandA2's loop takes
  the replay's car from 175 m/s to 51 at the top; gravity alone would leave 147); the replay's climb is matched
  within ~10 m/s at each height. The car is drawn turned in 3D there (`TmView::iCar3D`), and the camera follows it in
  its own frame (11 m back along its way, 4 m over it along its up, no roll).

## 0.19: the game's own tracks on the phone (.Challenge.Gbx)

- The game's own tracks where the PC game keeps them: `Documents\TrackMania\Tracks\` on the phone (`C:\Documents\...`)
  and the card (`E:\Documents\...`), shown together (copy the PC's `Documents\TrackMania\Tracks` folder over and the
  phone shows the same tree; `Challenges\My Challenges\` and `Challenges\Downloaded\` are made on the first run).
  Play Solo > Browse is the game's explorer: the title and Back on the left; in the middle "Available tracks" with
  the count, the path with the up button (Left / Esc), the folders (`Name\`) and the tracks (an orange tab, the
  environment's mark, the map's name, the author's time); on the right the chosen track as the game's (its name, its
  author and environment, its picture: the JPEG in the Gbx's header, `tmjpeg.cpp`, a baseline decoder of our own;
  bronze, silver, gold and author medals with their times, the record, Play). The installed fan tracks show at the
  top. The folder is kept between runs (`C:\Data\TM9300\browse.txt`). A track is known by its map's uid (the same
  map in two folders is one track; its record and its built file go by it); built the first time it is raced into
  `<its drive>:\Data\TM9300\cache\` (again when the Gbx or the kit is newer). Stadium and Coast files are not listed.
- Hot Seat > the series, as the game's: the campaigns' series in the middle (a page of rows: the environment's mark,
  the series, its tracks; the page under them), Official tracks, My tracks and Downloaded tracks on the left. Those
  open the explorer at `Campaigns\` (the installed campaigns as the PC's folders: `United\Race\Rally\White\` ...),
  `Challenges\My Challenges\` and `Challenges\Downloaded\`; Enter ticks a track or unticks it (as the game's: a press
  each, no Shift or Ctrl), the ticked ones in a dark blue bar, and Play (with their count) races them in the order
  ticked (up to 32).
- `tmgbx.cpp`: the Gbx reader in C++ (the LZO1X decoder, the header chunks, the reference table, chunk 0x0304301F's
  block list with skins), every read bounds-checked (a cut or bit-flipped file fails, it does not crash: fuzzed under
  ASan). It reads the 112 campaign and StarTrack tracks exactly as `gbxtrack.py` does.
- The block kits (`tools/pc/blockkit.py`, `data/<env>.tmk`, each in its environment's tracks package): every solid
  of the environment's blocks as `pack.py` makes it (simplified, wide pieces tiled, walls marked), each part's drawn
  mesh, far form and collision unturned; how much of each cell a ground piece covers (3 x 3 samples: the terrain tile
  dropped at 6; 16 x 16 for cutting a tile it covers in part); the blocks' variants, footprints, zone heights,
  waypoints and start offsets; the base plates and the car. "TMK2": a directory and ~900 KB chunks of mesh data, each
  LZMA-packed on its own (the phone unpacks one at a time and keeps what the track uses): Rally 0.7 MB, Snow 1.0,
  Desert 1.4, Bay 1.0, Island 2.5.
- `tmkit.cpp` builds the track as `assemble.py` and `pack.py` do, step for step: the blocks placed, the default
  terrain, the covered terrain dropped and the partly covered cut, each part turned by exact quarter turns of its
  1/16 m corners, the base plate, the car, the start from the start block, a TMB5 file. Against `pack.py` on all 112
  tracks: the same instances, collision (but 18 triangles of two terrain tiles kept on IslandC5) and starts; the
  drawn geometry the same but in the cut terrain tiles, where the kit's mask keeps a few hundred faces more under
  pieces' edges (overlap, never a hole; at most 3 faces fewer). The 105 campaign tracks built this way with their
  replays drive exactly as the installed ones (`DRIVER=2`). A build unpacks 4-6 MB of kit and peaks at ~5.7 MB of
  heap beside the kit's 2.5 MB (Island); 0.05 s on the PC.
- `pack.py`: `Prep` holds the per-solid work (shared with `blockkit.py`); the far forms are simplified unturned
  and turned with the rest (a few faces differ from before). `assemble.py`: the terrain cut used the last ground
  piece's shape for every cut tile (a closure bound late): fixed; triangles with no area seen from above (a wall's
  face: a line; a point) no longer "cover" the points on them.
- Host tools: `phone/host/gbx2tmb.cpp` (kit + Gbx [+ path] -> .tmb, the stats).

- The take-off (`tmphys.cpp`): the car leaves along the ramp at its speed on it. The ground's speed is flat speed with
  the climb on top, so flat speed and climb are both scaled to the flat speed's length. Before, the climb was kept and
  the flat speed cut, which on a steep lip threw the car steeper than the ramp: RallyC3's 35-degree hill lip launched
  at 44 degrees and the car flew 17 m over the finish. Jump test (mean |log| landing distance): Rally 0.110 -> 0.101,
  Desert 0.203 -> 0.194, Bay 0.195 -> 0.194, Island 0.169 -> 0.167, Snow 0.180 -> 0.219 (its jumps were short
  before and still are: something else).
- `STEEPROAD` takes the banked spirals (Desert's `SpeedSpinLeft` / `Right`, nine tracks) and Snow's twisting roads
  (`AlpineRoadWoodTiltSpin` / `UnSpin`: SnowD2, D5): their banks were walls, and the car stopped at a spiral's mouth
  (DesertD4). Those nine tracks repacked, the Desert and Snow kits rebuilt.
- The host driver (`DRIVER=2`) brakes in time for the replay's slowing down ahead on level ground (SnowD5: the
  replay brakes from 85 to 70 m/s before the drop to the finish; the driver flew over it).
- The regression, all 105 campaign tracks (unaided / with respawns / respawns in all), before -> after:
  Rally 16/20/11 -> 19/21/5, Snow 12/18/31 -> 11/21/24, Desert 7/19/51 -> 12/21/33, Bay 5/18/49 -> 7/17/47,
  Island 8/15/69 -> 8/17/69. Every Rally, Snow and Desert track finishes with respawns.

- Failure census (`tools/pc/respawns_where.py`): the host driver logs where and why each respawn began (off the
  line, stopped, fell, pinned: not moving a metre in a second whatever its speed says), and the blocks there are
  counted over all 105 tracks. Island's big loop pieces (`IslandPlatformLoopGT4Start` ...) had 14 respawns over 7
  tracks, Desert's loops 7 over 7. What was wrong:
  - integer overflow in the wall response at Island speeds (`vn * nx * 6` past 2^31 above ~170 m/s): the car was
    thrown off at 250-360 m/s; the wall and slope products are 64-bit now;
  - the loop press friction charged every turn of the road's up: rolling onto a bank, or riding a curved wall round
    a bend, cost as much as a loop (IslandC2's wall ride: 200 -> 110 m/s, the replay 197 -> 186). It is now by how
    much the road turns over (the up's turn about a level axis, as in a loop) against round (about an upright one);
  - steering on the steep road was capped at 0.8 rad/s: the car's own full-lock rate now (Island 1.4);
  - a steep-road face met glancingly (under 15 degrees) was a wall: any contact at speed takes the car onto it;
  - the host driver steers on the steep road (pure pursuit across the road's plane: nothing on a loop, the bend on
    a banked curve or a wall) with full throttle (its flat-road corner speeds braked it to a crawl).
  Respawns over all tracks 188 -> 151 (Island 81 -> 53, Desert 33 -> 27); unaided finishes Snow 11 -> 12,
  Island 8 -> 10. What is left: Bay's jumps onto its platform slopes and road ramps (the car lands ~10 m long and
  catches the slope's support: the take-off a tenth too steep there), Snow's metal-tube corners going down, Desert's
  tilted straights; most of the rest the driver losing the line.

- The phone test's reports, fixed:
  - **Checkpoints respawn at their middle** (the group's boxes' centre, on the road found from 3 m above), facing
    the nearest quarter turn of where the car was heading; was where the car crossed it.
  - **Rails hold the car, no bounce, no climbing up them.** Snow's road-side rails (the plank-and-iron kerb with
    snow on top) are wound with their faces outward, so a car met them from behind: it went into them and the
    ground probe then put it on the snow on top (2 m up), or through. Walls are two-sided now (a loop's road
    stays one-sided), level unit normals (a leaning face's 0.6-long normal left 40 % of the move into it: the car
    crept in), in 64 bits at full precision (at 1/16 m a slow move into a face, under 6 cm a step, came out as
    along it). A face lower than a climbable step (0.55 m over the car's ground) is not a wall (a snow mound's
    0.5 m lip at a road's end, its riser met from behind before). The body's corners and the last move check test
    a band, 0.35 / 0.25 m up to 1.2 m, not two points: a railing's bars at 0.9-1.4 m went between. The speed
    into a wall is taken out with no bounce (met more than ~45 degrees head on, up to a quarter back, so the car
    is not stuck against it); the last move check slides along the wall it meets instead of stopping the move
    whole (wedged in a rail's corner the car stood still at 100 km/h).
  - **SnowB2's red lake bridges' railings**: drawn as thin panels the simplified mesh kept half of (one triangle
    a panel); `pack.py` now takes the walls of the solids matching `NADEOWALLS_FOR` (default `LakeBridge`) from
    Nadeo's collision mesh, also for solids in tiles. SnowB2, SnowB5, SnowD1 repacked, the Snow kit rebuilt.
  - **The car holds on to bumpy crests** (the bridges at 110 km/h flew): the kink is measured against the road's
    climb over the last metres (Snow 10 m, others 4), not the last step, and the car holds on while it is under
    its gravity (Snow 40 m/s^2): the simplified roads' noise (climb 13, 19, 24, 10 m/s on SnowB2's bridge) evens
    out; a kink the car holds over starts no throw to follow. SnowB2's bridge: no air at 90-110 km/h, hops of
    0.6 m at 130-155 (was 0.4 s in the air at 90 and 110); SnowD4's rounded crest at 360 km/h still throws it as
    the replay's car.
  - **Pitch drawn from the road under the nose and tail** (1.3 m each way) and roll from 0.8 m each side: it
    turns smoothly as the car rolls over a kink; the faceted road's normal jumped from face to face (and gave a
    false roll on a straight bridge).
- Regression (unaided / with respawns, respawns in all), before -> after: Rally 19/21 -> 19/21, Snow 11/21 ->
  14/21, Desert 12/21 -> 11/21, Bay 7/17 -> 9/17, Island 8/19 -> 12/19; respawns 190 -> 162 (the host driver now
  meets the rails it used to cut through). Jump test (mean |log|): Rally 0.101 -> 0.105, Snow 0.219 -> 0.211,
  Desert 0.194 -> 0.177, Bay 0.194 -> 0.151, Island 0.167 -> 0.132.

## Next

1. **Speed on Snow**: SnowA1 draws ~2,000 triangles a frame (3.8 M instructions: ~18 fps on the phone), the
   Great Wall pieces are heavy. Near/far forms per solid (a 60-triangle stand-in beyond 60 m), a PVS per
   track cell, and a cheaper box test (`BoxVisible` is 7 % of a frame) are the levers; the geometry share is
   14 ms of 31 on Rally already.
2. Tune by feel after the phone test: the pull (the driver is ~10 % quicker than the replays), the steering
   ramp, the camera distance.
3. The depth test's per-polygon and per-row cost (the planes, the row bookkeeping): ~0.7 M a frame.
4. Wall rides by feel on the phone (the surface mode is fitted on loops only).
5. Campaign tracks from their Gbx files: a package would be its kit, the 21 Gbx files (~30 KB each), their replay
   paths and pictures (~1-3.5 MB instead of 4-11 MB, ~25 MB less in all) if the phone built each track when it is
   loaded (a build unpacks 4-6 MB of kit: the chunks could be ordered by how often the solids are used).
6. Bay's big loops (`BayPlatformLoopStart4` / `End4`): the two halves of the loop's top are placed 16 m apart
   (BayC2: a gap at z 792..808 the replay drives through); the turned multi-unit pieces' placing, or a solid's own
   offset the kit does not carry. None of the installed Bay tracks has one.
