#!/usr/bin/env python3
"""Prepare an offline, UNAPPROVED EC patch from matching live readouts.

This does not establish flash geometry, checksums, recovery, or a programming method.
It never accesses the laptop and does not produce an authenticated BIOS capsule.
"""
import argparse
import hashlib
import json
from pathlib import Path

SITES = [(0xC5EE, bytes.fromhex('4c00c008'), bytes.fromhex('40000009')),
         (0x11EFA, bytes.fromhex('d90b'), bytes.fromhex('9200'))]
GUARDS = [(0xC5F6, bytes.fromhex('5a086404')),
          (0x11EFE, bytes.fromhex('5a086409'))]

def digest(b):
    return hashlib.sha256(b).hexdigest()

def patch(a, b):
    if len(a) != 0x30000 or a != b:
        raise ValueError('requires two identical 192 KiB live readouts')
    for offset, expected in GUARDS + [(i, old) for i, old, new in SITES]:
        if a[offset:offset+len(expected)] != expected:
            raise ValueError(f'unexpected instructions at {offset:#x}')
    result = bytearray(a)
    for offset, old, new in SITES:
        result[offset:offset+len(old)] = new
    return bytes(result)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('pass0', type=Path)
    p.add_argument('pass1', type=Path)
    p.add_argument('output_directory', type=Path)
    args = p.parse_args()
    a, b = args.pass0.read_bytes(), args.pass1.read_bytes()
    try:
        candidate = patch(a, b)
    except ValueError as e:
        p.error(str(e))
    # A fresh directory makes accidental overwrite of an earlier candidate impossible.
    args.output_directory.mkdir(parents=True, exist_ok=False)
    (args.output_directory/'ec-vid-gates.UNAPPROVED.bin').write_bytes(candidate)
    manifest = dict(status='OFFLINE CANDIDATE; NOT CLEARED FOR PROGRAMMING',
        backup_sha256=digest(a), candidate_sha256=digest(candidate),
        bytes=len(candidate), preserves_secondary_100_predicates=True,
        patches=[dict(offset=hex(i), before=old.hex(), after=new.hex()) for i,old,new in SITES],
        changed_byte_offsets=[hex(i) for i in range(len(a)) if a[i]!=candidate[i]],
        unresolved=['physical flash geometry and protection',
                    'checksum and boot/mirror handling',
                    'validated programming and recovery path'])
    (args.output_directory/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))

if __name__ == '__main__':
    main()
