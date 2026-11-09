#!/bin/bash
# packs every Rally and Snow campaign track into /root/ref/packed, checks each against its replay and drives it
cd /tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad
for f in /root/ref/tracks/United/Race/Rally/*/*.Challenge.Gbx /root/ref/tracks/United/Race/Snow/*/*.Challenge.Gbx; do
  n=$(basename $f .Challenge.Gbx); case $n in Rally*) kit=rally_kit.json; opt="";; *) kit=alpine_kit.json; opt="--budget 200";; esac
  case "$1" in "") ;; *) echo "$n" | grep -q "$1" || continue;; esac
  out=/root/ref/packed/$(echo $n | sed 's/Rally/rally/; s/Snow/snow/').tmb
  r=$(python3 tm/pack.py $opt --far 60 dump/$kit /root/ref/tex/avgcolors.json $f /root/ref/replays/$n.json $out 2>&1 | grep -i "wrote\|unknown\|error\|Trace\|corner" | tr '\n' ' ')
  c=$(python3 /tmp/claude-0/chk.py $out 0); d=$(/root/tm/host/tmdrive $out /root/ref/frames/x 0 200 2>&1 | tail -1 | cut -c1-62)
  printf "%-8s %-14s %8s | %s | %s\n" "$n" "$(echo $r | grep -o 'unknown {[^}]*}')" "$(echo $r | grep -o '[0-9]* bytes' | cut -d' ' -f1)" "$c" "$d"
done
