import json, glob, math, sys, os
sys.path.insert(0, __import__('os').path.dirname(__file__))
from replaygrip import rot
for env in ['Rally', 'Snow']:
    X = []; Y = []
    for f in sorted(glob.glob('/root/ref/replays/%s*.json' % env)):
        n = os.path.basename(f)[:-5]; n = n[0].lower() + n[1:]
        sf = '/tmp/claude-0/phys/%s.surf' % n
        if not os.path.exists(sf): continue
        surf = {int(l.split()[0]): (int(l.split()[1]), float(l.split()[2])) for l in open(sf)}
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T or i not in surf: break
            su, h = surf[i]
            if su != 0 or h > 0.6: continue
            v0, v1, v2 = s[i - 1]['v'], s[i]['v'], s[i + 1]['v']
            sp = math.sqrt(sum(c * c for c in v1))
            if not (40 <= sp < 65): continue
            sp0 = math.sqrt(sum(c * c for c in v0)); sp2 = math.sqrt(sum(c * c for c in v2))
            acc = (sp2 - sp0) / 0.2
            fwd = rot(s[i]['q'], (0, 0, 1))
            # straight-ish only: little lateral accel
            a = [(v2[k] - v0[k]) / 0.2 for k in range(3)]
            right = rot(s[i]['q'], (1, 0, 0))
            if abs(sum(a[k] * right[k] for k in range(3)) + 27 * right[1]) > 8: continue
            X.append(fwd[1]); Y.append(acc)
    n = len(X); mx = sum(X) / n; my = sum(Y) / n
    b = sum((x - mx) * (y - my) for x, y in zip(X, Y)) / sum((x - mx) ** 2 for x in X)
    print(env, 'n', n, 'accel = %.1f + %.1f * sin(pitch)  (G_eff %.1f)' % (my - b * mx, b, -b))
    for lo, hi in [(-0.6, -0.2), (-0.2, -0.05), (-0.05, 0.05), (0.05, 0.2), (0.2, 0.6)]:
        ys = [y for x, y in zip(X, Y) if lo <= x < hi]
        if ys: print('   sin %5.2f..%5.2f  n %4d  mean accel %6.1f' % (lo, hi, len(ys), sum(ys) / len(ys)))
