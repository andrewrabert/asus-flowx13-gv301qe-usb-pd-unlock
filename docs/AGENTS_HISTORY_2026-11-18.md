# Session handoff: GV301QE EC firmware

Always begin by reading `/dev/AGENTS.md` through the noted ReadNote tool. Read
`FINDINGS.md`, especially the end of §10. Its entries are chronological: later hardware
results supersede earlier statements such as "not deployed" or "charger unplugged".


## LATEST VERIFIED STATE: INTERNAL EC PATCH CONFIRMED (2026-11-18)

This supersedes all earlier pending/stock/not-deployed state below.
After the user reported reboot following physical EC-reset instructions, the
proven reader completed two matching full 192 KiB passes. BOTH match the modified
candidate exactly, SHA-256:
`eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e`.
Exactly five EC bytes changed: C5EE,C5F0,C5F1,11EFA,11EFB. Both secondary ==100
predicates are unchanged. Host patch was previously programmed and verified;
now propagation into the normal-mode internal EC read window is verified too.
This establishes the software deployment route on this machine: guarded host
SPI sector update, followed by the reported physical EC reset/reboot. The precise
reset action/timing was not independently observed. Charging behavior is NOT yet
verified; do not claim the third-party power limit is removed from readback alone.

Evidence: `out/update_415/ec_after_physical_reset/` contains result.json,
diagnostic.txt, both binary passes, patch-readback-audit.json and status-after.txt.
Result0, completed2, window196608, elapsed379660005us, chip51,25,02,00,00,00,
incomplete0,dirty0,restored1. Collector reports Windows LastBoot00:46:27.230634;
this OS timestamp is not the wall-clock readback completion time. Entry
{528a6353-15bb-11f1-870e-af7ba5ccfe69} removed, token consumed, bootmgr unchanged,
S: unmounted, no pending diagnostic. Post-check status08 eight times, zero writes.

NEXT: behavioral test with the user's third-party USB-PD charger and suitable
cable. Record charger/cable rating, battery state and workload; compare charging
and performance against stock ASUS adapter under comparable conditions. Windows
battery charging/discharging alone does not measure the negotiated PD contract.
No further firmware write or reset is needed for this test. Do not rerun updater.
Original stock host/EC backups and all pre-reset readouts remain preserved.

## Objective and working preferences

- Modify the ASUS Flow X13 GV301QE EC's third-party USB-PD vendor restriction, staying
  on BIOS **415 or a derivative**, with a software-only deployment route if possible.
- User prefers not to open the laptop and lacks manual flashing equipment. Windows is
  installed for testing; do not require a Linux live boot without a demonstrated need.
- User authorized research, SSH access (`ssh flowx13`), and the diagnostic boots already
  performed. Continue useful investigation without repeatedly asking permission for
  the same work. Do not interpret prior diagnostic approval as proof a new flash method
  is ready. Keep hardware recovery availability and AC power current before new tests.
- Do not spawn subagents unless explicitly requested. No `.codegraph` currently exists;
  if one exists later, use CodeGraph before searching/reading code to understand it.
- Repository contents were untracked at handoff. Do not commit unless asked.

## Current verified state (2026-11-17 21:36 local)

- Laptop reachable through `ssh flowx13`, Windows admin user `user`, exact model
  `ROG Flow X13 GV301QE`, BIOS `GV301QE.415`. Secure Boot and BitLocker protection
  were off at staging. AC was present, battery 95% and discharging before and after
  earlier tests; do not equate AC presence with verified charging health.
- **Two 192 KiB normal-mode indirect backup passes succeeded and match each other
  AND the stock capsule [31800,61800) byte for byte.** This verifies the read window,
  not physical capacity or additional hidden storage.
- Backups: `out/update_415/uefi_backup_probe/ec-backup-pass0.bin` and
  `out/update_415/uefi_backup_probe/ec-backup-pass1.bin`. Both SHA-256:
  `5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478`.
  Audit: `backup-audit.json`; log: `ec-backup-probe-collected.txt` in same directory.
- Live chip registers F02000..02 = **51,25,02**, supporting **IT5125 revision 02**.
  F02085..87 = 00. Earlier IT8226 identification was based on a legacy firmware tag.
  NDS32 big-endian instructions remain confirmed; embedded data is often little-endian.
- Backup result 0, two complete passes, 380198990 us; last page restored=1,
  incomplete=0, dirty=0. Windows returned automatically at
  `2026-11-17T19:49:10.5000000-05:00`. Status-only check read 08 eight times afterward.
- Temporary backup boot entry `{528a633e-15bb-11f1-870e-af7ba5ccfe69}` removed,
  one-use token consumed, no pending bootsequence, persistent boot order retained.
  S: unmounted. No diagnostic is armed. No flash programming has occurred.
- Read-only control probe succeeded: two matching passes, result0, 96 host writes
  (PMC read command/address bytes only), 32 reads, incomplete0, 13274 us.
  F010B0=00, F01063=01, F02030=01; mapping and chip ID unchanged.
  Log: `out/update_415/uefi_control_probe/hardware-success.txt`.
  Windows returned at `2026-11-17T21:21:27.5000000-05:00`; temporary entry
  `{528a633f-15bb-11f1-870e-af7ba5ccfe69}` removed, token consumed, S: unmounted.
  Native ASan/UBSan protocol tests and three QEMU guard tests passed.
- Windows boot manager unchanged, SHA-256:
  `3f8a4cbbc97b89836df92c4895236be1fac4a127665420d90509e897a7298511`.
- Local offline candidate exists at
  `out/update_415/ec_candidate_v1/ec-vid-gates.UNAPPROVED.bin`, SHA-256
  `eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e`.
  It changes only two VID branches and retains both secondary ==100 checks.
  It has NOT been copied to the laptop; it is NOT cleared for programming.
- Remote tools/logs: `C:\Users\user\AppData\Local\Temp\flowx13-ec-audit`;
  diagnostic binaries/logs remain on ESP `\EFI\FlowEcAudit`. Never replace bootmgfw.
- Official signed PawnIO 2.2.0 installed. `windows_ec_status.ps1` requires
  `-ModulePath C:/Users/user/AppData/Local/Temp/flowx13-ec-audit/LpcACPIEC.bin`.
  It reads status only, with zero writes.

## What failed earlier — do not repeat unchanged

The previous UEFI read probe (`tools/ec_read_probe.c`, `tools/uefi_ec_read_probe.c`)
entered the utility, then stalled before any flash ID/sample was obtained. It halted
the host with **EC EXIT UNCONFIRMED**; user physically recovered it to Windows.

`out/update_415/uefi_read_probe/hardware-failed.txt` records:
`result=2 ack=33 entry_sent=1 ack_seen=1 exit_consumed=0 cleanup_failed=1 commands=3 sample_bytes=0`.
The count includes cleanup commands; the exact failed command is unknown. JEDEC opcode
9F would be the fourth write and was never sent. Zero-filled buffers are not readouts.
Its boot entry and arm token have already been cleaned up. No firmware programming has
been performed in this investigation.

## Immediate next work

User confirmed physical recovery availability ("yes i am"). The control, routing and relocated direct-access diagnostics all succeeded;
Windows returned after each. No diagnostic is armed now.
Latest pre-control-test power: PowerOnline=true, Charging=false, Discharging=true, 100%.
Do not ask the same recovery-availability question again during this session.

User explicitly says **keep going until ready to actually flash modified firmware**.
Read the latest FINDINGS.md subsections; a prepared candidate is NOT flash readiness.

1. Resolve **IT5125** RAM execution before repeating follow mode. The firmware helper
   at file 4A618 writes F01040=00, F01041=(r1>>8), F01042=(r1>>16)+8,
   F010B0=81, waits for bit0 clear, then clears F01042 bit3. No caller found.
   Adjacent 4A65A is a ret stub (called by startup), not the helper. Exact scratch
   size/destination and DMA semantics remain unknown. Do not transplant newer ITE
   register layouts or write mapping registers speculatively.
2. Normal PMC **92 reads / 93 writes** are now proven. Send command to 66, address
   high then low to 62; read response from62 or send write value to62. Addresses
   <1000 map RAM80000+addr, others F00000+addr. All steps wait for IBF clear.
   Backup code permits writes ONLY to 103B..3E, selector only00/40; NEVER writes3F.
   Normal indirect reads avoid flash utility/follow mode entirely.
3. Live mapping: selector RAM8019F=4F; F01040..42=00,00,07; F01043..4E=00.
   This does not visibly map utility runtime2800..2A12. Follow-mode stall may be
   flash instruction-fetch loss; SMM/command-data interference remains alternative.
4. Establish capacity/protection and checksum/boot/mirror behavior before programming.
   The 192KiB window is exact stock. Related IT5571 ASUS work documents cold-boot
   mirroring; it is not proof for IT5125. Our header at80 is
   A5 A5 A5 A5 A5 A5 A5 94 85 12 5A 5A AA BF 55 55.
   Do not disable mirroring based only on another chip's flag definition.
