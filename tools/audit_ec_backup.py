#!/usr/bin/env python3
"""Compare two candidate EC readouts; never writes a programming image."""
import argparse
import hashlib
import json
from pathlib import Path

def sha(data):
    return hashlib.sha256(data).hexdigest()

def runs(offsets):
    result = []
    for i in offsets:
        if result and result[-1][1] == i:
            result[-1][1] = i + 1
        else:
            result.append([i, i + 1])
    return [[hex(a), hex(b)] for a, b in result]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('pass0', type=Path)
    p.add_argument('pass1', type=Path)
    p.add_argument('--capsule', type=Path, default=Path('bios/bios_version_415/GV301QEAS.415'))
    args = p.parse_args()
    a, b, cap = args.pass0.read_bytes(), args.pass1.read_bytes(), args.capsule.read_bytes()
    if sha(cap) != '55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94':
        p.error('unexpected stock capsule')
    if len(a) != 0x30000 or len(b) != 0x30000:
        p.error('both readouts must contain exactly the candidate 192 KiB window')
    stock = cap[0x31800:0x61800]
    result = {
        'window_bytes': len(a), 'geometry_confirmed': False,
        'pass0_sha256': sha(a), 'pass1_sha256': sha(b),
        'passes_equal': a == b,
        'pass_difference_ranges_end_exclusive': runs(i for i in range(len(a)) if a[i] != b[i]),
        'capsule_difference_ranges_end_exclusive': runs(i for i in range(len(a)) if a[i] != stock[i]),
        'pages': [dict(offset=hex(i), sha256=sha(a[i:i+4096]),
                       ff_bytes=a[i:i+4096].count(255), zero_bytes=a[i:i+4096].count(0),
                       capsule_equal=a[i:i+4096] == stock[i:i+4096])
                  for i in range(0, len(a), 4096)],
        'patch_sites': [dict(offset=hex(i), actual=a[i:i+len(old)].hex(),
                             expected=old.hex(), matches=a[i:i+len(old)] == old)
                        for i, old in [(0x11efa, bytes.fromhex('d90b')),
                                       (0xc5ee, bytes.fromhex('4c00c008'))]],
    }
    print(json.dumps(result, indent=2))
    return 0 if a == b else 1

if __name__ == '__main__':
    raise SystemExit(main())
