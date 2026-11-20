# jump fidelity: for each replay take-off, the car started 0.8 s before at the replay's speed, straight on; landing
# distance and apex vs the replay's.   jt.py <jt binary> <Env> [<Env>...]
import json,math,subprocess,sys,re,glob,os,statistics
jtb=sys.argv[1]
for env in sys.argv[2:]:
    errs=[];aps=[]
    for f in sorted(glob.glob(f'/root/ref/replays/{env}*.json')):
        n=os.path.basename(f)[:-5]; tmb=f'/root/ref/packed/{n[0].lower()+n[1:]}.tmb'
        if not os.path.exists(tmb): continue
        s=json.load(open(f))['samples']
        out=subprocess.run(['/tmp/claude-0/diag',tmb],capture_output=True,text=True).stdout.splitlines()
        diff={}
        for l in out:
            m=re.match(r'sample\s+(\d+) .*diff\s+([-\d.]+)',l)
            if m: diff[int(m.group(1))]=float(m.group(2))
        lines=[];info=[]
        i=10
        while i<len(s)-12:
            # take-off: on the ground (diff<0.3) then 4+ samples over 1.5 m and rising first
            if diff.get(i-1,9)<0.4 and all(diff.get(i+k,0)>1.0 for k in range(1,5)) and s[i]['v'][1]>3:
                j=i+1
                while j<len(s) and diff.get(j,0)>0.5: j+=1
                if j>=len(s) or j-i>60: i=j; continue
                a=s[i-8]; v=math.hypot(a['v'][0],a['v'][2])
                if v<20: i=j; continue
                yaw=math.degrees(math.atan2(a['v'][0],a['v'][2]))
                lines.append(f"{a['p'][0]} {a['p'][1]} {a['p'][2]} {yaw} {v} {(j-i+20)*5}")
                apex=max(s[k]['p'][1] for k in range(i,j))
                info.append((i,j,s[j]['p'],apex))
                i=j
            else: i+=1
        if not lines: continue
        r=subprocess.run([jtb,tmb],input='\n'.join(lines)+'\n',capture_output=True,text=True).stdout.split('\n')
        for (i,j,lp,apex),line in zip(info,r):
            p=line.split()
            if len(p)<5 or p[1]=='-1': continue
            a=s[i-8]['p']
            dr=math.hypot(lp[0]-a[0],lp[2]-a[2]); ds=math.hypot(float(p[2])-a[0],float(p[3])-a[2])
            if dr<30: continue
            errs.append(ds/dr); aps.append(float(p[4])-apex)
            if len(sys.argv)>2 and os.environ.get('V'): print(f'{n} t{i/10:.1f} replay {dr:.0f} m apex {apex:.1f}; sim {ds:.0f} m apex {float(p[4]):.1f}')
    import math as M
    good=[abs(M.log(e)) for e in errs if 0.5<e<2]
    if errs: print(f'  mean|log| {sum(good)/len(good):.3f} over {len(good)} ', end='');
    if errs: print(f'{env}: {len(errs)} jumps; sim/replay distance median {statistics.median(errs):.3f} (p25 {sorted(errs)[len(errs)//4]:.3f} p75 {sorted(errs)[3*len(errs)//4]:.3f}); apex diff median {statistics.median(aps):+.1f} m')
