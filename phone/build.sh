#!/bin/bash
# Builds build/tm9300.app + .rsc and bin/tm9300.sis (with the track files in data/) for the Nokia 9300.
# Needs the kit: . ~/sym/env.sh (EPOC=~/sym/s80_20/epoc32, TOOLS=~/sym/gcc539/bin)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
export EPOC=${EPOC:-$HOME/sym/s80_20/epoc32} TOOLS=${TOOLS:-$HOME/sym/gcc539/bin}
export OPT="-O2 -mno-short-load-bytes"
SRCS="src/tm9300_app.cpp src/tmlzma.cpp src/tmgbx.cpp src/tmkit.cpp src/tmui.cpp src/tmcore.cpp src/tmphys.cpp src/r3d.cpp src/r3dgeo.c src/r3dspan.c src/fastmem.c src/fastmem_ref.c"
LIBS="euser.lib ezlib.lib esock.lib insock.lib apparc.lib cone.lib eikcore.lib ckncore.lib fbscli.lib ws32.lib efsrv.lib hal.lib bitgdi.lib gdi.lib bafl.lib"
cd "$HERE"
PETRAN_EXTRA="-heap 0x10000 0xC00000" bash tools/symbuild.sh tm9300 0x0E3D9310 "$HERE/build" "$SRCS" "$HERE/inc" "$LIBS" group/tm9300.rss
$TOOLS/petran -v build/tm9300.app | grep -i "data size\|bss size" || true
mkdir -p bin
sed "s|EPOC32|$EPOC|g" sis/tm9300.pkg.in > build/tm9300.pkg
(cd build && $TOOLS/makesis tm9300.pkg ../bin/tm9300.sis)
# 0.18: the tracks packed with zlib for the phone (a third of the space; the app inflates them on loading)
mkdir -p build/z
for f in data/*.tmb; do o=build/z/$(basename $f); [ "$o" -nt "$f" ] || python3 tools/tmbz.py "$f" "$o"; done
sed "s|EPOC32|$EPOC|g" sis/tm9300_tracks.pkg.in | sed 's|"../data/\([^"]*\.tmb\)"|"z/\1"|' > build/tm9300_tracks.pkg
# 0.19: each environment's block kit (tools/pc/blockkit.py: data/<kit>.tmk) in its tracks package: the app builds the
# game's own tracks (.Challenge.Gbx in \TmTracks\) from it
kit() { [ -f data/$2.tmk ] && echo "\"../data/$2.tmk\"-\"!:\\system\\apps\\tm9300\\$2.tmk\"" >> build/$1.pkg; true; }
kit tm9300_tracks rally
(cd build && $TOOLS/makesis tm9300_tracks.pkg ../bin/tm9300_tracks.sis)
# 0.18: an environment's tracks a package of their own (Bay, Island, ...), when its .pkg.in is there
# 0.18: the fan tracks (tools/pc/fantrack.py: data/f_*.tmb, packed already), a package of their own when there are any
if ls data/f_*.tmb > /dev/null 2>&1; then
  { echo '; TrackMania 9300 - fan tracks (tools/pc/fantrack.py; cut from the game files and the tracks: never in git)'; echo '&EN'
    echo '#{"TM9300 fan tracks"},(0x0E3D9316),0,19,0'; echo '(0x101F8ED2), 0, 0, 0, {"Series80ProductID"}'
    for f in data/f_*.tmb; do b=$(basename $f .tmb); echo "\"../data/$b.tmb\"-\"!:\\system\\apps\\tm9300\\$b.tmb\""; [ -f data/$b.tmt ] && echo "\"../data/$b.tmt\"-\"!:\\system\\apps\\tm9300\\$b.tmt\""; done; } > build/tm9300_fan.pkg
  (cd build && $TOOLS/makesis tm9300_fan.pkg ../bin/tm9300_fan.sis)
fi
for e in snow desert bay island; do
  [ -f sis/tm9300_$e.pkg.in ] || continue
  sed "s|EPOC32|$EPOC|g" sis/tm9300_$e.pkg.in | sed 's|"../data/\([^"]*\.tmb\)"|"z/\1"|' > build/tm9300_$e.pkg
  case $e in snow) kit tm9300_$e alpine;; desert) kit tm9300_$e speed;; *) kit tm9300_$e $e;; esac
  (cd build && $TOOLS/makesis tm9300_$e.pkg ../bin/tm9300_$e.sis)
done
ls -l bin/*.sis
