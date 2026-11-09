import json, glob, math, sys, os
sys.path.insert(0, __import__('os').path.dirname(__file__))
from replaygrip import rot
G = 29.4
for env in (sys.argv[1:] or ['Rally', 'Snow']):
    B = {}
    for f in sorted(glob.glob('/root/ref/replays/%s*.json' % env)):
        n = os.path.basename(f)[:-5]; n = n[0].lower() + n[1:]
        sf = '/tmp/claude-0/phys/%s.surf' % n
        if not os.path.exists(sf): continue
        surf = {int(l.split()[0]): (int(l.split()[1]), float(l.split()[2])) for l in open(sf)}
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T or i not in surf: break
            su, h = surf[i]
            if su not in (0, 1) or h > 0.6: continue
            v0, v1, v2 = s[i - 1]['v'], s[i]['v'], s[i + 1]['v']
            sp = math.sqrt(sum(c * c for c in v1))
            a = [(v2[k] - v0[k]) / 0.2 for k in range(3)]
            fv = (a[0], a[1] + G, a[2]); q = s[i]['q']
            right = rot(q, (1, 0, 0)); fwd = rot(q, (0, 0, 1)); up = rot(q, (0, 1, 0))
            if sum(fv[k] * up[k] for k in range(3)) < 0.5 * G: continue
            if abs(sum(fv[k] * right[k] for k in range(3))) > 8: continue      # straight
            B.setdefault((su, int(sp // 10)), []).append(sum(fv[k] * fwd[k] for k in range(3)))
    pct = lambda a, p: sorted(a)[min(len(a) - 1, int(len(a) * p))]
    print(env, 'straight, fwd tyre force (slope out, G %.1f): band  n  median  75%%  -> table (x5.12)' % G)
    for k in sorted(B):
        if len(B[k]) < 10: continue
        print('  %s %3d-%3d m/s  n %4d   %5.1f  %5.1f   -> %d' % ('road' if k[0] == 0 else 'dirt', k[1] * 10, k[1] * 10 + 10, len(B[k]), pct(B[k], .5), pct(B[k], .75), pct(B[k], .5) * 5.12))
