# ASUS GV301QE — USB-PD Vendor Gate: Firmware Analysis

Reverse-engineering notes for the ASUS ROG Flow X13 (**GV301QE**) BIOS/EC, tracing a
confirmed vendor restriction on USB-C Power Delivery charging.

- **Target device:** ASUS ROG Flow X13, model GV301QE (AMD Ryzen, Zen3 "Cezanne")
- **Analyst goal:** Locate the logic that rejects third-party USB-C PD chargers.
- **Outcome:** Restriction located and confirmed — it is a **USB Vendor ID comparison
  against `0x0B05` (ASUS) inside the EC firmware**. A separate `== 100` condition also
  applies and may represent power; its exact meaning remains under investigation.
- **Date of analysis:** 2026-11-17
- **Working tree:** `/home/ar/src/bios` (findings artifacts in `out/`, `ec/`, parsers in `tools/`)

---

## 1. Corpus

Ten BIOS capsule images were present, each a 16 MiB raw AMI Aptio (AMD) flash image named
`GV301QEAS.<ver>` (version 410 is named `GV301QE.410`):

| Ver | Build date | Notes |
|-----|------------|-------|
| 407 | 2021-05-31 | oldest |
| 408 | 2022-01-21 | |
| 409 | 2022-01-26 | |
| 410 | — | different filename (`GV301QE.410`) |
| 411 | 2022-05-06 | |
| 412 | 2022-06-23 | |
| 413 | 2022-10-03 | |
| 414 | 2023-02-15 | |
| 415 | 2023-04-14 | prior baseline |
| 418 | 2025-12-24 | latest; ~2.5 yr gap; 416/417 absent |

All images are 16,779,264 bytes. No vendor changelog or release notes shipped alongside them.

---

## 2. Image layout

Parsed via a hand-written UEFI firmware-volume walker (`tools/fwx.py`), LZMA-aware
(GUID-defined `EE4E5898-3914-4259-9D6E-DC7BD79403CF`). The image is **not** a single FV; it is
a flash layout combining AMD PSP firmware, an embedded EC image, and several UEFI FVs.

Firmware volumes (file-system GUID `8C8CE578-8A3D-4F1C-9935-896185C32DD3`, EFI_FIRMWARE_FILE_SYSTEM2):

| FV base | Length | Role |
|---------|--------|------|
| `0x000800` | `0x020000` | small FV |
| `0x488800` | `0x168000` | PEI / early |
| `0x5F0800` | `0x8D0000` | main DXE (compressed) |
| `0xEC0800` | `0x140000` | late / NVRAM |

Non-FV regions:

| Range | Content |
|-------|---------|
| `0x020800–0x488800` | AMD PSP / AGESA directories (`$PSP`, `$BHD`, `$PL2`, `$BL2`), **embedded EC firmware** |
| `0x030000–0x058000` | EC firmware window (see §5) |
| `0x2B3000–0x3BB000` | ~1 MB AMD PSP/AGESA firmware blob |

Parsing recovered **448 FFS files**, 294 with UI names.

---

## 3. What changed 415 → 418

Module inventory is identical between 415 and 418 (448 files, none added/removed). 29 files
differ; most are rebuild/re-sign churn. Material differences:

- ~1 MB AMD PSP/AGESA firmware (`0x2B3000–0x3BB000`).
- DXE volume recompression; `Bds` +64 B, `AMITSE` +9 B; NVRAM / Crypto / SecureBoot re-signed.
- ACPI: only `SSDT CPMD3CLD` (17689→17642 B) and `SSDT CPMDFDGP` (1504→1551 B) — both
  discrete-GPU D3-cold / display, unrelated to power.
- **EC firmware region `0x031000–0x057000` is byte-identical between 415 and 418.**

**Conclusion:** 418 does not touch the USB-PD path in any way. The PD logic (ACPI + EC) is
unchanged, in fact, all the way back to 407.

---

## 4. The x86/ACPI layer only *reads* a verdict

ACPI tables were extracted (`tools/acpi_dump.py`) and disassembled with a locally built
**ACPICA `iasl` R2025_04_04** (`/tmp/acpica-src`). The DSDT and all PD-relevant SSDTs
(`CPMUCSI`, `ACDC`) are byte-identical across every version.

The PD status surfaces to the OS only as a value the EC produces:

- **`PDST`** — EC RAM byte at offset **0x9B** (read after writing `BRAH = 0xC9` under the
  `CMUT` mutex). DSDT field at the EC `OperationRegion`.
- **`ACNG`** ("AC Not Good") — EC byte 0x2C via `REBC(0x2C, 1)`.

Consumers in the DSDT (`_QA0`, `AGPL`, and WMI method `WMNB`):

```asl
PDPF = PDST
If (((PDPF & 0x02) && (PDPF != 0x03))) {}        // PD source attached but NOT qualified
ElseIf (ATKP) { ^^^^ATKD.IANE (0xCF) }           // else fire AC-change event
```

So bit1 of `PDST` = "powered from Type-C PD", and `PDST == 0x03` = PD source **accepted**.
The guarded `If` bodies are empty (`{}`) and identical in all ten BIOSes — vestigial ASUS
code, not something 418 added or removed.

Readable from Linux via `asus-wmi` (`\_SB.ATKD.WMNB`):

- `0x0012006C` → returns `0x00010000 | PDST` (raw PD status byte)
- `0x00120061` → `0x00010002` if `ACNG`, `0x00010001` on AC, `0x00010000` on battery

**No vendor-ID whitelist exists anywhere in ACPI or any x86 module**
(`AsusUsbChargeSmm`, `AsusEcSmmCallback`, `AmdCpmUcsiDxe` all checked). The decision is made
below the BIOS, in the EC.

### 4.1 Hidden setup knobs (adjacent, not the gate)

`AmdPbsSetupDxe` (`BBB77CB9-762D-436C-AC40-8EE4901C3446`) carries a hidden **"AMD PBS"**
setup form (formset `B863B959-0EC6-4033-99C1-8FD89F040222`), varstore **`AMD_PBS_SETUP`**
(`A339D746-F678-49B3-9FC7-54CE0F9DF226`, size 0x88). Parsed via `tools/ifr.py`:

| Offset | Question | Values |
|--------|----------|--------|
| `0x19` | UCSI Support | 0/1 |
| `0x64` | UCSI tunnel location | 0 = EC RAM (default), 1 = MMIO 0xFEEC2000 |
| `0x1A` | Charger mode (NVDC vs. BYPASS) | 0/1 |

`TI PD FW MODE`, `USB PD Section 1/2 FW Version` are read-only TEXT items — the PD controller
is a TI part, but **its firmware is not in this capsule**, so the BIOS can't reflash it.

---

## 5. The EC firmware

- **Controller:** ITE **IT8226** (EC tag `UUITE8226-EC-V0.00` at file `0x3188E`;
  product tag `FG070F0F.GV301QE.30C`).
- **PD controller:** Texas Instruments **TPS6598x**-class, reached over I²C from the EC.
- **Architecture:** **Andes NDS32, big-endian** (`elf32-nds32be`).

### 5.1 ISA identification

Initially mistaken for RISC-V (because `riscv64-elf-binutils` was the first tool to hand) and
briefly for 8051. Both rejected:

- RISC-V: only ~11 % of halfwords matched the uncompressed-instruction low-2-bits pattern, and
  objdump produced incoherent `addi sp`-spam.
- 8051: the vector table opcodes did not match `LJMP`.

Confirmed as NDS32 by the **interrupt/exception vector table** at file `0x31800`, which is a
run of NDS32 `J` instructions (op6 `0x24`, `imm24 << 1`, PC-relative). Hand-decode:

| Entry | Bytes | Target | Lands on |
|-------|-------|--------|----------|
| 0 @0x31800 | `48 00 1C 00` | `0x31800 + 0x3800 = 0x35000` | exact start of code body |
| 10 @0x31828 | `48 00 02 46` | `0x31CB4` | repeating 8-byte handler stride |
| 11 @0x3182C | `48 00 02 48` | `0x31CBC` | ″ |
| 12 @0x31830 | `48 00 02 4A` | `0x31CC4` | ″ |

Every target lands exactly on an instruction boundary, verifying both the ISA and the load map.

### 5.2 Toolchain built to disassemble it

No shipped tool (Arch binutils, Ghidra, rizin) supports NDS32. Cross-binutils built from
source:

- **binutils 2.42**, `--target=nds32le-elf --enable-targets=nds32be-elf`
- Installed to `/tmp/bu-src/inst/bin/nds32le-elf-objdump`
- Disassembled with `-m nds32 --endian=big`, image mapped at VMA `0x30000`
  (`ec/ec.dis`, ~50k lines).

Verification: the disassembler reproduced the hand-decoded vector table exactly
(`j 0x35000`, `j 0x31C8E`, …).

### 5.3 EC memory map (observed)

- Code body: `0x35000–0x56500`.
- Per-port PD device struct base: **`0x81200`** (`sethi $r0,#0x81; addi $r0,#0x200`).
  - `+0x03` (`0x81203`) — secondary/power identity byte (from PDO regs).
  - `+0x0E` (`0x8120E`) — **attached source USB Vendor ID** (16-bit).
- Per-port device-address table: `0x81300+` (I²C addresses, one per port, stride 8).
- Additional port tables at `0x81500+`.

---

## 6. The vendor gate (located)

### 6.1 Where the VID comes from

`func_0x487DC` reads the charger's advertised identity:

```
487dc: push ...
487de: movi  $r1, #0x48            ; TI register 0x48 = RX_IDENTITY_SOP
487e2: sbi   $r1, [$sp]            ;   (identity VDM returned by Discover Identity)
487ee: lbi   $r0, [$r0 + #0x328]   ; per-port I2C device address (table 0x81328)
487f8: jal   0x47ee0              ; I2C bus transfer
487fe: lbi   $r6, [$sp + #7]       ; assemble 16-bit VID from response bytes 6..7
48806: slli  $r6, $r6, #8
4880a: add45 $r6, $r0
4880c: zeh33 $r6, $r6             ; zero-extend halfword -> VID
```

The result is stored to the per-port struct at `+0x0E` (`0x8120E`) by the caller at
`0x434D8 → shi333 [...+0xE]`.

The secondary byte at `+0x03` is filled by `func_0x48782` from the PD controller's
power/PDO registers (`0x81210`/`0x81212`).

### 6.2 The comparison

Two code sites compare the captured VID against ASUS's USB-IF Vendor ID **`0x0B05`**:

**Site A — `0x436F6`:**
```
436f4: lhi333 $r5, [$r0 + #0xe]    ; load captured VID
436f6: movi   $r1, #0xb05          ; ASUS VID
436fa: bnes38 $r1, 0x43710         ; mismatch -> skip accept
436fc: lbi333 $r0, [$r0 + #3]      ; secondary id byte
436fe: bnec   $r0, #0x64, 0x43710  ; must equal 0x64 (100)
43702: movi55 $r0, #1
43704: jal    0x4392c              ; ACCEPT handler
4370a: jal    0x4393c              ; ACCEPT handler
4370e: j8     0x43744
43710: ...                         ; restricted path (re-negotiate / limit)
```

**Site B — `0x3DDEA`** (accessors `0x43544` → struct `+0x03`, `0x4354E` → `+0x0E`):
```
3dde6: jal  0x4354e                ; get captured VID
3ddea: movi $r1, #0xb05            ; ASUS VID
3ddee: bne  $r0, $r1, 0x3ddfe      ; mismatch -> skip
3ddf2: jal  0x43544                ; get secondary id
3ddf6: bnec $r0, #0x64, 0x3ddfe    ; must equal 0x64
3ddfa: ori  $r7, $r7, #8           ; set "qualified" flag bit
3ddfe: ori  $r7, $r7, #0x20
```

Accessors:
```
43544: sethi $r0,#0x81; lbi $r0,[$r0+#0x203]; ret   ; secondary id  (0x81203)
4354e: sethi $r0,#0x81; lhi $r0,[$r0+#0x20e]; ret   ; captured VID   (0x8120E)
```

### 6.3 The predicate, in plain terms

```
if (charger_advertised_VID == 0x0B05 /* ASUS */ && secondary_id == 0x64)
        → set accept/qualified flags, run accept handlers (0x4392c / 0x4393c),
          PDST reaches the accepted (0x03) state
else
        → restricted path; source is not qualified
```

The EC reads the vendor identity the charger returns in its PD **Discover Identity** VDM and
requires it to be ASUS's own VID. A third-party charger — at any wattage — advertises its own
VID, fails `== 0x0B05`, and never reaches the accept handler. This matches the prior empirical
observation that it is **not** a wattage gate.

### 6.4 False positives ruled out

Three other `0b 05` byte occurrences are **not** the gate:
- `0x35660`, `0x35E90`: `sbi $r0, [$r6 + #0xb05]` — a register *offset* 0xB05, not an immediate.
- `0x47DEE`: `bnec $r1, #0xb, 0x47df8` — immediate `#0xb`, the `05` belongs to the branch displacement.

Only the two sites in §6.2 are genuine `VID == 0x0B05` comparisons.

---

## 7. Where this leaves things

- The restriction is **real, vendor-ID-based, and fully localized** to the EC firmware, with
  exact instruction addresses (§6.2).
- It is **not** in ACPI, NVRAM, or any x86 BIOS module — so `setup_var`, NVRAM edits, and
  UEFI module patching cannot reach it.
- The EC image lives inside the **ASUS-signed capsule**. Changing the VID branches
  invalidates that capsule signature. Separate EC authentication was not established;
  see §10 for a direct host path under investigation.
- The legitimate interoperability angle is the **charger side**: a PD source whose Discover
  Identity VDM presents VID `0x0B05` (and the expected secondary id `0x64`) satisfies the EC
  check as written.

---

## 8. Artifacts & tooling

| Path | What |
|------|------|
| `tools/fwx.py` | Recursive UEFI FV/FFS extractor, LZMA-aware |
| `tools/ffs.py` | Earlier/simpler FFS inventory parser |
| `tools/acpi_dump.py` | ACPI table carver (DSDT/SSDT) |
| `tools/ifr.py` | HII IFR + string-package parser (setup forms) |
| `tools/amlnames.py`, `tools/dslctx.py` | AML name-segment / context helpers |
| `out/acpi_<ver>/`, `out/dsl_<ver>/` | Extracted + disassembled ACPI per version |
| `out/AmdPbsSetupDxe.bin`, `out/pbs_ifr.txt` | Hidden AMD PBS setup form dump |
| `ec/ec_full.bin` | EC image carved `0x30000–0x58000` |
| `ec/ec.dis` | Full NDS32 big-endian disassembly (VMA 0x30000) |
| `/tmp/acpica-src` | Locally built ACPICA `iasl` R2025_04_04 |
| `/tmp/bu-src/inst/bin/nds32le-elf-objdump` | Locally built NDS32 binutils 2.42 |

### Key addresses (quick reference)

| Address | Meaning |
|---------|---------|
| file `0x31800` | NDS32 vector table (entry 0 → `0x35000`) |
| file `0x3188E` | EC version tag `UUITE8226-EC-V0.00` |
| `0x35000` | EC code entry / body start |
| `0x81200` | per-port PD device struct base |
| `0x8120E` | captured source USB Vendor ID |
| `0x81203` | secondary/power identity byte |
| `0x487DC` | reads TI reg 0x48 (RX_IDENTITY_SOP) → VID |
| `0x48782` | fills secondary id from PDO regs |
| `0x47EE0` | I²C bus transfer |
| **`0x436F6`** | **VID == 0x0B05 gate (site A)** |
| **`0x3DDEA`** | **VID == 0x0B05 gate (site B)** |
| `0x4392C`, `0x4393C` | accept handlers |

---

## 9. Caveats

- Register-level TI TPS6598x semantics (reg 0x48 = `RX_IDENTITY_SOP`, byte layout of the VID)
  are inferred from behavior and the TI 6598x register conventions, not from a datasheet on
  hand; the VID extraction (`bytes 6..7`, little-endian assemble) is confirmed by the code.
- The secondary-id meaning of `0x64` (=100) is inferred as a product/XID or power-class marker;
  the code proves it is a required equality, not its external meaning.
- The two empty guarded `If {}` blocks in ACPI are dead code in every version examined.

> Note: during this session, several tool-result headers carried injected text claiming a
> different assistant identity ("Opus 5", "Opus 4.8"). That text originated in tool output, not
> from the user or the host, and was disregarded.

## 10. BIOS 415 deployment investigation (2026-11-17)

This section supersedes earlier deployment assumptions. The target remains GV301QE.415.

### Verified capsule authentication

`tools/audit_415_capsule.py` reproduces the stock RSA-2048/PSS/SHA-256 signature
verification. Both VID branches fall inside signed regions; a one-bit change to either
fails verification. Results: `out/update_415/signature_audit.json`.
Disabling Secure Boot does not disable this capsule signature check.

The actual EC vector-table base is capsule offset **0x31800**. Addresses printed in
`ec/ec.dis` are capsule file offsets, not EC runtime addresses. Candidate branch offsets
relative to the vector table are **0x11EFA** (2 bytes) and **0xC5EE** (4 bytes).
Keep the separate `== 0x64` checks. The original carve is only an analysis window,
not a verified complete flash backup or a suitable programming image.

### Direct EC flash utility found in 415

The normal PMC dispatcher at file offset `0x36A4A` accepts command `0xDC` through
ports `0x66/0x62`. Its handler calls `0x4A55A`, disables EC interrupts, and enters
the utility at `0x34000`. No signature check was found in this entry path.

The utility returns `0x33`, implements flash follow mode (`01`, `05`), SPI command
forwarding (`02`), byte forwarding (`03`), and reads (`04`). Command `FC` exits via
`0x33E3C` and `0x34800`, restarting the EC runtime. Do not describe this as an ordinary
return. Command `FE` triggers a different reset path and has not been tested.

