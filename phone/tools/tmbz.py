#!/usr/bin/env python3
"""tmbz.py in.tmb out.tmb : the track packed with zlib for the phone (0.18): "TMBZ", its size (u32), the zlib stream.
The app inflates it on loading (the phone's ezlib); a plain .tmb still loads as it is"""
import sys, struct, zlib
d = open(sys.argv[1], 'rb').read()
if d[:4] == b'TMBZ': open(sys.argv[2], 'wb').write(d); raise SystemExit
open(sys.argv[2], 'wb').write(b'TMBZ' + struct.pack('<I', len(d)) + zlib.compress(d, 9))
