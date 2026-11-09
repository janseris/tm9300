import re,subprocess,sys
out=subprocess.run(['/tmp/claude-0/diag',sys.argv[1]],capture_output=True,text=True).stdout.splitlines()
none=0; off=0; n=0; bad=[]
for l in out:
    m=re.match(r'sample\s+(\d+) \(\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\): ground (NONE\s+[-\d.]+|\s*[-\d.]+)\s+diff\s+([-\d.]+)',l)
    if not m: continue
    n+=1
    if m.group(5).startswith('NONE'): none+=1; bad.append(l)
    elif not (-1.0 <= float(m.group(6)) <= 1.5): off+=1; bad.append(l)
print(f'{none} none, {off} off by more than a metre, of {n}')
for l in bad[:int(sys.argv[2]) if len(sys.argv)>2 else 25]: print(l)