5. Direct I2EC access lead: stock415 RELOCATES config ports to **025E/025F**.
   Fresh function at4BD36 writes F02014=03,F02015=80,F0200D=03,F02012=29,
   then F0140A=02,F01408=5E,F0200A=02. Early table4E/4F is superseded.
   Live routing probe confirmed F0200A=02,F0200D=03,F01202=00.
   PCI00:14.3 ID790E1022, config48=03A0FF07,64=02501640,74=0C,90=380.
   No PCI writes needed. v1/v2 direct probes at4E/4F were inconclusive/FF IDs;
   neither read EC memory. **v3 SUCCEEDED at025E/025F**: result0, hostID51,25,
   two matching passes, saved selectors00,A5,00 restored and verified. Log
   `out/update_415/uefi_direct_probe_v3/hardware-result.txt`. Windows returned
   21:36:22.5000000-05:00; entry6343 removed, token consumed, boot manager unchanged.
   No diagnostic armed. EFI SHA256:
   `5de170e04a181165e7b105f6ba41890c2d6c6118ae65d68c4cd9da49a4244f5c`.
   Current direct sources/build producev3; use v3 Windows scripts, notv1/v2.
   Direct probe only writes host address selectors, never EC register data.
   Exact IT5125 flash/CPU halt behavior remains unresolved. Do not transplant
   Chromium DBGR I2C command27 (CPU halt) to an EC MMIO register without evidence.
   Stock AsusEcDxeSmm.efi also uses dedicated I2EC ports381/382/383 for C9xx
   mailbox RAM; potential contention remains. See latest FINDINGS subsection.
6. Next utility diagnostic needs per-command identity/status/timing, bounded waits,
   primary vs cleanup failures, distinct arm token, and validated recovery. No blind
   retry of old follow reader; no WREN/erase/program as a diagnostic shortcut.

New tools: `ec_indirect_probe.{c,h}`, `uefi_ec_backup_probe.c`,
`build_uefi_ec_backup_probe.sh`, Windows select/collect backup scripts,
`tests/ec_indirect_probe_test.c`. Backup EFI SHA-256:
`75af4282af1c86dd903d53961601da3aee13db67c2eb6bb3a0701d8e5d4d9935`.
Native ASan/UBSan and three QEMU guard tests passed. Hardware backup succeeded.
`tools/audit_ec_backup.py` compares readouts and capsule; `prepare_ec_candidate.py`
produces guarded offline candidates only. Audit/patch guards tested including wrong
sizes, unequal readouts and already patched input; NOPs verified with NDS32 objdump.

## Addresses and evidence

- Stock capsule: `bios/bios_version_415/GV301QEAS.415`.
  SHA-256 `55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94`.
- Actual EC vector base is capsule **31800**. `ec/ec.dis` addresses are capsule offsets;
  subtract 31800 for runtime addresses. RAM/MMIO operands are already runtime addresses.
  `ec/ec_full.bin` is an analysis carve, NOT a verified full backup/programming image.
- Architecture: big-endian NDS32 instructions; live chip ID IT5125 (51,25). Firmware tag `FG070F0F.GV301QE.30C`.
- Entry: file `36AB6 -> 4A55A -> 34000`; utility through `34212`.
  FC exits via `33E3C -> 34800`, restarting EC runtime rather than an ordinary return.
- Utility command 01 sets indirect follow mode; 02 forwards next byte as SPI opcode;
  03 forwards next byte; 04 reads; 05 exits follow mode. Do not use FE reset command.
- File `4B5BC` initializes selector 4F in RAM 8019F. Related ITE definitions identify
  bit 6 as internal-flash selection. Live selector=4F; mapping registers recorded above.
- `F0103B–3E`: indirect address registers; `F0103F`: data; `F01040–42`: SCAR0-like mapping;
  `F01063`: flash control. Early startup sets ILMB=0F, DLMB=80001; absolute runtime call
  1A4BE resolves to file **4BCBE**, which sets F01063 bits 0/7, not SCAR0.
- Target VID branches: file 436FA (2 bytes), 3DDEE (4 bytes); EC-relative 11EFA/C5EE.
  Keep the separate `==100` predicate. Its power meaning is not fully resolved.
- `tools/audit_415_capsule.py` verified stock RSA-PSS signature and demonstrated that
  branch changes invalidate the signed capsule. Secure Boot off does not bypass this.
  Separate EC authentication has not been established.
- Fresh disassembly excerpts and comparison source snapshots:
  `out/update_415/failure_analysis/`. Main comparison sources:
  https://github.com/coreboot/chrome-ec/blob/main/chip/it83xx/flash.c
  https://github.com/coreboot/chrome-ec/blob/main/chip/it83xx/registers.h
- Earlier protocol references: tomios/ITE_ECTool and hz12opensource/zenbook-q410-powerfix.
  Their flash tools are not validated for this board; do not run them as-is.

## Working tools and validation

- Entry-only core/header: `tools/ec_entry_probe.c`, `tools/ec_entry_probe.h`.
  EFI wrapper: `tools/uefi_ec_entry_probe.c`. Test: `tests/ec_entry_probe_test.c`.
- Build: `sh tools/build_uefi_ec_entry_probe.sh /tmp/flow-gnu-efi`.
  Binary `out/update_415/uefi_entry_probe/entry-probe.efi`, SHA-256
  `d6b067db34533561207bcc7c3f02401302a066c9daeb7358efbda53d545ddf1c`.
- Native tests passed with ASan/UBSan: DC/FC-only writes, all busy initial status values,
  entry/ACK/exit failures, trace integrity, frozen clock. QEMU tests passed wrong model,
  missing token, and matching/armed absent EC with return to test child. These do not
  emulate the actual ITE controller. Logs in `out/update_415/uefi_entry_probe/`.
- `tools/windows_select_ec_entry_probe.ps1` stages AND ARMS one boot but does not restart;
  `tools/windows_collect_ec_entry_probe.ps1` collects and removes the temporary entry.
  Existing logs intentionally block rearming: archive deliberately if another run is
  justified. Do not blindly execute staging scripts while merely inspecting state.
- Cross objdump rebuilt this session: `/tmp/flow-binutils/build/binutils/objdump`, use `-D -b binary -m nds32
  --endian=big` with bounded start/stop addresses on the stock capsule. The broad existing
  disassembly can be misaligned after padding; re-disassemble from known entry boundaries.
- GNU-EFI: `/tmp/flow-gnu-efi`; OVMF under `/usr/share/edk2/x64/`.
  VM runner from last session: `/tmp/test-flow-entry-vm.py`, images in
  `/tmp/flow-entry-probe-test`. Test child at the VM Windows boot path must NEVER be
  copied over the laptop's Windows boot manager.
- Temporary toolchains and VM files may disappear; repository source/artifacts and
  FINDINGS.md are the durable evidence. Update findings after new results.

## Latest offline analysis (2026-11-17, after 21:36 hardware result)

- `tools/audit_415_update_paths.py` reproduces stock SMM table decoding and
  cross-version helper searches. Results: `failure_analysis/update-path-audit.json`.
- ASUS SMM guard clears only FED802A4 bits12–13, selected through table value09;
  save/restore routine2C44 is not itself a global SMI disable. Do not treat this
  as an established flash-access lock. No chipset writes were performed.
- Exact66-byte RAM helper matches all ten saved BIOS versions407–418; bounded
  halfword J/JAL scans find no direct caller to the exact entry. Indirect calls
  and ROM are unexcluded. DMA destination/size remain unknown.
- IT5570 datasheet describes1KiB cache-sharing scratch but uses SCAR0H bit7;
  its bits6..3 are reserved. Stock415 uses bit3 plusF010B0. This cross-chip
  procedure/size is therefore NOT transferable. Source/hash in FINDINGS.md.
- Broader448-module/587-body raw constant scan did not resolve the copy path;
  `failure_analysis/all-pe-patterns.json` is bounded negative evidence only.
- No hardware operation in this analysis. Latest live state remains21:36,
  no diagnostic armed, no programming performed. Still not ready to flash.

## Follow-reader logging work (offline only, 2026-11-17)

- `ec_read_probe.{c,h}` now separates primary/cleanup counts and failures and
  records command byte, sent flag, wait stage, status and timing for each attempt.
  Native ASan/UBSan tests passed306 command fault positions,198 response faults,
  frozen clock and existing guards. The protocol itself is unchanged.
- `build_uefi_ec_read_probe.sh` now builds **v2** into `uefi_read_probe_v2/`,
  preserving old failure artifacts. Wrapper uses distinct v2 token/log paths.
  EFI SHA256 `0c29dd8096e9de88c5e69c10a021c01f4e6a3d6d0ff7121c768b6e1274a45580`.
  No v2 staging scripts or QEMU validation yet; not deployed/armed. Better logging
  is NOT clearance to repeat follow mode before resolving RAM execution/recovery.
