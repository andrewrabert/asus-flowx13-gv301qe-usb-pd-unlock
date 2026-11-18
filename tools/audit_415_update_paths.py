#!/usr/bin/env python3
"""Offline evidence only: decode stock SMM tables and locate the EC DMA helper.

No hardware access. Table interpretation depends on the separately disassembled
stock routines at RVAs 2C44/2CBC; this is not an automatic control-flow proof.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def pe_reader(blob):
    pe = struct.unpack_from('<I', blob, 60)[0]
    if blob[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Expected PE image')
    count = struct.unpack_from('<H', blob, pe + 6)[0]
    size = struct.unpack_from('<H', blob, pe + 20)[0]
    sections = [struct.unpack_from('<8sIIII', blob, pe + 24 + size + i * 40)
                for i in range(count)]

    def read(address, size):
        for _, _, va, raw_size, raw in sections:
            if va <= address and address + size <= va + raw_size:
                result = blob[raw + address - va:raw + address - va + size]
                if len(result) == size:
                    return result
        raise ValueError(f'Unbacked RVA {address:x}')
    return read


def main():
    stock = (ROOT / 'bios/bios_version_415/GV301QEAS.415').read_bytes()
    smm = (ROOT / 'out/update_415/AsusEcDxeSmm.efi').read_bytes()
    if digest(stock) != '55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94':
        raise SystemExit('Stock capsule hash mismatch')
    if digest(smm) != '39c4c0f1193e6d5daa0566e45e892d3d805b177edb1855d439bc486633728c60':
        raise SystemExit('SMM image hash mismatch')
    read = pe_reader(smm)
    selected = read(0x3214, 1)[0]
    entries = []
    for i in range(256):
        row = struct.unpack('<6I', read(0x3220 + i * 24, 24))
        if row[0] == 0x78:
            break
        if row[1] == selected:
            entries.append({'index': i, 'words': [hex(x) for x in row],
                            'mmio': hex(0xFED80200 + row[4]),
                            'cleared_bits': hex(3 << row[5])})
    else:
        raise ValueError('Missing SMI table sentinel')
    masks = [struct.unpack('<2I', read(0x31C0 + i * 8, 8)) for i in range(10)]
    helper = stock[0x4A618:0x4A65A]
    versions = []
    for path in sorted((ROOT / 'bios').glob('bios_version_*/GV301QE*')):
        if not path.is_file():
            continue
        blob = path.read_bytes()
        matches, start = [], 0
        while (target := blob.find(helper, start)) >= 0:
            callers = []
            low = max(0, target - 0x30000) & ~1
            high = min(len(blob) - 3, target + 0x30000)
            for pos in range(low, high, 2):
                word = int.from_bytes(blob[pos:pos + 4], 'big')
                if word >> 24 not in (0x48, 0x49):
                    continue
                disp = word & 0xFFFFFF
                if disp & 0x800000:
                    disp -= 0x1000000
                if pos + 2 * disp == target:
                    callers.append(hex(pos))
            matches.append({'offset': hex(target), 'nearby_direct_call_candidates': callers})
            start = target + 1
        versions.append({'file': str(path.relative_to(ROOT)), 'sha256': digest(blob),
                         'exact_helper_matches': matches})
    print(json.dumps({
        'smm_sha256': digest(smm), 'selected_smi_table_value': selected,
        'selected_entries': entries,
        'save_restore_masks': [{'offset': hex(a), 'mask': hex(b)} for a, b in masks],
        'save_semantics': 'saved = current & ~mask; mask FFFFFFFF skipped',
        'restore_semantics': 'restored = (current & mask) | saved',
        'helper_sha256': digest(helper), 'helper_versions': versions,
        'limitations': ['SMI guard is targeted, not global SMM exclusion',
                        'Halfword J/JAL scan excludes indirect calls and ROM',
                        'No DMA size/destination or programming safety established'],
    }, indent=2))


if __name__ == '__main__':
    main()
