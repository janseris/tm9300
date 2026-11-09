import json, glob, math, sys, os
sys.path.insert(0, __import__('os').path.dirname(__file__))
from replaygrip import rot
pct = lambda a, p: sorted(a)[min(len(a) - 1, int(len(a) * p))] if a else float('nan')
for env in ['Rally', 'Snow']:
    yr = {}
    for f in sorted(glob.glob('/root/ref/replays/%s*.json' % env)):
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T: break
            v = s[i]['v']; sp = math.hypot(v[0], v[2])
            if sp < 3 or abs(v[1]) > 0.3 * sp: continue
            f0 = rot(s[i - 1]['q'], (0, 0, 1)); f2 = rot(s[i + 1]['q'], (0, 0, 1))
            a0 = math.atan2(f0[0], f0[2]); a2 = math.atan2(f2[0], f2[2])
            da = (a2 - a0 + math.pi) % (2 * math.pi) - math.pi
            yr.setdefault(int(sp // 10), []).append(abs(da) / 0.2)
    print(env, 'yaw rate rad/s by 10 m/s band: 90 / 98 / 99.5 %   (KTurn units = rad/s * 208.6)')
    for b in sorted(yr):
        if len(yr[b]) < 20: continue
        print('  %3d-%3d  n %4d   %.2f  %.2f  %.2f   -> %d' % (b * 10, b * 10 + 10, len(yr[b]), pct(yr[b], .9), pct(yr[b], .98), pct(yr[b], .995), pct(yr[b], .98) * 208.6))