- Official DLB4 source is only a USB front-end for an external programmer, with
  flash-unprotect initialization. Do not execute it as a read diagnostic.
  Official IT82302 static DMA offset80/4KiB banks does not establish IT5125 B0
  behavior. Sources/hashes: `failure_analysis/ite-official-source-audit.json`.
- No laptop access during this work. Latest live state remains21:36 as above.

## Latest offline investigation and live-state correction (2026-11-17 22:07 local)

- Fresh SSH check: **PowerOnline=false**, Charging=false, Discharging=true,
  RemainingCapacity43255; BIOS415 and last boot21:36:22 unchanged. No pending
  fwbootmgr bootsequence. No new diagnostic staged or armed. See
  `out/update_415/failure_analysis/latest-windows-state.txt`. AC must be rechecked
  before any new hardware diagnostic; earlier AC=true is stale.
- `fwx.py` fixed valid all-FF GUID PAD handling and added AMD zlib decompression.
  Old448-module extraction missed most PEI! Now547 modules/96 PEIMs. All391 unique
  PE/TE hashes agree with independently built UEFIExtract. Six regression tests
  pass (`python tests/fwx_test.py`). Results in `out/update_415/full_extract/`.
- New IA32 AsusEcPei/AsusEcPeiBiosReady analyzed; no IT5125 DMA sequence established.
  Do not treat old negative PEI searches as complete. Full details at FINDINGS end.
- Offline ITE IFU EFI2.3.1a detects different chip families at2E/4E, not51 at025E.
  FEVM copy identical; Windows IFU_XE is older2015v1.463. Neither executed or copied
  remotely. Do not invoke even as a query or patch chip detection to force use.
  Provenance and disassembly: `out/update_415/ifu_analysis/`.
- Stock AmdSpiRomProtectDxe ReadyToBoot callback4C0 writesFEC10009=06 and clears
  FEC10000 bits22/23 (host-SPI access controls). Static evidence, not a live read or
  irreversible-lock proof. Host-SPI programming is not a validated fallback and
  would still need EC mirroring/authentication resolved. Default flashrom probing
  writes ROM protection registers, so is not a read-only diagnostic.
- Still not flash-ready. Material missing evidence: exact IT5125 DMA/execution and
  flash/boot semantics or a validated chip/board-specific updater. Do not repeat
  unchanged follow mode or speculative mapping writes to manufacture progress.

- User-supplied `bios_update_windows_v415/ASUS_GV301QE_415_BIOS_Update_3.exe`
  extracted offline. Payload capsule is byte-identical to our stock415. Installer
  uses PowerShell/pnputil + Firmware INF, resource GUID
  9FEB6FC6-433F-59F0-A6B2-66A1D7046D5B, for Windows UpdateCapsule delivery.
  No separate EC flasher/.sys or DMA procedure found. Wrapper changes do not solve
  capsule authentication. Audit/extracted files: `out/update_415/windows_package/`.
  Never run BIOSInstall.ps1 merely to inspect: it uninstalls/installs firmware packages.

- Stock update path traced offline; details at FINDINGS end, artifacts under
  `out/update_415/update_handler/`. New audit `tools/audit_415_update_layout.py`
  passed, as did existing capsule signature audit.
- ROML entry4 (table capsuleEC0AB0): hostFF031000, raw31000, size40000, flags200.
  Contains EC at capsule31800..61800; **256KiB host region != EC capacity**.
  ReFlash1724/1594 ->146C -> generic flash protocol+18, then FlashDriver2028
  ->1BC8/2854 -> SPI NOR backend with controllerFEC10000. FlashSmiDxe/Smm can
  forward these calls into SMM. Registration order/live selection not established.
- SecSMIFlash protected write guards1F28 and RSA verification3148/2F64/2C04 cover
  this region; no standalone unsigned EC acceptance found. UpdateCapsule138C
  stages data for reboot; not an EC RAM loader. Full reboot-time chain remains
  incomplete. EC boot-ROM mirroring remains an inference, not established behavior.


## Latest live SPI audit (2026-11-17 23:01 local)

- User connected power. Preboot AC=true AND Charging=true, Discharging=false.
- Read-only SPI v1 PCI reads worked, RootBridge Mem.Read failed invalid parameter.
  FFFFFFFF values in v1 are placeholders. Evidence preserved; do not use as data.
- V2 exact-model/415/PCI-ID/BAR-guarded direct MMIO succeeded twice:
  FEC10000=4F0C2105, +04=0, +08=600. Bits22/23clear, restricted byte09=06.
  PCI00:14.3 ID790E1022, BARFEC10002; ROM protection50/54/58/5C all0.
- Windows returned23:01:40.5; entry528a6345-15bb-11f1-870e-af7ba5ccfe69 removed,
  token consumed, bootmgr hash unchanged, S unmounted. Nothing armed. No programming.
- Artifacts out/update_415/uefi_spi_probe_v2/{hardware-result.txt,audit.json}.
  EFI SHA edcdc1b873f8978f77e0941cc99ba8d27f0b71582a41035abe4e6031c0eb90fd.
  Native ASan/UBSan and QEMU guard tests passed. No EC/SPI command/control writes.
- Stock FlashDriver3120 enable and31BC restore match measured protected state.
  Enable manipulates FEC10002/09 plus conditional FED8029B/B2; NOT executed.
  Details at FINDINGS end and update_handler/FlashDriver.enable-disable.{c,dis}.
  Current protection is NOT proof of permanent locking; stock software has an
  intended reopening path. Custom writability/SMM policy remains unproven.
- Still NOT flash-ready: host SPI access is only one issue; signed update guards
  and exact IT5125 boot/mirror/programming behavior remain unresolved. Next offline
  work: SMI policy around enable hooks and stock EC propagation. Do not repeat
  unchanged probes or run flashrom's default mutating probe as a read-only check.


## Latest investigation (2026-11-17 23:13+, READ FINDINGS END)

- Full mapped BIOS backup succeeded: two identical16MiB files under
  out/update_415/uefi_host_backup/, SHA256
  74c8b75a51496d9d7821883552b0157a6685e233aa0581cdf888a899680ffade.
  Host EC copy matches internal backup and stock.96 other4KiB pages differ from
  stock; preserve live device-specific data. Backup boot entry cleaned.
- Local host derivative prepared in host_candidate_v1/, SHA256
  a7132dae37e0ebe8632de0da5ff4b9fa977e247781eab35c1f426bb76a0b1369.
  Five changed bytes at3D5EE,3D5F0,3D5F1,42EFA,42EFB; two4KiB comparison pages,
  NOT confirmed erase geometry. Not copied remotely/not cleared for programming.
- PSPTool padding heuristic missed canonical FET20000; checked locator adapter
  tools/inspect_415_psp.py parses live/stock unchanged. No directory entry overlaps
  EC region31000..71000. This is not proof against indirect authentication.
- Live platform controls: SPI+1D98, FED80298=1DFFBE77, A4=0, B0=01400004.
- Direct EFI access-bit test attemptedCC then restored0C atFEC10002. Enable request
  IGNORED; before/during/after4F0C2105, restriction600unchanged. Windows23:13:39.5;
  entry528a6347...removed, tokenconsumed, bootmgrunchanged. No flashcommands.
- New SmiFlash20DC interface lead: SW-SMI20..25, registration26F4 no observed
  capsule-boot-mode gate.20/24 enable/disable ALSO touch power,keyboard,PIC and
  NVRAMvariablebackup/restore; DO NOT execute as read-only probes. Read FINDINGS.
- Version-only SW-SMI25 probe is currently booting; source uefi_smi_info.c.
  Collect with windows_collect_smi_info.ps1 when SSH returns; output dir
  out/update_415/uefi_smi_info/. No20/24or mode2request is included.

Latest correction: SMI25 version probe completed successfully; Windows returned
23:19:58.5, temporary entry528a6348 removed, token consumed, bootmgr unchanged.
Paired SMI20/24 probe is local only, under review; NOT armed or run.
Its token now says PAIRED SMM ACCESS TEST V1; rebuild and refresh staging hash
before any use. Stock disable includes an unbounded SMM status-clear loop.

Paired SMI access diagnostic staged for one boot: entry
528a6349-15bb-11f1-870e-af7ba5ccfe69, EFI SHA256
6dff273b952656f210c3569dda45a7dad91cb9c2d227442f4d37455745e47ecf.
Native core + wrapper ASan/UBSan tests and QEMU3 guard tests passed.
AC/charging present. Reboot requested; COLLECT RESULT before declaring completion.
Only20/24 requested, no erase/write; stock callbacks modify platform controls.

