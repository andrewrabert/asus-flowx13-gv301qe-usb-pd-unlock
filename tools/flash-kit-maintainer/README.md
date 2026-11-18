# Flash-kit maintenance

The downloadable kit is `../../flash-kit/`. It contains five visible files and
uses the tested EFI binaries directly. This directory retains source, build
inputs, evidence, and the original manual workflow outside that download.

- `sh build.sh /absolute/path/to/built/gnu-efi` rebuilds into `build/` and compares
  each result with `bin/`. All four rebuilt byte-identically during packaging.
- `python validate.py` checks this source bundle and the exact five-byte delta.
- `python package.py` reproducibly generates `../../flash-kit/payload.zip` and
  updates the launcher's pinned payload/manifest hashes and license notices.
  It packages the existing `bin/` files, leaving `build/` results for comparison.
- `flash.ps1` and `scripts/` are the retained manual engine. The packager relocates
  its backups to the public kit's `work/backups/` and adds final validation.
- `MANUAL-WORKFLOW.md` documents the original individual stage commands.

From the repository root, run `python tests/flash_kit_package_test.py` and
`pwsh -NoProfile -File tests/flash_kit_workflow_test.ps1` for offline distribution
and resume-state tests. These do not perform hardware operations. The guided
Windows launcher still needs an end-to-end hardware run before being described
as hardware-tested; the bundled EFI binaries already have hardware evidence.

The tested binaries and their checks stay as they are, whatever new model, BIOS
version, or failed precondition appears; widening support starts with fresh
hardware evidence. Full machine-specific backups stay local to the operator's
machine, outside both directories and any public distribution.
