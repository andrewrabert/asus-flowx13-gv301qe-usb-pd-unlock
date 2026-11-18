#!/usr/bin/env python3
"""Compare a completed read-probe log with the known stock GV301QE.415 capsule.

A match verifies only these samples. It does not verify a complete backup, flash
writability, mirror behavior, or resumed EC thermal/charger management.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

STOCK_SHA256 = '55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94'
REGIONS = ((0, 32), (0x80, 32), (0xc5e0, 16), (0x11ef0, 16))


def check(log: str, capsule: bytes) -> dict:
    if hashlib.sha256(capsule).hexdigest() != STOCK_SHA256:
        raise ValueError('Not the verified stock 415 capsule')
    fields = dict(re.findall(r'\b([a-z_]+)=([0-9A-Fa-f]+)\b', log.split('pass=')[0]))
    required = dict(result='0', entry_sent='1', ack_seen='1', exit_consumed='1',
                    cleanup_failed='0', sample_bytes='192')
    for name, value in required.items():
        if fields.get(name) != value:
            raise ValueError(f'Incomplete/failed probe: {name}={fields.get(name)} (expected {value})')
    if fields.get('ack', '').lower() != '33':
        raise ValueError('Unexpected utility acknowledgement')
    passes = re.findall(r'pass=([01]) id=([0-9a-fA-F]{6}) sample=([0-9a-fA-F]{192})(?![0-9a-fA-F])', log)
    if len(passes) != 2 or [p[0] for p in passes] != ['0', '1']:
        raise ValueError('Expected exactly two complete sample passes')
    first = (bytes.fromhex(passes[0][1]), bytes.fromhex(passes[0][2]))
    second = (bytes.fromhex(passes[1][1]), bytes.fromhex(passes[1][2]))
    if first != second:
        raise ValueError('Read passes differ')
    expected = b''.join(capsule[0x31800+a:0x31800+a+n] for a,n in REGIONS)
    if first[1] != expected:
        differences = [i for i,(a,b) in enumerate(zip(first[1],expected)) if a != b]
        raise ValueError(f'Sample differs from stock 415 at packed sample offsets {differences}')
    return dict(sample_matches_stock_415=True, bytes_per_pass=len(expected),
                flash_id=first[0].hex(), flash_id_interpretation='not established',
                regions=[dict(ec_offset=hex(a), length=n) for a,n in REGIONS],
                complete_backup=False, write_access_tested=False, ec_runtime_health_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--capsule', type=Path, default=Path('bios/bios_version_415/GV301QEAS.415'))
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.log.read_text(), args.capsule.read_bytes()), indent=2))
    except ValueError as error:
        parser.exit(1, f'NOT VERIFIED: {error}\n')


if __name__ == '__main__':
    main()
