# GV301QE BIOS 415 experimental EC vendor-check patch

This directory contains the **exact EFI updater flashed on our GV301QE**, plus
the backup/readback utilities and a Windows workflow. Copy this whole directory
to the target laptop. No compiler, Python, Linux boot, PawnIO, or full BIOS image
is needed to use the supplied binaries.

**Experimental, tested on one machine. This is not a confirmed charging-limit
fix.** Host flashing and propagation into the internal EC were verified. Later
tests still showed about 29 W input from an Anker charger despite a negotiated
100 W contract, versus about 92–94 W from ASUS. Higher Anker input was observed
but was not reliably reproducible.

Flashing erases and rewrites two host-flash sectors. Power loss or failure can
leave the laptop unbootable and require an external programmer. There is no
tested automatic rollback or bundled uninstaller. The writer deliberately halts
on unsafe/partial completion. A saved backup does not itself provide recovery.

## Requirements

- Exact model `ROG Flow X13 GV301QE`, BIOS `GV301QE.415`, Windows x64 in UEFI mode.
  This kit does not install/downgrade BIOS 415. Other Flow X13 models/versions are
  unsupported. Do not edit guards to force acceptance.
- Administrator **Windows PowerShell 5.1**, with `Get-BitLockerVolume` available.
- Secure Boot disabled and OS-drive BitLocker **ProtectionStatus Off** during
  the workflow. Save your recovery key first; remember previous settings and
  restore them after successful verification. The scripts do not change them.
- ASUS power adapter connected and battery at least 80% before flashing.
- Unused drive letter `S:` and at least 50 MiB free on the EFI system partition.
- No pending firmware update or other diagnostic boot. Finish Windows updates
  first; avoid updates during this procedure.
- A separate place to save your own backups and a recovery plan you accept.

The original scripts were adapted to save/check **your** Windows boot-manager
hash, rather than requiring the development laptop's Windows build. EFI binaries
and firmware guards are unchanged. The new packaging scripts have offline
validation; this packaged end-to-end workflow has not been run on another laptop.

## Run the workflow

Extract to a local writable directory, such as `C:\flow-flash-kit`. Keep that same
directory throughout. Open **Windows PowerShell as Administrator**, then:

```powershell
cd C:\flow-flash-kit
Set-ExecutionPolicy -Scope Process Bypass
.\flash.ps1 Init
```

