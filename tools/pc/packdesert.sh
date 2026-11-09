#!/bin/bash
# packs Desert campaign tracks (all, or those matching $1) into /root/ref/packed and drives each
S=/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad
cd $S
for f in /root/ref/tracks/United/Race/Desert/*/*.Challenge.Gbx; do
  n=$(basename $f .Challenge.Gbx)
  case "$1" in "") ;; *) echo "$n" | grep -q "$1" || continue;; esac
  out=/root/ref/packed/$(echo $n | sed 's/Desert/desert/').tmb
  r=$(COARSE='RiverBeach|DesertTo|RockToRock|Cliff|SpeedToCanyon|Rocks' ROAD_BUDGET=300 D_GRID=3 R_GRID=2 python3 tm/pack.py --budget 150 --far 60 dump/speed_kit.json /root/ref/tex/avgcolors.json $f /root/ref/replays/$n.json $out 2>&1 | grep -i "wrote\|error\|Trace" | grep -o "[0-9]* placed triangles\|Error.*\|Trace.*")
  d=$(DRIVER=2 /root/tm/host/tmdrive $out /root/ref/frames/x 0 200 2>&1 | tail -1 | cut -c1-75)
  printf "%-9s %-24s | %s\n" "$n" "$r" "$d"
done
