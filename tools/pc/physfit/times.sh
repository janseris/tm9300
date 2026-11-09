# auto-driver times against the medals: name, our time, author, gold
for t in "$@"; do
  n=$(echo $t | sed 's/^r/R/; s/^s/S/'); env=$(echo $n | sed 's/[A-E].*//')
  m=$(python3 /tmp/claude-0/medals.py /root/ref/tracks/United/Race/$env/*/$n.Challenge.Gbx | grep -o '([^)]*)' | tr -d '()' | awk -F', ' '{printf "%.2f %.2f %.2f", $5/1000, $4/1000, $3/1000}')
  r=$(/root/tm/host/tmdrive /root/ref/packed/$t.tmb /tmp/claude-0/x 0 200 | tail -1 | awk '{print ($1=="FINISHED") ? $3 : "DNF"}')
  echo "$t ours $r | author gold silver $m"
done