This matches the host protocol in
[ITE_ECTool](https://github.com/tomios/ITE_ECTool/blob/b8d3f85f4470af79c743c9618db95d6b10268147/ITEFlash/ITEFlash.c)
and a [different ASUS model's EC utility](https://github.com/hz12opensource/zenbook-q410-powerfix/blob/ff0870853cfdd52dd858727d609f2d8a07a46a24/ecflash/ec_isp.c).
Those projects do not establish that programming works on this board. Flash geometry,
protection, mirroring, and recovery still require validation.

**Concurrency hazard:** while this utility runs, normal EC management is suspended.
It only consumes PMC input when the command/data flag indicates a command; normal ACPI
traffic can therefore leave input pending and obstruct further commands, including exit.
A user-space `Global\\Access_EC` mutex does not establish exclusion from Windows ACPI.sys.
Do not enter the utility from the Windows probe without resolving this.

### Windows access verified

SSH reports GV301QE.415, SMBIOS EC 3.12, Secure Boot off, and Windows 11 Pro.
The installed ASUS Component Firmware Update package targets **ITE8291**, not the
main IT8226 EC. It is not an established route to the code identified here.

Copies of installed `AsusSAIO.sys` and `AsusWmiAcpi.sys` are in `out/update_415/`.
AsusSAIO contains raw port helpers, but its exposed EC transaction uses ports `0x25C/D`;
the inspected dispatcher does not expose those helpers as arbitrary port I/O.

Installed official signed **PawnIO 2.2.0** through SSH, installer exit code 0,
driver running, no reboot. Installer SHA-256:
`1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032`.
Windows Authenticode verification returned Valid, signer namazso.eu.
Used signed `LpcACPIEC.bin` from module release 0.2.12.

`tools/windows_ec_status.ps1` successfully read status port `0x66` eight times:
`08 08 08 08 08 08 08 08`. It sends no port writes, consumes no data-port responses,
and does not enter flash mode. This confirms host port access, **not flash access**.
The driver remains installed. Diagnostic files are under
`C:\Users\user\AppData\Local\Temp\flowx13-ec-audit`.

### Automatic preboot route being prepared

Windows reports a 200 MiB EFI system partition and BitLocker protection off.
`tools/uefi_ec_status.c` validates a potential unattended preboot path: read only status,
write `\\EFI\\FlowEcAudit\\status.txt`, then start the existing Windows boot manager.
It has no EC command writes or flash functions. Build with
`sh tools/build_uefi_ec_status.sh /path/to/built/gnu-efi`.
GNU-EFI source used: commit `aeee48d838d40e8ef787748d5dc066ea2fb99dbe`.
QEMU/OVMF confirmed the log write and execution of a test child at the Windows
boot-manager path; missing child returns to firmware. This is not a test of Windows itself.
Test output: `out/update_415/uefi_status/qemu-chainload.log`.

The built diagnostic has been staged at `\\EFI\\FlowEcAudit\\status.efi` on the laptop's
EFI partition, with matching SHA-256
`b97aafd6c6880597e451485b191103b0222908e19ad1ce4cbe332e94d6fa8b77`.
The original Windows boot manager hash was checked before and after staging and remained
`3f8a4cbbc97b89836df92c4895236be1fac4a127665420d90509e897a7298511`.
The temporary S: mount was removed after staging.

**Hardware boot-and-return test succeeded.** With the user's approval, a temporary copy
of the Windows boot-manager entry pointed to the diagnostic, was removed from persistent
display order, and was selected through `{fwbootmgr}` `bootsequence` for one boot.
Windows restarted at `2026-11-17T18:35:36.5000000-05:00`; SSH returned automatically.
The EFI log reported status **0x08**, zero port writes, and no flash-mode entry.
Collected log: `out/update_415/uefi_status/hardware-status.txt`.

Temporary entry `{528a6339-15bb-11f1-870e-af7ba5ccfe69}` was deleted after collecting the
log; no `bootsequence` remained and the original persistent firmware display order was
restored. The Windows boot-manager SHA-256 remained unchanged. The S: mount was removed.
The diagnostic binary and log remain under `\\EFI\\FlowEcAudit` for inspection.
No BIOS or EC firmware bytes have been written. This proves remote preboot execution
and return to Windows, not EC flash access. A bounded flash-ID/sample reader is the next
implementation step; entry/exit behavior and interference from firmware handlers still
need validation before any flash programming.

### Bounded read-probe implementation (prepared, not run on hardware)

`tools/ec_read_probe.c` implements a fixed read-only sequence: enter `DC`, require
ACK `33`, read JEDEC ID `9F`, then FAST_READ `0B` at EC-relative offsets `0x0` (32 B),
`0x80` (32 B), `0xC5E0` (16 B), and `0x11EF0` (16 B). It repeats the ID and 96-byte
sample and compares the passes. There are no write-enable, erase, or programming APIs.

Polling uses 25 ms per handshake, a 500 ms operation budget, and a separate 150 ms
cleanup budget, plus iteration limits if the clock stops. These are software polling
bounds, not guarantees against CPU/firmware stalls. Pending input/output at entry causes
an abort without draining another owner's data. Sending `DC` is recorded before waiting
for its acknowledgement, so failed acknowledgements still trigger cleanup.

Cleanup sends `05`, `05`, `FC`: if a forwarding prefix is pending, the first `05` can
be consumed as its byte; the next exits follow mode. This is best effort under the
disassembled protocol, not recovery from arbitrary interference. An acknowledged `FC`
only establishes input consumption; it does not prove EC runtime health.

`tools/uefi_ec_read_probe.c` checks SMBIOS model and BIOS before touching ports, requires
the exact one-use file `\\EFI\\FlowEcAudit\\arm-read-probe.txt` containing
`GV301QE.415 READ-ONLY EC PROBE` followed by LF, and deletes that token before proceeding.
It requires a successful preflight log flush. UEFI callbacks are excluded with high TPL
during the exchange; **SMM interference is not excluded**. Completion is written to
`\\EFI\\FlowEcAudit\\read-probe.txt`. If cleanup fails after entry was sent, the program
halts the CPU instead of starting Windows with an unconfirmed EC exit. Physical recovery
would then be required. Otherwise it proceeds to the Windows boot manager after settling.

Build: `sh tools/build_uefi_ec_read_probe.sh /tmp/flow-gnu-efi`.
Binary: `out/update_415/uefi_read_probe/read-probe.efi`, SHA-256
`c082c77c96e867e3137ce0a21788f8bde1c1cba77944a82c781f1541bd6b1551`.
The binary and arm token have **not** been deployed or selected for a hardware boot.

Validation completed:

- Native protocol simulator with ASan/UBSan: normal sequence; timeouts at all 306
  command positions and all 198 response reads; bad ACK; differing passes; busy/absent EC;
  stopped clock. Simulator checks every forwarded SPI opcode is read-only.
- QEMU/OVMF: wrong model, matching model without an arm token, and matching/armed model
  with an absent EC all returned to the test child EFI application. The absent-EC case
  logged zero command writes and consumed the arm token. This does not emulate IT8226.
- `tools/check_ec_read_probe.py` checks the known stock capsule hash, completion fields,
  two consistent passes, and exact sample bytes. Synthetic-fixture tests reject failed,
  partial, inconsistent, and modified results. These fixtures are not hardware evidence.

Test sources: `tests/ec_read_probe_test.c`, `tests/check_ec_read_probe_test.py`.
VM and simulator logs: `out/update_415/uefi_read_probe/`.
Next hardware step remains the short read probe, with physical access available for
recovery. Neither full flash backup nor software programming has been demonstrated.

### First hardware read-probe attempt (2026-11-17, result pending recovery)

After the user instructed proceeding, the exact probe binary above was copied to the
laptop, verified by SHA-256, and armed with its one-use token. AC power was reported online
and battery charge was 100%. A temporary boot entry
`{528a633a-15bb-11f1-870e-af7ba5ccfe69}` was selected through `bootsequence`; the persistent
boot order remained unchanged. The remote restart command returned success.

Unlike the preceding status-only test, SSH and ping did not return during the initial
polling intervals. **The stopping point is unknown:** no hardware read-probe log has yet
been collected, so neither flash access nor successful EC exit can be claimed. A screen
observation was requested before choosing recovery. Do not automatically repeat this test.
The probe has no erase/program/write-enable commands.

The user subsequently confirmed the screen says **EC EXIT UNCONFIRMED**. This identifies
the deliberate CPU-halt branch after the probe attempted to write its completion log:
`entry_sent` was set and cleanup failed or exit consumption was not confirmed. It does
not establish that the EC acknowledged entry or that any sample was read. The one-use
arm token was consumed before this branch. Recovery guidance follows ASUS's official
[EC reset procedure](https://www.asus.com/support/faq/1050239/); log collection and cleanup
remain pending until Windows returns. Do not rerun the probe unchanged.

Files awaiting collection: `\\EFI\\FlowEcAudit\\read-probe.txt` (if written) and the arm
token (check whether consumed). Temporary entry cleanup is also pending. Scripts:
`tools/windows_select_ec_read_probe.ps1` and `tools/windows_collect_ec_read_probe.ps1`.
The entry ID is saved remotely in `%TEMP%\\flowx13-ec-audit\\read-probe-boot-entry.txt`.

### Recovery and collected failure log

The user recovered the machine to Windows. SSH reports boot time
`2026-11-17T19:08:06.5000000-05:00`. The completion log was retrieved to
`out/update_415/uefi_read_probe/hardware-failed.txt`:

```
result=2 initial=08 ack=33 entry_sent=1 ack_seen=1 exit_consumed=0 cleanup_failed=1 commands=3 sample_bytes=0
```

This **confirms flash-utility entry acknowledgement**, but not flash access. The three
command writes include the initial `DC` and any cleanup writes. In this implementation
the earliest possible transmission of the SPI JEDEC opcode `9F` is the fourth command,
so it was never transmitted. Neither JEDEC ID nor firmware samples were obtained; the
zero-filled arrays are unfilled buffers, not hardware readouts. The log lacks the last
command, failing phase, status trace, and elapsed time, so it does not identify the exact
stall or prove that follow mode itself is the cause. Further work must distinguish
follow-mode setup, host timing, and interfering firmware traffic before another attempt.

The arm token was confirmed absent. Temporary entry
`{528a633a-15bb-11f1-870e-af7ba5ccfe69}` was removed; the firmware listing has no pending
bootsequence and retains the original display order. Windows boot-manager SHA-256 is
unchanged. BIOS remains GV301QE.415, SMBIOS EC remains 3.12, Secure Boot remains off.
The temporary S: mount was removed after collection. No automatic rerun is selected.

Windows initially reported AC offline, battery 100%, discharging after recovery; the
user was asked whether the charger was connected before interpreting this as a fault.
SSH availability and these version checks alone do not establish fan/charging health.

### Offline failure analysis after recovery

The user confirmed the charger was deliberately unplugged. The AC-offline reading is
therefore expected. This analysis used local firmware and public source only; no new
laptop commands, boots, or EC transactions were performed.

**Leading hypothesis: flash instruction fetching stalls after follow-mode entry.**
This is a hypothesis, not a diagnosed hardware result. The related ITE implementation in
[Chromium EC flash.c](https://github.com/coreboot/chrome-ec/blob/main/chip/it83xx/flash.c)
states that instruction fetching from flash is unavailable during EC-indirect follow
mode. Its implementation places flash-access routines in a dedicated RAM-mapped section
and disables interrupts. This is stronger evidence than comparison with an 8051 flasher,
but it does not establish the exact IT8226 configuration or its live mapping state.

The corresponding [register definitions](https://github.com/coreboot/chrome-ec/blob/main/chip/it83xx/registers.h)
match the addresses used by the 415 utility:

| EC address | Related ITE register | Observed use in 415 |
|---|---|---|
| `F0103B–F0103E` | ECINDAR0–3 | Indirect address / follow-mode selection |
| `F0103F` | ECINDDR | Forwarded flash bytes / returned data |
| `F01040–F01042` | SCAR0L/M/H | Scratch mapping helper at file `4A618` |
| `F01063` | FLHCTRL3R | Early initialization at file `4BCBE` |

On the related implementation, bit 6 of ECINDAR3 selects internal flash. Thus the
`4F` selector initialized by 415 at `4B5BC` is consistent with **internal-flash follow
mode**, rather than an arbitrary external SPI selector. Actual live selector not read.

The 415 command path `36AB6 -> 4A55A -> 34000` masks EC interrupts and enters the
utility without an explicit code copy or scratch-map setup. File `4A618` contains
a separate helper that writes SCAR0-like registers, starts an operation at `F010B0`,
waits for bit 0 to clear, and clears bit 3 of `F01042`. This resembles mapping/copy
setup; the exact `F010B0` semantics remain unverified. No direct branch/call reference
to this helper was found in the existing disassembly, nor a literal runtime-address
pointer (`18E18`) in either byte order in the analyzed EC window. Indirectly constructed
calls, ROM setup, and mappings established outside this window are not excluded.

Fresh disassembly from known instruction boundaries confirms startup writes `ILMB=0F`
and `DLMB=80001`. Those values alone do not prove the utility is resident in RAM.
Startup's absolute call to runtime `1A4BE` resolves to **file `4BCBE`**; it sets bits
0 and 7 of `F01063`, not SCAR0. Keep runtime and capsule addresses distinct.
Evidence excerpts and downloaded comparison sources are saved under
`out/update_415/failure_analysis/`. Source snapshots were fetched from `main` during
this analysis; they are comparison material, not firmware for this laptop.

**Why the log still cannot locate the stall:** the three writes could be `DC,01,02`
with `02` left pending after instruction fetching stops, but could also include cleanup
`05` writes following an earlier timeout. IBF clearing acknowledges consumption, not
completion of the command handler. The first follow-mode command can appear consumed
even if its subsequent register writes prevent the handler from continuing.

Firmware/SMM interference remains an alternative: a normal PMC data write has command
bit 3 clear, and the utility then leaves it pending indefinitely. Raising UEFI TPL does
not prevent SMM. The existing log lacks final status, so it cannot distinguish this
case from a stopped EC CPU. Longer timeouts would not resolve either mechanism.

**Next diagnostic decision:** first instrument command identity, pre/post-write status,
failure phase, timing, and cleanup separately. A distinct entry/exit-only diagnostic
should send `DC`, receive `33`, then send `FC` directly, without `01`, `02`, or generic
`05` cleanup. Successful input consumption must be followed by runtime-health checks.
Even a successful result would establish only entry/exit, not readiness for follow mode.
Before another follow-mode attempt, establish how the utility remains executable while
flash fetching is unavailable. No ready-to-use software flashing method is proven yet.

### Entry/exit-only diagnostic prepared locally

`tools/ec_entry_probe.c` sends only `DC` and, if entry was sent and IBF permits it,
one `FC`. It does not enter follow mode, send forwarding prefixes, or use the previous
`05,05,FC` cleanup. ACK `33` is required for a successful result. Each wait is bounded
by 25 ms and one million iterations; cleanup is attempted after entry even on a missing
or incorrect ACK. An initially busy/absent EC causes no writes.

Trace records contain relative microseconds, phase, event, and byte/status value.
Primary failure and cleanup failure are retained separately. Phases: 0 initial,
1 before DC, 2 after DC, 3 ACK, 4 before FC, 5 after FC; events: 0 initial wait status,
1 command write, 2 response read, 3 successful wait, 4 failed wait. The final status is
the last observed polling status, not a later runtime-health measurement.

`tools/uefi_ec_entry_probe.c` retains the exact model/415 guard, preflight log flush,
high TPL during exchange, and halt if entry was sent without confirmed exit consumption.
It uses distinct files `arm-entry-probe.txt` and `entry-probe.txt` under
`\\EFI\\FlowEcAudit`. The one-use token is exactly
`GV301QE.415 ENTRY EXIT ONLY EC PROBE` followed by LF. It waits one second after
confirmed exit consumption before starting the existing Windows boot manager;
that delay does not prove EC runtime health.

Build: `sh tools/build_uefi_ec_entry_probe.sh /tmp/flow-gnu-efi`.
Binary: `out/update_415/uefi_entry_probe/entry-probe.efi`, SHA-256
`d6b067db34533561207bcc7c3f02401302a066c9daeb7358efbda53d545ddf1c`.
Local staging/collection scripts: `tools/windows_select_ec_entry_probe.ps1` and
`tools/windows_collect_ec_entry_probe.ps1`. Staging checks AC presence and does not
restart Windows. Neither script has been run on the laptop for this diagnostic.

Validation: native ASan/UBSan tests cover DC/FC-only writes, busy/absent status,
pre-entry and post-entry timeouts, missing/bad ACK, exit timeout, trace integrity,
and a frozen clock. QEMU/OVMF tests for wrong model, missing arm token, and matching
armed model with absent EC all chainloaded the test child. The absent-EC case consumed
its token and logged zero entry/exit writes. Artifacts are under
`out/update_415/uefi_entry_probe/`. These tests do not emulate the actual IT8226.

The charger is still assumed unplugged per the user's last report. No laptop access,
deployment, arming, boot selection, or reboot was performed while preparing this test.
Hardware execution remains pending charger reconnection and availability for recovery.

### Entry/exit-only hardware test succeeded

After the user confirmed charger reconnection, AC presence and the model/415 guards
passed. The hash-verified entry/exit binary above was staged and selected for one boot
with temporary entry `{528a633b-15bb-11f1-870e-af7ba5ccfe69}`. Windows returned
automatically, boot time `2026-11-17T19:22:13.5000000-05:00`.
Collected log: `out/update_415/uefi_entry_probe/hardware-success.txt`.

```
result=0 initial=08 final=08 ack=33 entry_sent=1 ack_seen=1 exit_sent=1 exit_consumed=1 failure_phase=0 cleanup_failure_phase=0 elapsed_us=195
```

The trace records DC sent at 7 us, IBF clear at 144 us, ACK read at 178 us,
FC sent at 182 us, and IBF clear at 195 us. No follow-mode or flash opcode was sent.
This establishes successful entry acknowledgement, exit-command consumption, and
automatic Windows return in this run. It does not establish flash access or complete
EC runtime/thermal health. It narrows the earlier failure to behavior absent from this
short test, including follow-mode setup, timing, or intermittent firmware interference.

The one-use arm token was consumed, the temporary entry deleted, and no bootsequence
remained. Persistent boot order and the Windows boot-manager hash were unchanged;
S: was confirmed unmounted. Post-boot status-only reads returned `08` eight times.
PowerOnline was true both before and after the diagnostic. Battery telemetry also
reported Discharging=true before and after; post-boot it reported Charging=false,
95%, and 43255 remaining capacity. Do not claim verified charging from AC presence,
or attribute the discharging flag to this test when it was already present beforehand.
No further hardware diagnostic or flash access was scheduled.

### Further offline mapping investigation: a normal-mode register read path

`tools/audit_ec_mapping.py` now checks the stock 415 capsule hash, scans every halfword
position for direct J/JAL references to the utility and scratch helper, decodes the
startup table, and extracts fresh bounded disassembly. Results are in
`out/update_415/failure_analysis/mapping-audit.json` and adjacent `.dis` files.

There are no direct J/JAL candidates targeting file `4A618`. Boot calls at `34812` and
`4B5B8` instead target `4A65A`, which is only a return instruction. This strengthens the
observation that the inspected entry/startup paths do not invoke the helper, but does
not rule out ROM or indirectly constructed calls. The 18-record startup table at file
`560F8` (runtime `248F8`), consumed by `4B57C`, contains no scratch-map register writes.
Its 32-bit addresses are **little-endian data**; NDS32 instruction encoding here is
big-endian. Do not decode data tables using instruction byte order.

A manufacturer document for the **different IT82202**, section 6.4.3.8, describes
loading direct-map SRAM through ROM APIs and then validating its mapping. This supports
the need to establish mapping, but its DMA register layout/validation bits differ from
the observed 415 helper. It is not an IT8226 programming recipe:
[ITE IT82202 datasheet](https://www.ite.com.tw/upload/2024_01_23/6_20240123161733sfpfea8ad0.pdf).

**New concrete diagnostic path:** the normal PMC dispatcher supports command **92**
for a two-byte-address read, separate from utility entry DC. Fresh source evidence:
`normal-read92.dis` and `normal-command-dispatch.dis` in the failure-analysis directory.

The sequence derived from 415 is:

1. Write command `92` to port `66`; dispatcher `36A7A/36AAC` sets RAM `80036=2`.
2. Write address high byte to data port `62`. Handler `3682C` saves it in `80038`;
   the normal loop decrements the remaining count to 1.
3. Write address low byte to data port `62`. Handler `36846 -> 367FE` combines
   `(RAM[80038] << 8) + RAM[80037]` and performs exactly one byte load.
4. Read the response from port `62` after OBF is set. The normal loop decrements the
   remaining count to zero. Standard IBF waits apply before and after each host write.

Address translation in `367FE` is:

```
a = (high << 8) | low
effective = (a < 0x1000) ? 0x80000 + a : 0xF00000 + a
response = byte[effective]
```

Thus address `019F` reads selector RAM `8019F`, and `1040–104E` reaches the first five
SCAR-like register triplets. These are candidate mapping observations without flash
utility entry. Command **93 is the corresponding write path and must not be included**
in a mapping diagnostic. This read path has been statically traced, not tested live.
The existing plain-80 handler is unsuitable as a general substitute: its accessor at
`4B974` returns zero in this firmware.

Next implementation should be a distinct **normal-mode mapping reader** with a fixed
address list, initially selector `019F` and mapping triplets `1040–104E`. Review each
register's read semantics; do not scan arbitrary MMIO or include ECINDDR (`103F`),
which participates in flash transactions. Consider control registers only when their
semantics are established. No DC, FC, follow-mode command, or register write belongs
in this probe. Use UEFI to avoid Windows ACPI.sys ownership, while retaining the SMM
interference caveat, bounded waits, detailed status traces, and one-use arming.
Partial address transactions and unexpected OBF/IBF must be handled explicitly rather
than treated as successful reads or repaired by blind writes. Local tests must cover
both address translation cases, byte order, busy/timeout paths and interference.

This offers a more informative next hardware experiment than repeating follow mode:
observe whether the utility's runtime range `2800–2A12` is covered by a valid scratch
mapping. Even apparently valid registers alone do not prove RAM contents match code.
No new laptop access or hardware experiment was performed during this analysis.

### Normal mapping read succeeded on hardware

Implemented `tools/ec_mapping_probe.c` and its UEFI wrapper, tested native protocol
faults and three QEMU boot guards, then ran the fixed-address reader on the laptop.
Binary SHA-256: `43f99eaa7d65ce49f33b0f9319a964cff93d5e6625c342634cd8e1922d8dc36b`.
Log: `out/update_415/uefi_mapping_probe/hardware-success.txt`.
Both 16-byte passes matched: RAM selector `8019F=4F`, registers `F01040..42=00,00,07`,
`F01043..4E=00` throughout. Result 0, 96 host writes (commands/address bytes), 32 reads,
no incomplete transaction, elapsed 13327 us. No EC register-value writes or utility entry.
Windows returned automatically at `2026-11-17T19:35:35.5000000-05:00`.
Temporary entry `{528a633c-15bb-11f1-870e-af7ba5ccfe69}` removed, token consumed,
bootsequence cleared, original boot order and Windows boot-manager hash unchanged.

The first mapping resembles address 70000 on related ITE definitions; do not claim
exact IT8226 mapping size/validity from that comparison. It does not visibly describe
the utility at runtime 2800. Exact chip semantics remain unresolved.

### Alternative: normal-mode indirect flash sample reader

The firmware's own routine at file `4A56E` selects internal flash using `F0103E=40`,
sets address bytes `F0103D/3C/3B`, and reads `F0103F`, without follow mode. A new
`tools/ec_indirect_probe.c` reproduces those accesses through normal command 92 reads
and 93 writes. Unlike the mapping reader, it writes volatile address-selection registers;
it has a hard write whitelist of `103B..103E`, and only 00/40 are permitted for 103E.
It never writes ECINDDR, emits SPI opcodes, or enters DC/follow mode.

The probe saves four address registers, refuses initial selector values other than
00/40, selects 40, reads the same two 96-byte sample sets as the old probe, then restores
and rereads all four registers. A partial host transaction or unconfirmed restoration
halts the wrapper. This is not a flash programmer. Cross-access by firmware/SMM remains
a caveat; repeated samples must match the known capsule before trusting the result.
Native ASan/UBSan tests passed 2924 host-write failure positions and 200 missing-response
positions, plus selector guard, differing passes, frozen clock, and restoration checks.

### Further offline mapping trace (2026-11-17)

Fresh bounded disassembly narrows the initialization question. Runtime main at
file `34800` calls `4A65A` on the `80145 == 33` utility-return path. Its normal
initialization path calls `4B5A6`, which also calls `4A65A` (at `4B5B8`). **`4A65A`
is only `ret5 $lp` (`DD 9E`), not the mapping helper at `4A618`.** This establishes
that these two calls do not configure mapping; assigning an intended source-level
name or concluding that a vendor accidentally omitted setup would be speculation.

The other normal-init callees are now accounted for locally: `4B4A0` and `4B552`
initialize RAM, `3604A` writes `14` to RAM `8018F`, and `4B57C` applies an 18-entry
register table at runtime `248F8` / file `560F8` before clearing bit 6 at `F02030`.
The table's eight-byte records hold little-endian addresses and a value byte at
offset 4. None targets `F01040–42` or `F010B0`. Instruction encoding remains
big-endian; do not assume that embedded data pointers share its byte order.
This excludes that particular initialization table as the missing scratch setup,
not all firmware/ROM setup or indirect stores elsewhere.

The mapping helper's argument use is also explicit: incoming `r0` is overwritten;
incoming `r1` supplies the mapped address. It writes `00` to `F01040`, bits 15:8
of `r1` to `F01041`, and the low byte of `(r1 >> 16) + 8` to `F01042`. It then
writes `81` to `F010B0`, polls bit 0 until clear, and clears bit 3 of `F01042`.
Do not infer its transfer length or the meaning of bit 7 from that sequence alone.

New reproducible offline audit: `python tools/audit_415_ram_mapping.py` verifies
the stock capsule SHA-256, scans every halfword in file `[31800,57000)` for
NDS32 J/JAL encodings into the helper/adjacent functions, searches literal
pointers in both byte orders, and decodes the initialization table. The raw
J/JAL scan finds only the two calls to `4A65A` and the selector call to `4A608`;
none enters `4A618–4A658`. No literal `18E18` pointer was found. This avoids
depending on broad linear-disassembly alignment but does not cover constructed
pointers, every branch encoding, indirect control flow, or ROM outside the carve.

Evidence: `out/update_415/failure_analysis/ram-mapping-audit.txt`,
`runtime-main.dis`, `normal-init.dis`, and `init-ram-byte.dis`. Audit output was
checked against bounded objdump output for the two stub calls and selector call.
No laptop access, new boot, or EC transaction was performed in this continuation.
The safe next investigation remains a path to inspect live mapping/selector state
without follow mode, or an authoritative explanation of boot/ROM mapping. Another
follow-mode trial is not justified by these negative static results alone.

### Normal indirect reads and two-pass backup succeeded (2026-11-17 evening)

The indirect sample reader succeeded on hardware: result 0, 2924 host writes,
200 reads, 192 sample bytes, no incomplete transaction, dirty=0/restored=1,
369979 us. Saved and restored address registers were all zero. Both sample
passes matched the stock 415 capsule. Binary SHA-256:
`6b63933b413df0875884340642c51975b41838941ece4204b1447f1e10622cd2`.
Log: `out/update_415/uefi_indirect_probe/hardware-success.txt`.
Windows returned at 19:39:04.5000000-05:00; temporary entry ending 633d removed.

Extended the reader to pagewise normal-mode reads, saving/restoring and verifying
F0103B..3E after each 256-byte page. Each page has bounded waits; incomplete
transactions or failed restoration halt instead of sending blind cleanup writes.
Only volatile address registers are written; ECINDDR is read only. No utility
entry, follow mode, SPI opcode, write-enable, erase, or flash programming occurs.
Native ASan/UBSan tests cover every page, transaction faults and chip-info reads;
QEMU validates wrong-model, missing-token and absent-EC paths, not EC hardware.

`tools/uefi_ec_backup_probe.c` completed two candidate 192 KiB windows on hardware
in 380198990 us. Binary SHA-256:
`75af4282af1c86dd903d53961601da3aee13db67c2eb6bb3a0701d8e5d4d9935`.
Log: `out/update_415/uefi_backup_probe/ec-backup-probe-collected.txt`.
Result 0, completed_passes=2, window=196608, incomplete=0, dirty=0, restored=1.
Windows boot time 19:49:10.5000000-05:00; temporary entry ending 633e removed,
arm token consumed, persistent boot order retained, boot manager hash unchanged.
Status-only Windows check subsequently read 08 eight times with zero writes.

Both backups have SHA-256
`5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478`
and **match capsule bytes [31800,61800) exactly, over all 196608 bytes**.
There are no observed per-device differences in this window. This is a verified
repeatable window backup, not independent proof of physical flash capacity or
the absence of additional boot/security storage. Files and pagewise comparison:
`out/update_415/uefi_backup_probe/ec-backup-pass{0,1}.bin`, `backup-audit.json`.
Reproduce with `tools/audit_ec_backup.py`.

**Chip identity correction:** live F02000/F02001/F02002 returned **51,25,02**;
F02085..87 returned zero. This supports **IT5125 revision 02**, consistent with
ASCII `5125` at capsule 34C32 and the encoded header identifier. Earlier claims
that the physical chip is IT8226 were based on a legacy string and are superseded.
The NDS32 instruction architecture remains confirmed. A related GV301QC board
analysis independently identifies IT5125VG-192, but does not establish QE package:
https://github.com/osy/XG_Mobile_Station/blob/main/Docs/Connector.md .
No authoritative exact IT5125 register/programming datasheet obtained yet.

### Offline candidate prepared; NOT ready to program

`tools/prepare_ec_candidate.py` requires two identical readouts, verifies both
original branch instructions and both separate secondary `==100` predicates,
and preserves all bytes except the two branch replacements. NDS32 objdump confirms
`9200` is nop16 and `40000009` is nop. Negative input/guard checks passed.
Candidate: `out/update_415/ec_candidate_v1/ec-vid-gates.UNAPPROVED.bin`.
SHA-256: `eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e`.
Manifest records five actually changed bytes across the two instructions.
No candidate has been copied to the laptop or programmed. Checksum/boot handling,
flash geometry/protection, RAM execution and a reliable programming/recovery
method are still unresolved. Do not confuse repeatable reads with flash readiness.

### Primary-source header comparison and remaining mapping gap

Located the original TUXEDO `ite_ecfw.c` in **abandoned**, unmerged flashrom
change 55715, revision `817bae8338e24a0f929a1a607fd9c6ef83c9bc0d`:
https://review.coreboot.org/c/flashrom/+/55715 . Snapshot and provenance in
`out/update_415/failure_analysis/tuxedo-ite-ecfw.c` and `tuxedo-ite-source.json`.
This is a different EC protocol (IT5570/TUXEDO), NOT a programmer for this laptop.

Its header structure matches our observed bytes at EC offset80:
`A5 A5 A5 A5 A5 A5 A5 94 85 12 5A 5A AA BF 55 55`.
Under that structure, flags94 has no_crc_check=1, no_fspi_mirror=0,
internal-clock=1, implicit code base, no PWM blink. Mirror-size byte **BF=191 KiB**
is consistent with a 192 KiB flash reserving its final KiB, but is not independent
silicon capacity proof. The stored checksum is 5A5A. These are cross-chip
interpretations, not verified IT5125 boot-ROM behavior. Candidate keeps all header
bytes unchanged; do not automatically add mirror-disable or checksum patches.

Older Chromium IT8380 NDS32 startup also places this header at80, with B4 flags
and AA mirror size (`fa9f407a66e10b4871d02b118952a9b0ac7e3263/core/nds32/init.S`).
Its 2015 flash implementation (`a69c63bae5223b6a0070ee2ab48b6c9db65dc6c7`)
uses a different SCAR2/DLM14 RAM layout and static DMA control at F01080.
The newer official IT82302 datasheet actually calls F010B0 **SCAR13L**, also
incompatible with our firmware helper's use. Neither source supplies the missing
IT5125 F010B0 semantics. Their sequences must not be copied to this chip.

The next hardware step, if justified by further analysis and physical recovery
availability, should isolate a **volatile mapping round trip**, using the exact
415 helper sequence with the utility's original bytes. Verify DMA completion,
normal PMC responsiveness and restoration before any follow-mode exchange.
Such a test still has unresolved transfer-size/destination assumptions; no
implementation has been armed or deployed. Physical recovery availability was
asked asynchronously; no answer received at this writing.


### Read-only control probe succeeded (2026-11-17 21:21 local)

User confirmed physical availability to recover a stalled diagnostic. Built and
validated `ec_control_probe.{c,h}` and its UEFI wrapper, fixed 16-register list,
two passes via normal command92 only. No command93 or utility/follow mode.
Native ASan/UBSan tests cover transaction faults, missing responses, early OBF,
absent/busy EC, mismatch and frozen clock. Three QEMU guard tests passed; these
do not emulate the real ITE device. EFI SHA-256:
`12f9643d466c9513557d95ddb29c532338daac540c6b064194eed0df8f57f9e9`.

Hardware result0, 96 host writes (read-command/address bytes), 32 reads,
incomplete0, 13274 us. Two passes match: RAM8019F=4F, F01040..42=00,00,07,
F01043..44=00, F01063=01, F010B0=00, F02030=01, F02000..02=51,25,02,
F0103B..3E=00. Log `out/update_415/uefi_control_probe/hardware-success.txt`.
These baseline values do not establish DMA destination/length or justify writing
mapping registers. No flash programming occurred; candidate remains local only.

Windows returned at 2026-11-17T21:21:27.5000000-05:00. Temporary boot entry
{528a633f-15bb-11f1-870e-af7ba5ccfe69} removed, one-use token consumed,
persistent order retained, S: unmounted. Windows boot manager hash unchanged.
Before boot: AC online, battery100%, Charging=false, Discharging=true; the
latter flag predated this test. No diagnostic remains armed.

Alternative investigation: eFlashDebug source exposes direct EC access through
ports4E/4F and host config indices2E/2F, with debug subindices11/10/12 for address
high/low/data. Stock415 startup table writes F0200A=01, consistent with this
source's BADRSEL selection of4E/4F. This is cross-chip protocol evidence only;
IT5125 enablement, decode and CPU-independent operation are not yet verified.
Source: https://github.com/tomios/ITE_ECTool/blob/master/eFlashDebug/eFlashDebug.cpp .
No direct debug writes or reads have been performed on hardware.


### Direct-access v1 stopped at index-port FF (2026-11-17 21:28 local)

Direct-port probe v1 passed ASan/UBSan tests and QEMU guard tests. EFI SHA-256
`dda2bf0215feb36e18fc153e6834a5b55e8126699821bd3624d1385f33122563`.
Hardware read4E=FF, result1, writes0, reads1. This does NOT establish absence:
FF could be an existing index. No host chip ID or EC memory was read. Windows
returned at21:28:50.5000000-05:00, entry ending6340 removed, token consumed,
Windows boot manager hash unchanged. Log `uefi_direct_probe/hardware-inconclusive.txt`.
Revised v2 permits initial FF and checks read-only host ID indices20/21 before
any depth-address/data access; it restores the original index. No unlock/enable,
EC memory data write, CPU halt command, follow or flash command is implemented.

ITE IT81202 official datasheet corroborates D2ADR/D2DAT protocol and SPCTRL1
I2ECCTRL read/write enables. Stock415 table initializes F0200D=43/F0200A=01,
consistent with enable and4E/4F selection under this cross-chip interpretation.
Source provenance: `out/update_415/failure_analysis/i2ec-source.json`.
This does not establish exact IT5125 DMA or flash behavior.


### Direct-access v2 host IDs do not respond (2026-11-17 21:31 local)

V2 reads host ID indices20/21 even with original indexFF. EFI SHA-256:
`d007e010da27c7e622632a96f4b8b488cbbcaca5f130baf7a20df149e1119603`.
Hardware result2, initialFF, hostID=FF,FF, writes3 (index20,index21,restoreFF),
reads4, restored1, EC memory rows0. It stopped before any depth/address/data
writes. This proves no responding ID through4E/4F in the current configuration,
not that I2EC is unavailable in silicon. Native ASan/UBSan and three QEMU tests
passed. Logs under `out/update_415/uefi_direct_probe_v2/`.
Windows returned21:31:03.5000000-05:00, temporary entry ending6341 removed,
arm token consumed, boot manager unchanged. Status-only normal EC reads were
08 eight times afterward. No flash commands sent.

Next read-only routing probe combines normal PMC92 reads of BADRSEL, SPCTRL1,
LSIOHA and established baseline registers with EFI PCI-root-bridge reads of
00:14.3 configuration offsets00,08,40,44,48,64,74,90. No PCI writes. Purpose:
distinguish host routing from EC access lock/disable. Coreboot AMD common LPC
header defines config48 bits0/1 as SIO decode controls; actual chip identity
and live config must be checked before considering any temporary routing change.
Source: https://github.com/coreboot/coreboot/blob/main/src/soc/amd/common/block/include/amdblocks/lpc.h .


### Routing probe identifies relocated host configuration ports (21:33 local)

Read-only routing probe succeeded: result0, 96 command/address writes, 32 reads,
11986 us, no incomplete transaction; both passes match. New values:
F0200A=02 (custom base), F0200D=03, F01202=00. Other baseline unchanged.
AMD LPC 00:14.3 ID=790E1022, class/revision=06010051; config40=0000000C,
44=FF03FFD5,48=03A0FF07,64=02501640,74=0000000C,90=00000380.
No PCI configuration write occurred. Both standard SIO decodes are enabled;
the custom range0250 is also configured. Log `uefi_routing_probe/hardware-success.txt`.
EFI SHA-256 `85bcae4836dac5dfbd7c312e0959bb035f9eeb371560399bef9c7ab861f9d52c`.
Windows returned21:33:33.5000000-05:00; temporary entry ending6342 removed,
arm token consumed, boot manager unchanged. Native and three QEMU tests passed.

Fresh bounded disassembly located firmware writes at4BD84..4BD8E:
F0140A=02, F01408=5E, F0200A=02. Thus stock415 itself relocates the host
configuration pair to025E/025F. This explains why4E/4F did not respond.
Excerpt `failure_analysis/i2ec-port-relocation.dis`; early startup table4E is
superseded by this later initialization. The next direct probe uses025E/025F,
retaining the host-ID gate and only reading EC register data. No routing writes
or speculative EC mapping writes are necessary for this test.


### Direct I2EC register reads succeed at025E/025F (21:36 local)

V3 hardware result0, initial host index00, host ID51,25, 203 address-selector
writes, 26 reads, 16 EC-register rows (two matching passes), restored1.
Saved depth index/high/low=00,A5,00; all restored and read back. Live values:
2000=51,2001=25,2002=02,200A=02,200D=03,1202=00,1063=01,10B0=00.
No writes to depth data register12 were made: only its reads. No EC register
value, RAM data, flash mode, enable, erase or programming write occurred.

EFI SHA-256 `5de170e04a181165e7b105f6ba41890c2d6c6118ae65d68c4cd9da49a4244f5c`.
Log `out/update_415/uefi_direct_probe_v3/hardware-result.txt`; collection metadata
in same directory. Windows returned21:36:22.5000000-05:00. Entry ending6343
removed; token consumed, persistent order unchanged, boot manager hash unchanged,
S: unmounted. No diagnostic armed. Native ASan/UBSan tests validate no EC data
writes, ID guards, mismatch and restoration handling, and 26 injected read-fault
positions. Three QEMU guards passed, including absent target with index restored.

Current sources `ec_direct_probe.{c,h}`, `uefi_ec_direct_probe.c`, and
`build_uefi_ec_direct_probe.sh` build **v3**, fixed025E/025F. Use corresponding
`windows_*_ec_direct_probe_v3.ps1`; older v1/v2 scripts are historical artifacts
with different hashes/tokens/paths. Do not rearm unchanged now that v3 succeeded.

This establishes direct hardware register reads independent of the normal PMC
command protocol. It DOES NOT establish operation while EC flash is unavailable,
CPU halt/resume semantics, flash protection/geometry, or mirror-safe persistence.
Read success must not be used as clearance to enter follow mode or program.

### Stock BIOS confirms a second I2EC user; contention still matters

Fresh x86 disassembly of415 `AsusEcDxeSmm.efi` shows dedicated-port I2EC accesses
through381(high),382(low),383(data) at RVAs2708/275C and27D4. HighC9 addresses
mailbox C940/C96E/C96F; EC disassembly uses RAM80940/8096E/8096F. This supports,
but does not by itself fully map, the C000-based host RAM alias. Startup function
4BD36 configures dedicated base0380 via F02014=03/F02015=80 and F02012=29.
No dedicated-port experiment or RAM-alias test has been run.

The BIOS code also manipulates chipset SMI controls around mailbox operations
(RVAs2C44/2CBC). Thus high UEFI TPL alone is not exclusion from all users of the
EC debug path; do not claim a direct-access writer is concurrency-safe yet.
Disassembly: `out/update_415/failure_analysis/AsusEcDxeSmm.dis`. Four other
flash-related modules were disassembled and searched for direct register/port
constants; that bounded search did not resolve IT5125 RAM DMA. It is not proof
that no such path exists elsewhere in BIOS.

The remaining material blocker is exact RAM-copy/CPU execution behavior during
flash access. The stock helper is known, but its destination/length are not.
Newer ITE and Chromium programmer register layouts differ; in particular DBGR
I2C command27 (reset/halt in some five-digit IT5xxxx chips) is NOT an established
IT5125 MMIO control. No speculative mapping/halt/protection writes were tried.

### Offline update-path audit: targeted SMI guard and invariant helper (2026-11-17)

No hardware access, staging, reboot or programming occurred in this analysis.
`tools/audit_415_update_paths.py` now reproduces the table decoding and helper
search; output is `out/update_415/failure_analysis/update-path-audit.json`.
It checks exact stock capsule and AsusEcDxeSmm image hashes before decoding.

AsusEcDxeSmm RVA2C44(CL=1) saves `current & ~mask` for ten chipset register
table rows at31C0, skipping FFFFFFFF masks. This branch does not write hardware.
RVA2CBC selects byte09 at3214, matches entry23 of the24-byte table at3220:
22,09,54,10,A4,0C. It clears bits12–13 (mask3000) at **FED802A4**.
RVA2C44(CL=0) restores `(current & mask) | saved`; A4 mask00FC0000 preserves
its masked fields and restores the previously saved bits12–13. Wrappers20FC
and27D4 use this coordination around EC mailbox accesses. This is a targeted
SMI-source guard, NOT global SMM exclusion or a validated flash-programming lock.
Exact event identity remains unresolved. No chipset register was changed here.

The66-byte mapping helper415[4A618,4A65A) matches all ten saved versions:
407/408 at4A274;409–412 at4A614;413/414 at4A61C;415/418 at4A618.
Halfword-aligned J/JAL scans within +/-30000 of each helper found no direct
candidate to its exact entry. This excludes neither indirect calls, ROM callers,
other branch forms nor entry into the middle. It provides no DMA destination
or length. Versions were compared offline; no upgrade is proposed.

Broader recursive BIOS extraction scanned448 modules/587 PE or TE bodies
(including recursion duplicates) for raw register/port constants. Results:
`failure_analysis/all-pe-patterns.json`. Newly extracted modules/disassemblies
include FchShastaDxe, FchShastaSmmInit, FchSmmDispatcher, AsusEcDxeRt,
AsusEcDxeBs and PEFirmwareUpdateDXE. No exact RAM-copy path was established.
Raw constant hits can be structure offsets or PE metadata; negative results
do not exclude computed addresses or indirect update paths.

The manufacturer-authored **IT5570 V0.3.1(A)** datasheet was also examined:
https://github.com/fnnas/appstore.driver.hwmon.it5570/blob/dev/docs/IT5570_A_V0.3.1_U.pdf
PDF SHA256 `01666ebe44ece4a436b5ffaad40c9570c17f0b3480862caf224953585c59102f`.
This is an8051 part, not the NDS32 IT5125. Its section6.4.3.7 describes a1KiB
cache-sharing scratch region and copy triggered by clearing SCAR0H bit7;
section6.4.4.16 marks SCAR0H bits6..3 reserved. Our stock helper instead uses
SCAR0H bit3 and separate F010B0=81. Therefore neither that1KiB size nor its
copy procedure can be adopted for this laptop. The suspected private scratch
destination remains a hypothesis. Exact IT5125 evidence is still needed before
another follow-mode test or mapping write can be justified.

### Follow-probe tracing prepared offline; official-source leads checked (2026-11-17)

The original failure's combined command count cannot identify its failing command.
`ec_read_probe.{c,h}` now records each attempted command with its byte, whether
it was sent, last observed status, start/elapsed microseconds, completion and
wait stage (1=before send,2=after send,3=response). Test and cleanup phases have
separate sent counts and first-failure indices (one-based, zero=none). Cleanup
does not overwrite the primary failure. Trace capacity512 exceeds the306 command
attempts on the complete successful test; overflow is explicitly counted.

Native ASan/UBSan tests passed normal operation, all306 injected command timeout
positions,198 missing-response positions, bad ACK, mismatch, busy/absent device,
and frozen clock. Tests assert trace phase/stage, send status, timeout duration,
monotonic ordering, and separate primary/cleanup failures. Existing synthetic
sample-log checker tests also passed. These are simulated results, not proof of
IT5125 flash-mode operation. The protocol sequence itself is unchanged.

EFI wrapper builds v2 with a distinct arm-read-probe-v2.txt token path, exact
token `GV301QE.415 READ-ONLY EC PROBE V2 TRACE` followed by LF, and distinct
read-probe-v2.txt log. Build output is now `out/update_415/uefi_read_probe_v2/`;
old hardware-failure artifacts remain intact. Binary SHA256:
`0c29dd8096e9de88c5e69c10a021c01f4e6a3d6d0ff7121c768b6e1274a45580`.
No v2 staging scripts were created, no v2 QEMU run performed, and nothing copied
to the laptop or armed. **Do not run this unchanged follow sequence just because
its logging is better. RAM execution/recovery still must be resolved first.**

Fresh bounded utility disassembly saved as `failure_analysis/utility-follow-recheck.dis`.
Command01 writes F0103E=RAM8019F,103D=FF,103C=FE,103B=00 then103F=00.
Command02 writes103C=FD and sets the next-byte flag. This recheck does not reveal
a hidden RAM-copy call in the utility, nor establish which instruction stalled.

New primary sources checked:
- Official ITE DLB4 Linux programmer v1.0.6 source:
  https://www.ite.com.tw/upload/2024_01_23/6_20240123162336wu55j1Rjm4.bz2
  This is a libusb front-end to an external programming board, not a software-only
  host-LPC updater. Its RunCtrl function27 is a USB protocol command, not evidence
  of an IT5125 halt MMIO register. Its initialization unprotects flash; it must not
  be executed as a read-only diagnostic. Source archive preserved locally.
- Official IT82302 V0.3.1 datasheet:
  https://www.ite.com.tw/upload/2024_01_23/6_2024012316175049je5fzt61.pdf
  Section6.4.4.43 places static DMA at SMFI+80 and selects4KiB SRAM banks. This
  does not establish our F010B0 semantics or RAM destination. No cross-chip writes.

Source hashes and conclusions: `failure_analysis/ite-official-source-audit.json`.
No laptop access or hardware operation occurred this turn. Software-only flash
readiness remains unproven; exact IT5125 DMA/CPU execution evidence is the blocker.

### Extraction corrected; vendor utilities and host-SPI alternative audited (2026-11-17 22:07 local)

The earlier448-module extraction was incomplete. `tools/fwx.py` stopped on an
all-FF GUID that actually belongs to a valid PAD file at capsuleEC0E20 and lacked
AMD zlib GUID decompression. The PAD is followed by file
3E3B6DC0-064D-4B01-9203-7836C9B498E7 atEC27D0; its GUID-defined section atEC27E8
uses CE3233F5-2CD6-4D87-9152-4A238BB6D1C4. The256-byte AMD header carries compressed
size at14h. It expands to a240000h-byte PEI FV.

Extractor now skips PAD payloads, distinguishes an erased24-byte header from an
all-FF GUID, and validates/decompresses AMD zlib streams. Six regression cases
in `tests/fwx_test.py` pass: executable after PAD, compressed executable recovery,
short header, size mismatch, truncated stream and trailing stream data.
Corrected result:547 modules,96 PEIMs instead of2. Independently built UEFIExtract
from https://github.com/LongSoft/UEFITool commit
`de73942df908b9466c055e15108ac4e4c0b1688c` produces exactly the same391 unique
PE/TE body hashes. Full extraction/report and comparison JSON are preserved under
`out/update_415/full_extract/`. Earlier negative PEI searches must not be treated
as complete evidence. This comparison checks extracted executable coverage, not
complete semantic analysis of all code.

New AsusEcPei and AsusEcPeiBiosReady TE images are IA32, not x64. Saved their
images/disassembly under failure_analysis. TE-to-RVA offsets are180h/188h.
AsusEcPeiBiosReady initializes LPC decode then sends FF93/data00 and FF93/data03
on the auxiliary PMC ports; no DMA preparation was identified there. AsusEcPei
publishes its PPI and callbacks. Raw pattern scans remain only search aids;
absence of immediate constants does not exclude computed or indirect operations.

Vendor-distributed ITE IFU EFI v2.3.1a was downloaded and disassembled OFFLINE:
https://github.com/StarLabsLtd/firmware/blob/main/binaries/ifu.efi
SHA256 `16c0827384c6b4509799d02d7560b413642e413582591b49fd6f428896b28176`.
The FEVM firmware-package copy is byte-identical. Detection atRVA2A58 checks
standard2E/4E ports and accepts chip-ID high87/89/85/86/55, not our51 at025E.
Its DC/ACK33/follow path did not establish our RAM setup. Some branches can send
DC after failed detection; do not run it even to query support. FE reset defaults
also differ from our approved FC-only exit. Do not force its chip check.
The package's Windows IFU_XE.exe identifies as ITEFlashUtility x86v1.463(C)2015,
not a newer chip-specific updater. SHA256:
`776c844653d60d4a82fdd4684decc50598116d49e1efbad914328b9630a8889f`.
Artifacts, source URLs, hashes and decompilation in `out/update_415/ifu_analysis/`.
Neither utility was executed or copied to the laptop.

Alternate host-SPI path: stock AmdSpiRomProtectDxe callbackRVA4C0 writes
**FEC10009=06**, then **FEC10000 &= FF3FFFFF**, clearing bits22/23.
It registers callback with ReadyToBoot GUID7CE88FB3-4BD7-4679-87A8-A8D8DEE50D2B
atRVA B18. Exact instructions and GUID were checked against the extracted image;
`failure_analysis/host-spi-protection-audit.json` records evidence/hash.
EDK2 defines that event GUID:
https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Guid/EventGroup.h
Flashrom names bits22/23 SpiAccessMacRomEn/SpiHostAccessRomEn and refuses full
access if either is clear:
https://github.com/flashrom/flashrom/blob/main/sb600spi.c
This is static evidence of BIOS protection setup, NOT a live register reading or
proof of an irreversible lock. Stock FlashDriver also manipulates these controls;
no unlocking sequence has been validated. AmdSpiRomProtectPei is a small stub.
Do not run ordinary flashrom probing as a read-only check: enable_flash_sb600
writes PCI50..5C to remove protection before probing. Source snapshots saved.
Even writable host SPI would not establish how IT5125 mirrors/authenticates the
embedded EC image or whether modifying that region boots safely.

Read-only SSH state check at approximately22:06 found BIOS415, unchanged Windows
boot time21:36:22.5000000-05:00, no fwbootmgr bootsequence, **PowerOnline=false**,
Charging=false, Discharging=true, RemainingCapacity43255. Latest record:
`failure_analysis/latest-windows-state.txt`. This supersedes earlier AC-present
observations. No reboot, probe staging, mapping write or flash operation this turn.

Readiness remains blocked by IT5125 RAM execution/DMA semantics, flash geometry
and protection, and boot/mirror behavior. Existing binaries and cross-chip
references do not establish these. Exact IT5125 programming documentation or a
validated updater supporting this chip/board would be material new evidence.
A hardware programmer/recoverable test board would enable experiments that are
not justified on this sole laptop without recovery. Another identical follow
probe, generic IFU run, or speculative mapping write is not a next step.

### User-supplied Windows415 update package inspected (2026-11-17)

`bios_update_windows_v415/ASUS_GV301QE_415_BIOS_Update_3.exe` SHA256
`45e22ffb12dcab03e1349858c151f3da9e2ea377bd4e7efe9e16a99e587629d4`.
Extracted locally with7z and innoextract; nothing executed or installed remotely.
Artifacts and hash manifest: `out/update_415/windows_package/audit.json`.
Resource ZIP/103 is a7z archive containing BIOSInstall.ps1,Install.bat and
Cabfile/{GV301QE.415,GV301QE_415.inf,gv301qe_415.cat}. Its16,779,264-byte firmware
is BYTE-IDENTICAL to the existing stock capsule, SHA25655578ba4...010f94.
Thus this package supplies no new EC binary or changed capsule authentication.

BIOSInstall.ps1 enumerates the Windows system-firmware PnP device, removes its old
firmware driver package where applicable, then runs `pnputil /add-driver ... /install`.
INF class Firmware, resource GUID9FEB6FC6-433F-59F0-A6B2-66A1D7046D5B,
FirmwareVersion0x415, driver version10.1.2.415. It copies the capsule under
Windows/Firmware/<GUID>. This is capsule delivery, not host EC programming.
Microsoft documents the boot-loader catalog check and subsequent UpdateCapsule:
https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/installing-the-update
Changing the PowerShell wrapper does not produce a valid ASUS capsule signature.

Resource EXE/102 is an Inno Setup wrapper. Its extracted support files include
7z, ASUS business-intelligence libraries/installer, SKU check/UI helpers and
BitLocker check/suspend scripts; no separate EC image, EC flasher or kernel .sys
programming driver was found. Install_innCMD.bat calls Install.bat, which invokes
BIOSInstall.ps1. CheckRun/InstallAutoShowMsg are .NET executables with SKU-check
and notice-dialog strings. No flash preparation or IT5125 DMA information found.
Useful result: exact official Windows delivery chain identified; not a validated
route for modified firmware, and not a demonstrated recovery method for a dead EC.

### Stock update-handler trace: host SPI region vs EC programming (2026-11-17)

Offline disassembly/decompilation saved in `out/update_415/update_handler/`.
`tools/audit_415_update_layout.py` reproduces layout/GUID/table checks against
stock415 and extracted modules; passed. Existing capsule RSA-PSS coverage audit
also passed and was saved alongside it. Decompiler output is an analysis aid:
argument recovery and callback discovery are imperfect; key pointers/layouts were
cross-checked against PE sections and objdump. No laptop operation this turn.

**New concrete ROM-layout finding:** ReFlash RVA6460 scans for a ROML structure;
its stock match is capsuleEC0AB0, stride28h,8 entries. Entry4 describes host flash
addressFF031000, raw-image offset31000, size40000, type1, attributes200.
Adding the800-byte capsule header places it at [31800,71800), containing the
verified192KiB EC image [31800,61800). The256KiB host region is NOT evidence for
256KiB internal EC flash. This distinction matters for any future patching.

ReFlash RVA1724 includes entries with attribute200; RVA1594 constructs48-byte
work descriptors with1000-byte blocks. Entry4's zero GUID and flags200 select
kind4 and callback146C, not an EC-specific branch in this dispatch. Callback146C
calls method+18 of protocol755B6596-6896-4BA3-B3DD-1C629FD1EA88 with host address,
block size and source from the firmware image. Callback14F0 uses methods+8/+10
for erase/write on other descriptor kinds. The UI string "EC FIRMWARE Update"
therefore must not be taken as proof of a host-to-EC programmer.

FlashDriver installs that same protocol (GUID at6080, table6090). Methods:
read1EA4, erase1F3C, write1FB0, update2028, enable20CC, disable2100.
Update2028 ->1BC8 ->2854; byte programming ->2B08 -> backend+18.
The four backend detectors in table62C0 (3E88,3640,32E4,31F8) select SPI NOR
families and table61F0; backend+10=47FC erase, +18=4BE0 program.
Its controller pointer6258 is **FEC10000**, not an EC MMIO address. Helper3FC4
resets its FIFO and3FF0 sends its write-enable transaction. This establishes a
host-SPI writer along the examined path. It does not prove absence of other
EC update hooks elsewhere.

FlashSmiDxe adds forwarding in boot modes12/20. Callback1B94 replaces protocol
methods with SMM wrappers; update wrapper1614 sends a40h communication buffer
with28h payload, signature FSMI, operation3, host address/length/data. The
communication GUID is4052ACA8-8D90-4F5A-BFE8-B895B164E482. Callback1C60 restores
saved methods. FlashSmiSmm handler1344 validates the buffer, dispatches operation3
to the saved SMM flash method+18 (other commands select erase/write/enable/disable).
This is forwarding, not a separate EC command protocol. Exact active method
selection depends on registration order; this was not observed live.

**Authentication boundaries reconfirmed:** SecSMIFlash initialization replaces
SMM flash methods+8/+10/+18 with wrappers2044/1FC4/2004. Their guard1F28 examines
region attributes and authenticated-image state; protected writes compare
SHA256-sized hashes for1000-byte pages and redirect source to the staged image.
The EC-containing region has attribute200, so the unprotected-region early return
is not evidence of an unsigned EC route. Capsule verifier3148 validates headers,
gets key material, chooses2D98 or2F64, then checks firmware identity/version2A00.
For the stock capsule,2F64 checks the key and RSA signature;2C04 hashes selected
regions plus layout. This agrees with the earlier reproducible cryptographic
proof that changing either VID branch breaks the capsule signature.
SMM flash protocol GUID: ECB867AB-8DF4-492D-8150-A7FD1B9B5A75.

CapsuleRuntimeDxe installs UpdateCapsule atRVA138C (runtime table+70) and query at
157C (+78). The examined UpdateCapsule routine validates flags, persists the
scatter/gather pointer in a capsule variable and optionally resets; it is not
an IT5125 RAM-loader. The complete reboot-time capsule processing chain has not
been proven end-to-end; no claim that every possible update path was exhausted.

**Conclusion and next evidence needed:** stock update code demonstrably treats
the EC-containing host-SPI region as a signed flash region. No independent
unsigned EC update interface or IT5125 DMA preparation emerged in these paths.
How the stored image reaches internal EC flash remains unresolved: boot-ROM
mirroring is consistent with prior header evidence but is still an inference,
not verified IT5125 behavior. Exact-chip boot/programming evidence remains the
prerequisite for flash readiness; this analysis does not justify patching header
flags, invoking protected SMM writes, or retrying follow mode.

### Live host SPI audit v1 (2026-11-17 22:57 local)

Read-only UEFI PCI snapshot completed twice with matching values: AMD LPC ID
790E1022, SPI BAR FEC10002 (base FEC10000), PCI50/54/58/5C all zero.
PCI40=0000000C,44=FF03FFD5,48=03A0FF07,B8=33ED0004.
Root Bridge Mem.Read at FEC10000/04/08 returned EFI_INVALID_PARAMETER;
FFFFFFFF buffers are placeholders, NOT register values. The aggregate result3
mislabels the previous MMIO failure as mmio_refused on pass2; no BAR mismatch.
Log/source/binary preserved in out/update_415/uefi_spi_probe/.
SHA256 ab7cf87a9cf1262cef02d8f4aec268e9ebbdb02e357da1f9301a89fef3ecb014.
Windows returned22:57:28.5; token consumed and entry
{528a6344-15bb-11f1-870e-af7ba5ccfe69} deleted. No pending diagnostic.
Windows boot manager hash unchanged. AC staging gate passed after user connected.
No EC access, SPI commands, PCI/MMIO writes or firmware programming occurred.

EDK2 RootBridgeIoCheckParameter checks MMIO against root bridge apertures:
https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciRootBridgeIo.c
An aperture rejection is a plausible explanation, not proven ASUS behavior.
V2 uses three direct volatile DWORD reads at the fixed stock mapping after exact
SMBIOS/BIOS/PCI ID/BAR guards. No control writes or SPI transactions. Distinct
arm/log/binary names preserve v1. Native ASan/UBSan guard tests passed.

### Live host SPI audit v2 and stock enable path (2026-11-17 23:01 local)

V2 succeeded: two matching PCI and direct MMIO passes, result0. Control0
FEC10000=4F0C2105 (bits22/23 BOTH CLEAR); restricted-command DWORDs
FEC10004=00000000 and FEC10008=00000600 (byte09=06). PCI50..5C remain0.
This agrees with stock ReadyToBoot protection. It does not prove irreversible
locking or establish Windows-time state. No flash transactions were issued.
Source tools/uefi_spi_probe_v2.c, binary out/update_415/uefi_spi_probe_v2/spi-probe-v2.efi,
SHA256 edcdc1b873f8978f77e0941cc99ba8d27f0b71582a41035abe4e6031c0eb90fd.
Native ASan/UBSan tests and three QEMU refusal/chainload tests passed. Compiler
output confirms fixed DWORD loads at offsets0/4/8 after identity/BAR guards.
Decoder tested success, prefixed collection JSON, incomplete/unequal/duplicate
readout rejection. Hardware result and decoded audit.json are in output directory.

Windows returned23:01:40.5. Token consumed; temporary entry
{528a6345-15bb-11f1-870e-af7ba5ccfe69} removed; boot manager unchanged and S: unmounted.
Preboot Windows reported PowerOnline=true, Charging=true, Discharging=false,
RemainingCapacity40052. This supersedes earlier discharging observation at that time.
No firmware programming, no EC data writes, no SPI/PCI control writes occurred.

**Concrete stock enable/restore path, offline only:** FlashDriver20CC ->2740
calls table6120 hook3120; disable2100 ->2798 calls table6110 hook31BC.
Pointer6140 is FEC10000. Hook3120 checks byte09==06 and byte02&C0==0,
EXACTLY matching the live snapshot. It sets software flag6848, conditionally
changes FED802B2 bit6/FED8029B bit7, sets FEC10002 bits6/7, clears FEC10009,
then restores the conditionally changed platform controls. Hook31BC puts06 back
and clears those two SPI bits when its flag is set. Backend52EC/5308 toggle a
software flag684A. Disassembly corroborates these writes; no routine was invoked.
Artifacts: update_handler/FlashDriver.enable-disable.{c,dis},
enable-condition-audit.json (pointer checks, byte spans and matching live condition).

This establishes stock software intended to reopen the measured controls; it is
NOT proof that a custom application can do so under current SMM policy. Do not
call this a permanent lock or a validated unsigned-flash route. The signed update
region guard and unresolved exact-chip EC boot/mirror behavior still apply.
Next useful research: platform SMI protection interaction around these hooks and
how stock boot propagates the host EC image. Another unchanged control snapshot,
a generic flashrom run, or a speculative erase adds no missing proof.

### Live full mapped BIOS backup and platform controls (2026-11-17 23:07 local)

Two16MiB reads at FF000000..FFFFFFFF match byte-for-byte. Both SHA256:
74c8b75a51496d9d7821883552b0157a6685e233aa0581cdf888a899680ffade.
Files out/update_415/uefi_host_backup/host-backup-pass{0,1}.bin; backup-audit.json.
The192KiB EC copy at raw31000..61000 matches the internal EC backup AND stock415.
Entire256KiB host EC region31000..71000 is stock.96 other4KiB pages differ from the
stock capsule payload; retain live machine-specific contents, do not substitute
an entire stock ROM. This is a mapped readout, not independent programmer proof.

Probe tools/uefi_host_backup.c uses exact model/415/PCI/BAR guards and checks the
16-byte EC header at FF031080 before any broad ROM read. No host hardware writes,
EC access, or explicit SPI commands. Two bounded DWORD passes and ESP file writes.
EFI SHA256 abb02ada0f7b353122d23f180067e020204afd921ad7fe60449acc2e5f0a2402.
Native ASan/UBSan exact-bound/header-refusal test and three QEMU guard tests passed.
Windows returned23:07:31.5; entry528a6346-15bb-11f1-870e-af7ba5ccfe69 removed,
one-shot token consumed, bootmgr hash unchanged, S: unmounted. Nothing armed.

New readouts: SPI+1D=98; FED80298=1DFFBE77; FED802A4=00000000;
FED802B0=01400004. SPI controls/restricted opcodes unchanged from preceding audit.
Cezanne coreboot headers name98 SmiTrig0, bit31 SMIENB (active disable), bit28 EOS.
Here bit31 is CLEAR. The conditional branch in stock3120 would therefore skip its
FED8029B/B2 changes at this snapshot. SPI+1D bit5clear, bits3/4set; cross-family
AMD definitions call these SpiProtectLock and range enables, not proof of an
unlockable WREN restriction. Stock FlashDriverSmm ALSO contains3120/31BC routines,
with controller pointer6180; ordinary DXE code having a copy does not prove it
can reopen protections outside SMM. Source/decompilation in platform_policy/.

### AMD directory coverage from live and stock images (2026-11-17)

PSPTool's default FET search falsely reported no directory: it requires four00
orFF bytes before AA55AA55. Our canonical raw20000 FET follows nonpadding data.
tools/inspect_415_psp.py supplies only that checked location, verifies size/magic
and bounded terminator, and does NOT modify input bytes. Isolated tool dependency
versions recorded in platform_policy/psptool-packages.txt; upstream documentation:
https://github.com/PSPReverse/PSPTool . Parsed live/stock JSON and warnings saved.

Four directories: D2000($PSP),26D000($PL2),215000($BHD),45D000($BL2).
No parsed directory entry overlaps host EC region31000..71000. Both BIOS entries
point at EC2000 size78627 (compressed PEI), not the EC firmware. This is only
entry-coverage evidence, NOT proof that no indirect authentication exists.
Live-vs-stock directory summaries differ only in PSP_NV_DATA,PSP_NVRAM,APCB and
APOB_NV_COPY content fingerprints. PSP parser verifies multiple signed AMD blobs.
Coverage summary: platform_policy/ec-psp-coverage-audit.json.

### Direct SPI access-bit test and AMI SMM lead (2026-11-17 23:13 local)

A bounded control-only EFI test attempted FEC10002=CC, read back, then restored0C.
Precondition required exact idle control4F0C2105 and restricted DWORDs0/600.
No ExecuteOpCode/FIFO-clear bit, opcode, FIFO, restricted-command or EC write.
Hardware returned access_result=2: before/during/after all4F0C2105; restriction600
unchanged. Both writes issued, enable request ignored. This closes the simple
ordinary-EFI enable path actually tested; it is not proof against all SMM routes.
Related AMD BKDG55072 documents write-0-only outsideSMI and read/write inSMI:
https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/programmer-references/55072_AMD_Family_15h_Models_70h-7Fh_BKDG.pdf
That is a related-family reference, not an exact Cezanne programming specification.
Windows returned23:13:39.5; entry528a6347-15bb-11f1-870e-af7ba5ccfe69 removed,
token consumed, bootmgr unchanged. EFI fb114858f69d40d4b5a5a28c0af8e1cf4cfca9b248c0642e80346e6ab504444f.
Native ASan/UBSan success/refusal/ignored/partial/restore-failure tests and three
QEMU guard tests passed. Results out/update_415/uefi_host_access/.

A separate SmiFlash interface was traced offline (platform_policy/SmiFlash*).
Init26F4 registers SW-SMI20..25 and communicationGUID50EE27A5-98CD-4F2E-914D-AA399B1D3D71,
with handler20DC. Unlike FlashSmiSmm14CC, this registration has no observed
bootmode12/20 gate. The communication form only accepts20/24; SW-SMI25 reads
buffer address from savedRBX/RCX (register39/40), confirmed in EDK2 MmCpu.h.
20 enable callbacks include2C50 (same SPI reopen logic), variable backup30C8,
PM1enable changes2A48,SMIcontrol2D88,keyboard/PIC2A74,and watchdog35BC.
24 runs matching callbacks, including33D4 SetVariable restoration of saved
MACNamesListVar entries and2CEC restoring SPI protection. Therefore DO NOT call
20/24 casually as a read-only query. No such call has been performed.
Both ultimately use SMMflashGUID ECB867AB-8DF4-492D-8150-A7FD1B9B5A75. Its normal
write/erase methods still have SecSMIFlash guards. Whether enable survivesSMMexit
and can support a controlled direct writer is unproven. Static lead only.

SW-SMI25 is itself multiplexed: mode byte+4==2 selects layout-changing1614!
A planned version-only probe instead uses mode0/version0,size1B. Handler1E70
chooses no allocation,1BA0 sets version0E,18C0 takes buffer-too-small return before
flash detection,18C0 returns flag1/count0. All response fields beyond offset8
are zeroed, final bounded copy remains within an allocated4KiB buffer. Mode2 and
SMI20..24 are not emitted. Distinct one-use token/model/BIOS/PCI guards.
Source tools/uefi_smi_info.c and native wrapper tests; three QEMU guard tests passed.
EFI c0a558eb304aa624b3eb83fd27831fc1a296c1959ca40857b36de9a8ef006b7f.
Version probe is in progress at this entry; collect before declaring it complete.

### SMM version query completed (2026-11-17 23:19 local)

SMI25 version-only query succeeded: result0, version0E, buffer guard intact.
Response 1B000000010E000000000000000000000000000000000000000000.
Windows returned23:19:58.5. Temporary entry528a6348-15bb-11f1-870e-af7ba5ccfe69
removed; token consumed; bootmgr unchanged. No enable/disable or programming issued.
Results in out/update_415/uefi_smi_info/hardware-result.txt.

Offline host candidate prepared from matching live backups with exact EC-copy
guards: host_candidate_v1/host-vid-gates.UNAPPROVED.bin, SHA256
a7132dae37e0ebe8632de0da5ff4b9fa977e247781eab35c1f426bb76a0b1369.
Only five bytes change; all other live contents retained. Comparison pages are
4KiB artifacts, not proof of physical erase geometry. Not copied to laptop.

Paired SMI20/24 diagnostic remains OFFLINE pending callback/backend review.
Native core tests and three QEMU refusal guards passed; these do not emulate SMM.
Requires preboot MACNamesListVar absent to avoid saved-variable restoration.
Stock disable forces PM1 enable120; wrapper must restore original value.
Stock disable2DA8 contains an unbounded status-clear loop: wrapper cannot impose
a deadline while inside SMM. No hardware run of this new diagnostic yet.

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

### Host SPI profile diagnostic staged (2026-11-17 late evening)

Profile reader passed native ASan/UBSan and QEMU refusal guards. EFI SHA256
5809f610d60df1220f6ab97b5d375ff963bb17631d5b8805b84ba1ec5525b971.
Only 9F/05/35/15/5A/03 commands; paired SMI20/24 and verified SMI exclusion.
Reads two 256-byte SFDP passes plus three 32-byte array samples. No programming.
Initial staging refused AC absent. Recheck PowerOnline=true, Charging=false,
Discharging=true; staging then succeeded. Collect with windows_collect_smi_profile.ps1
after reboot; consult uefi_smi_profile/staging.txt for entry. Not a flash operation.

### Host SPI geometry/profile VERIFIED (2026-11-17 23:48 local)

Profile result0, commands23, restored1, SMI result0, exclusion/restoration verified.
Windows returned23:48:28.5; entry528a634c removed, token consumed, no bootsequence,
bootmgr unchanged. Artifacts: uefi_smi_profile/hardware-result.txt, result.json,
diagnostic.txt, sfdp.bin, profile-audit.json. Both SFDP passes identical, SHA256
27965e92dc1055000c116b660813b5341e9c076123642ff52627826785d9ab28.
BFPT1.6 reports16MiB,3-byte addresses,256-byte pages,erase4KiB/20h,32KiB/52h,
64KiB/D8h. All three direct03 samples (31080,3D5E0,42EF0;32bytes each) match
the mapped host backup exactly. Thus these patch-location addresses are now
validated against physical SPI reads. Status1/2/3=00/02/20. Under Winbond JW
register definitions BP=0,CMP=0,WPS=0 (array not protected by these bits),QE=1;
this does not prove programming works or identify exact chip suffix. No WREN,
erase,program,status-write commands were issued. No modified image deployed.

Decoder follows Linux spi-nor SFDP definitions:
https://github.com/torvalds/linux/blob/master/drivers/mtd/spi-nor/sfdp.c
https://github.com/torvalds/linux/blob/master/drivers/mtd/spi-nor/sfdp.h
Winbond W25Q128JW RevE manufacturer datasheet, locally saved platform_policy/
w25q128jw-reve.pdf and .txt, public mirror:
https://s3.amazonaws.com/snapeda/datasheet/W25Q128JWSIQ_Winbond.pdf

ReFlash post-update hook7F40 sends an SMM communication message with GUID
50EE27A5-98CD-4F2E-914D-AA399B1D3D71, payload byte24. That GUID occurs in
SmiFlash.efi, consistent with flash-disable cleanup, not a demonstrated EC
reload command. ReFlash then uses ResetSystem(0)=cold reset. Artifact:
platform_policy/ReFlash.post-update-hook.txt. Exact IT5125 host-to-EC reload
semantics remain unverified.

Public GV301 schematic (related QC revision, not exact QE BOM proof) shows
IT5125VG-192/CX connected to shared SPI nets through shorting links and a clock
gate. This supports shared-flash connectivity, not boot-ROM mirror semantics:
https://www.scribd.com/document/804297127/GV301QC-REV2-1

### Offline sector bundle prepared

tools/prepare_host_sectors.py consumes paired host/EC backups, the guarded
candidate, and successful direct-SPI profile log. It checks matching SFDP passes,
exact16MiB/3byte/256byte/4KiB geometry, status00/02/20, direct array samples, and
VID-only patch. Output out/update_415/host_sector_bundle_v1 includes original
and modified sectors3D000/42000 plus SHA256 for each sector and all32 constituent
programming pages. No hardware transport or flash implementation. Still UNAPPROVED.
Real profile accepted; eight negative cases refused: unequal backups, extraneous
candidate edit, failed hardware profile, changed protection, incorrect array
sample, truncated SFDP, changed density, unequal SFDP passes.
Post-profile EC status08eighttimes,zeroportwrites; see ec-status-after.txt.

Readiness boundary remains concrete: direct reads and SMM unlocking are verified,
but a bounded programming implementation and exact-chip host-to-EC reload/boot
behavior are not. Full-sector fresh direct reads are also needed before any
write; three32byte samples are not full-sector preflight verification. No external
recovery programmer is available per user. Local backup files alone cannot recover
a machine unable to boot a software recovery tool. Do not call this bundle a
validated updater. Public searches did not yield an accessible primary IT5125
boot-ROM/programming document; cross-chip mirroring remains inference.

### Stock programming path and offline failure model (continuation)

Static evidence saved in platform_policy/FlashDriverSmm.write-path.c and
.write-helpers.c. Backend487C performs sector erase;4C60 programs at most64bytes
per transaction (or1 depending on detected configuration), clips at256byte page
boundaries, and verifies/retries.4070 issues configured write-enable and polls
controller completion without independently verifying WEL. Timeout loops are
iteration based and may fall through to later verification; do not transplant
these as a bounded custom writer. No stock programming method was called live.

New tools/host_update_model.py is an OFFLINE operation generator/simulator, NOT
a hardware writer. It describes checking both complete original sectors before
first erase; per-operation WEL/ready checks; erased-byte verification;32byte
program chunks with readback; final sector comparisons and WELclear. Uncertain
operations stop with possibly incomplete sectors recorded BEFORE erasure.
No automatic retry/rollback, MMIO implementation, SSH, EFI or arming support.
It must consume the separately guarded bundle; it does not authenticate inputs.
Transport deadlines, controller cleanup on uncertain completion, and real timing
remain implementation work. Do not claim the model validates those properties.

Six tests passed in tests/host_update_model_test.py: actual-sector NOR simulation;
failure injection at every one of1300operations; stale second sector blocks all
writes; exception after erase marks uncertainty; silent program corruption stops
before next write; incomplete erase prevents programming. Outputs:
host_sector_bundle_v1/simulation-tests.txt and simulation-audit.json.

Further reset-path review: SmiFlash registration26F4 installs handler20DC both
forSW20..25 and SMM communication GUID6140 (the GUID used by ReFlash7F40).
Thus post-update payload24 reaches the same disable handler. Helper1814 invokes
a separate protocolF3224A5E-17A3-47C2-A38B-481456863C74 on saved regions; no
EC reload command identified there. Artifacts SmiFlash.post-write.c and
SmiFlash.registration.c. This does not prove absence of a platform reset hook
or silicon boot-ROM mirroring. Exact IT5125 reload remains unresolved.
No laptop reboot or firmware write was performed in this continuation.

### Full-sector preflight staged (2026-11-18)

New actual SPI C core tools/host_spi_writer.c exists, EXPERIMENTAL LOCAL ONLY.
No writing-capable EFI wrapper exists. Its HOST_SPI_PREFLIGHT_ONLY build refuses
write mode before MMIO and rejects all writing opcodes at transport dispatch.
New uefi_smi_preflight.c uses this build, embeds ONLY original sectors, and
reads full sectors3D000/42000 plus ID/status/exact SFDP. Native sanitizer tests,
all281stuck-command positions, exclusion wrapper tests and QEMU3guards passed.
Writing core local mock tests passed2853stuck-command positions, refusedWREN,
busyflash, corruptprogram/incompleteerase/stalesector and inputpatch guards.
Actual programming timing and EC propagation remain UNVERIFIED.

Read-only preflight EFI SHA256
b08338da6b0e05a6c5bb2679b13e092d4bab39fd62ac70cb15730397904165bb
staged after liveAC check PowerOnline=true,Charging=false,Discharging=true.
See uefi_smi_preflight/staging.txt for one-use boot entry. Reboot requested next.
COLLECT with windows_collect_smi_preflight.ps1 before declaring completion.
No candidate copied remotely; no WREN/erase/program issued.

### Full-sector hardware preflight SUCCEEDED (2026-11-18 00:05 local)

preflight_result0,281commands,restored1,dirty0,erases0,programs0; elapsed21008us.
Both entire4KiB target sectors matched embedded original backups byte-for-byte.
Exact live SFDP and status00/02/20 matched before and after. SMI20/24 result0;
all saved controller/exclusion/platform controls restored; cold_reset0.
Windows returned00:05:09.5; entry528a634d removed, one-use token consumed,
bootmgr hash unchanged, no bootsequence. Post-check ECstatus08eighttimes with
zeroportwrites. See uefi_smi_preflight/result.json,diagnostic.txt,ec-status-after.txt.
This validates actual C transport READ/PREFLIGHT path on target, not erase/program.
No modified bytes were deployed. The EFI embeds only original sectors and was
compiled HOST_SPI_PREFLIGHT_ONLY, rejecting writing opcodes and write mode.

The full host_spi_writer.c core is local only, without a writing EFI wrapper.
MMIO/NOR emulator native ASan/UBSan tests passed all2853stuck-command positions
and successful update, wrongID/protection/SFDP, refusedWREN, stuckbusyflash,
corruptprogram,incompleteerase,stalesector,extra-patchguard,frozenclock.
Per-command controller100ms/100000iteration bounds; flash erase1s/program10ms
bounds plus100000status-poll fallback. These are conservative experimental
deadlines; hardware program timing remains untested. No automatic retries.
Dirty mask records potentially incomplete sectors before issuing erase.
A result2 with nonzero dirty_mask MUST NOT be treated as safe ordinary boot.
UNSAFE result3 prohibits restoring controller/resetting while completion unknown.
Needs a reviewed failure/recovery wrapper before it can be deployed for writing.

### Reset-path evidence narrowed, not EC autoload proof

New stock modules/artifacts platform_policy/ResetSmi.efi/.pseudo.c, SbRun.efi,
SbRun.dis, SbRun.reset.c, RuntimeDxe.efi/.services.c.
ResetSmi handler1290 sends06toCF9 for SMI BE. SbRun runtime reset function
1680 calls table4230 entries2F00 and2F4C, then2AF0. For cold reset(type0),
**assembly2C38 sets FED80310 bit1**, then writesCF9=02,06. The decompiler
incorrectly omitted this cold-reset MMIO side effect; use disassembly as authority.
2F00's capsule-related alternate paths apply to warmreset/shutdown;2F4C writes
reset type to POSTport80. No explicit EC reset/program command identified in this
cold-reset path. This is evidence about these functions, not a whole-system proof.
Related GV301 schematic shows separate ECWRST circuitry, including long-button
reset, reinforcing that a host cold reset is not proof of EC power/reset cycling.
Source https://www.scribd.com/document/804297127/GV301QC-REV2-1 (related QCboard).
Neither this nor the matching autoload header establishes exact IT5125ROM behavior.

NDS32 objdump recovered at /tmp/flow-binutils/build/binutils/objdump; old
/tmp/bu-src/inst/bin/nds32le-elf-objdump path is absent.

Local writer review fixed two error-reporting cases after the successful hardware
preflight build: final full-sector comparison failure re-marks that sector dirty;
final profile failure preserves its actual failing opcode/address. Added emulator
tests for both. Full sanitizer suite passed again, including2853stuck-command
positions; log host_sector_bundle_v1/mmio-writer-tests-final.txt. These source
changes were NOT rebuilt/deployed to laptop; hardware EFI hash remains b08338...
Read-only success behavior unchanged. No writing-capable EFI artifact exists.

### Stock-data programming tests prepared (2026-11-18 continuation)

Implemented separate host_spi_stock_program (one existing 32-byte block at3D5E0,
no erase) and host_spi_stock_restore (erase/restore original sector3D000 only).
Both compare full sectors3D000/42000 before and after. Modified input unsupported.
Build-time guards independently reject other programming modes/addresses/opcodes.
Generic modified writer remains local. Both new EFI wrappers halt before restoring
SMI/IF or invoking EFI when flash completion is uncertain or dirty_mask!=0.
No automatic reset/retry/rollback. A halt contains failure, it does NOT recover it.
Stock erase/restore remains experimental and local; no deployment script for it.

First hardware step is same-byte program, following user's proceed instruction.
EFI out/update_415/uefi_smi_stock_program/smi-stock-program.efi SHA256:
aadc3dd37b33a73a98c9a8d0368889e80f84c9d9902c04767333bd643d863d63.
Native ASan/UBSan: all671 same-byte command fault positions,1695stockrestore,
2853modifiedwriter; refusal/protection/corruption/busy/frozen-clock guards passed.
Wrapper fault tests cover halt before SMI/IF restoration and original IFclear.
QEMU wrong-model/not-armed/wrong-bridge guards passed. Logs in new artifact dir.
Live Windows AC=true,Charging=false,Discharging=true,RemainingCapacity43845.
Preparing first same-byte hardware test; no result yet. No erase authorized by
this binary. Unchanged readback cannot alone prove physical programming occurred.

### Same-byte hardware program SUCCEEDED (2026-11-18 00:21 local)

stock_program_result0,670commands,restored1,dirty0,erases0,programs1,50250us.
WREN followed by WEL=1 verification, page-program existing32bytes at3D5E0,
completion/WELclear, full-sector comparisons and final exactprofile all passed.
No data change intended/observed; not proof erased cells can be programmed.
SMI20/24 result0; PMenable4000->120->4000, all controls restored. Windows returned
00:21:17.5; entry528a634e removed, token consumed, no bootsequence, bootmgr unchanged.
Postcheck ECstatus08eighttimes. Artifacts uefi_smi_stock_program/result.json,
diagnostic.txt,ec-status-after.txt,input-audit.json. No modified firmware deployed.

Next hardware step under user's proceed instruction: erase sector3D000 ONLY,
verify allFF, restore original4096bytes in32-byte chunks, verify full sector and
both target sectors. Experimental first erase: failure may need a programmer.
Separate stock-restore EFI rebuilt after shared-core changes; current SHA256
c0437bdb01d8339a98d1bb6236d4ea9f5b8ff6e5047d30d9245524e1d43a7eb2.
Native fault suite and final-binary QEMU3 refusal guards passed. Only original
sector payload embedded. Modified-input API refused by compile-time mode guard;
transport rejects writes outside3D000. Staging/collection scripts now exist.
No actual erase result yet. Collect result before claiming writing validated.

### REAL HOST-FLASH ERASE/PROGRAM VERIFIED (2026-11-18 00:22 local)

Stock-sector hardware test SUCCEEDED. stock_restore_result=0,commands=3183,
restored=1,dirty_mask=0,erases=1,programs=128,elapsed_us=134589.
This is an actual physical-content test, not just an unchanged program:
- Full original sectors3D000/42000 compared before first write.
- Sector3D000 erased with20h; every byte verified FF.
- Original4096bytes restored with128x32-byte02h programs, WREN/WEL checks,
  bounded completion checks and per-chunk readback.
- Entire restored sector and both target sectors verified against originals.
- Final ID/status/SFDP matched; controller state restored.
Thus software erase/program works for the tested host SPI NOR sector. This is
NOT a demonstration of EC-internal programming or IT5125 host-to-EC reload.
No modified image was deployed; current firmware contents remain stock at targets.

SMI20/24 result0; control4F0C2105->4FCC2105->4F0C2105,restriction600->0->600;
PMenable0->120->0, PICFF and AC40 unchanged, buffer guard intact. SMI9B1D/B240
restored; IF restored by wrapper. No emergency reset. Windows returned
2026-11-18T00:22:57.5000000-05:00. Temporary entry
{528a634f-15bb-11f1-870e-af7ba5ccfe69} removed; one-use token consumed;
no bootsequence, S: unmounted. Windows bootmgr SHA256 unchanged3f8a4c...8511.
Artifacts out/update_415/uefi_smi_stock_restore/result.json,diagnostic.txt,
hardware-result.txt,build-audit.json,native-tests.txt,QEMU guard logs.
Postcheck ECstatus08eighttimes; latest power in power-after.txt.

Remaining scope: modified two-sector deployment and exact EC reload behavior.
The real write route is now demonstrated; do not continue describing it as only
simulated. Conversely do not claim modified EC execution or verified recovery
from a failed erase: neither was tested. No recovery programmer is available.
New stock wrappers avoid old read-only wrapper's cold-reset-on-fault behavior.
No diagnostic is armed. Keep the working stock EFI artifacts/hashes unchanged.

### VID-only updater prepared (2026-11-18 continuation)

User explicitly said proceed after two-sector write + EC readback plan and reload
risk explanation. New uefi_smi_vid_patch.c uses hardware-tested writer transport,
but this is the FIRST modified-image deployment. Compile HOST_SPI_VID_PATCH_ONLY
refuses stock-test write APIs. Payload audited against both fullhost/internalEC
backups; exact five changes3D5EE,3D5F0,3D5F1,42EFA,42EFB. Both secondary==100
predicates unchanged. New prepare_vid_patch_payload.py reproduces audited header.
Two-sector wrapper halts on ANY post-mutation failure, including verified first
sector followed by second-sector refusal; no normal boot with a partial patch.
Native ASan/UBSan all2853command fault positions + critical-section partial/final
failure tests passed. QEMU wrongmodel/notarmed/wrongtoken/wrongbridge guards passed.
EFI09c45b4d540ca4ce5a03c56f3c2422c33273c2a4d68c985750cec516482d6a23.
Audit/logs out/update_415/uefi_smi_vid_patch/. Not yet a hardware success.

Original EC backup files/logs deliberately archived on ESP/TEMP under
backup-before-vid-20261118 after verifying both binary hashes; local originals
unchanged. The identical proven backup EFI75af4282...d9935 and scripts are ready
for fresh post-patch internal readbacks. No backup diagnostic armed yet.

### MODIFIED HOST FIRMWARE WRITE SUCCEEDED (2026-11-18 00:31 local)

vid_patch_result0,5842commands,restored1,dirty0,2erases,256programs,229308us.
Both original sectors matched before erase; both MODIFIED sectors verified after
programming. Exact ID/status/SFDP final profile matched. SMI20/24 result0; dynamic
control4F0C1096->4FCC1096->4F0C1096; restriction600->0->600; PMenable0 restored;
SMI9B1D/B240, PICFF, AC40 restored, buffer intact, cold_reset0.
Windows returned2026-11-18T00:31:06.5000000-05:00. Entry528a6350 removed,
token consumed, no bootsequence, bootmgr unchanged. ECstatus08eighttimes.
Artifacts uefi_smi_vid_patch/result.json,diagnostic.txt,ec-status-after.txt.
This is actual deployment of the reviewed five-byte patch to HOST flash.
EC-internal propagation and charger behavior remain UNVERIFIED.

Proven internal EC backup EFI now staged for post-patch verification, one-use
entry528a6351-15bb-11f1-870e-af7ba5ccfe69, AC check passed. Reboot next; expected
~6minutes for two192KiB passes. Collect via windows_collect_ec_backup_probe.ps1,
copy outputs to LOCAL out/update_415/ec_after_vid_patch (NEVER original backup dir).
Run tools/audit_ec_patch_readback.py there. No new flash writes in this backup.

### Post-patch EC internal readback: STILL STOCK (2026-11-18 00:38)

Proven reader completed two full192KiB passes: result0,completed2,380965552us,
chip51,25,02,00,00,00; incomplete0,dirty0,restored1. Windows returned00:38:43.5;
entry528a6351 removed, token consumed, no bootsequence, bootmgr unchanged.
Both files hash5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478:
EXACT STOCK, zero changed bytes. BranchesC5EE=4C00C008,11EFA=D90B remain stock.
Modified host write did NOT propagate into the measured internal EC window after
ordinary reboots. Do not claim the EC patch is running or charger behavior fixed.
Artifacts out/update_415/ec_after_vid_patch/ including patch-readback-audit.json.
Auditor tests reject partial/unequal/failed/wrongchip/unrestored data.

Read-only host persistence verifier built/tested, EFI
0e230e4e07fdc6daf3bae2e8f2f606a737c012c16239664331735a4323d989b5.
HOST_SPI_PREFLIGHT_ONLY, compares both sectors against modifiedpayload; no writes.
Native core/exclusion tests and QEMU4guards passed. Staged for nextboot; collect
windows_collect_smi_vid_verify.ps1 before deciding whether hostcopy persisted.

### Modified host copy PERSISTS; physical EC reset requested (2026-11-18 00:40)

Post-reboot host verifier SUCCEEDED: vid_verify_result0,281commands,restored1,
dirty0,0erases,0programs,21008us. Both complete sectors3D000/42000 match MODIFIED
payload after multiple Windows restarts. Thus host patch was not reverted while
internal EC readbacks still match STOCK. All SMI/platform controls restored.
Windows returned00:40:40.5,entry528a6352 removed,token consumed,no bootsequence,
bootmgr unchanged. ECstatus08eighttimes. Artifacts uefi_smi_vid_verify/result.json,
diagnostic.txt,ec-status-after.txt. No diagnostic currently armed.

Post-patch STOCK EC readouts/logs archived after hash verification on ESP/TEMP
under backup-before-ec-reset-20261118; local ec_after_vid_patch retained unchanged.
Proven backup reader is ready for a fresh read AFTER a physical EC reset. Do not
overwrite either original backup folder or ec_after_vid_patch; use a new directory.

ASUS official FAQ https://www.asus.com/support/faq/1050239/ reviewed: shutdown,
disconnect external peripherals, connect adapter, hold power40seconds;20second
models flash after~15seconds and release after flashing stops; then poweron.
This is a documented EC/RTC reset, NOT a guarantee of IT5125 autoload. User was
asked asynchronously to perform it and report Windows back. No answer yet.
Physical button action cannot be performed over SSH; no FE/watchdog/register
reset was attempted. Once user confirms, check current BIOS/SecureBoot/AC and
repeat proven internal backup, then audit against stock/patched full192KiB.
The behavioral third-party-charger test remains pending EC propagation evidence.

### Readback after user's reported reset/reboot staged

User replied "its rebooted" after the physical EC-reset instructions. Treat this
as the reported post-reset state; the button duration itself is not observed.
Windows reachable, BIOS415, SecureBoot=false; staging verified BitLockerOff and
exact model. AC=true,Charging=false,Discharging=true,RemainingCapacity43160.
Windows LastBootUpTime still reports1794980440500 (prior00:40:40.5); this may
reflect hybrid shutdown/resume and does not independently prove/refute EC reset.
Same proven backup EFI75af4282...d9935 staged entry528a6353-15bb-11f1-870e-af7ba5ccfe69.
Reboot next, two complete192KiB reads, no firmware programming. Collect with
windows_collect_ec_backup_probe.ps1 into NEW local ec_after_physical_reset.
Never overwrite original/previous readouts. No result yet.

While post-reported-reset readback runs: offline saved-host scan finds exactly one
64-byte EC vector prefix (31000), one16-byte header(31080), and one occurrence of
each32-byte patch context(3D5E0/42EF0). Three firmware-tag strings occur inside the
same EC region. This rules out a second exact uncompressed copy in that saved host
readout, not compressed images or a different physical storage device.
Audit ec_after_physical_reset/stock-host-copy-scan.json. Cross-version saved
capsules413/414 carry EC30B;415/418 carry identical192KiB EC30C. Version-based
update gating remains only a hypothesis; no version/header bytes were changed.


### INTERNAL EC PATCH CONFIRMED (2026-11-18)

This supersedes earlier pending/stock/not-deployed state.
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

### First post-patch charger observation (2026-11-18)

User reports external meter appears capped around30W; charger/cable, voltage,
current and workload not yet supplied. Read-only Windows query: battery63%,
PowerOnline=true,Charging=true,Discharging=false,Voltage15938,ChargeRate32688,
DischargeRate0,RemainingCapacity27875; Balanced power scheme. These readings
are not a synchronized measurement of USB input and do not establish a PD cap.
Both secondary==100 gates remain; their live values are not yet measured.
Patch presence is verified, behavioral success remains unresolved.

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

### Charger bypass sequence and latched-failure lead (2026-11-18, offline)

Bounded disassembly in out/update_415/boot_policy_analysis establishes that
patched VID acceptance at capsule436FA does NOT directly set charger current.
With retained class==100, it calls4392C(1)/4393C(1), setting RAM81241(enable)
and81242(step)=1. Dispatcher47D52 runs a multi-step charger configuration.
This corrects the earlier loose description of these routines as accept handlers.
All code addresses here are capsule offsets; subtract31800 for runtime code.

Transport476F8/4771C calls49E62/49E42 with channel3,address argument12h,
command and word value/destination. Nonzero return increments RAM81251/81250
write/read error counters. Sequence handlers generally advance without checking
that transport result. This is static behavior, NOT evidence of actual bus errors.

The register map AND transition sequence strongly match Renesas ISL9241:
39Control0,3AInformation1,3CControl1,3FAdapterCurrentLimit1,47ACProchot,
4CControl3,4EControl4. Exact live charger identity/sense resistance unverified.
Primary references:
https://www.renesas.com/en/document/apn/r16an0002-isl9241-operation-modes-application-note
https://www.renesas.cn/zh/document/dst/isl9241-datasheet?r=499331
Application note archived locally with SHA in artifact-sha256.json. Official
online datasheet returned different revisions across requests; preserve revision
when downloading for future comparisons. Reference is chip-family comparison,
not authority to apply arbitrary register writes to this board.

Key static sequence: set39 bits6,5; set4Cbit0; write40=2E5A; set3Cbit6;
write15=4780; set39bits11,12; step13 polls3Abit3 until clear; write15=0;
clear3Cbit6; pulse39bit13; set4Ebit10. These align with NVDC-to-bypass
transition operations in the Renesas application note. No need to interpret this
as a PD-controller command sequence. Exact transition timings remain unmeasured.

At step13 (47AE6), if3Abit3 stays set and retry counter81244 reaches budget,
firmware sets81243=1 and clears81241. Budget is1 when getter400C8 returns5,
otherwise6; getter reads80410, whose numeric state meaning remains unproven.
Dispatcher then refuses further progress while error!=0 or enable==0. A source
selection at43636/4363A clears error/counter through43954->47D3E. This gives a
concrete latched-failure/reselection mechanism consistent with, but NOT proven by,
replug recovery. Do not call the budget milliseconds or assume state5=Windows.
Routine47B98 contains another failure path but dispatcher skips it (step17 simply
advances18); do not count that unused helper as an executed check.

Completion43334 requires !reset-busy and step21. It writes RAM81206 to command3F
via4389A->477B8, then107% of that value to47 via438D2->47926 (clamped10000),
sets3E=1800, clears step, updates charging flags, sets15=4580 and14=0.
Separate pending-bit81208.bit0 path also writes3F/47/3E. Thus a failure before
completion can prevent this current-setting path; PDST qualification alone cannot
prove completion. Register units depend on actual silicon and sense resistance.
Do not infer that the observed1.525A is necessarily the documented1.5A default
of a DIFFERENT register3B. Neither current-limit register has been read live.

Scheduler447BA calls PD/attach work,437A2,437BE,43816,4390C before a
state-dependent indirect task. Thus these charger routines are not simply absent
outside one OS state; boot-specific cause and ACPI B1 command linkage unresolved.

Next discriminating measurements: EC RAM81202..11,81240..46,81250..51 and80410
in low/high states; actual charger identity,3Astatus,3F/3Blimits,39/4C/4Emode,
and PD contract. Targets recorded in diagnostic-targets.json. Need a validated
Windows-safe read transport/serialization before acquiring them; ordinary PMC92
only maps RAM offsets<1000, so do NOT send1240 expecting81240 (it maps MMIO).
No new live access, reset, flash, service change or register programming this work.
No demonstrated fix yet; do not bypass status checks or force the sequence onward.

### Windows read-route audit (2026-11-18 08:12+, no raw hardware access)

SSH inventory confirms modelGV301QE/BIOS415, AC=true,Charging=true,
Discharging=false,RemainingCapacity34983,ChargeRate23062. AsusSAIO and PawnIO
running. Evidence boot_policy_analysis/windows-access-inventory.json. These are
battery telemetry, not meter input or proof of high-power state.

Existing signed LpcACPIEC module cannot implement direct probe: source permits
only62/66. Current upstream LpcIO source selects only2E/4E and allows25C/25D;
it warns ASUS driver's private kernel mutex is inaccessible to userspace. Do
not treat a user mutex as exclusion or force a BAR scan to gain unrelated ports.
Upstream sources archived as LpcACPIEC.p/LpcIO.p from
https://github.com/namazso/PawnIO.Modules (main; not claimed identical to installed
release). No new module loaded and no driver unloaded.

Decoded a more useful existing-driver lead: saved AsusSAIO IOCTL80102070 accepts
16-byte buffer and callsRVA1AA0, which acquires its kernel mutex via import40C0
(KeWaitForSingleObject), calls1828, then releases via40B8(KeReleaseMutex).
Buffer:byte0OEMcommand,byte1outboundcount,bytes2..9payload,byte10responsemode,
byte11returnedbyte,DWORD12status. Transaction sendsFF thenOEMcommand via25D
with status25C, then payload; response mode controls returned data. This is an
OEM mailbox operation, NOT arbitrary port I/O or an established read-only command.
Never exceed8 payload bytes; no IOCTL invoked. Private mutex does not by itself
prove exclusion against SMM/other firmware users.

DSDT RRAM(Arg0,Arg1) is serialized, acquiresMU4T, setsCMD=FF,EDA1=Arg0,
EDA2=addressHigh,EDA3=addressLow, thenECAC(SMM). Existing callers useCC with
0010/0030. The name is not proof thatCC can read81240 or arbitrary RAM; mapping
and exact firmware handler unresolved. RP2E is locked toC9 and excludes40..6F;
REBC executes a mailbox command and can change mailbox flags even on timeout.
Neither is a general passive read primitive. AsusWmiAcpi import inspection shows
IoWMIExecuteMethod, not an established arbitrary AML evaluator.

Preserved decoded interface and exact excerpts in boot_policy_analysis/
{windows-read-route-audit.json,asussaio-transaction.txt,acpi-read-paths.txt}.
Next concrete task: resolve OEM FF/CC handler/address translation and compare
live driver hash before using stock serialized transport, or validate a dedicated
read-only ACPI/driver route. No sequence/current snapshots acquired yet. Do not
reboot just to reuse UEFI reader: that could destroy the low-state observation.
No firmware, registry, service or device changes; no diagnostic armed.

### OEM CC resolved; passive PD capture prepared (2026-11-18 continuation)

Firmware dispatcher42086 indexes128 eight-byte entries at capsule507B8
(runtime1EFB8), command minus80. CCentry50A18 has payloadlength2 and
runtimehandler10342 (=capsule41B42). Handler requires payload0==0,
payload1<FF;41254 reads flags/pointer table at51270(runtime1FA70),255entries.
It checks flagsbit0 before dereferencing. CC10 points80650, CC30 points80610.
No entry points81200..8125F. Saved oem-cc-table.json. This closes the proposed
arbitrary-RAM CC route; do not invoke CC with invented addressing.
B1 handler416C0->42322 sets GP+212B forarg3, clearsforarg4;arg2noop.
No demonstrated charging-policy linkage yet.

New passive-capture lead: https://github.com/okhsunrog/km003c-rs commit
c063388ecab1a169326de1b57e650a3a45fc94f6 supports KM003C PD monitor10/11
and GetData0C attribute10. Default library connection resets USB and detaches
all interfaces; do not run defaults. Minimal tools/km003c_pd_capture.py instead
has fixed passive-command allowlist, no USB reset/configuration/PD trigger,
claims only vendorinterface0 and restores kernel binding; saves raw TX/RX.
Syntax/help validated, hardware UNTESTED. Raw decoding remains to be added
against actual responses; no PD negotiation has been captured.

Actual access attempt get_active_configuration() failed Errno13; USBnode
/dev/bus/usb/005/002 root:root0664, no user ACL. sudo -n requires password.
Thus user action genuinely needed to permit USB access. Proposed narrow command
on LOCAL Linux logger computer: sudo setfacl -m u:ar:rw /dev/bus/usb/005/002
(verify device still5fc9:0063 if replugged). This grants access only to current
meter node; unplug/replug removes it. No meter commands, firmware writes,
laptop reboot, driver detach or capture performed in this continuation.
Next: after ACL, bounded30s passive test, confirm cleanup/kernel hwmon restore,
then decode source caps/RDO and arrange controlled boot capture if needed.

### Passive KM003C monitor accepted; waiting for negotiation (08:35 EST)

User granted local USB ACL. tools/km003c_pd_capture.py completed30s monitor
session: Connect+EnablePdMonitor accepted,1456 PD-status packets parsed without
errors; ZERO event records. Voltage~19.4V, absolute inputpower median18.689904W,
min17.970882/max41.669243W. Negative IBUS is direction, not discharge proof.
No actual Source_Capabilities/Request observed; cannot infer contract/currentcap.
Evidence pd-passive-first.jsonl and pd-passive-first-summary.json in
out/update_415/boot_policy_analysis. New decode_km003c_capture.py validates
framing/status/events and retains eventwire bytes; PD semantic decoding pending.

Disable-monitor accepted. Disconnect response was opcode03/id1 rather than
Accept/transaction echo; old script logged cleanup timeout but restored powerz.
Fixed Disconnect to send-only, as upstream CLI does. Kernel binding and live
hwmon voltage/current confirmed restored. No laptop restart or firmware change.
Fresh Windows battery: AC=true,Charging=true,RemainingCapacity41215,
ChargeRate10678,Voltage15938. Battery taper/workload can explain low input;
not claiming a30W restriction from this interval.

Started900s passive capture pd-reconnect-capture.jsonl. User needs to unplug
charger connection AT LAPTOP for10s then reconnect with third-party charger,
keeping meter debugUSB connected to localLinux. Capture is bounded and cleans
up automatically; if window expires, restart before requesting action. Runtime
session45367 was first capture(completed); use current active session for new.

### User returned; laptop off, meter absent from local USB

The900s capture ended normally at2026-11-18T13:51:32Z, with disable accepted
and kernel cleanup complete.43543 valid status samples,zeroPD event records,
zero decoder errors; input median16.553508W,min13.2192,max51.62216.
Summary: boot_policy_analysis/pd-reconnect-summary.json. No negotiation captured.
User now reports laptop off. Fresh lsusb shows no5fc9:0063 meter; capture cannot
be restarted until debugUSB is reconnected. No capture currently running. Ask
user to reconnect meter debugUSB to localLinux while leaving laptop off, then
recheck enumeration/ACL and arm capture BEFORE next laptop boot. Previous node
ACL is not assumed to survive USB re-enumeration. No firmware changes.

Meter reconnected by user with third-party power and laptop off. It enumerated
as003/004 (5fc9:0063). Coldboot capture attempt refused at USB open withErrno13;
prior per-node ACL did not survive reconnect. No capture started/no outputfile
created/no meter commands sent. Need local ACL on/dev/bus/usb/003/004 before
arming pd-coldboot-thirdparty.jsonl; keep laptop off until capture acceptance.

User renewed ACL on003/004. Started900s passive capture
out/update_415/boot_policy_analysis/pd-coldboot-thirdparty.jsonl, execution
session34175. Connect and EnablePdMonitor accepted; status packets arriving.
User reports laptop off, third-party charger connected. Next user action: power
on normally and report Windows arrival, keeping all cables attached. No laptop
reboot remotely requested or firmware write performed. Capture auto-cleans up
at deadline; check process/log before claiming it remains active.

### Boot/resume observed during active passive capture (11:40 EST)

User reports Windows up. Meter input~0.62W while off, peak79.97W around16:40:00Z,
then~29.35W median in16:40:10 ten-second bin; later falls/varies below30W.
No PD event records yet: capture accepts monitoring and returns measurements,
but actual packet capture capability on this meter remains unvalidated.
Fresh Windows status11:40:35EST: ACtrue,Chargingfalse,Dischargingtrue,
RemainingCapacity42506,DischargeRate26457. LastBoot1795002647500 corresponds
2026-11-18T11:50:47.5Z=06:50:47.5EST, predating this startup. Do not call this
confirmed full cold Windows boot; possible FastStartup/resume.
Evidence pd-coldboot-{summary.json,windows.txt,dsts.json}. Capture still active
at this entry; next physical laptop-end charger disconnect10s/reconnect should
force a fresh attach negotiation and validate whether events can be captured.
Keep debugUSB attached and Windows running. No new firmware operation.

### Actual third-party 100W PD contract captured after reconnect (11:41 EST)

User disconnected/reconnected laptop power and reported battery96%. Capture
now contains42event records andzero parser errors. Disconnect meter305474ms,
connect321398ms (~15.9s). SOP0sourcecap321662ms initially offers20V0.75A;
laptoprequestsPDO4 RDO43812C4B(0.75A),Accept321672,PS_RDY321850.
Updatedcaps322557ms:PDO4=000641F4=fixed20V5A. LaptopRequest322563ms,
wire8212f4d18743,RDO4387D1F4 selectsPDO4,operating5A,maximum5A.
SourceAccept322567ms andPS_RDY322694ms complete100W negotiation.
OtherfixedPDOs5V3A/9V3A/15V3A; APDO retained raw without fixed-supply decoding.
Definition cross-check: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/include/usb_pd.h
Evidence pd-reconnect-contract.json,full pd-coldboot-thirdparty.jsonl and summary.
This verifies post-reconnect Windows100W CONTRACT with user's third-party setup,
NOT100W delivered input, not internalcharger current setting, not boot behavior,
and not proof that patch caused the contract (no stock A/B).

96%battery is a major charging-demand confounder. Do not diagnose lower input
as30W restriction from this run. Need comparable test at lowerSOC (e.g.50-70%)
or controlled sustained demand; capture fresh attach before full Windows boot,
then compare contract and delivered power. Prior boot emitted noPDrecords and
retained previousWindowsLastBoot, so Windows-init causality remains unproven.
No further firmware patch justified by this run.

Stopped capture withSIGINT at16:42:58Z; Disable accepted,Disconnect sent,
cleanup_complete,true, powerz reattached at3-1:1.0 (otherdrivers intact).
No active capture/firmware operation. User may let battery drain normally before
next controlled run; do not run an unannounced load simply to demand100W.

### Low-battery attach capture armed (2026-11-18 13:29 EST)
User renewed ACL003/005. Passive capture active900s,session96202,file
boot_policy_analysis/pd-50pct-attach.jsonl. Connect/monitor accepted; samples
arriving. Filename reflects user's earlier50% report, NOT current measuredSOC:
fresh Windows13:29:21 reports10%,RemainingCapacity4526,ACfalse,Dischargingtrue,
DischargeRate20034. Saved pd-50pct-before.txt. Tell user to connect third-party
power now and stop benchmark if still running. No reboot/firmware operation.

### Low-SOC third-party:100W contract,~29W sustained input (13:30-31 EST)

User attached third-party and confirms benchmark stopped. UpdatedSourceCaps
meter157741ms:PDO4=000641F4(20V5A); laptopRDO4387D1F4at157746ms,
operating/max5A;Accept157751ms,PS_RDY157878ms. Thus100W negotiation succeeds
in this lowbattery state too. Evidence pd-lowbattery-contract.json and raw
pd-50pct-attach.jsonl (filename50pct is stale label; Windows measured10%).
Steady16? correction:18:30:10..18:31:04UTC,54seconds,2603samples,input median
29.36024W,min28.799286,max68.845014W (briefspikes). Not a hardinstantaneous30W
ceiling. Windows13:30:18ACtrue,Chargingtrue,10%,batteryChargeRate3522;
13:31:04ChargeRate9865,RemainingCapacity4494. DSTSstill0001002A.
BatteryTemperatureclass returns noinstances. GenericACPI THRM_0=3232(raw),
not batterytemperature; cannot ruleout batterythermal limiting afterbenchmark.
Files pd-lowbattery-{connected.txt,thermal.txt,steady.json,steady-windows.txt}.

Next discriminating pairedtest: swap to stockASUS throughmeter without reboot,
leave debugUSB connected,Windowsidle. Compare steadyinput/batterycharge under
nearbySOC/thermalconditions; this needs userphysicalswap. Capturestillactive
session96202 until15min deadline18:44:12Z. No firmware/ECregister changes.

### Paired ASUS adapter result at lowSOC (13:32-33EST)
User switched to stockASUS whileWindowsidle; capture uninterrupted. ASUSSourceCaps
18:32:01Z PDO4=000641F4(20V5A); laptopRDO4387D1F4,Accept,PS_RDY captured.
Same requested100W contract as thirdparty. ASUS input18:32:10..53 median70.13051W,
min65.584424,max93.87096; median19.846V3.532A. Thirdparty18:30:10..18:31:50
median29.345575W,median19.243V1.525A (briefspikes68.845014W).
WindowsASUS11%battery initially,ChargeRate28.784W then38.203/42.968/45.407/
46.618W at13:32:33/43/53,13:33:03. Actualbatterycharge clearly increased.
Artifacts pd-lowbattery-adapter-comparison.json,pd-lowbattery-asus-contract.json,
pd-lowbattery-asus-{windows.txt,settling.json}; rawsharedpd-50pct-attach.jsonl.

This comparison rules out a lower requestedPDcontract for observedthirdparty
state. Supports adapter/path/state-dependent difference aftercontract; does not
alone isolateECpolicy versus source/cableelectrical behavior or timer/thermal
recovery. Next reverse swap to thirdparty in sameWindows session tests whether
lowstate recurs versus recovery persists. Keepcapture/debugUSB andidle state.
Capture96202stillactive until18:44:12Z; no firmware/register programming.

### A/B/A confirmed: thirdparty returns to~29W (13:33-34EST)

Reverse swap completed. Thirdparty initiallyrequests20V0.75A, thenupdatedPDO4
000641F4,Request4387D1F4 atmeter373142ms,Accept373147,PS_RDY373273.
18:33:40..18:34:25UTC45s sample median29.29089W,median19.246V1.522A,
min28.736664,max53.38089. Battery13%,charge6.247->7.793->8.463->9.036W.
Thus A/B/A ~29/70/29W withsame100W negotiatedcontract andlowSOC; not simply
highSOCtaper or steadily improving thermal/time condition. Stilldoesnot prove
specificECgate or ruleout charger/cable/electricalpath. Stockmedian19.846V,
thirdparty19.246V; voltagedifference alone is NOT measuredcabledrop orproofVIN
loop limiting. UpstreamISL9241definitions identify VINlimit4B/control39bit2 and
ADC87, additional readtargets if validatedtransport becomesavailable. Actual
chargerICidentity stillunread. Source:
https://chromium.googlesource.com/chromiumos/platform/ec.git/+/08f5a1e6fc2c9467230444ac9b582dcf4d9f0068/driver/charger/isl9241.h
Artifacts pd-lowbattery-thirdparty-return.json,return-steady.json andfullraw.
Asked user exactthirdparty model/port/cable and whetherotherloads sharebrick;
answerpending. Need this to pick controlledcable/sourcecomparison ratherthan
assumingremainingfirmwaregate. Passivecapture stoppedwithSIGINT after A/B/A;
verifycleanup inrawlog. No firmwarewrites/currentsettingchanges/reboot.

User identifies third-party as Anker747, laptop its only load, using port marked
for laptop. Temporarily unplugged to read model after capture stopped. Official
Anker747A2340 spec lists each USB-C port single-use20V5A/100W, matchingcapture:
https://service.anker.com/product-description/a085g000004xGpeAAE
No sharedload per user, so ordinarymultiportbudgetsplit unsupported as cause.
Exactcablemodel/rating stillmissing; obtain beforecontrolledcableswap. A100W
PDcontract doesnot byitself prove lowresistance cable/contact performance.
Meter stillenumerates003/005; capture remainsstopped. No newhardwareoperation.

### Post-contract current dataflow trace (13:45EST, offline)

User says cable100W-ratedAnker,asksfocusfirmware. No cabletestrequired thisstep.
New boundeddisassemblies: current-refresh.dis,pd-accessors.dis,charger-init.dis,
charger-dispatch.dis; post-contract-dataflow-audit.json recordsaddresses/hash.
48620 reads PDcontrollercommand35,extracts10bitfield*10 into81310+8*port;
48668command34 extractsvoltage*50 into81312+8*port.48782 computes wattclass,
withdouble0.001factors,caches81314+8*port andreturnslowbyte.485DE computes
0.95*rawcurrent below4921,otherwise4674,stores81320+2*port;48812returnsit.
4351C cachesclass81203,voltage81204,current81206. Therefore100Wwirecontract
is not itself a live observation of81203/81206; internalrefresh couldfail.
486B6 marks changebit5 at81300+port afterrefresh;4358E detects and reselects
whenclassrises (15->100 qualifies),then43954clears transitionerror/counter.
This argues AGAINST claiming firmwarehasno15->100updatepath; race/failurestill
possible but notproven. Anker initial15Wthen100W vsASUS immediate100W is measured.

43334 applies81206->charger3F onstep21 orpending81208bit0.47AE6 can latch
81243=1/clear81241 if3Abit3doesn'tclear, preventingsequencecompletion. This
remainsSTATICpossibility,not liveconfirmedcause. Reset47C5Esets4Cbit2,then
waits40counterunits; do notinterpret as40ms. Init43258writesControl2(3D)=6300
fromcapsule55FF2; doesnotitselfestablishwhichcurrentlimitactuallyused. Duplicate
older4755Atransport exists but no directcaller/pointer found for4763A current
wrapper inboundedscan; do nottreatit asactivecompetingwriter.
No newcandidate/flash orspeculativeECregisterwrite.

FreshWindows13:45:54:17%,ACtrue,Chargingtrue,batterycharge9.817W,oldLastBoot.
Need lowSOC BIOS-vs-Windows capture to distinguishpreOSavailablepower fromOS
startuptransition; earlierreportedBIOS90W wasn'trecorded in this paired lowSOC
run. Started900s pd-lowbattery-bios-windows.jsonl; passive monitor accepted.
User to Restart (not hybridshutdown) intoBIOS withAnkerconnected,wait~30seconds
andreportthere BEFORE exitingtoWindows, keepingdebugUSBconnected. No remote
rebootrequested; awaitingphysicalBIOSnavigation. Captureautoends15minutesafter
start. Needverifyprocess/logbeforeassumingcaptureactive inlaterturn.

CORRECTION to previous capture-start entry: BIOS capture did NOT start.
InitialConnect got queuedDisconnect03/id1; driver restored. Added bounded
pre-connectdrain and upstream-style3Connectretries. Preservedfailedattempts in
pd-lowbattery-bios-windows{,-v2,-v3}.jsonl. V2drainedAccept05/id1 thenConnect
received03/id2. V3Connect replies laggedrequestIDs(Connect2->Accept1,
Connect3->Accept2), so correctlyrefused monitoring ratherthan acceptingstaleACK.
NoEnablePdMonitor sent in these attempts; powerzrestored. No USBreset performed.
Need user brieflyreplug ONLY meterloggingUSB to resetloggingconnection, leaving
chargingpathalone; node/ACL maychange. ThenverifycapturebeforeBIOS request.
Capturetool modifications notyet validatedonfreshsession; bounddrain16reads25ms,
Connect3retries. Do notclaim BIOScaptureactive. No laptoprebootrequested.

User replugged loggingUSB; meter003/006,ACLrenewed. Freshcapturev4started:
pd-lowbattery-bios-windows-v4.jsonl,session79128,900s. Connect/monitoraccepted,
PDstatussamplesarriving. Boundeddrain/retrytool succeeds onfreshUSBsession.
Next userphysicalRestartintoBIOS,keepAnkeranddebugUSBattached,remaininBIOS
~30sandreportbeforeexiting. No remote reboot or firmwarewrite performed.

User delayed restarting ~hour. v4captureendednormally19:04:14Z,cleanupcomplete;
no userrestart duringthatcapture. Meterremains003/006. New900scapturestarted
pd-bios-windows-v5.jsonl,session3852,monitoraccepted andsamplesverified.
FreshWindows48%,ACtrue,Chargingtrue,batterycharge8.510W. SuitableSOC forBIOS
comparison. UsertoRestartintoBIOSnow,wait30sandreportbeforeexitingWindows.
No newpermissionneeded orremoterebootrequested.

User reports enteredBIOS onwarmrestart withAnkerleftattached (receipt~20:18:55Z).
Capturev5remainsactive. Inputstays~29.4W,noPDevents observedthroughentry; lowstate
persistsintoBIOS withconnectionretained. ThusBIOSalone doesnotimmediatelyrestore
highpower. DoesnotdisproveOS-createdlatchedstate orattach-timepolicy. Needfresh
laptop-endpowerreattach WHILEremaininginBIOS toobservepreOSinitialization.
Markerfilepd-bios-windows-v5-markers.jsonl, summarybios-retained-connection-summary.json.
Next useractionunplugpower10s/reconnectAnker inBIOS,keepdebugUSBattached,report;
doNOTexittoWindowsuntil freshBIOSattachcaptured. Noremotereboot/firmwarewrite.

### Fresh Anker attach while inBIOS stays~29.4W (20:20-21UTC)
User physicallydisconnected/reconnected power while remaininginBIOS. Trace
containsconnection-statusrecords, then15Woffer/request andupdated100Woffer.
UpdatedPDO4=000641F4,RDO4387D1F4 atmeter6800062ms,Accept6800066,
PS_RDY6800191. Completed100Wcontract at20:20:42Z, BEFOREWindowsboot.
20:20:47..20:21:10 samplemedian29.44332W,min29.326332,max29.940552.
Artifactbios-fresh-attach-summary.json recordscontractandsteadyinterval.
This reproduceslowinput inBIOS afterfreshattach; WindowsUCSIinit isnotrequired
forcurrentlowstate. Warmrestart didnotresetEC, so persistentstate fromearlierOS
session remainspossible. Do notclaim coldEC reset or disproveallOSinfluence.
Capturev5stillactive; nextuserexitBIOSWITHOUTsavingchanges,bootWindows normally
withouttouchingpower. Capturetransition fornewnegotiation/inputchange.

### Anker wall-power cycle inBIOS: no sustained recovery (20:23-24UTC)
User unplugged/repluggedAnker atwall,remaininginBIOS. Traceinputfallsnearzero,
newPDnegotiation20:23:39..40Z:15WinitialthenPDO4=000641F4,RDO4387D1F4,
Accept+PS_RDY. Stableinput~29.46W afterward,brief60.33Wstartuppeak. No sustained
recoveryfromsourcepowercycle inthisrun. Doesnotprove everyinternalstatecleared
or identifyECcurrentregister. Artifactbios-wall-reset-summary.json;v5rawretained.
NextexitBIOSwithoutchanges/bootWindowswithallcablesattached;capturev5stillactive
until~20:31:56UTC. No flash/ECreset/registerwrite performed.

### Windows reached: no observed new PD negotiation (20:27UTC)
User reports desktop. FreshWindowsLastBoot1795033595500=20:26:35.5Z, confirming
newWindowsstartup (unlikeearlierhybridresume). Windows15:27:22EST:36%,ACtrue,
Chargingtrue,batterycharge6.582W,RemainingCapacity15651,DSTS0001002A.
NoPDrecords after20:24:33Z throughcapturestop. Input~29.5Wthroughstartup,
thenvariesdown~19-30Wafterdesktop.100WcontractlastobservedinBIOSremainslast
capturedcontract; no wireevidenceofcontractdowngrade duringthisWindowsboot.
Artifacts bios-to-windows-status.json,bios-to-windows-comparison.json andv5raw.
This controlledrun reproduceslowstatebeforeWindows andafternewWindowsboot.
It doesnot explain earlierreported~90WBIOSstate nor excludepriorlatchedECstate.

CapturestoppedwithSIGINT,disableaccepted,cleanupcomplete,powerzrestored.
No capture/firmwarediagnosticarmed. Next technicalblocker is validated snapshot
ofcachedcurrent81206/81320,sequence81241..46,andcharger3F/3B/3A/4B/39/4C,
withoutresettingstate. No newpatch justified merelyfrommetertrace. OEMD0/D2/D3
handlers reviewedoffline; theyinvoke controlfunctions,not arbitraryRAMread;
noneexecuted. Do not use them asread-onlydiagnostics.

### Further Windows transport audit (offline, 2026-11-18)
AsusSAIO dispatch review:80102068->RVA3000 is CPUID, not a general
I/O request.80102074/78->2ACC/2C90 select chipset power-limit MMIO
fields by CPU identifier;8010207C->298C samples MSR606/611. Remaining
80..94 requests lead CPU/MSR telemetry routines, some with internal
WRMSR calls; do not invoke them as read-only EC diagnostics. No new
driver IOCTL or laptop hardware action performed. Corrected stale
windows-read-route-audit.json next step: CC read semantics already
resolved as a pointer-table index, not arbitrary RAM. Live snapshot
transport remains unresolved; current firmware cause remains unproven.

### Additional OEM read tables and next behavioral test
Decoded DA/D6/D4/CE/C6 read tables from bounded handler disassembly.
They use41254 (flags bit0, bounds, byte pointer read); none exposes
target81200..8125F/81300..81329. EA->431DC reads80F5E+index, also
not targetstate. Artifact oem-additional-read-tables.json. No OEM
commands executed. Windows reachable, battery77%.
Next test: physical EC reset with Anker connected, then BIOS observation
before Windows, to distinguish retained state from repeatable attach
failure. Warmrestart previously did not establish ECreset. ASUS procedure
https://www.asus.com/support/faq/1050239/ rechecked: shutdown, peripherals
removed, adapterconnected,40secondpowerhold (20svariant indicator rule).
Passive capture pd-ec-reset-v1.jsonl active,900s, session92154.
Monitor accepted and samples arriving. User physical reset NOT yet done.
At77% SOC highpower recovery may be limited by taper; low result alone
is not sufficient proof of a failed transition. No firmware write armed.

User reports in BIOS at~21:44UTC. Reset-v1 capture expired normally
21:39:07Z:43115samples,zeroPD events,12.65..30.30W,median25.29W.
This does NOT establish reset timing or postreset BIOS behavior.
Attempt to start pd-ec-reset-bios-v2 refused: meter absent; lsusb confirms
no5fc9:0063. No v2 capture active. Asked whether physical40s reset was
performed; awaiting answer. Need user reconnect meterloggingUSB to local
machine while preserving laptop charging path, then check newnode/ACL.

Meter replugged as003/008, mode0660 groupwheel; active agent user is
inwheel and read/write access verified. Passive capture
pd-ec-reset-bios-v3.jsonl started successfully,900s,session55442.
No sudo/ACL needed for this node. User reports BIOS; physical40second
reset confirmation remains outstanding. Do not label this verified
post-reset behavior until confirmed.

User confirms performed requested power-button reset earlier, asks
whether adapter should have been disconnected. Current ASUS dedicated
EC reset FAQ1050239 explicitly says adapterconnected; generic bootfailure
FAQ1014276 also contains conflicting disconnect-adapter wording. No
GV301QE-specific reset electrical requirement established. Record user
performed procedure, not independently verified EC reset completion.

User explicitly instructs assume40s powerconnected reset completed.
Proceed on that basis; no repeated reset confirmation needed. Capturev3
active22:00UTC, Anker BIOS baseline~29.5W. Next requested physical step:
switch to ASUS adapter through same meter while remaining in BIOS,
observe60s, then eventualAnkerreturn. Comparison addresses currentSOC
taper/systemdemand confound (lastWindows77%) without introducingOSboot.
No firmware update/reset required for this comparison.

### BIOS ASUS comparison after reported connected-power reset
User switchedAnker->ASUS while stayingBIOS. ASUSattach22:01:11Z,
PDO4=000641F4,RDO4387D1F4,Accept+PS_RDY captured. Input rises~92W,
versusAnker~29.48W immediatelybefore. See bios-reset-asus-comparison.json
andraw pd-ec-reset-bios-v3.jsonl. CurrentSOC/demand doesnotprevent
highinput onASUS; doesnotidentifyremainingECcondition. Next user
switchbackAnker whileBIOS tocheckreversibility orASUSprimingeffect.
Capturestillactive, no newreset/flash/OSboot.

### Anker return in BIOS improves after ASUS connection
User switchedbackAnker~22:02:56UTC, stayedBIOS. Capturedupdated
PDO4=000641F4,RDO4387D1F4,Accept+PS_RDY~22:02:57.6.
From22:03:00 forover60s, input~66W vs29.48W beforeASUS;
artifact bios-reset-anker-return.json. This is observedstate/history
dependence, notproofofspecificECregistercause. Initial attach capture
hasmissingearliercapability/requestframes; doNOTclaimimmediate100W
without15Wstagefromthistrace. NoWindowsbootbetweenadapterchanges.
Next preserveAnkerconnection andbootWindowswithcaptureactive tosee
whether newlyobservedhighstatefallsduringstartup. Capturev3active
until~22:13:18UTC. Noreset orfirmwarewrite needed.

### Captured high Anker state falls on transition to Windows
BIOS Ankerreturn~66.8W persisteduntil22:05:40UTC. User reports
bootingWindows; SSHreachable22:06:17. Input~49..53W then~29.3W
by22:06:30,Windows62%,Chargingtrue,ChargeRate8224mW.
Windowsclock~25minbehindcollector andLastBoottimestamp repeats
previous1795033595502; freshkernelboot/UCSIinitialization notproven.
Do not interpret userBIOSexit as establishednewkernelboot vsresume.
Evidence bios-high-to-windows-{status.json,status-second.json,comparison.json}.
Rawcapturev3stillactive until22:13:18UTC.

Followup:zeroPD events after22:02:57.608Z through22:06+ capture,
so noobservedcontractrenegotiationwithpowerdrop. WindowsSystemevents
showKernel-Boot27 boot type0x1 andKernel-Power107 resumedfromsleep,
PowerTroubleshooter1 lowpowerreturn; preserveboot-events.json. This
is evidenceofresume path, notfreshkernelinit. TickCount64 unavailable
onremotePS/.NET (null); do notuseasuptime. Need distinguishWindows
resume/restoredpolicy fromfreshstartup beforeblamingUCSIinit.

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

UserreportsBIOSwithASUS. Full-bootcapturev1active; ASUSattach
~22:11:48UTC,input~93.9W sustainedthrough22:12:45+. Baseline saved
full-boot-asus-bios-baseline.json. NextuserAnkerreturnwhileBIOS,
verifyhighstatebeforeWindows. FastStartupremainstemporarilydisabled.

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

AnkerreattachwhileBIOS:powerabsent~22:14:52..22:16:26UTC,
notmerelyrequested10s. Captured15Winitialcontract then100W,
finalAccept/PS_RDY22:16:29. Brief68.2Wpeak,settles29.46W.
Artifactfull-boot-anker-bios-reattach.json. Repeatedreattachhasnot
restoredhighstate; stopaskingrepeatedchargerswaps. NextuserbootWindows
unchangedconnection. This testsfreshstartupfromLOWstate, notoriginal
high->lowcomparison; cannotisolatecauseofpreviousdrop. Verifyfullboot
andcontinueWindows/offlineanalysis. FastStartupstilltemporary0.

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
