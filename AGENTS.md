# Working instructions for the GV301QE investigation

## Read first

1. Read `/dev/AGENTS.md` through the noted `ReadNote` tool at the beginning of a session.
2. Read [README.md](README.md) and [SESSION_HANDOFF_2026-11-18.md](SESSION_HANDOFF_2026-11-18.md).
3. Consult the relevant sections and latest entries in [FINDINGS.md](FINDINGS.md). Entries are chronological; later verified results supersede earlier pending, stock, or not-deployed statements.

The former long AGENTS.md is preserved in [docs/AGENTS_HISTORY_2026-11-18.md](docs/AGENTS_HISTORY_2026-11-18.md). It is historical evidence, not a list of actions still pending.

## Objective and working preferences

Investigate reliable high-power third-party USB-PD charging on an ASUS ROG Flow X13 **GV301QE**, staying on **BIOS 415 or a derivative**. The user prefers software deployment and does not have a known external programmer or want to open the laptop. Windows is a test installation; Linux is an option only when a defined experiment benefits from it.

- Continue useful authorized work until genuinely blocked on user input or physical action. Do not repeatedly request the same approval or charger information.
- Prefer firmware analysis over further undirected hardware experiments. The user's latest technical direction is to decompile/reconstruct the relevant code and examine whether the patch exposes a firmware defect or unsupported assumption.
- No subagents unless explicitly requested. Do not commit or push new changes unless asked; previous requests authorized specific commits/pushes, not every future change.
- Preserve unrelated in-progress work. Inspect Git status before editing. Untracked flash-kit files were present when these instructions were written and were left untouched.
- Keep persistent conclusions and evidence paths in FINDINGS.md. Update the consolidated handoff when conclusions or next steps materially change.

## Current verified state

The five-byte VID patch was programmed to host SPI flash and subsequently verified in two matching full 192 KiB internal EC reads after the reported physical reset/reboot. Modified EC SHA-256:

`eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e`

Both secondary `==100` predicates remain intact. Reliable third-party high-power charging is **not solved**. A successful deployment or 100 W PD contract does not prove the laptop draws 100 W.

Latest behavioral evidence:

- Low-SOC Windows Anker/ASUS/Anker input was about 29/70/29 W, with 100 W contracts.
- BIOS ASUS input reached 92–94 W. Anker sometimes sustained about 67 W after ASUS, but other attempts remained near 29 W.
- A captured higher-power Anker state fell to about 29 W on Windows resume without observed PD renegotiation.
- A later confirmed full Windows startup retained a pre-existing low state. This did not test full startup from high power.

At the last hardware handoff: Windows was reachable via `ssh flowx13`, Anker was connected, the meter capture was stopped cleanly, and no firmware diagnostic was armed. Fast Startup's original `HiberbootEnabled=1` was restored after testing with zero. These are historical observations: recheck live state before acting. The user already confirmed the connected-power 40-second reset procedure; do not ask that same confirmation again.

## Immediate offline investigation

Trace both patched branches through the complete charger state machine, producing readable pseudocode with source/capsule address references. Compare ASUS's immediate 100 W offer with Anker's initial 15 W → 100 W update and with OS initialization/resume state changes.

Focus on:

1. Assumptions downstream of the bypassed ASUS VID comparisons.
2. Raw PD current/voltage refresh, selected-source changes, and selected current updates.
3. Charger transition failure, retry budgets, error latching, and completion writes.
4. OS/ACPI paths that change state or reset/reapply charger configuration.

Separate observed behavior, static code facts, and hypotheses. Do not claim a bug merely because a plausible failure path exists. The deliverable should identify a concrete defect or the smallest discriminating measurement, rather than invent another patch.

## Address and protocol facts

- Chip: **IT5125 revision 02**, live ID begins `51 25 02`; the earlier IT8226 identification is obsolete.
- Stock capsule: `bios/bios_version_415/GV301QEAS.415`, SHA-256 `55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94`.
- EC window: capsule `[0x31800, 0x61800)`. Subtract `0x31800` from capsule code offsets for runtime code addresses. Do not subtract it from RAM/MMIO operands.
- NDS32 instructions are big-endian; embedded data often little-endian. Broad disassembly can misalign at tables: decode from known entry boundaries.
- Changed EC bytes: `C5EE/C5F0/C5F1/11EFA/11EFB`; host bytes: `3D5EE/3D5F0/3D5F1/42EFA/42EFB`.
- Normal PMC command `92`: offsets below `1000` map to RAM `80000+offset`; offsets at/above `1000` map to MMIO `F00000+offset`. `1240` does not read RAM `81240`.
- Proven direct I2EC configuration ports are `025E/025F`, not conventional `2E/4E`. UEFI success does not establish concurrency-safe Windows access.

