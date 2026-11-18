#!/usr/bin/env python3
"""Offline, stock-415-only evidence for utility RAM mapping; no device access.

J/JAL candidates are scanned at every halfword to avoid linear-disassembly
alignment errors. This is not a control-flow proof: data may decode as code,
and constructed pointers, indirect calls, ROM code and live state are excluded.
"""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPSULE = ROOT / "bios/bios_version_415/GV301QEAS.415"
EXPECTED = "55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94"
BASE, END = 0x31800, 0x57000


def main():
    blob = CAPSULE.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    if digest != EXPECTED:
        raise SystemExit("Refusing analysis: stock 415 capsule hash mismatch")
    print(f"capsule_sha256={digest}")
    print(f"scan file interval=[{BASE:06x},{END:06x}); all halfword starts")
    print("J/JAL candidates into mapping helper and adjacent functions:")
    for pos in range(BASE, END - 3, 2):
        word = int.from_bytes(blob[pos:pos + 4], "big")
        if word >> 24 not in (0x48, 0x49):
            continue
        displacement = word & 0xFFFFFF
        if displacement & 0x800000:
            displacement -= 0x1000000
        target = pos + 2 * displacement
        if 0x4A608 <= target <= 0x4A67A:
            print(f"  {pos:06x}: {'JAL' if word >> 24 == 0x49 else 'J'} {target:06x}")

    print("Literal 32-bit pointer candidates (all byte positions):")
    for value in (0x18E18, 0x18E5A, 0xF01040, 0xF01041, 0xF01042, 0xF010B0):
        for order in ("little", "big"):
            pattern = value.to_bytes(4, order)
            start, hits = BASE, []
            while (pos := blob.find(pattern, start, END)) >= 0:
                hits.append(f"{pos:06x}")
                start = pos + 1
            print(f"  {value:08x} {order}: {', '.join(hits) or 'none'}")

    print("Initialization table: runtime 248f8; 18 entries, stride 8")
    print("Addresses decoded little-endian (instruction encoding is big-endian).")
    for pos in range(BASE + 0x248F8, BASE + 0x248F8 + 18 * 8, 8):
        address = int.from_bytes(blob[pos:pos + 4], "little")
        print(f"  {pos:06x}: {blob[pos:pos+8].hex()} -> [{address:08x}] = {blob[pos+4]:02x}")
    print("No live mapping, DMA semantics, or indirect-call reachability established.")


if __name__ == "__main__":
    main()