### Paired SMM controller unlock VERIFIED (2026-11-17 23:30 local)

SMI20 then24 succeeded, result0: control4F0C2105 ->4FCC2105 ->4F0C2105;
restricted DWORD600 ->0 ->600. Both status bytes00. PM1 enable0 ->120
restored0 by wrapper; PICFF unchanged; SMIcontrolAC40 unchanged; buffer guard
intact; cold_reset0. No explicit erase/program commands. Stock callbacks execute
platform-control changes, so this was not a read-only control snapshot.
Windows returned23:30:28.5; entry528a6349 removed, token consumed, no bootsequence,
bootmgr hash unchanged. SSH delay was not a demonstrated diagnostic stall.
Screen-status question is obsolete. Results uefi_smi_access/hardware-result.txt.
This proves a live controller-unlock round trip, NOT successful flash programming
or host-to-EC propagation. Next: allowlisted flash identification/status/SFDP
reads, protection/geometry, and exact IT5125 boot/mirror behavior. No candidate
has been copied remotely. Never infer flash readiness from access bits alone.

Host SPI ID/status reader staged: entry528a634a-15bb-11f1-870e-af7ba5ccfe69,
EFI a467f7da97c8d51ad71e700d7a7df32f80f8a873d42320361360cf7e311a3b5e.
Uses paired20/24 around allowlisted9F/05 only. Stock indexed counters5/6, FIFO80,
executebit16, pointer-resetbit20. Bounded waits including frozen-clock fallback.
Temporarily disables SMI using exact stock2ECC control sequence with readback,
CLI around transactions, then restores controls and prior IF state. No EFI calls
inside that critical section. Native core/wrapper/exclusion tests, QEMU3 guards
passed. Programming commands absent. Reboot requested; collect before declaring
completion. tools/windows_collect_smi_read.ps1 is the corresponding collector.

### ID/status diagnostic REFUSED safely (2026-11-17 23:35 local)

Windows returned23:35:57.5; entry528a634a removed, token consumed, bootmgr unchanged.
Result1 requests0: snapshot initially4F0C2105, later precondition4F0C1096 after
50ms timer calibration. Thus last-opcode/count fields are mutable during preboot.
Host_read_result99, commands0, all-zero ID/status are NOT readouts. No exclusion
controls changed. Log uefi_smi_read/hardware-result.txt.
Adjustment under test: require exact HIGH16 control4F0C and restricted0/600,
preserve actual lower16 state; unlocked high16 must4FCC. These retain idle,
mode, access protection requirements while allowing prior command/count state.

ID/status v2 staged: entry528a634b-15bb-11f1-870e-af7ba5ccfe69, EFI SHA256
821124bd2838dec754e77ffd953cc2374f8a4f6436225cb721574faafd7e3d57.
High16 exact idle/protection guard; actual opcode/count saved/restored.
SPI+1D bits1:0 must zero (primary CS). Native tests include alternate prioropcode
96 and wrongCS refusal, plus prior timeout/restoration scenarios. QEMU3guards
passed on finalbinary. AC/charging verified. Reboot requested; collect via
windows_collect_smi_read_v2.ps1 before declaring completion.

### Direct host SPI ID/status reads SUCCEEDED (2026-11-17 23:39 local)

Host_read_result0, commands2 (9F/05), restored1, JEDEC EF,60,18; status00.
SMI exclusion readback verified; FED8029B1D ->disabled ->1D; FED802B240 restored40.
Paired SMM result0; control4F0C2105 ->4FCC2105 ->4F0C2105; restriction600->0->600.
PM enable0 restored, PICFF and SMIcontrolAC40 unchanged, buffer intact, no reset.
Windows returned23:39:04.5; entry528a634b removed, token consumed, bootmgr unchanged.
Results uefi_smi_read_v2/hardware-result.txt and result.json. This establishes
actual software SPI reads, not programming or exact Winbond suffix identification.
Next: manufacturer documentation and SFDP/protection register readout. No WREN,
erase, program or status-write has been issued. Modified images remain local.

## Latest continuation: SPI profile succeeded (23:48 local)

Read latest FINDINGS. SMI profile diagnostic completed, Windows returned, entry
528a634c removed; no pending diagnostic. Host JEDEC EF6018; two identical SFDP
passes establish16MiB/256byte page/4KiB erase20h (also32KiB52h/64KiBD8h).
Direct03 samples at31080,3D5E0,42EF0 exactly match full host backup.
Status00/02/20 implies BP0/CMP0/WPS0 under Winbond JW definitions, not exact
suffix identification or demonstrated write ability. All controls restored.
Artifacts out/update_415/uefi_smi_profile/profile-audit.json and diagnostic.txt.
No candidate copied remotely; no WREN/erase/program/status write. AC was detected
before staging but charging false/discharging true. Recheck for future tests.
Next: exact IT5125 boot/mirroring behavior, offline guarded write preparation
using now-established geometry, and recovery limitations. Do not rerun obsolete
follow utility or declare readiness based on successful reads.

Offline two-sector bundle now prepared at out/update_415/host_sector_bundle_v1.
Generator tools/prepare_host_sectors.py validates hardware SFDP/profile and
paired backups before emitting sector/page hashes; eight negative cases passed.
This has NO writer/transport. Fresh full-sector direct reads still needed for
preflight. Exact IT5125 mirror semantics and a bounded writer/recovery route
remain unresolved; do not equate bundle existence with flash readiness.
User replied charger connected; successful profile already followed live AC check.

Latest offline work: stock FlashDriverSmm487C erase/4C60 program traced;
program chunks<=64bytes clipped to256byte boundaries, timeout fallthrough and
retry semantics unsuitable for direct reuse. Artifacts platform_policy/
FlashDriverSmm.write-path.c and .write-helpers.c.
tools/host_update_model.py + tests/host_update_model_test.py provide OFFLINE
transaction model only, six tests including1300failure positions passed.
No transport/deadline validation, no new laptop reboot or flash action.
Next implementation work: full-sector direct SPI preflight; bounded transport
and cleanup on ambiguous completion. Exact IT5125 reload remains unresolved.
Do not represent the model as a hardware-tested writer.

## Latest verified result: full-sector preflight (2026-11-18 00:05)

Read latest FINDINGS. Hardware preflight SUCCEEDED:281 SPI reads,21008us,
full sectors3D000/42000 match originals, exact SFDP/status before/after, all
controls restored. Windows returned00:05:09.5; entry528a634d removed; no token
or bootsequence; bootmgr unchanged. ECstatus08eighttimes afterward.
Artifacts out/update_415/uefi_smi_preflight/result.json and diagnostic.txt.
EFI SHA b08338da6b0e05a6c5bb2679b13e092d4bab39fd62ac70cb15730397904165bb.
New tools/host_spi_writer.c is a REAL indexed-MMIO core, but writing path tested
ONLY with local NOR/MMIO emulator (2853stuck-command positions). Laptop app
compiled HOST_SPI_PREFLIGHT_ONLY: write mode refused before MMIO, all writing
opcodes refused. It embeds only original bytes. No WREN/erase/program on laptop.
No writing-capable EFI wrapper exists; do not call the writing path validated.
Next blockers: reviewed failure/recovery wrapper and exact IT5125 reload behavior.
A failed write with dirty_mask!=0 cannot simply boot Windows; unsafe engine or
busyflash cannot simply restore controls or reset. No recovery programmer known.
Stock SbRun coldreset2AF0 sets FED80310bit1 thenCF9=02,06; decompiler omitted
this MMIO write, so consult SbRun.dis. Its hooks show no direct EC reload command.
NDS32 objdump now /tmp/flow-binutils/build/binutils/objdump.

## Active same-byte program test (2026-11-18 continuation)

New tools/uefi_smi_stock_program.c compiled HOST_SPI_STOCK_PROGRAM_ONLY allows
one32-byte program of ORIGINAL bytes at3D5E0, no erase, no modified input.
Native fault tests and QEMU guards passed. SHA256 aadc3dd37b33a73a98c9a8d0368889e80f84c9d9902c04767333bd643d863d63.
Staged one-use entry528a634e-15bb-11f1-870e-af7ba5ccfe69 after AC/battery check.
Reboot next. COLLECT RESULT with windows_collect_smi_stock_program.ps1 before
claiming success. Artifacts out/update_415/uefi_smi_stock_program/.
New separate stock-erase/restore wrapper LOCAL ONLY, no staging scripts. Its
old built artifact predates latest shared-core edits; rebuild before any use.
Unsafe/dirty writer HALTS before restoring SMI/IF or calling EFI; no reset/retry.
Unchanged same-byte readback does not prove physical array programming occurred.

