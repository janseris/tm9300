# respawns_where.py: each respawn of the host driver (tmdrive RESPAWN=1, its "from ... at (x, y, z): why" log lines in a folder) mapped to the blocks there, counted by block over all tracks: where the tracks or the physics are wrong (0.19)
import sys, re, json, glob, os, collections, math
sys.path.insert(0, '/home/claude/tm9300/tools/pc'); import assemble
S='/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad/dump/'
KIT={'rally':'rally','snow':'alpine','desert':'speed','bay':'bay','island':'island'}
kits={}
events=[]
for f in sorted(glob.glob('/tmp/claude-0/gbx/rl2/*.txt')):
    n=os.path.basename(f)[:-4]; env=re.match(r'[a-z]+',n).group(0)
    ev=[]
    for l in open(f):
        m=re.search(r'from ([\d.]+) s at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\): (.*)$', l)
        if m: ev.append((float(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4)), m.group(5).strip()))
    if not ev: continue
    if env not in kits: kits[env]=json.load(open(S+KIT[env]+'_kit.json'))
    kit=kits[env]
    N=n[0].upper()+n[1:]
    g=[x for x in glob.glob(f'/root/ref/tracks/United/Race/*/*/{N}.Challenge.Gbx')][0]
    t=assemble.Track(kit, open(g,'rb').read())
    boxes=[]
    for sname, bn, x, y, z, d, ground, kind in t.instances:
        if bn.lower() in t.zone_height: continue
        bb=t.solids[sname]['bbox']
        pts=t.place([(bb[0],bb[1],bb[2]),(bb[3],bb[4],bb[5]),(bb[0],0,bb[5]),(bb[3],0,bb[2])],x,y,z,d)
        xs=[p[0] for p in pts]; zs=[p[2] for p in pts]
        boxes.append((min(xs),max(xs),y+bb[1],y+bb[4],min(zs),max(zs),bn))
    for T,X,Y,Z,why in ev:
        near=[b[6] for b in boxes if b[0]-3<=X<=b[1]+3 and b[4]-3<=Z<=b[5]+3 and b[2]-6<=Y<=b[3]+6]
        events.append((env,n,T,X,Y,Z,why,sorted(set(near))))
cnt=collections.defaultdict(collections.Counter); trk=collections.defaultdict(lambda: collections.defaultdict(set))
for env,n,T,X,Y,Z,why,near in events:
    for b in near: cnt[env][b]+=1; trk[env][b].add(n)
for env in ['rally','snow','desert','bay','island']:
    print(f'== {env}')
    for b,c in cnt[env].most_common(14): print(f'   {c:3d} {b:42s} {len(trk[env][b])} tracks')
json.dump(events, open('/tmp/claude-0/gbx/events2.json','w'))
