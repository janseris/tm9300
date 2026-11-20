#!/bin/bash
# packs an environment's campaign tracks (all, or those matching $2) into /root/ref/packed and drives each
#   packenv.sh Bay|Island|Desert [pattern]
S=/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad
E=$1
case $E in
  Desert) KIT=speed_kit.json; OPT="--budget 150"; export COARSE='RiverBeach|DesertTo|RockToRock|Cliff|SpeedToCanyon|Rocks' ROAD_BUDGET=300 D_GRID=3 R_GRID=2;;
  Bay) KIT=bay_kit.json; OPT="";;
  Island) KIT=island_kit.json; OPT="";;
esac
cd $S
for f in /root/ref/tracks/United/Race/$E/*/*.Challenge.Gbx; do
  n=$(basename $f .Challenge.Gbx)
  case "$2" in "") ;; *) echo "$n" | grep -q "$2" || continue;; esac
  lo=$(echo ${n:0:1} | tr A-Z a-z)${n:1}
  out=/root/ref/packed/$lo.tmb
  r=$(python3 tm/pack.py $OPT --far 60 dump/$KIT /root/ref/tex/avgcolors.json $f /root/ref/replays/$n.json $out 2>&1 | grep -i "wrote\|error\|Trace" | grep -o "[0-9]* placed triangles\|Error.*\|Trace.*")
  c=$(python3 /tmp/claude-0/chk.py $out 0 | head -1)
  d=$(DRIVER=2 /root/tm/host/tmdrive $out /root/ref/frames/x 0 200 2>&1 | tail -1 | cut -c1-62)
  printf "%-9s %-22s | %-38s | %s\n" "$n" "$r" "$c" "$d"
done
