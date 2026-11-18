# GV301QE BIOS 415 vendor-check patch

**Download this directory, extract it to a local folder, and double-click
`flash.cmd`.** Accept the administrator prompt and follow the instructions.
No compiler, additional downloads, or blob generation is needed.

The launcher saves backups, applies the exact five-byte patch tested on our
laptop, and checks the result. **After each restart, double-click `flash.cmd`
again**; it automatically continues from the previous step. Keep the folder in
the same location. It will also guide you through a physical EC reset using the
power button. Allow about 20 minutes and several restarts.

## Before starting

- Exact **ROG Flow X13 GV301QE**, already running **GV301QE.415**, with Windows
  x64 and Windows PowerShell 5.1. Other models and BIOS versions are refused.
- Connect the ASUS adapter and charge the battery to at least **80%**.
- Disable Secure Boot and suspend BitLocker protection for the procedure; save
  your recovery key first. Restore your previous settings after completion.
- Finish pending Windows/firmware updates first. Drive letter `S:` must be free,
  and the EFI system partition needs at least 50 MiB free.

**Experimental, tested on one machine.** Flashing failure can make the laptop
unbootable and require an external programmer. There is no tested automatic
rollback. The launcher asks you to copy your backups elsewhere before flashing.
Let each firmware operation run to completion, and treat any failed check as a
stop.

The patch was verified in both host flash and internal EC. **It is not yet a
proven charging-limit fix:** subsequent tests still showed approximately 29 W
Anker input despite a 100 W PD contract; higher input was not reproducible.

## Files and results

- `flash.cmd` / `flash.ps1`: the launcher.
- `payload.zip`: ready-built flashing, backup, and verification tools. Leave it
  zipped; the launcher verifies and extracts it automatically.
- `LICENSES.txt`: bundled third-party notices.

Backups and results stay under `work\backups`; detailed output is in
`work\session.log`. Keep these private and preserve them. To restart a failed
installation, leave `work` in place; the launcher resumes from it. If you
previously ran our research tools,
existing diagnostics cause a refusal rather than being overwritten.

To cancel a selected boot **before restarting**, open administrator PowerShell
in this directory and run `powershell -ExecutionPolicy Bypass -File .\flash.ps1 -Cancel`.
If an operation has already started, preserve the error/screen and investigate
the cause; that evidence, rather than another updater run, is what recovery
depends on. If the machine halts during flashing, power-cycling is
not an established recovery procedure.

The reset instructions follow the
[ASUS EC reset procedure](https://www.asus.com/support/faq/1050239/): shut down,
remove peripherals, keep the adapter connected, hold power for 40 seconds, then
power on. ASUS also describes a 20-second variant with a flashing indicator.
The final readback, not the reset alone, confirms that the patch reached the EC.

The included EFI binaries are unchanged from the tested versions. The guided
launcher has offline tests; this packaged Windows workflow has not yet been
tested end-to-end on hardware. Maintainer sources, rebuild tools, and evidence
are kept separately in `tools/flash-kit-maintainer/` in the repository.