Same-byte program SUCCEEDED: Windows00:21:17.5,670commands,1program,0erase,
full sectors unchanged; all controls restored, entry634e removed, ECstatus08.
Artifacts uefi_smi_stock_program/result.json. Does not prove changed cells.
Stock erase/restore binary REBUILT current hash c0437bdb01d8339a98d1bb6236d4ea9f5b8ff6e5047d30d9245524e1d43a7eb2; QEMU3guards passed.
Now STAGED one-use entry528a634f-15bb-11f1-870e-af7ba5ccfe69 after AC/battery
checks. Reboot next. Collect via windows_collect_smi_stock_restore.ps1.
This erases and restores ORIGINAL sector3D000; no modified image. Unsafe/dirty
failure halts without resetting. DO NOT infer success until hardware log collected.

## LATEST VERIFIED: REAL ERASE/PROGRAM SUCCEEDED (2026-11-18 00:22)

This supersedes earlier "no programming"/"writer only simulated" statements.
Stock restore result0:3183SPIcommands,1sector erase,128page-program commands,
134589us,restored1,dirty0. Sector3D000 verified FF aftererase, then restored
original4096bytes. Both sectors3D000/42000 compared before/after. Profile matched.
Windows returned00:22:57.5; entry528a634f removed, token consumed, no bootsequence,
S: unmounted, bootmgr unchanged. ECstatus08eighttimes afterward. No diagnostic armed.
Artifacts out/update_415/uefi_smi_stock_restore/result.json,diagnostic.txt,
build-audit.json,ec-status-after.txt,power-after.txt. EFI c0437bdb...43a7eb2.
Prior same-byte test also passed (670commands,1program,0erase; entry634e removed).

Software HOST SPI NOR erase/program now demonstrated on target sector. No modified
firmware deployed; no EC-internal programming or host-to-EC reload demonstrated.
Remaining: review modified deployment and establish/test exact IT5125 propagation.
Do not call writer merely simulated anymore; do not call EC patch running either.
New tools/uefi_smi_stock_restore.c / uefi_smi_stock_program.c stop unsafe/dirty
writes before SMI/IF restoration and EFI callbacks. Failure containment is NOT
recovery. No automatic reset/rollback. Generic modified writer still local only.
Compile flags HOST_SPI_STOCK_RESTORE_ONLY and HOST_SPI_STOCK_PROGRAM_ONLY restrict
write APIs and transaction addresses; HOST_SPI_PREFLIGHT_ONLY still read-only.
Native sanitizer tests cover671program/1695restore/2853modified stuck positions,
plus failure guards. Actual3183command count includes extra hardwarebusy polls.
User's "proceed" followed explicit discussion of test's physical-programmer risk.

## ACTIVE MODIFIED DEPLOYMENT (2026-11-18 continuation)

User authorized proceeding with two-sector VID patch and post-reboot EC readback.
New tools/uefi_smi_vid_patch.c built with HOST_SPI_VID_PATCH_ONLY; exact payload
five bytes, both secondary==100 guards preserved. Native2853fault positions,
partial-update halt tests and QEMU4guards passed. Artifacts uefi_smi_vid_patch/.
EFI09c45b4d540ca4ce5a03c56f3c2422c33273c2a4d68c985750cec516482d6a23.
STAGED entry528a6350-15bb-11f1-870e-af7ba5ccfe69, reboot next. COLLECT with
windows_collect_smi_vid_patch.ps1 before claiming success. This IS modified
firmware, unlike prior tests. EC reload remains unverified until internal readback.
Original EC backups archived after hash verification in ESP/TEMP directories
backup-before-vid-20261118. Local original backups unchanged. Proven backup EFI
75af4282...d9935 and original select/collect scripts ready to reuse after patch;
no backup entry armed. Keep new readbacks under a DIFFERENT LOCAL DIRECTORY.

## LATEST: MODIFIED HOST PATCH WRITTEN, EC READBACK PENDING

VID update SUCCEEDED:5842commands,2erases,256programs,229308us; both modified
sectors verified, all controls restored. Windows00:31:06.5, entry6350 removed,
token consumed, bootmgr unchanged, ECstatus08. Artifacts uefi_smi_vid_patch/.
The five-byte VID patch IS NOW in host flash as verified by updater. No proof yet
that internal EC firmware changed or third-party charger behavior changed.

Internal EC backup STAGED entry528a6351-15bb-11f1-870e-af7ba5ccfe69, reboot next.
Uses identical proven EFI75af4282...d9935, two192KiB passes (~6minutes).
Collect with windows_collect_ec_backup_probe.ps1; local outputs MUST go under
out/update_415/ec_after_vid_patch, preserving original backups. Run
python tools/audit_ec_patch_readback.py out/update_415/ec_after_vid_patch.
Do not re-run updater: original-sector precondition should refuse patched inputs.

EC readback completed00:38:43.5: two full matching192KiB passes, EXACT STOCK
5bc14f...7478, zero changed bytes. Internal branches remain4C00C008/D90B.
Ordinary reboot did not load modified hostcopy. Entry6351 removed, bootmgr unchanged.
Results out/update_415/ec_after_vid_patch/patch-readback-audit.json.
Read-only host persistence verifier now STAGED entry528a6352-15bb-11f1-870e-af7ba5ccfe69,
EFI0e230e4e07fdc6daf3bae2e8f2f606a737c012c16239664331735a4323d989b5.
Reboot next; collect windows_collect_smi_vid_verify.ps1. Reads compare both sectors
against modified payload, compile-time refuses programming. No new flash write.
If host patch persisted, next candidate is documented physical ASUS EC reset,
not speculative FE/watchdog/MMIO writes. Official ASUS FAQ1050239 reviewed.
Do not request/reset while diagnostic is pending; complete collection first.

## LATEST VERIFIED STATE (2026-11-18 00:40 local)

Host patch PERSISTED across reboots: readonly verifier result0,281commands,
0erases/0programs,21008us,both full modified sectors match. Entry6352 removed,
token consumed,no bootsequence,S: unmounted,bootmgr unchanged,ECstatus08.
Windows00:40:40.5. Host patched; internal EC STOCK per matching two192KiB passes.
DO NOT conflate successful host flashing with patched EC execution/charging.

All diagnostics complete; NONE armed. User ASYNC REQUEST PENDING: perform ASUS
physical EC reset (shutdown, peripherals off, adapter connected, hold40s; if20s
variant flashing after15s release when stops), poweron and report Windows back.
Official https://www.asus.com/support/faq/1050239/ ; reset may trigger reload but
IT5125 behavior still unproven. No speculative software FE/watchdog/MMIO reset.
Do not claim user performed reset until confirmed. If confirmed, check live
model415/SecureBoot/BitLocker/AC, then reuse proven backup EFI75af4282...d9935
and windows_select_ec_backup_probe.ps1 + collector. Save fresh readouts under
NEW local out/update_415/ec_after_physical_reset (not old original/after_vid dirs).
Run audit_ec_patch_readback.py; expect ~6min20s plus Windows/network startup.

Original backups preserved in local uefi_backup_probe; post-patch STOCK backups
in ec_after_vid_patch. Remote original archive backup-before-vid-20261118;
postpatch STOCK archive backup-before-ec-reset-20261118. Both verified before
archiving, so original backup paths are clear for the next read. No need to
rearchive those archives or rerun updater. User may not be physically available;
continue independently where useful, but don't fake a physical reset over SSH.

## ACTIVE POST-REPORTED-RESET READBACK

User said "its rebooted" after physical reset instructions; button duration not
independently observed. BIOS415/SBoff/BitLockerOff/model guards pass; ACtrue,
Chargingfalse,Dischargingtrue,capacity43160. LastBoot timestamp unchanged from
00:40:40.5 (possible hybrid shutdown), so don't assert reset from timestamp alone.
Backup STAGED entry528a6353-15bb-11f1-870e-af7ba5ccfe69, reboot next. Same proven
EFI75af4282...d9935. Collect with windows_collect_ec_backup_probe.ps1 into local
out/update_415/ec_after_physical_reset; run audit_ec_patch_readback.py. No writes.
Wait6min20s plus boot/network (~8min observed last time); collect before claiming
EC propagation. Do not overwrite any prior backup directory.

### User reports ~90 W in BIOS, reduced input after Windows boot (2026-11-18)

User observes third-party charger input around90W while in BIOS setup, falling
after Windows boot; describes ASUS-only100W behavior in Windows. This is strong
behavioral evidence of high-power capability before OS boot, not an independently
logged PD contract or proof of which component changes policy. Do not conflate
USB input power with battery charge power. Exact voltage/current/load/timing and
charger model remain unrecorded. SSH attempt this turn timed out; no remote
service/driver inventory or live status was collected and nothing was changed.

