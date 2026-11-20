# tm9300

TrackMania United Forever's Rally, Snow, Desert, Bay and Island environments, driven on the Nokia 9300
(Symbian S80 v2, ARM925T 150 MHz, no FPU, 640×200 screen, 320×200 3D view).

The game's own tracks: as on the PC, in `Documents\TrackMania\Tracks\` (on the phone, `C:\Documents\...`, or the
memory card, `E:\Documents\...`): Play Solo > Browse and Hot Seat's track choice are the game's folder explorer, and
the app builds each track (Rally, Snow, Desert, Bay, Island) from its environment's block kit (installed with that
environment's tracks package) the first time it is raced.

- `phone/` – the app: renderer (`r3d`, depth buckets + coverage + near-polygon depth test),
  physics (`tmphys`, 1/256 m, 50 Hz), track core and the S80 app; `host/` PC test drivers,
  `emu/` ARM emulator scripts, `sis/` package templates, `build.sh`.
- `tools/` – PC tools that read the original game's data (pak dumps, kits, tunings, replays)
  and pack tracks into `.tmb` files for the phone.
- `docs/` – analysis, physics notes, phone test reports, per-track status.

No Nadeo game data is in this repository (`dump/`, `*.tmb`, `*.tmk`, built `.sis` files are ignored);
the tools need your own copy of the game. The Symbian S80 SDK is not included.

Versions are numbered x.y (the phone shows only two numbers).
