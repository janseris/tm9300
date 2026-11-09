# the real cars' tyre forces per mass (a - g in the car's frame) by surface and speed band, from the author replays
# joined with the ground under each sample (tmsurf). G = 27 m/s^2 (GravityCoef 3)
import json, glob, math, sys, os
sys.path.insert(0, __import__('os').path.dirname(__file__))
from replaygrip import rot
G = 27.0
SN = {0: 'road', 1: 'dirt', 2: 'grass', 3: 'rock', 6: 'turbo', 7: 'ice', 8: 'snow'}
def rows(env):
    out = []
    for f in sorted(glob.glob('/root/ref/replays/%s*.json' % env)):
        n = os.path.basename(f)[:-5]; n = n[0].lower() + n[1:]
        sf = '/tmp/claude-0/phys/%s.surf' % n
        if not os.path.exists(sf): continue
        surf = {int(l.split()[0]): (int(l.split()[1]), float(l.split()[2])) for l in open(sf)}
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T or i not in surf: break
            su, hgt = surf[i]
            if su < 0 or hgt > 1.0: continue
            v0, v1, v2 = s[i - 1]['v'], s[i]['v'], s[i + 1]['v']
            sp = math.sqrt(sum(c * c for c in v1))
            if sp < 3: continue
            a = [(v2[k] - v0[k]) / 0.2 for k in range(3)]
            fv = (a[0], a[1] + G, a[2]); q = s[i]['q']
            right = rot(q, (1, 0, 0)); up = rot(q, (0, 1, 0)); fwd = rot(q, (0, 0, 1))
            fl = sum(fv[k] * right[k] for k in range(3)); fn = sum(fv[k] * up[k] for k in range(3)); ff = sum(fv[k] * fwd[k] for k in range(3))
            if fn < 0.3 * G: continue
            # slip: the velocity against the heading, in the ground plane
            vf = sum(v1[k] * fwd[k] for k in range(3)); vr = sum(v1[k] * right[k] for k in range(3))
            out.append(dict(sp=sp, su=su, lat=abs(fl), fwd=ff, n=fn, slip=math.degrees(math.atan2(abs(vr), max(vf, 0.1)))))
    return out
pct = lambda a, p: sorted(a)[min(len(a) - 1, int(len(a) * p))] if a else float('nan')
for env in ['Rally', 'Snow']:
    R = rows(env)
    print(env, len(R), 'samples on the ground')
    for su in sorted(set(r['su'] for r in R)):
        rs = [r for r in R if r['su'] == su]
        if len(rs) < 40: continue
        print('  %-6s n %4d  slip 90/98 %.1f %.1f deg' % (SN.get(su, su), len(rs), pct([r['slip'] for r in rs], .9), pct([r['slip'] for r in rs], .98)))
        print('     band m/s    n   lat 90/98   fwd 90 (pull, slope out)   fwd 5 (brake)')
        for b in range(0, 14):
            r2 = [r for r in rs if b * 10 <= r['sp'] < b * 10 + 10]
            if len(r2) < 12: continue
            print('     %3d-%3d %5d   %5.1f %5.1f   %6.1f                  %6.1f' % (b * 10, b * 10 + 10, len(r2), pct([r['lat'] for r in r2], .9), pct([r['lat'] for r in r2], .98), pct([r['fwd'] for r in r2], .9), pct([r['fwd'] for r in r2], .05)))