Next investigation: compare PDST/ACNG and source-power state across OS transition;
distinguish ACPI/EC OS-mode policy, UCSI/PD renegotiation, ASUS driver/service
policy, and changed charging demand. Safe Mode or a live Linux comparison could
isolate layers, but neither has been run. Earlier claim that ACPI only reads a
verdict is bounded to inspected status paths, not proof OS cannot alter EC policy.
Static415 DSDT EC _REG only sets ECFL; _INI calls KINI which invokes STB1(2),
STB1(4), clears FNIV. ATKD INIT only sets ATKP. These are leads, not identified
charge-limit triggers. No further patch or bypass of secondary==100 justified.

### Windows power-policy inventory and live PD status (2026-11-18 06:52)

User reports meter drops during spinning Windows boot animation, before desktop.
SSH now works. Read-only inventory saved windows_power_policy/inventory.json.
ASUS System Control Interface v3 3.1.72.0, ATKWMIACPIIO (System start),
ArmouryCrateControlInterface and ASUSOptimization plus other ASUS services active.
Microsoft UcmUcsiAcpiClient/UcmUcsiCx/UcmCx active, UCM-UCSI ACPI device
ACPI\USBC000\0 driver10.0.26100.9818. Presence is NOT causal attribution.
No services/devices disabled, no reboot or firmware writes this turn.

Discovered AsusAtkWmi_WMNB.DSTS(Device_ID:uint32). Queried ONLY0012006C
three times through ordinary serialized ACPI getter; all returned0001002A.
Getter selects EC bankC9 under CMUT and reads PDST. This is a status query,
not a zero-port-write operation. Scripts/results in windows_power_policy/.
Third-party charger currently connected is awaiting user confirmation. Battery
PowerOnline=true,Charging=true,ChargeRate908,RemainingCapacity7251 at inventory;
no contemporaneous external meter reading collected.

Important correction to early FINDINGS: PDST is a flag byte; interpreting only
exact03 as qualified is insufficient. EC producer3DDA6 builds r7=2/6, adds08
only after the (now bypassed) VID branch and retained secondary==100 predicate,
adds20, then3DE60 passes r7 to3EE1A, which writes RAM8099B. Live2A contains08,
consistent with qualified branch passing in Windows. This argues AGAINST blindly
removing secondary==100. Full consumer/OS-transition policy still unresolved.
Evidence disassembly windows_power_policy/pd-status-producer.dis; raw WMI samples
pd-status.json. No conclusion that Windows restores original firmware or VID test.
Next: same-state ASUS vs third-party PDST/meter comparison and isolate optional
ASUS stack versus UCSI/ACPI transition. Safe Mode/live Linux not yet tested.

### Third-party ~80 W in Windows after reconnect (2026-11-18 07:09)

User clarified prior status could have been on ASUS adapter; ASUS meter reading
was60–80W. After switching back to third-party, user now observes~80W in Windows.
New explicitly third-party DSTS0012006C samples all0001002A (three readings).
Battery32%,PowerOnline=true,Charging=true,Discharging=false,ChargeRate43861,
DischargeRate0,Voltage15938,RemainingCapacity14280. Artifacts:
out/update_415/windows_power_policy/thirdparty-after-replug-pd-status.json and
thirdparty-after-replug-battery.txt. Battery rate and external USB input are
different measurements; not a synchronized energy balance.

This demonstrates user-observed high-power third-party input in Windows as well
as BIOS. A persistent Windows-only30W ceiling is contradicted by this observation.
Initial boot-time reduction remains unexplained: reconnect/PD negotiation state,
ASUS-to-third-party state carryover, or demand changes remain hypotheses. Do not
claim unconditional100W operation or proven causal mechanism. No services,
drivers or firmware changed during this comparison. No further patch justified.
Next validation: boot with ONLY third-party attached (without ASUS first), observe
input during boot and after a third-party-only unplug/replug if it drops. Capture
PDST in low/high states; compare comparable charge/load conditions.

### Third-party-only boot reproduces low input, PDST unchanged (2026-11-18 07:18)

User booted with third-party charger only and reports apparent Power-Z
renegotiation during Windows spinner, then~30W input. No decoded PD packets or
contract values yet; renegotiation is a user observation, not established trace.
Before any requested cable change, three DSTS samples again0001002A, SAME as
third-party~80W state. Battery32%,Charging=true,PowerOnline=true,Discharging=false,
ChargeRate956 versus43861 in previous high-input state; RemainingCapacity14376.
Artifacts windows_power_policy/thirdparty-boot-low-{pd-status.json,battery.txt}.
PDST qualification flag remains set in BOTH states; status does not distinguish
the low/high power behavior. Do not infer re-enabled VID gate or bypass secondary
checks. No remote changes/reboot. Next: unplug/replug ONLY same third-party
charger while Windows stays running, without ASUS insertion, then recapture.
Record meter voltage/current or negotiated contract if available.

### Third-party-only reconnect restores high input (2026-11-18 07:20)

User unplugged/reconnected ONLY same third-party charger in running Windows,
without ASUS insertion. Meter jumped~80W then settled60–80W. Three new PDST
samples remain0001002A, identical to low state. Battery34%,Charging=true,
PowerOnline=true,Discharging=false,ChargeRate39303,RemainingCapacity15109.
Artifacts windows_power_policy/thirdparty-only-replug-{pd-status.json,battery.txt}.
This rules out ASUS insertion as necessary for the observed recovery. Practical
workaround demonstrated once: reconnect third-party after Windows boot. Boot
transition/negotiation policy remains unresolved; no proof of specific UCSI,
ASUS service, EC handler or PD contract change. Patch supports observed60–80W
third-party Windows input, not proven unconditional100W. No further firmware
changes or service/device changes performed. Next investigation should compare
boot versus attach negotiation/EC state rather than remove qualification checks.

### UCSI reset path traced; contract capture preparation (2026-11-18)

User reports ASUS cold boot holds~93W through spinner then tapers60–80W, unlike
third-party abrupt~30W boot drop. This strengthens charger-specific transition
behavior; does not independently prove a changed PD contract.

Read-only live inventory: ACPI\USBC000\0 statusOK; UCMUCSICX Operational
log enabled, zero events. Provider569d11aa-5068-5ee5-da22-ce541c0b1481 manifest
exposes error event only, not full command history. Artifacts ucsi-provider.txt
and ucsi-logging-inventory.txt under windows_power_policy.

Static command dispatcher469B2 reads RAM80908, indexes little-endian20-entry
table at capsule51A70 (runtime20270). Decoded ucsi-command-table.json.
Command01 PPM_RESET ->468F4 ->4930C(port0, then1 if first succeeds). Backend
issues I2C command sequence then reads register29 and modifies first two data
bytes as (old & 5F)|50 before writing four-byte payload back. Both port paths
are real PD-controller writes; exact chip/register-bit semantics not proven.
No VID comparison in this bounded reset backend. Command03 CONNECTOR_RESET
->468A0 ->493B8; command05 notification enable ->46830 ->49414(ret stub).
Fresh disassembly ucsi-{dispatch,reset-handlers,pd-reset-backend}.dis. This
establishes a candidate boot reset path, NOT that Windows actually invoked it
at the observed drop or that it causes the charger-dependent behavior.

Official Microsoft UCSI docs describe GET_CONNECTOR_STATUS and GET_PDOS via
UCSIControl, requiring TestInterfaceEnabled=1 plus device restart. Restart can
perturb measured state. Interface NOT enabled; no registry/device/service changes,
no UCSI commands or firmware writes. Meter model/current source/contract details
asked asynchronously, not answered yet. External trace remains preferred for
unperturbed boot contract evidence.

MUTT3.0.2 downloaded from official HTTPS download.microsoft.com URL
https://download.microsoft.com/download/3856f445-db3d-4e15-ac03-622bfd453e12/MUTTPackage-3_0_2.msi
extracted locally, not installed. MSI SHA7741618eeb97572b3a83965799738b2934b5d474782ba2670ccd07c678c022de;
x64 UCSIControl SHA22ac51ef4503e4ff3344b7c33ffb7509f43d26f5f4942a2c382dd91f4fb7b9a7.
Both copied to remote auditTEMP only. EXE Authenticode NotSigned; MSI has signer
Microsoft Windows but target reports untrusted root, not valid chain. Guarded
help invocation exited BEFORE executable launch. No trust-store modifications.
Evidence mutt-signature.json, ucsi-tool-help.txt. Investigate provenance/trust
if using tool later; don't silently call it signature-validated.
Sources https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/ucsi
and https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/mutt-software-package .

### Existing bertbox powerz-logger verified with live KM003C (2026-11-18 07:44)

User identified meter KM003C and directed inspection of
/home/ar/src/github/andrewrabert/tools. Read its AGENTS.md; no .codegraph there.
src/tools/powerz_logger.rs reads labeled Linux powerz hwmon sensors; outputs
timestamped JSONL VBUS/IBUS in milli-units, human power=V*abs(I). Does NOT
capture PD packets, RDOs, advertised PDOs or negotiated current limit. Whole
second polling; unchanged repository. Existing release bertbox used successfully.

