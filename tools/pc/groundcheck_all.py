import re,subprocess,sys,glob
for f in sorted(glob.glob('/root/ref/packed/*.tmb')):
    out=subprocess.run(['/tmp/claude-0/diag',f],capture_output=True,text=True).stdout.splitlines()
    none=0; above=0; below=0; n=0; worst=0; where=''
    for l in out:
        m=re.match(r'sample\s+(\d+) \(\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\): ground (NONE\s+[-\d.]+|\s*[-\d.]+)\s+diff\s+([-\d.]+)',l)
        if not m: continue
        n+=1
        if m.group(5).startswith('NONE'): none+=1; continue
        d=float(m.group(6))
        if d>1.5: above+=1
        if d<-0.7:
            below+=1
            if -d>worst: worst=-d; where=f'sample {m.group(1)} ({m.group(2)},{m.group(3)},{m.group(4)})'
    print(f'{f.split("/")[-1]:14s} {n:5d} samples: {none:3d} none, {above:3d} in the air, {below:3d} under our ground (worst {worst:.1f} m at {where})')
