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
- The camera further from the car: 9.5 m back and 5.6 m up (0.10: 7.8 m and 5 m, still too close).
- The menus' picture pack is no longer a writable static (the phone's programs may not have one: petran had
  refused it), it sits in the thread's slot.

## Next

1. **Speed on Snow**: SnowA1 draws ~2,000 triangles a frame (3.8 M instructions: ~18 fps on the phone), the
   Great Wall pieces are heavy. Near/far forms per solid (a 60-triangle stand-in beyond 60 m), a PVS per
   track cell, and a cheaper box test (`BoxVisible` is 7 % of a frame) are the levers; the geometry share is
   14 ms of 31 on Rally already.
2. Tune by feel after the phone test: the pull (the driver is ~10 % quicker than the replays), the steering
   ramp, the camera distance.
3. The depth test's per-polygon and per-row cost (the planes, the row bookkeeping): ~0.7 M a frame.
4. Wall rides and loops (Snow C/D).
5. Desert, Bay, Island on the same pipeline (their kits are not exported yet; Bay and Island have 64 m cells).
6. A menu (track list, best times kept in `C:\Data\TM9300`), a countdown, a replay of the ghost from the path.
