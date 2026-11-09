# the real cars from the author replays: speed, forward and lateral acceleration (horizontal), yaw rate, slip
import json, glob, math, sys
def qfwd(q):
    # car forward = rotate +z by quaternion (x, y, z, w)
    x, y, z, w = q
    fx = 2 * (x * z + w * y); fy = 2 * (y * z - w * x); fz = 1 - 2 * (x * x + y * y)
    return fx, fy, fz
def analyse(pattern):
    lat = {}; lon = {}; top = 0; yaws = {}; slips = []
    for f in sorted(glob.glob(pattern)):
        d = json.load(open(f)); s = d['samples']; T = d['time']
        for i in range(1, len(s) - 1):
            if s[i]['t'] > T: break
            v0, v1, v2 = s[i - 1]['v'], s[i]['v'], s[i + 1]['v']
            vh = math.hypot(v1[0], v1[2])
            if vh < 3 or abs(v1[1]) > 0.35 * vh: continue          # standing / steep (air or ramp)
            # on the ground: vertical speed steady (no free fall: dvy not ~ -g*dt*2)
            dvy = (v2[1] - v0[1]) / 0.2
            if dvy < -6: continue
            ax = (v2[0] - v0[0]) / 0.2; az = (v2[2] - v0[2]) / 0.2
            ux, uz = v1[0] / vh, v1[2] / vh
            along = ax * ux + az * uz; across = abs(-ax * uz + az * ux)
            b = int(vh // 10)
            lat.setdefault(b, []).append(across); lon.setdefault(b, []).append(along)
            top = max(top, vh)
            fx, fy, fz = qfwd(s[i]['q']); fh = math.hypot(fx, fz)
            if fh > 0.5:
                c = (fx * ux + fz * uz) / fh; slip = math.degrees(math.acos(max(-1, min(1, c))))
                slips.append(slip)
    pct = lambda a, p: sorted(a)[min(len(a) - 1, int(len(a) * p))]
    print(pattern.split('/')[-1], 'top speed %.0f km/h' % (top * 3.6))
    print(' speed band   n   lat 50/90/98%   accel 90%  brake 10%')
    for b in sorted(lat):
        if len(lat[b]) < 15: continue
        print('  %3d-%3d km/h %4d  %5.1f %5.1f %5.1f    %5.1f     %5.1f' % (b * 36, b * 36 + 36, len(lat[b]), pct(lat[b], .5), pct(lat[b], .9), pct(lat[b], .98), pct(lon[b], .9), pct(lon[b], .1)))
    print(' slip deg 50/90/98%%: %.1f %.1f %.1f' % (pct(slips, .5), pct(slips, .9), pct(slips, .98)))
analyse('/root/ref/replays/Rally*.json'); analyse('/root/ref/replays/Snow*.json')
