#!/usr/bin/env python3
"""Packs PNG renders of the icon into a macOS .icns (PNG-based entries)."""
import struct, sys
ENTRIES = [('icp4', 16), ('icp5', 32), ('icp6', 64), ('ic07', 128), ('ic08', 256), ('ic09', 512), ('ic10', 1024)]
out = sys.argv[1] if len(sys.argv) > 1 else 'openbrc.icns'
body = b''
for kind, size in ENTRIES:
    data = open('openbrc-%d.png' % size, 'rb').read()
    body += kind.encode() + struct.pack('>I', len(data) + 8) + data
open(out, 'wb').write(b'icns' + struct.pack('>I', len(body) + 8) + body)
