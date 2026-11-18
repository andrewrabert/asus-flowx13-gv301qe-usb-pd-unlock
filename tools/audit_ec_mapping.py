#!/usr/bin/env python3
"""Offline BIOS-415 evidence extraction; never accesses laptop hardware."""
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAPSULE = ROOT / "bios/bios_version_415/GV301QEAS.415"
OUT = ROOT / "out/update_415/failure_analysis"
EXPECTED = "55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94"


def main():
    blob = CAPSULE.read_bytes()
    if hashlib.sha256(blob).hexdigest() != EXPECTED:
        raise SystemExit("Unexpected capsule: this audit is specific to stock 415")
    # J/JAL encode a signed 24-bit displacement scaled by two. Scan all halfword
    # positions to avoid omissions due to linear disassembly losing alignment.
    targets = [0x4A618, 0x4A65A, 0x34000]
    refs = {f"{t:05x}": [] for t in targets}
    for pos in range(0x31800, 0x56500, 2):
        word = int.from_bytes(blob[pos:pos + 4], "big")
        if word >> 24 not in (0x48, 0x49):
            continue
        delta = word & 0xFFFFFF
        if delta & 0x800000:
            delta -= 0x1000000
        target = pos + 2 * delta
        if target in targets:
            refs[f"{target:05x}"].append(f"{pos:05x}")
    # The startup loop at 4B57C consumes 18 records of pointer + byte + padding.
    # Pointers in this data table are little endian, unlike instruction encoding.
    startup = []
    for pos in range(0x560F8, 0x560F8 + 18 * 8, 8):
        startup.append({"file": f"{pos:05x}",
                        "address": f"{int.from_bytes(blob[pos:pos+4], 'little'):08x}",
                        "value": f"{blob[pos+4]:02x}"})
    report = {"capsule_sha256": EXPECTED, "direct_j_jal_candidates": refs,
              "startup_table": startup,
              "limitations": "No direct reference is not proof of unreachability; ROM and indirect calls remain unresolved."}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "mapping-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    objdump = pathlib.Path("/tmp/bu-src/inst/bin/nds32le-elf-objdump")
    for name, start, stop in [("normal-read92", 0x367FE, 0x3684E),
                              ("normal-command-dispatch", 0x36A16, 0x36B60),
                              ("startup-table-loop", 0x4B57C, 0x4B5C6)]:
        result = subprocess.run([str(objdump), "-D", "-b", "binary", "-m", "nds32",
                                 "--endian=big", f"--start-address={start}",
                                 f"--stop-address={stop}", str(CAPSULE)],
                                capture_output=True, text=True, check=True)
        (OUT / (name + ".dis")).write_text(result.stdout)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