Meter USB5fc9:0063 attached LOCAL Linux host via5-1:1.0 with powerz driver.
Twelve-second initial log powerz-initial-20261118.jsonl:19.850–19.889V,
2.531–3.236A,50.33–64.23W. Charger identity and inline wiring confirmation
asked asynchronously, still pending. Do not label these as confirmed third-party.
Concurrent target PDST captured powerz-initial-pd-status.json.

Started bounded900-second local recording (1s cadence) to
windows_power_policy/powerz-transition-20261118-0745.jsonl and .txt. It can
survive target Windows reboot because logging runs on separate Linux host.
No target reboot/reset or charger change initiated. No firmware/driver changes.
A same-voltage current drop does not distinguish lower negotiated current from
local charging policy; raw PD capture still needed for that distinction.

### Logged third-party low state after reported boot (2026-11-18 07:52)

Local powerz recording remains running (bounded900s from~07:44:51). User
reported booting; SSH accessible and DSTS again0001002A three times. Meter
reads~19.324V,1.524–1.530A (~29.45–29.56W), giving direct quantitative low-state
evidence. Low current plateau was already present in07:47–07:49 samples before
latest booting message; do NOT align message time with actual firmware/OS handoff
or claim entire drop occurred at that message. Need controlled timing/boot stamp.
Full log powerz-transition-20261118-0745.jsonl, event markers in matching
-events.jsonl, thirdparty-logged-boot-pd-status.json and -battery.txt.
Next physical action: reconnect ONLY third-party while local HID stays connected
to capture recovery in same log. Same~20V with reduced current still does not
prove whether negotiated current or local current limit changed.

### Logged reconnect did NOT restore high power (2026-11-18 07:55)

User reports requested reconnect done. Same local JSONL shows VBUS falls toward
zero07:53:59–07:54:07, brief37.03W at07:54:08, then~29.45W plateau. First48
post-attach samples07:54:10–07:54:57:19.324–19.326V,1.506–1.535A,
median29.4513W. Before median29.4691W. Summary logged-replug-summary.json.
Three new target DSTS reads0001002A. Battery61%,Charging=true,ChargeRate9514,
RemainingCapacity26951; files thirdparty-logged-replug-{pd-status.json,battery.txt}.
Reported Windows LastBoot still1795002647500; hybrid shutdown could explain
unchanged timestamp, don't independently assert fresh kernel boot.

This contradicts reliability of earlier successful replug workaround; retain
earlier observation but do NOT generalize. Battery level now61% vs prior32–34%,
workload/charger/cable/port and disconnect end require matching. Asked user
which end unplugged and whether same charger/cable/port as high-power trial.
No decoded PD contract; measured~19.3V/1.5A alone cannot prove1.5A RDO.
Logger remains bounded900s running until~07:59:51 unless earlier stop/failure.
No new reset/device/driver/firmware operations performed.

### Higher input restored in logged laptop-end test (2026-11-18 07:58)

User clarified failed07:54 reconnect removed cable at brick AND brick AC power.
Then instructed leave brick powered and reconnect laptop end only; reports done.
Log contains TWO subsequent disconnect intervals07:56:22–54 and07:57:32–41.
First recovery peaks74.2W and settles~50–56W; second reaches~70W and settles
~49W by07:58. Both exceed prior sustained29.45W. Exact mapping of both physical
actions/duration is not independently established. Do not claim disconnect end
is sole cause: first interruption~32s, second~9s, earlier~8s.

New DSTS remains0001002A allthree; battery63%,Charging=true,ChargeRate8287.
Battery sample not synchronized to meter peaks; do not equate USB input to
battery charge power. Artifacts laptop-end-replug-{pd-status.json,battery.txt,
summary.json}. Same full powerz transition log remains active until~07:59:51.
This captures real recovery at roughly same bus voltage with increased current;
RDO/current contract versus local input policy still unresolved. Firmware and
Windows driver/service settings unchanged. No need for further manual replug
loops before comparing actual PD messages or controlled software reset.

### Controlled UCM-UCSI device restart did NOT reproduce drop (2026-11-18 08:02)

User explicitly authorized one device restart with meter logging. Exact-model
and device/class/status guards passed. pnputil /restart-device ACPI\USBC000\0
succeeded exit0 at08:02:10.630–10.734; deviceOK afterward. No OS restart,
service disables, registry/test-interface changes, direct UCSI command or flash
write. This is a PnP device restart, NOT verified execution of PPM_RESET.
Powerz log: before 22 samples median43.17W; after
78 samples median42.20W, range40.92–76.12W.
No~30W plateau reproduced. EC status0001002A three times. Battery68% at
preflight, remained charging. Artifacts windows_power_policy/ucsi-restart-
{result.json,powerz.jsonl,powerz.txt,pd-status.json,summary.json}, script
restart-ucsi-once.ps1. Local bounded logger deliberately stopped after capture;
no logger or diagnostic intentionally left running.

This weakens the simple 'any UCSI initialization causes cap' hypothesis; does
not rule out boot-only UCSI behavior, ordering/race, ASUS/ACPI OS transition or
charger-specific behavior. No basis for an automated UCSI restart workaround.
Next investigate actual boot command sequence/PD contract or EC OS-mode path.

### Latest offline lead: charger bypass sequence (2026-11-18)

Read latest FINDINGS. Patched436FA starts enable81241/step81242, not direct
current programming. Separate charger sequence strongly matches ISL9241 NVDC
->bypass (primary Renesas app note archived), silicon not live-identified.
Step13 at47AE6 polls charger3Abit3; timeout latches81243=1 and clears81241,
blocking completion/current writes. Source reselection43636/4363A clears error.
Retry budget depends80410 (1 when5, otherwise6), numeric state meaning unknown.
Completion43334 writes81206 to charger3F,107% to47. PDST2A does not establish
sequence completion. Plausible low-power/replug mechanism, NOT proven live.
Artifacts out/update_415/boot_policy_analysis with disassembly, hashes and
non-executable diagnostic-targets.json. Next: validated Windows-safe snapshot
of81200/81240 state and charger status/limits, plus actual PD contract. PMC92
RAM window stops at offset1000;1240 maps MMIO, NOT81240. No new hardware action
this analysis; no logger/diagnostic armed, firmware unchanged. Do not force mode
or bypass charger status checks. UCSI restart earlier did not reproduce drop.

### Windows snapshot transport investigation (2026-11-18 08:12+)

Snapshot not yet acquired. Existing PawnIO module cannot reach25E/25F.
Decoded stock AsusSAIO IOCTL80102070:16-byte OEM transaction under driver's
private kernel mutex; FF/command/payload through25C/25D. DSDT RRAM usesFF/CC
for existing0010/0030 reads viaSMM, but arbitrary RAM mapping NOT established.
Next resolve exactFF/CC handler before invoking anything; live driver hash must
match saved analysis. Artifacts boot_policy_analysis/windows-read-route-audit.json
and excerpts. No raw I/O/IOCTL executed, no driver/module/service changes.
Fresh Windows inventory AC=true,Charging=true; not input-power measurement.

## Latest: passive KM003C capture blocked on local USB access
Read FINDINGS end. OEM CC is allowlisted table, NOT arbitrary RAM; none of255
entries expose81200..8125F. Evidence boot_policy_analysis/oem-cc-table.json.
Prepared tools/km003c_pd_capture.py from km003c-rs commitc063388e; raw passive
capture only, syntax/help checked, NOT hardware validated. No USB reset or active
PD triggering; only interface0 detached temporarily/restored. Default upstream
library resets USB/detaches all interfaces, avoid defaults.
Local meter5fc9:0063 node/dev/bus/usb/005/002 denies access (Errno13), sudo requires
password. Need user run LOCAL `sudo setfacl -m u:ar:rw /dev/bus/usb/005/002`.
Then bounded30s test before asking replug; decode actual PD packets to distinguish
negotiated contract versus local current limiting. No meter/laptop state changed.

### Latest live capture (2026-11-18 08:35+)
ACL granted.30s passive monitor succeeded:1456status packets,zeroPDevents;
median18.69W is measured input, NOT contract/powerlimit. Disable accepted,
powerz kernel driver/hwmon restored. Disconnect send-only handling corrected.
Started900s pd-reconnect-capture.jsonl; waiting for user laptop-end unplug10s
and reconnect third-party while meter debugUSB stays attached. No reboot needed.
Read FINDINGS latest; collect/decode this capture before more tests. No firmware
changes. Low input may be battery taper (batteryChargeRate10.678W).

Latest correction:900s capture finished cleanly,43543status samples but zeroPD
events. User reports laptop off; meter currently ABSENT from local lsusb.
No capture running. Need reconnect meter debugUSB to Linux, recheck node/ACL,
arm capture before asking user to boot. See FINDINGS latest.

