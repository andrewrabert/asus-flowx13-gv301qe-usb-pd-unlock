# ASUS Flow X13 GV301QE USB PD unlock investigation

Firmware analysis and experimental tooling for the ASUS ROG Flow X13 **GV301QE**, based on BIOS **415**, investigating its restriction on high-power charging with third-party USB-C adapters.

**The EC patch is deployed and verified on the test laptop, but reliable high-power third-party charging is not solved.** This repository records an investigation, not a generally validated flashing procedure for other laptops.

## Current findings

Two ASUS USB Vendor ID checks were bypassed with a five-byte EC modification. A guarded software update changed the host SPI flash, and subsequent internal EC readbacks matched the modified image after a reported physical EC reset/reboot. Both separate `==100` conditions remain intact.

The charging tests distinguish a successful PD contract from actual input power:

- Anker 747 repeatedly negotiated **20 V at 5 A**, yet often drew about **29–30 W**.
- A low-battery Windows comparison measured approximately **29 W Anker → 70 W ASUS → 29 W Anker**, with the same 100 W contract.
- In BIOS, ASUS sustained approximately **92–94 W**. One switch back to Anker sustained about **67 W**, but later attempts returned to about 29 W.
- The captured 67 W Anker state fell to about 29 W when Windows resumed, with no observed PD renegotiation.
- A confirmed full Windows startup also retained the low state. That test started low, so it did not establish what a fresh startup would do to the high state.

The remaining cause may involve cached current, a charger-mode transition, OS state changes, or input regulation. The next investigation is a focused reconstruction of the firmware paths enabled by the patch, including assumptions specific to ASUS adapters and Anker's initial 15 W → 100 W negotiation.

## Start here

| Document | Purpose |
| --- | --- |
| [Session handoff](SESSION_HANDOFF_2026-11-18.md) | Consolidated history, results, hypotheses, evidence locations, and next steps |
| [Findings](FINDINGS.md) | Detailed chronological analysis and hardware results; later entries supersede earlier claims |
| [Agent instructions](AGENTS.md) | Current working rules and the immediate technical objective |
| [Historical agent notes](docs/AGENTS_HISTORY_2026-11-18.md) | Preserved earlier instructions and session state; not current operating guidance |

## Repository layout

| Path | Contents |
| --- | --- |
| `bios/` | Ten vendor BIOS capsules, versions 407–415 and 418, used for comparison |
| `bios_update_windows_v415/` | Official Windows BIOS 415 installer investigated offline |
| `tools/` | Firmware extraction/auditing, EC and host-SPI diagnostics, UEFI builds, Windows orchestration, and meter capture tools |
| `tests/` | Parser, protocol, wrapper, guard, and failure-injection tests |
| `docs/` | Archived project notes |
| `out/` | Local generated output, live backups, disassembly, logs, and captures; ignored by Git |
| `ec/` | Local extracted EC analysis images and disassembly; ignored by Git |

The reviewed sector payload and SFDP headers in `tools/` are included because the source uses them. The original-sector bytes match the corresponding stock capsule bytes; the modified header changes only the reviewed five bytes. Full live host backups are excluded because they preserve machine-specific content.

Links into `out/` in the historical documents refer to local evidence and will not resolve in a fresh clone. Some audits and tests require those artifacts. A clone alone does not reproduce every hardware result or supply a new laptop's required backups.

## Offline starting points

Use Python 3 for the analysis tools. Individual tools have additional requirements; for example, capsule-signature auditing uses `cryptography`, and KM003C capture uses `pyusb` plus device access.

From the repository root, this parser regression test uses synthetic inputs:

```sh
python tests/fwx_test.py
```

This audit uses the included stock 415 capsule and performs signature checks and in-memory mutation checks, without writing firmware:

```sh
python tools/audit_415_capsule.py
```

Other useful entry points include `tools/fwx.py`, `tools/audit_415_update_paths.py`, and `tools/audit_415_update_layout.py`. Inspect each tool's inputs before invoking it; not all scripts are standalone or read-only.

UEFI builds use GCC/binutils and a built GNU-EFI tree. Native protocol tests use a C compiler, with ASan/UBSan where appropriate. Previous QEMU guard tests used OVMF. These tests validate software behavior and refusal conditions; they do not emulate the actual ITE controller or establish hardware recovery.

## Firmware addressing

The live EC was identified as **IT5125 revision 02**. Instructions are big-endian NDS32; embedded data tables are often little-endian.

- Stock capsule: `bios/bios_version_415/GV301QEAS.415`.
- Verified internal EC window: **192 KiB**, matching capsule range `[0x31800, 0x61800)` before modification.
- Convert capsule code offsets to EC runtime code addresses by subtracting `0x31800`. RAM/MMIO operands are already runtime addresses.
- Changed EC-relative bytes: `C5EE`, `C5F0`, `C5F1`, `11EFA`, `11EFB`.
- Modified EC SHA-256: `eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e`.

The measured window is not proof of total physical EC flash capacity. Early analysis carves and the old IT8226 identification are superseded by the verified backup and live chip ID.

## Hardware tooling boundaries

Windows staging/select scripts can arm a one-time firmware diagnostic. Flashing tools can erase or program hardware. Do not run them simply to inspect their behavior, and do not transplant the tested payloads to another laptop.

The demonstrated update route used paired backups, exact model/BIOS/profile checks, full-sector preflight, a real stock-sector erase/restore test, and verified modified writes. The original follow-mode EC reader stalled and required physical recovery. It must not be repeated unchanged.

No external-programmer recovery was demonstrated. Preserve original live backups and verify current hardware state before any future deployment. The detailed handoff explains the tested route and its limits; this README intentionally does not present it as a one-command unlock.

## Next work

Reconstruct readable pseudocode and dataflow for both patched branches, PD capability refresh, charger transition/retry handling, and boot/resume state changes. Determine whether the VID bypass exposes an ASUS-specific assumption before proposing another patch.

A useful result is a concrete defect with a narrowly scoped correction, or a precise diagnostic prediction identifying the internal values needed to distinguish causes. More undirected charger swaps and removal of the retained `==100` checks are not justified by the existing measurements.