Useful static locations, expressed as capsule code offsets unless explicitly RAM:

| Location | Role |
| --- | --- |
| `4351C` | Selects/caches class `81203`, voltage `81204`, current `81206` |
| `48620`, `48668` | Raw current/voltage cache refresh |
| `485DE` | Derived current: 95% below raw 4921, otherwise 4674 |
| `4358E`, `486B6` | Change handling; a 15 → 100 power-class rise does have a refresh/reselection path |
| `47D52` | Charger sequence dispatcher |
| `47AE6` | Step 13 status wait; can latch error `81243` and clear enable `81241` |
| `43334` | Completion applies `81206` to charger register `3F`; another pending-bit path also applies limits |
| RAM `81240..46`, `81250..51` | Sequence state and transport error counters |
| RAM `81300..29`, `80410` | PD caches/change flags and state affecting retry budget |

The charger register map resembles ISL9241, but live chip identity/sense resistance are unverified. Counter delay units are not established as milliseconds. State value 5 is not established as “Windows.” DSTS `0012006C` returned `0001002A` in both high/low states and does not prove transition completion.

## Hardware operations and failed approaches

Do not rerun the original follow-mode EC reader unchanged: it stalled before a valid flash sample and required physical recovery. Do not use another ITE chip's DMA/halt/register layout speculatively. Do not run vendor flash utilities or default flashrom probing as if they were read-only.

The successful route was guarded host-SPI writing through paired SMM access, followed by verified EC propagation. Preserve original live backups and device-specific host data. Never replace the laptop's Windows boot manager. A candidate, unlocked controller, or emulator test alone is not validation of a new hardware write.

Stock-sector erase/restore and modified-sector programming were demonstrated on this machine. Recovery from a failed physical write was not. A future change needs evidence for its purpose, exact input/profile guards, bounded completion, readback, and defined failure behavior. No further firmware write is currently justified by meter traces alone.

Windows live snapshot access remains unresolved. OEM CC and the reviewed additional read tables do not expose the needed arbitrary RAM. AsusSAIO's private mutex does not prove exclusion against SMM. The installed PawnIO LpcACPIEC module only permits `62/66`. Do not treat controls such as OEM D0/D2/D3 as passive reads or guess charger-register writes to bypass this blocker.

## Tools and verification

- Follow the `.codegraph/` rule below if an index exists. Otherwise prefer `rg` for searches.
- Tools, tests, vendor capsules, and reviewed payload/profile headers are tracked. `out/`, `ec/`, full live backups, captures, and generated binaries are ignored.
- Read a script before running it. Windows select/stage scripts can arm diagnostics, and some tests require local evidence not present in a clone.
- Python parser regression example: `python tests/fwx_test.py`. Capsule-signature audit: `python tools/audit_415_capsule.py` (requires `cryptography`).
- UEFI builds need a built GNU-EFI tree plus GCC/binutils. Use appropriate native sanitizer/fault tests and QEMU guards for hardware-tool changes; these do not emulate actual EC behavior.
- Temporary paths such as `/tmp/flow-binutils/build/binutils/objdump` and `/tmp/flow-gnu-efi` may disappear. Do not assume they exist in a new session.
- Meter capture: `tools/km003c_pd_capture.py`; decoding: `tools/decode_km003c_capture.py`. Verify capture is active before requesting a physical test, use a new filename, and confirm cleanup on stop.
- Check `lsusb -d 5fc9:0063` and current device permissions. Latest access worked via mode `0660`, group `wheel`; do not request stale per-node ACL commands automatically.
- Keep PD contract, meter input power, and battery charge telemetry separate. Correlate using logger UTC; Windows clock and boot/resume behavior caused misleading timestamps earlier.

<!-- CODEGRAPH_START -->
## CodeGraph

If `.codegraph/` exists at the repository root, use `codegraph_explore` or `codegraph explore "<question or symbols>"` before grep/file reads to locate or understand code. If there is no index, skip CodeGraph; indexing is the user's decision.
<!-- CODEGRAPH_END -->
