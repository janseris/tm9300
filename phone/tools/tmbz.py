#!/usr/bin/env python3
"""tmbz.py in.tmb out.tmb : the track packed for the phone (0.18): "TMBL", its size (u32), the LZMA properties byte
((pb * 5 + lp) * 9 + lc), the raw LZMA stream (preset 9 extreme, lc 0 lp 1 pb 1, a 16 MB dictionary): a fifth of the
plain file. The app decodes it on loading (tmlzma.cpp); a "TMBZ" (zlib) or a plain .tmb still loads"""
import sys, struct, lzma
d = open(sys.argv[1], 'rb').read()
if d[:4] in (b'TMBL', b'TMBZ'): open(sys.argv[2], 'wb').write(d); raise SystemExit
lc, lp, pb = 0, 1, 1
z = lzma.compress(d, format=lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME, 'lc': lc, 'lp': lp, 'pb': pb, 'dict_size': 1 << 24}])
open(sys.argv[2], 'wb').write(b'TMBL' + struct.pack('<I', len(d)) + bytes([(pb * 5 + lp) * 9 + lc]) + z)