`Init` checks package hashes, records your boot-manager hash, and creates `work\`
and `S:\EFI\FlowEcAudit`. It refuses existing work or diagnostic directories to
avoid colliding with prior runs. Do not delete earlier backups to bypass this.
For our already-patched development laptop, this refusal is expected: **do not
reflash it**.

Run each stage separately. A staging command selects one temporary boot entry;
it **does not restart**. After each staging command succeeds, save your work and
choose **Restart** from Windows. Wait for Windows to return, reopen administrator
PowerShell in this directory, and run the corresponding collection command.
Never paste all stages together or proceed after an error.

| Stage command | After restarting and returning to Windows | Purpose |
| --- | --- | --- |
| `.\flash.ps1 BackupHost` | `.\flash.ps1 CollectHost` | Two matching 16 MiB host backups; validate original target sectors and embedded EC |
| `.\flash.ps1 BackupEC` | `.\flash.ps1 CollectEC` | Two matching 192 KiB stock internal-EC backups; takes about 6–7 minutes |

**Before continuing, copy the entire `work\` directory to another computer or
external drive.** Preserve both host and EC passes. Full host backups contain
device-specific data; keep them private. The workflow checks their contents again
before arming the flash. It cannot check whether your external copy exists.

| Stage command | After restarting and returning to Windows | Purpose |
| --- | --- | --- |
| `.\flash.ps1 Flash` | `.\flash.ps1 CollectFlash` | Erase/program the two guarded sectors and verify their patched contents |
| `.\flash.ps1 VerifyHost` | `.\flash.ps1 CollectVerifyHost` | Verify the patched host sectors persisted across another reboot |

Successful flash collection requires `vid_patch_result=0`, `restored=1`,
`dirty_mask=0`, `erases=2`, `programs=256`, plus successful SMM cleanup.
Successful host verification requires `vid_verify_result=0` and zero erase/program
operations. Reaching Windows alone is not success; the collector checks the log.

### Propagate the host patch into the internal EC

Ordinary restarts initially left our internal EC unchanged. Propagation was
verified after the user reported the following physical reset/reboot procedure:

1. Shut down and remove external peripherals.
2. Leave the power adapter connected.
3. Hold the power button for 40 seconds, then release and power on. ASUS also
   documents a 20-second variant: if the power indicator flashes after about
   15 seconds, release once flashing stops.

These are the [ASUS EC/RTC reset instructions](https://www.asus.com/support/faq/1050239/),
checked on 2026-11-18. The precise electrical reset/timing on our machine was not
independently observed. A physical reset is not a substitute for readback.

Once Windows is back, run:

| Stage command | After restarting and returning to Windows | Purpose |
| --- | --- | --- |
| `.\flash.ps1 VerifyEC` | `.\flash.ps1 CollectVerifyEC` | Two full internal-EC reads, approximately 6–7 minutes, compared to the exact patched hash |

`VerifyEC` preserves the earlier stock EC files under `work\original-ec\` and
an equivalent ESP archive before reusing the reader. Success requires both full
readouts to have SHA-256:

```text
eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e
```

This proves the expected bytes are present in the internal read window. It does
not prove a particular charging wattage. Test charging separately with a suitable
cable, comparable battery charge/workload, and a meter if available. Negotiated
PD power, actual adapter input, and battery charging power are different values.

## If anything fails

- Stop on any refusal, mismatched hash, missing/incomplete log, or nonzero result.
  Preserve `work\` and the screen message. Do not weaken guards or repeat `Flash`.
- If the updater halts after a write failure, do not assume power-cycling is safe.
  Record the screen; recovery may require external hardware. The kit has no safe
  generic software recovery command for partial programming.
- If host verification succeeds but EC readback is still stock, preserve the
  readouts and investigate reset/propagation. **Do not rerun the updater.**
- Collectors remove only their saved temporary boot entry and check that the
  arm token was consumed and Windows boot manager stayed unchanged. They retain
  diagnostic files/backups on the ESP and under `work\` for investigation.
- If Windows changes `bootmgfw.efi` during the procedure, the hash check will
  stop it. Review the update rather than editing the saved hash blindly.
- If a stage is armed but you decide not to reboot, use `.\flash.ps1 Cancel`.
  Cancellation is allowed only before that operation has produced a log.

After verified completion, keep backups, restore your previous security settings,
and confirm that BitLocker protection and normal boot work as expected.

## What is included

| Path | Contents |
| --- | --- |
| `flash.ps1` | Staged workflow, backup/content checks, result validation |
| `scripts/` | Adapted original Windows staging/collection scripts; invoke through `flash.ps1` |
| `bin/` | Four unchanged, hardware-used EFI binaries |
| `payload/` | Original/patched two-sector comparison data, 8 KiB each |
| `source/` | EFI sources and embedded payload headers; writer/verifier from archived snapshots |
| `build.sh` | Optional Linux/GNU-EFI rebuild into `build/`, leaving `bin/` untouched |
| `SHA256.json` | Integrity manifest; detects corruption, not a cryptographic signature |
| `evidence/` | Trimmed successful development-machine hardware diagnostic logs |
| `validate.py` | Offline package/payload checks; needs Python 3 only for maintainer validation |

Only five bytes differ, at host offsets `0x3D5EE`, `0x3D5F0`, `0x3D5F1`,
`0x42EFA`, `0x42EFB` (EC-relative `0xC5EE`, `0xC5F0`, `0xC5F1`, `0x11EFA`,
`0x11EFB`). They bypass two vendor-ID branches. Both separate `==100` predicates
are unchanged. The writer embeds complete sectors at `0x3D000` and `0x42000`,
requires their exact original contents, and checks the measured flash profile.
It does not replace the full BIOS or use ASUS signed-capsule delivery.

To rebuild for inspection, install GCC/binutils and build GNU-EFI for x86-64,
then run `sh build.sh /absolute/path/to/gnu-efi` and `python3 validate.py`.
Rebuilding is optional. Do not substitute newly compiled binaries for the tested
ones just by changing hashes; toolchain changes need review and validation.

Share this directory **without `work/` or `build/`**. The EFI files are explicitly
included by this directory's `.gitignore`; a source-only checkout without `bin/`
is not the ready-to-use kit. No full machine-specific BIOS backup is distributed.
