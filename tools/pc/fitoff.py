import sys; sys.path.insert(0,'/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad/tm')
import json, assemble, collections, glob, os, statistics, sys
sys.path.insert(0,'/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad/tm')
kit=json.load(open('/tmp/claude-0/-home-claude/7d10ecc4-1de6-5ce8-8092-4477744be6b0/scratchpad/dump/rally_kit.json'))
offs=collections.defaultdict(list)
for tf in sorted(glob.glob('/root/ref/tracks/United/Race/Rally/*/*.Challenge.Gbx')):
    n=os.path.basename(tf).split('.')[0]; rj=f'/root/ref/replays/{n}.json'
    if not os.path.exists(rj) or os.path.getsize(rj)<100: continue
    rp=json.load(open(rj))['samples']
    t=assemble.Track(kit, open(tf,'rb').read())
    cells=collections.defaultdict(list); placed={}
    for inst in t.instances:
        sname,bn,x,y,z,d,g,kind=inst
        s=t.solids[sname]; bb=s['bbox']; xs=[];zs=[]
        for lx in (bb[0],bb[3]):
            for lz in (bb[2],bb[5]):
                rx,rz=assemble.rot_xz(lx,lz,d,16); xs.append(rx+x); zs.append(rz+z)
        for cx in range(int(min(xs)//32),int(max(xs)//32)+1):
            for cz in range(int(min(zs)//32),int(max(zs)//32)+1): cells[(cx,cz)].append(inst)
    for s in rp[::3]:
        px,py,pz=s['p']
        for inst in cells.get((int(px//32),int(pz//32)),[]):
            sname,bn,x,y,z,d,g,kind=inst
            key=(sname,x,y,z,d)
            if key not in placed:
                so=t.solids[sname]; v=so['coll']['verts']; f=so['coll']['tris']
                pts=t.place([(v[i],v[i+1],v[i+2]) for i in range(0,len(v),3)],x,y,z,d)
                placed[key]=[(pts[f[i]],pts[f[i+1]],pts[f[i+2]]) for i in range(0,len(f),4)]
            hs=[]
            for A,B,C in placed[key]:
                if max(A[0],B[0],C[0])<px or min(A[0],B[0],C[0])>px or max(A[2],B[2],C[2])<pz or min(A[2],B[2],C[2])>pz: continue
                dd=(B[2]-C[2])*(A[0]-C[0])+(C[0]-B[0])*(A[2]-C[2])
                if abs(dd)<1e-9: continue
                l1=((B[2]-C[2])*(px-C[0])+(C[0]-B[0])*(pz-C[2]))/dd; l2=((C[2]-A[2])*(px-C[0])+(A[0]-C[0])*(pz-C[2]))/dd; l3=1-l1-l2
                if l1<-1e-6 or l2<-1e-6 or l3<-1e-6: continue
                hs.append(l1*A[1]+l2*B[1]+l3*C[1])
            if not hs: continue
            h=min(hs,key=lambda q:abs(q-(py-0.6)))
            if abs(h-(py-0.6))<24: offs[(kind, 'hill' if 'Hill' in bn else bn[:16])].append(round(py-0.6-h,1))
for k,v in sorted(offs.items(), key=lambda kv:-len(kv[1])):
    c=collections.Counter(round(o/4)*4 for o in v)
    print(f"{str(k):44s} n={len(v):4d} median {statistics.median(v):6.1f}  by 4 m: {dict(sorted(c.items()))}")
