#!/usr/bin/env python3
"""Build an OFFLINE two-sector bundle from backups and the successful SPI profile.

No hardware access, transport, arming token, or executable flash commands.
The bundle is NOT a validated programming method.
"""
import argparse
import json
import re
import struct
from pathlib import Path
from prepare_host_candidate import prepare
from prepare_ec_candidate import digest


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def geometry(sfdp):
    require(len(sfdp) == 256 and sfdp[:4] == b'SFDP', 'invalid SFDP')
    require(sfdp[5] == 1 and sfdp[6] < 8, 'unsupported SFDP headers')
    tables = []
    for i in range(sfdp[6] + 1):
        h = sfdp[8 + i * 8:16 + i * 8]
        pointer = int.from_bytes(h[4:7], 'little')
        length = h[3] * 4
        require(pointer + length <= len(sfdp), 'truncated SFDP table')
        if h[0] == 0 and h[7] == 255:
            require(h[2] == 1 and h[3] >= 11, 'unsupported BFPT')
            tables.append(struct.unpack('<' + 'I' * h[3], sfdp[pointer:pointer + length]))
    require(len(tables) == 1, 'requires exactly one BFPT')
    words = tables[0]
    # Deliberately accept only the live geometry, rather than arbitrary devices.
    require(words[1] == 0x07ffffff, 'capacity is not 16 MiB')
    require((words[0] >> 17) & 3 == 0, 'requires three-byte addressing')
    require((words[10] >> 4) & 15 == 8, 'requires 256-byte programming pages')
    erases = [(w >> shift) & 0xffff for w in words[7:9] for shift in (0, 16)]
    require(0x200c in erases, '4 KiB erase opcode 20 not advertised')
    return dict(capacity=0x1000000, page_size=256, sector_size=4096,
                erase_opcode='20', sfdp_sha256=digest(sfdp))


def build(host0, host1, ec0, ec1, candidate, log):
    expected, changed, sectors = prepare(host0, host1, ec0, ec1)
    require(candidate == expected, 'candidate differs from guarded VID-only patch')
    require('host_read_result=0 commands=23 restored=1 jedec=EF,60,18 status=00 ' in log,
            'profile was not a successful EF6018 read')
    require('smi_access_result=0 requests=2 ' in log and 'cold_reset=0 buffer_guard_failed=0' in log,
            'SMM cleanup was not successful')
    require('status2=02 status3=20' in log, 'unexpected protection registers')
    values = []
    for n in range(2):
        matches = re.findall(rf'^sfdp_pass{n}=([0-9a-fA-F]+)\r?$', log, re.M)
        require(len(matches) == 1, 'missing or duplicate SFDP pass')
        values.append(bytes.fromhex(matches[0]))
    require(values[0] == values[1], 'SFDP passes disagree')
    geo = geometry(values[0])
    for offset in (0x31080, 0x3d5e0, 0x42ef0):
        matches = re.findall(rf'^sample_{offset:06X}=([0-9a-fA-F]+)\r?$', log, re.M | re.I)
        require(len(matches) == 1, 'missing or duplicate array sample')
        sample = bytes.fromhex(matches[0])
        require(len(sample) == 32 and host0[offset:offset + 32] == sample,
                'direct SPI sample differs from backup')
    require(sectors == [0x3d000, 0x42000], 'unexpected affected sectors')
    entries = []
    for start in sectors:
        old, new = host0[start:start + 4096], candidate[start:start + 4096]
        entries.append(dict(offset=hex(start), bytes=4096,
                            original_sha256=digest(old), modified_sha256=digest(new),
                            changed_offsets=[hex(i) for i in changed if start <= i < start + 4096],
                            programming_pages=[dict(offset=hex(p), sha256=digest(candidate[p:p + 256]))
                                               for p in range(start, start + 4096, 256)]))
    return dict(status='OFFLINE ONLY; NOT CLEARED FOR PROGRAMMING', geometry=geo,
                backup_sha256=digest(host0), candidate_sha256=digest(candidate), sectors=entries,
                required_before_any_write=['Fresh full-sector SPI reads must match original hashes',
                    'Verify AC and controller/protection state again',
                    'Resolve IT5125 reload and boot behavior',
                    'Validate writer, failure handling, and recovery availability'],
                limitations=['No hardware writing implementation is included',
                    'Saved originals cannot recover a machine that cannot execute a recovery tool'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('host0', 'host1', 'ec0', 'ec1', 'candidate', 'log', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    inputs = [getattr(args, name).read_bytes() for name in ('host0', 'host1', 'ec0', 'ec1', 'candidate')]
    result = build(*inputs, args.log.read_text())
    args.output.mkdir(exist_ok=False, parents=True)
    for item in result['sectors']:
        offset = int(item['offset'], 16)
        (args.output / f'{offset:06x}.original.bin').write_bytes(inputs[0][offset:offset + 4096])
        (args.output / f'{offset:06x}.modified.UNAPPROVED.bin').write_bytes(inputs[4][offset:offset + 4096])
    (args.output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
