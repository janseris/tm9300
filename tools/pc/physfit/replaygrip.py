# the tyres' force per mass in the car's frame: f = a - g (g down, the class's gravity), split along the car's right and up
import json, glob, math, sys
def rot(q, v):
    x, y, z, w = q
    # quaternion rotate vector v
    ux, uy, uz = x, y, z
    tx = 2 * (uy * v[2] - uz * v[1]); ty = 2 * (uz * v[0] - ux * v[2]); tz = 2 * (ux * v[1] - uy * v[0])
    return (v[0] + w * tx + (uy * tz - uz * ty), v[1] + w * ty + (uz * tx - ux * tz), v[2] + w * tz + (ux * ty - uy * tx))
def run(pattern, G):
    rows = []
    for f in sorted(glob.glob(pattern)):
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T: break
            v0, v1, v2 = s[i - 1]['v'], s[i]['v'], s[i + 1]['v']
            sp = math.sqrt(sum(c * c for c in v1))
            if sp < 10: continue
            a = [(v2[k] - v0[k]) / 0.2 for k in range(3)]
            fvec = (a[0], a[1] + G, a[2])
            q = s[i]['q']
            right = rot(q, (1, 0, 0)); up = rot(q, (0, 1, 0)); fwd = rot(q, (0, 0, 1))
            fl = sum(fvec[k] * right[k] for k in range(3)); fn = sum(fvec[k] * up[k] for k in range(3)); ff = sum(fvec[k] * fwd[k] for k in range(3))
            if fn < 0.3 * G: continue          # off the ground (a jump)
            rows.append((sp, abs(fl), fn, abs(fl) / fn, up[1]))
    pct = lambda a, p: sorted(a)[min(len(a) - 1, int(len(a) * p))] if a else 0
    print(pattern.split('/')[-1], 'g', G)
    print(' band km/h    n  |lat| 90/98   normal 50/90   lat/normal 90/98   flat-only lat 90/98 (up.y>0.97)')
    for b in range(0, 15):
        r = [x for x in rows if b * 10 <= x[0] < b * 10 + 10]
        if len(r) < 20: continue
        fl = [x for x in r if x[4] > 0.97]
        print('  %3d-%3d %5d   %5.1f %5.1f    %5.1f %5.1f      %5.2f %5.2f        %5.1f %5.1f (%d)' % (b * 36, b * 36 + 36, len(r), pct([x[1] for x in r], .9), pct([x[1] for x in r], .98), pct([x[2] for x in r], .5), pct([x[2] for x in r], .9), pct([x[3] for x in r], .9), pct([x[3] for x in r], .98), pct([x[1] for x in fl], .9), pct([x[1] for x in fl], .98), len(fl)))
run('/root/ref/replays/Rally*.json', float(sys.argv[1]) if len(sys.argv) > 1 else 27.0)
run('/root/ref/replays/Snow*.json', float(sys.argv[2]) if len(sys.argv) > 2 else 27.0)
