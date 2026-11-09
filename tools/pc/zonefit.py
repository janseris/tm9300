import sys,json,glob,os,collections; sys.path.insert(0,'tm'); import assemble
env=sys.argv[1]; kit=json.load(open('dump/%s_kit.json'%('rally' if env=='Rally' else 'alpine')))
zones={z['block'].lower():z for z in kit['zones']}
def surf_h(t, inst, px, pz, near):
    sname,bn,x,y,z,d,g,kind=inst; so=t.solids[sname]; L=so['lods'][-1]; v=L['verts']; f=L['faces']
    pts=t.place([(v[i],v[i+1],v[i+2]) for i in range(0,len(v),3)],x,y,z,d); best=None
    for i in range(0,len(f),4):
        A,B,C=pts[f[i]],pts[f[i+1]],pts[f[i+2]]
        if max(A[0],B[0],C[0])<px or min(A[0],B[0],C[0])>px or max(A[2],B[2],C[2])<pz or min(A[2],B[2],C[2])>pz: continue
        dd=(B[2]-C[2])*(A[0]-C[0])+(C[0]-B[0])*(A[2]-C[2])
        if abs(dd)<1e-9: continue
        l1=((B[2]-C[2])*(px-C[0])+(C[0]-B[0])*(pz-C[2]))/dd; l2=((C[2]-A[2])*(px-C[0])+(A[0]-C[0])*(pz-C[2]))/dd; l3=1-l1-l2
        if l1<-1e-6 or l2<-1e-6 or l3<-1e-6: continue
        h=l1*A[1]+l2*B[1]+l3*C[1]
        if best is None or abs(h-near)<abs(best-near): best=h
    return best
offs=collections.defaultdict(list)
for tf in sorted(glob.glob('/root/ref/tracks/United/Race/%s/*/*.Challenge.Gbx'%env)):
    n=os.path.basename(tf).split('.')[0]; rp=json.load(open(f'/root/ref/replays/{n}.json'))['samples']
    t=assemble.Track(kit, open(tf,'rb').read())
    bycell=collections.defaultdict(list)
    for i in t.instances:
        bb=t.solids[i[0]]['bbox']; xs=[]; zs=[]
        for lx in (bb[0],bb[3]):
            for lz in (bb[2],bb[5]):
                rx,rz=assemble.rot_xz(lx,lz,i[5],t.blk/2); xs.append(rx+i[2]); zs.append(rz+i[4])
        for cx in range(int((min(xs)+0.5)//t.blk),int((max(xs)-0.5)//t.blk)+1):
            for cz in range(int((min(zs)+0.5)//t.blk),int((max(zs)-0.5)//t.blk)+1): bycell[(cx,cz)].append(i)
    for s in rp[::2]:
        px,py,pz=s['p']; lst=bycell.get((int(px//t.blk),int(pz//t.blk)),[])
        if len(lst)!=1 or lst[0][1].lower() not in zones: continue
        inst=lst[0]; h=surf_h(t,inst,px,pz,py-0.5)
        if h is not None: offs[(n, inst[1], inst[4] if False else inst[3])].append(round(py-0.5-h,1))
for k,v in sorted(offs.items(), key=lambda kv:-len(kv[1])):
    c=collections.Counter(round(o/4)*4 for o in v)
    print(f'{str(k):50s} n={len(v):4d} within 1.5 m: {sum(1 for o in v if abs(o)<1.5):4d}  by 4 m: {dict(sorted(c.items()))}')