Latest: user renewed USB ACL on003/004; coldboot passive capture now active,
session34175,900s, file boot_policy_analysis/pd-coldboot-thirdparty.jsonl.
Monitor accepted and status packets received. User to boot laptop normally with
third-party power and meter debugUSB connected. Collect this before next test.

## Latest decisive result: third-party 100W PD contract verified
Read FINDINGS end. Laptop-end reconnect captured42events: initially20V0.75A,
then updatedPDO4=000641F4(20V5A),laptopRDO4387D1F4(operating/max5A),source
Accept thenPS_RDY. Evidence boot_policy_analysis/pd-reconnect-contract.json and
pd-coldboot-thirdparty.jsonl. This is100W CONTRACT, not measured100W consumption
or proven boot fix. Userbattery96%, so low input is confounded by charging demand.
Need lowerSOC/comparableload before judging actual limit; no new patch justified.
Capture stopped cleanly16:42:58Z,powerz restored3-1:1.0,nothing capturing now.

Latest active capture13:29EST:pd-50pct-attach.jsonl,session96202,900s,monitor
accepted. IMPORTANT fresh WindowsSOC10%,not earlier reported50%; filename stale
label only. ACfalse. User to connect third-party power now; no further discharge.
See FINDINGS end and pd-50pct-before.txt. Capture needs collection/cleanup.

Latest lowSOC result:third-party negotiated20V5A accepted+PS_RDY,but54sinput
median29.36W at10%battery withbenchmarkstopped. Brief~69Wspikes,not hard30W
ceiling. Batterycharge3.5Wthen9.9W; batterytempunavailable. Nextuseractionstock
ASUSswap throughmeter,keepWindowsidle/debugUSBconnected,forpairedcomparison.
Capture96202stillactive(until18:44:12Z). Files pd-lowbattery-*; seeFINDINGS.

Latest pairedtest: ASUSsame20V5A/RDO4387D1F4 contract but~70.13Wmedian input,
vs thirdparty29.35W. ASUSbatterycharge rose to46.618W. Needreverse swap back
to thirdparty underongoingcapture96202 to separateadaptereffect fromstate/time.
No firmware changes; seeFINDINGS andpd-lowbattery-adapter-comparison.json.

Latest A/B/A result:thirdparty->ASUS->thirdparty input~29/70/29W; each100W
PDcontract. Reversebattery13%,charge6-9WvsASUS47W. Model/cable/sharedloads
questionpending. Need distinguishECpolicy/state fromsource/cableinputbehavior.
Voltagedifference19.246Vvs19.846V isnot proofcabledrop. Capturestopped aftertest;
seeFINDINGS end,fullrawpd-50pct-attach.jsonl andpd-lowbattery-thirdparty-return*.

Latest offline trace:post-contract-dataflow-audit.json. EC does have15->100W
refresh/reselection path, so no provenmissingupdatebug. Cachedlimit81320+2*port
is0.95*PDcurrent(<4921)else4674,copied81206,applied3F onsequencecompletion.
Latchedtransitionfailure remainsunconfirmed. No newfirmwarepatchjustified.
Fresh17%batteryACtrue. Passivecapturepd-lowbattery-bios-windows.jsonl started
900s; usertoRestartintoBIOS,hold30sandreportbeforeexitingtoWindows. No remote
reboot/ECwrites. ReadFINDINGSend; capturemayexpirebetweenmessages.

CORRECTION: BIOScapturefailed atConnect,notactive. Threepreservedlogs show
staleDisconnect/laggedAcceptIDs; noMonitorEnable sent. powerzrestored. Need
loggingUSBreconnect beforetryagain; noreset/flash/laptopreboot performed.
ReadFINDINGSend; previousstarted/acceptedentry superseded.

Latest: freshUSB003/006 +ACL, BIOScomparisoncapturev4nowACTIVE,session79128,
900s. Filepd-lowbattery-bios-windows-v4.jsonl; accepted+samplesverified. Userto
RestartintoBIOS,wait30sandreportbeforeWindows. No firmwarewrite/remotereboot.

Latest: v4expiredcleanlybeforeuserrestart. Newcapturepd-bios-windows-v5.jsonl
ACTIVE900s,session3852,accepted/samplesverified. Battery48%,ACchargingtrue.
UsertoRestartintoBIOS,wait30sandreport. Checkcapturefreshnessinlaterturn.

Latest: userinBIOSafterwarmrestart,Ankerleftattached. Capturev5active;~29.4W
persists,noPDevents. NeedfreshpowerreattachinBIOS (laptop-end10sdisconnect),
keepdebugUSB; donotexitWindowsyet. SeeFINDINGS andbios-retained-connection-summary.

Latest: freshAnkerreattach INSIDEBIOS negotiated100W but input29.44Wmedian.
CurrentlowstateexistsbeforeWindows,notsolelyWindowsUCSIstartup. Warmrestart
leavespersistentECstatepossible. NextuserexitBIOSwithoutchanges,bootWindows,
keepallcables. Capturev5active; seebios-fresh-attach-summary.json/FINDINGSend.

Latest: userwall-cycledAnkerinBIOS;100Wrenegotiated,steady~29.46W,no recovery.
NextexitBIOSwithoutchangesandbootWindows;v5captureactiveuntil~20:31:56UTC.
Evidencebios-wall-reset-summary.json; seeFINDINGS. No newfirmwareaction.

Latest: Windowsdesktop afterBIOS; LastBoot20:26:35.5Z confirmsnewstartup.
NoPDmessagesafter20:24:33Z;~29.5Wthroughstartup,thenlowerwithdemand.
36%battery,charging6.582W,DSTS0001002A. LowstateprecededWindows. Capturev5
stoppedcleanly,powerzrestored,nothingarmed. NeedvalidatedEC/chargerstatesnapshot
beforefurtherpatch. SeeFINDINGSend,bios-to-windows-comparison/status artifacts.

### Full-startup comparison prepared (22:10UTC)
Live inventory confirms HiberbootEnabled1, UCSIhealthy with0Operational
events, ASUSservicesrunning. Saved windows-resume-inventory.json.
Temporarily setHiberbootEnabled0, verified. Original1 backedup remote
C:\Users\user\AppData\Local\Temp\flowx13-ec-audit\fast-startup-before-test.json
andlocal fast-startup-test-setting.json. Restoreoriginal1 aftertest unless
user requestskeepingdisabled. No service/driverdisabled; no rebootissued.
fwbootmgr hasno bootsequence. Capturev3stoppedcleanly22:10:33UTC.
Newpd-full-boot-v1.jsonl active900s,session5676, monitoraccepted.
Nextphysicalsteps: user WindowsRestart intoBIOS,ASUS60s,thenAnker60s
toestablishhigherinputagain; verifyhighbeforeexitBIOStoWindows.
NeedfreshKernel-Boot event/LastBoot validation afterexit. This isolates
FastStartupresume fromfreshWindowsstart; it doesnot assumeitisthecause.

### Full-boot experiment: high initial state NOT reproduced
User switchedASUS->Anker whileBIOS~22:13:20UTC. Brief66.7Wpeak,
then29.45W sustained through22:14+. PD100W completed:PDO4
000641F4,RDO4387D1F4,Accept+PS_RDY22:13:21Z. Artifact
full-boot-anker-bios-baseline.json. UnlikepreviousASUS->Anker
BIOSreturn, thisattempt stayedlow. Do notclaimASUSalwaysprimes
highpower, norproceedasifhighstateexists forWindowscomparison.
FastStartup0 remains temporary; original1 saved. Nextonecontrolled
Ankerpowerreattach10s inBIOS, loggerUSBunchanged, tocheckattach
variability. Capturepd-full-boot-v1 activeuntil~22:25:38UTC.

### Full Windows startup confirmed; low Anker state persists
User reportsWindows. Kernel-Boot27type0x0, newLastBoot1795040352500
(22:19:12.5UTC), FastStartup0 atboot; priorresume type0x1. Battery53%.
Input~29.5W inBIOS and~29.3W afterWindows, zeroPD events after
22:17:40 baseline. Full-boot-comparison.json andstatus.json saved.
ImmediateWMIcharge27365mW later1386mW; do notuseinitialstale/settling
batterytelemetry assteadyinputmeasurement. Meter is inputevidence.
This fullstartup didnotclearLOWstate; doesNOTtestwhetherfullstartup
woulddropaHIGHstate (highbaselinefailedtoreproduce).
Restoredoriginal HiberbootEnabled1 verified; backupretained. No other
service/driverchanges. Stoppedcapturecleanly, cleanupcomplete, no
logger orfirmwarediagnosticarmed. No userphysicalactionneedednow.
Returntotechnicalanalysis/liveECread-routework; repeatedcharger
swaps have not established deterministichighstate preparation.
