# Flow X13 firmware and charging investigation handoff

Updated through 2026-11-18, approximately 22:21 UTC. Workspace: `/home/ar/src/bios`.

The five-byte EC patch has been deployed through software and verified in the internal EC read window. The original goal of reliable high-power charging with a third-party adapter is **not yet achieved**. The Anker successfully negotiates 100 W but often draws about 29–30 W. Higher draw is possible and has been recorded, including about 67 W in BIOS, but it is not reliably reproducible.

This document consolidates the investigation and conversation into a usable handoff. [FINDINGS.md](FINDINGS.md) remains the chronological evidence log; [AGENTS.md](AGENTS.md) contains working instructions and historical handoffs. Both contain earlier states that later results supersede. In particular, old statements that no firmware was written or that the EC is still stock are no longer current.

## Current state

| Item | Last verified state |
| --- | --- |
| Laptop | ASUS ROG Flow X13 GV301QE, BIOS 415 with the investigated EC modification |
| Operating system | Windows; reachable with `ssh flowx13` |
| Charger | User last connected Anker 747, with the laptop its only load |
| Charging behavior | About 29.3 W input after a confirmed full Windows startup |
| Battery | 53% at the latest full-startup status query; this will change |
| Windows Fast Startup | Original value **1 restored** after the temporary disabled test |
| Firmware work | Host patch and internal EC propagation verified; no additional patch prepared as a justified fix |
| Diagnostics | No firmware diagnostic armed; last boot-manager inspection showed no one-time boot sequence |
| Meter recording | Stopped cleanly; raw capture contains `cleanup_complete: true` |
| User action pending | None at handoff; avoid asking for another undirected charger swap |

These are observations at the end of the session, not permanent properties. Check live power, SSH availability, meter enumeration, and process state before a new operation.

## Objective and user preferences

The user wants to remove the third-party USB-PD charging restriction while remaining on **BIOS 415 or a derivative**. They prefer a software deployment route, do not want to open the laptop, and do not have a known external programmer. Windows is installed for testing; they normally use Linux and could use a live distribution if there is a demonstrated reason.

The user authorized SSH access, research, the diagnostics performed, and eventually the modified firmware deployment. They repeatedly asked the assistant to keep working until a physical action or missing information genuinely required them. Do not repeatedly ask for authorization already given. Do not confuse broad investigation authorization with evidence that a speculative firmware write is ready.

They asked about “YOLO mode” to reduce permission interruptions, and later asked for durable handoff notes because the investigation kept stopping. Preserve that working preference: complete independent analysis and preparation before requesting another physical test. Do not spawn subagents or commit the repository unless asked.

Start a new session by reading `/dev/AGENTS.md` with the noted `ReadNote` tool. No `.codegraph/` was present during this work; if one exists later, follow the repository's CodeGraph instructions before code searches.

## What was modified and how deployment was established

### Firmware identification and address conventions

The EC uses big-endian NDS32 instructions. Live identification established **IT5125 revision 02**, with chip registers beginning `51 25 02`. The earlier IT8226 attribution came from a legacy tag and should not be used as the live chip identity.

The verified EC image is a **192 KiB window** at capsule offsets `0x31800..0x61800`. This establishes the measured window, not total physical EC flash capacity. For code addresses in disassembly, subtract `0x31800` from the capsule offset to obtain the EC runtime address. RAM and MMIO operands are already runtime addresses. Embedded data tables often use little-endian values despite the instruction endianness.

| Object | SHA-256 |
| --- | --- |
| Stock BIOS 415 capsule | `55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94` |
| Verified stock 192 KiB EC image | `5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478` |
| Modified EC image and matching internal readbacks | `eb73de50e65e67915ee7474203dc2fbc3dd055a635157c2de82457299dfc957e` |
| Original paired 16 MiB host backup | `74c8b75a51496d9d7821883552b0157a6685e233aa0581cdf888a899680ffade` |

The patch bypasses two ASUS VID checks. Exactly five bytes changed at EC-relative offsets `C5EE`, `C5F0`, `C5F1`, `11EFA`, and `11EFB`. The separate `==100` checks remain intact. Corresponding host offsets are `3D5EE`, `3D5F0`, `3D5F1`, `42EFA`, and `42EFB`, in sectors `3D000` and `42000`.

### Routes investigated before the successful write

| Route or test | Result and significance |
| --- | --- |
| Modified official capsule | Stock RSA-PSS verification was reproduced; modifying the EC invalidates the capsule signature. Secure Boot being off does not remove this authentication. |
| User-supplied Windows 415 updater | Extracted offline. It contains the same stock capsule and uses firmware INF/Windows capsule delivery, not an independent unsigned EC writer. |
| Original EC follow-mode reader | Entered the utility but stalled before obtaining a flash ID or sample; cleanup exit was unconfirmed and physical recovery was needed. Do not repeat unchanged. |
| Entry/exit-only utility test | Succeeded, separating entry/exit behavior from the failing follow-mode transaction. |
| Normal PMC indirect reads | Succeeded; two complete EC backups matched stock exactly. |
| Direct I2EC access | Initial conventional-port attempts were inconclusive. Relocated config ports `025E/025F` succeeded in UEFI with selector restoration. |
| Vendor ITE utilities and other-board projects | Useful references, but not validated for this chip/board. They were not run as flashers. |
| Direct host SPI access-bit change | Ignored by hardware in the tested context. |
| AMI SMM access pair | SW-SMI `20/24` successfully opened and restored host-SPI access. These callbacks change platform controls and are not purely read-only. |
| SPI identification and profile | JEDEC `EF 60 18`; matching SFDP passes established 16 MiB, 256-byte pages, and 4 KiB erase opcode `20h`. Exact Winbond suffix remains unconfirmed. |

The first host ID/status reader safely refused because its guard assumed lower controller fields were constant. A corrected guard retained exact access/protection requirements while allowing changing previous-opcode/count fields. Successful identification, profile, and full-sector preflight followed.

The parser also needed correction: valid all-FF GUID padding and AMD zlib sections had hidden much of PEI. Corrected extraction produced 547 modules and 96 PEIMs, with 391 unique executable hashes agreeing with independent UEFIExtract. Earlier 448-module negative searches were incomplete.

### What “validated updater” meant in this investigation

Read access, an unlocked controller, and a generated candidate were not treated as proof of writing. The investigation progressed through explicit checks:

1. Two matching original host backups, preserving device-specific data that differs from the stock capsule.
2. Live identification, protection/status and erase-geometry reads.
3. Full-sector preflight reads matching original sectors.
4. A same-byte 32-byte program, verifying WREN/WEL behavior and completion without intended content changes.
5. A real erase of one original sector, verification that every byte became FF, restoration of all original bytes, and full verification.
6. Deployment of the two modified sectors with exact preconditions, bounded transport, readback, and fail-stop behavior after mutation.

Native sanitizer/fault-injection tests and QEMU refusal tests supported the implementation. They did not emulate the ITE chip or prove recovery from every physical failure. The actual stock-sector erase/restore test established hardware writing; no external-programmer recovery was demonstrated.

The modified host write succeeded at approximately 00:31 local on November 18: two erases, 256 program operations, both complete modified sectors verified, and controller/platform state restored. The Windows boot manager was not replaced.

### Host modification did not immediately mean internal EC modification

After ordinary reboots, the host sectors still matched the patch, but two internal EC reads remained exactly stock. After the user reported reboot following physical EC-reset instructions, two new complete 192 KiB internal reads matched the modified image exactly.

The demonstrated route on this laptop is therefore **guarded host-SPI sector modification followed by the reported physical reset/reboot and verified EC propagation**. Precise reset timing and the full IT5125 mirroring mechanism were not independently established. Firmware readback proves deployment, not reliable high-power charging.

Key evidence directories:

- [Original internal backups](out/update_415/uefi_backup_probe/)
- [Original full host backups](out/update_415/uefi_host_backup/)
- [SPI geometry and protection profile](out/update_415/uefi_smi_profile/)
- [Same-byte programming](out/update_415/uefi_smi_stock_program/)
- [Real stock-sector erase and restore](out/update_415/uefi_smi_stock_restore/)
- [Modified host deployment](out/update_415/uefi_smi_vid_patch/)
- [Stock internal reads after host patch](out/update_415/ec_after_vid_patch/)
- [Persistent modified host verification](out/update_415/uefi_smi_vid_verify/)
- [Matching modified internal EC readbacks](out/update_415/ec_after_physical_reset/)

## Charging equipment and measurement interpretation

The third-party adapter is an **Anker 747 GaNPrime 150 W**, with the laptop as its only load on the port the user calls the laptop port. The user reports a 100 W-rated Anker cable and explicitly asked not to keep blaming or swapping the cable. Anker's single-port 100 W specification is consistent with the captured contracts.

The meter is a **ChargerLAB POWER-Z KM003C**. Its logging USB connects to the local Linux machine, separately from the laptop's charging path.

Keep three quantities distinct:

- **Negotiated PD contract:** what source and laptop agree may be supplied. A 100 W contract does not mean 100 W is actually being drawn.
- **Meter input power:** voltage times current through the meter, including laptop operation and charging demand.
- **Windows battery charge rate:** battery-side telemetry, which can lag or be transient during startup. It is not PD contract data or total input power.

The first Windows reconnect observation was confounded by a 96% battery. The user then discharged the battery with a benchmark and stopped it for the low-SOC comparison. A file named `pd-50pct-attach.jsonl` actually contains a test starting at **10%**, not 50%; use recorded telemetry rather than filenames.

Generic ACPI thermal data was available, but the battery-temperature WMI class returned no instances. Do not treat the generic approximately 50 °C reading as actual battery temperature.

## Behavioral tests and what changed our understanding

Early user observations included about 90 W in BIOS, a drop during the Windows spinner, stock ASUS sustaining about 93 W before tapering, and occasional third-party recovery after reconnecting or using ASUS first. Those observations motivated controlled captures; not all were immediately reproducible.

| Test | Observed result | Supported conclusion |
| --- | --- | --- |
| Early third-party reconnects | Some restored high power; others did not | Reconnect recovery is intermittent. |
| Controlled UCM-UCSI PnP restart | About 43.2 W before and 42.2 W after; no 30 W plateau | Restarting that device did not reproduce the drop. It is not proof that a specific PPM_RESET command executed. |
| Low-SOC Windows Anker → ASUS → Anker | About 29.35 → 70.13 → 29.29 W at roughly 10–13% battery | Adapter/path/state matters; high-SOC taper alone does not explain the contrast. |
| Contracts during that comparison | All completed 20 V, 5 A negotiation | A lower negotiated contract does not explain the recorded low state. |
| Warm restart into BIOS with Anker | About 29.4 W persisted | Being in BIOS alone does not clear the low state. |
| Fresh laptop-power reconnect in BIOS | New 100 W contract, still about 29.4 W | A BIOS attach can produce the low state. |
| Anker wall-power cycle in BIOS | New 100 W contract, brief approximately 60 W peak, then about 29.5 W | Source wall cycling did not restore sustained high input in that run. |
| Subsequent BIOS-to-Windows transition | Low input persisted, no observed new PD negotiation | That run did not demonstrate a new Windows-caused drop because it was already low. |
| After user-reported connected-power reset, BIOS ASUS comparison | Anker 29.48 W; ASUS 92.37 W | Laptop demand/battery state allowed much higher power on ASUS. |
| Return to Anker in that BIOS run | About 66.64 W sustained, later about 66.8 W | Higher Anker draw was captured after ASUS, without entering Windows between swaps. |
| Higher-power Anker BIOS state → Windows resume | About 67 W fell to about 29.3 W, no captured PD renegotiation | The user's transition symptom was reproduced, but exact driver or firmware cause was not identified. |
| Attempt to recreate high state for full-startup test | ASUS 93.92 W; Anker brief 66.7 W then 29.46 W | ASUS-first does not reliably restore high Anker power. |
| Another Anker reconnect in BIOS | Initial 15 W then 100 W contract, brief 68.2 W peak, then 29.46 W | Repeated reconnecting did not establish a deterministic high-power starting state. |
| Confirmed full Windows startup with Fast Startup disabled | BIOS about 29.5 W; Windows median 29.31 W; no new captured PD events | A fresh startup did not clear the low state. It did not test a fresh startup from high power. |

Low-state traces contain brief peaks well above 30 W. Describe a sustained approximately 30 W state, not an absolute instantaneous ceiling.

### PD contract details

Repeated successful high-capability negotiations used fixed PDO4 `0x000641F4`, representing 20 V and 5 A, and laptop RDO `0x4387D1F4`, requesting operating and maximum current of 5 A. Source Accept and PS_RDY followed.

Anker often initially advertises and supplies 20 V at 0.75 A, then updates capabilities to 100 W. ASUS advertised 100 W immediately in the paired test. This difference is a plausible timing/input condition for further investigation, but firmware already contains an explicit path for an increase from power class 15 to 100. Do not claim that refresh is absent.

Some reattachment captures lack the earliest capability/request frames. Absence of those frames is not evidence that the initial 15 W stage did not occur.

### Windows resume and full startup

The successful high-power BIOS-to-Windows transition test returned through a resume path: Windows retained the earlier kernel boot time, reported Kernel-Boot event 27 type `0x1`, and logged return from low power. The Windows clock was temporarily about 25 minutes behind the logger before correcting, so correlate with collector UTC and event sequence rather than blindly aligning OS timestamps.

Fast Startup was enabled (`HiberbootEnabled=1`). It was temporarily set to zero, with the original value saved locally and on the laptop. The later startup reported boot type `0x0` and a new kernel boot time of **2026-11-18 22:19:12.5 UTC**. That confirmed the intended full-startup test, but the starting Anker state was already low.

The original value **1 was restored and verified** after the test. No ASUS service or driver was disabled. Do not describe Fast Startup as fixed, ruled out as a contributor to the earlier transition, or currently disabled.

### EC reset discussion

The user confirmed performing the requested 40-second power-button procedure with power connected and later instructed us to proceed on that assumption. Do not ask them to confirm the same action again.

ASUS's dedicated EC-reset guide says to connect the adapter during the hold; its general boot-failure guide contains conflicting disconnect-adapter wording. A model-specific electrical requirement was not established. Record the procedure as user-reported, rather than claiming that a reset counter or all EC state was instrumented. This uncertainty does not erase the observed charger comparisons.

## Current firmware hypotheses

The remaining behavior could involve stale cached current, a failed charger-mode transition, an OS-triggered policy change, or an electrical/input-regulation condition. The traces distinguish contracts from actual consumption but do not identify which register or code path causes the difference.

### Current selection and charger transition

The strongest static lead is the path from PD current caching to charger configuration:

| Location | Meaning established by disassembly |
| --- | --- |
| `81310 + 8*port` | Raw PD current cache; accessor at capsule `48620` reads command 35 and scales a 10-bit field by 10 |
| `81312 + 8*port` | Voltage cache; accessor `48668` reads command 34 and scales by 50 |
| `81314 + 8*port` | Computed power-class cache |
| `81320 + 2*port` | Derived current: 95% of raw below 4921, otherwise 4674 |
| `81203`, `81204`, `81206` | Selected power class, voltage, and current |
| `81300 + port`, bit 5 | Change indication used in refresh/reselection |
| `81241`, `81242` | Charger sequence enable and step |
| `81243`, `81244` | Latched transition error and retry counter |
| `81245`, bit 0 | Busy condition consulted by dispatcher |
| `81250`, `81251` | Charger transport read/write error counters |
| `80410` | State used to select retry budget; numeric value 5 is not established as “Windows” |

The patched VID acceptance leads to sequence enable and step 1; it does not directly set the charger current. Dispatcher `47D52` refuses progress when busy, disabled, or error-latched. Step 13 at `47AE6` polls charger register `3A`, bit 3, and can latch `81243=1` and clear enable if the retry budget is exhausted.

Completion at `43334` writes selected current `81206` to charger register `3F`, then 107% of that value to register `47` with a 10000 clamp, and also programs `3E`. Another pending-bit path can apply the same current settings. A transition failure before completion could therefore prevent this current-setting path, but these internal values have **not been captured in the live low state**.

The register map and sequence strongly resemble **Renesas ISL9241**. Actual charger IC identity and sense resistance remain unverified. Candidate live reads include `3A` status, `3F` and `3B` current limits, `39` control, `4B` VIN limit, `4C`/`4E` control, and `87` VIN ADC. Do not assume 1.5 A measured at the meter proves a particular register is at a 1.5 A default.

The transition reset delay is 40 counter units, not a verified 40 milliseconds. An adjacent failure helper at `47B98` is skipped by the investigated dispatcher; do not count it as an executed check.

### Status does not establish successful completion

ASUS WMI `DSTS(0x0012006C)` returned `0x0001002A` in both high and low states. The underlying PDST byte is a bitfield, and its qualification bit can be set without proving charger-sequence completion. The original simplistic interpretation that only exact `PDST==03` means accepted is superseded.

The retained `==100` checks are not currently justified patch targets. Neither wire contract success nor WMI qualification proves the internal cache and current-limit registers contain the expected values.

## Why the live internal snapshot is still blocked

UEFI read success does not automatically provide a safe Windows reader. Windows drivers, ACPI, and SMM may concurrently use EC interfaces; rebooting to a UEFI diagnostic can change the state being investigated.

- Normal PMC command `92` maps offsets below `1000` to `80000+offset`, but offsets at or above `1000` map to `F00000+offset`. Sending `1240` does **not** read RAM `81240`.
- Installed signed PawnIO/LpcACPIEC permits ports `62/66`; it cannot provide the proven relocated `25E/25F` direct access. Upstream LpcIO support and its private ASUS mutex warning do not establish a usable installed route.
- Saved AsusSAIO has a serialized OEM transaction IOCTL `0x80102070`. Its private driver mutex helps with driver transactions but does not prove SMM exclusion. It is not arbitrary port I/O.
- OEM `CC` is an indexed flags/pointer table, not arbitrary RAM. None of its 255 entries exposes the needed `81200..8125F` state. Additional `DA/D6/D4/CE/C6` tables and the inspected `EA` getter likewise do not expose the target ranges.
- OEM `D0/D2/D3` invoke control functions. They were not executed as read diagnostics. `DE` configures a battery-oriented SMBus task, not an established general charger-register reader.
- ACPI `RRAM`, despite its name, invokes an OEM mailbox path. `RP2E` is restricted to a bank/range, and `REBC` has mailbox side effects. No arbitrary safe AML read interface was established.
- Other inspected AsusSAIO requests expose CPUID or CPU/chipset telemetry. Some internally write MSRs; they are not substitutes for a read-only EC transport.

No speculative raw Windows EC access or charger-register writes were performed to overcome this blocker. See [Windows read-route audit](out/update_415/boot_policy_analysis/windows-read-route-audit.json), [OEM CC table](out/update_415/boot_policy_analysis/oem-cc-table.json), and [additional read tables](out/update_415/boot_policy_analysis/oem-additional-read-tables.json).

## Meter tools and operational lessons

The existing `powerz-logger` in `/home/ar/src/github/andrewrabert/tools` was inspected and used for ADC/hwmon logging. It does not provide the raw PD negotiation evidence needed here. No changes were made to that separate repository.

[tools/km003c_pd_capture.py](tools/km003c_pd_capture.py) implements a bounded passive capture based on upstream `km003c-rs`, preserving raw USB TX/RX with UTC timestamps. It avoids the upstream default USB reset and all-interface detach. Its command allowlist covers connect/disconnect, monitor enable/disable, and status/event polling, not PD triggers or source programming.

[tools/decode_km003c_capture.py](tools/decode_km003c_capture.py) decodes status and raw events. It is not a complete automatic semantic PD analyzer; contract claims were checked against packet objects and Accept/PS_RDY events.

Typical invocation, using a new filename:

```sh
python tools/km003c_pd_capture.py --seconds 900 --output out/update_415/boot_policy_analysis/NEW_CAPTURE.jsonl
python tools/decode_km003c_capture.py out/update_415/boot_policy_analysis/NEW_CAPTURE.jsonl
```

Before asking the user to do anything, verify that capture actually reports active and samples are arriving. Several early attempts failed because of stale/lagged connection acknowledgements. The tool gained bounded queue draining and connect retries, and a logging-USB replug resolved the observed stale session. Preserve failed capture files as failed attempts, not evidence of a recorded negotiation.

Earlier USB access used per-device ACLs that disappeared on re-enumeration. The latest device was `/dev/bus/usb/003/008`, mode `0660`, group `wheel`; the agent session is in `wheel` and successfully opened it. The user arranged group access. Always run `lsusb -d 5fc9:0063` and check the actual current node before requesting sudo; do not reuse an old bus/device number.

The user sometimes took longer than the 15-minute recording window. One reset attempt was not captured, and the meter was absent when recording was restarted. Do not infer reset timing from that gap. A later capture recorded BIOS power, not the earlier physical hold itself.

Stop the exact logger with SIGINT and verify `cleanup_complete` in the raw file. A KeyboardInterrupt traceback with successful finally-block cleanup is expected. Do not start two processes competing for the meter.

## Evidence map for charging tests

All paths in this table are under [out/update_415/boot_policy_analysis](out/update_415/boot_policy_analysis/).

| Files | Purpose |
| --- | --- |
| `pd-50pct-attach.jsonl`, `pd-lowbattery-adapter-comparison.json`, `pd-lowbattery-thirdparty-return-steady.json` | Low-SOC Windows Anker/ASUS/Anker comparison; filename 50pct is stale |
| `pd-bios-windows-v5.jsonl`, `bios-fresh-attach-summary.json`, `bios-wall-reset-summary.json`, `bios-to-windows-comparison.json` | Low state reproduced in BIOS, reconnect/wall cycle, then Windows |
| `pd-ec-reset-v1.jsonl` | Recording expired before the later BIOS report; does not prove reset timing |
| `pd-ec-reset-bios-v3.jsonl`, `bios-reset-asus-comparison.json`, `bios-reset-anker-return.json` | BIOS 29.5 W Anker → 92.4 W ASUS → 66.6 W Anker |
| `bios-high-to-windows-comparison.json`, `bios-high-to-windows-boot-events.json` | Higher Anker state falls after Windows resume, with no new captured PD events |
| `windows-resume-inventory.json` | ASUS services, UCSI devices/drivers/log inventory, Fast Startup setting |
| `pd-full-boot-v1.jsonl`, `full-boot-asus-bios-baseline.json`, `full-boot-anker-bios-baseline.json`, `full-boot-anker-bios-reattach.json` | Attempt to recreate high state before full startup; Anker stayed low |
| `full-boot-windows-status.json`, `full-boot-comparison.json` | Confirmed full startup and persistent approximately 29.3 W input |
| `fast-startup-test-setting.json`, `full-boot-setting-restored.json` | Original setting, temporary change, verified restoration |
| `diagnostic-targets.json`, `post-contract-dataflow-audit.json`, disassembly excerpts | Static charging-state investigation and desired live snapshot |

Earlier UCSI restart evidence is under [out/update_415/windows_power_policy](out/update_415/windows_power_policy/). Remote working files are under `C:\Users\user\AppData\Local\Temp\flowx13-ec-audit`; the Fast Startup backup is `fast-startup-before-test.json` there.

## What to do next

### Prioritize a discriminating internal measurement

The most valuable result would be matched low/high snapshots of the selected current, PD current caches, sequence step/error, bus error counters, and actual charger current/mode registers. Establish a synchronized transport and validate address mapping before reading these under Windows. A successful read from an unrelated bank does not validate the target RAM alias.

Continue tracing stock OEM/ACPI/SMM interfaces only where they plausibly expose those values or a useful telemetry alias. The already-reviewed table getters do not. A dedicated signed/read-only driver or module may be necessary, but its port access, locking, and interaction with firmware need validation. Do not bypass those requirements with speculative mapping writes or an arbitrary general-purpose flasher.

Use the snapshot to distinguish concrete cases:

| Observation | Next code path to investigate |
| --- | --- |
| Raw PD current is high but selected current remains low | Cache refresh and source reselection |
| Selected current is high, sequence error is latched or step never completes | Charger transition, retry conditions, and ignored transport errors |
| Selected current is high but charger limit remains low | Completion/pending-bit writes and competing writers |
| Charger limits are high but input remains low | Charger mode, VIN regulation, actual electrical conditions, and system/battery demand |

### Trace OS transitions without assuming UCSI is responsible

Follow ACPI initialization/wake and EC state changes into the charging routines. The `B1` command changes a flag at GP-relative `212B`, but its charging connection remains unresolved. Map state `80410` rather than calling value 5 “Windows.” A direct UCSI device restart already failed to reproduce the drop, so repeating it unchanged is low value.

If live tracing is added, prepare it before asking for another boot: use collector UTC, preserve raw events, distinguish resume from full startup, and monitor actual meter input. A future full-startup **high-to-low** comparison still needs a verified high baseline; the completed full-startup test started low.

### Use another OS only for a defined comparison

Linux could help distinguish OS-specific behavior or provide a better-controlled access environment. It does not automatically solve EC locking, validate a RAM alias, or prove that Windows caused the existing low state. A useful live-Linux experiment would preserve a verified charger state and compare transitions/behavior under known conditions. Prepare the image, logging, and expected observation first; then ask the user for the physical boot only if needed.

A controlled Windows service or Safe Mode comparison is another possibility. Inventory alone does not incriminate ASUS services, and stopping a service after it has already changed a latched state can give an inconclusive result. Choose one variable, record original settings, and restore them afterward. No such service-disable experiment has yet been performed.

### Avoid more undirected physical tests or patches

Repeated reconnects have shown variability, not a reliable workaround. Do not keep asking for ASUS/Anker swaps without a new measurement or a specific prediction. Do not remove the secondary `==100` checks, force the charger sequence onward, disable current/voltage protections, or write guessed charger registers based only on the meter plateau.

Before any future firmware change, identify the exact condition being changed, explain how evidence links it to the symptom, preserve current host/device-specific data, and validate the new candidate and failure behavior. The previous deployment route is demonstrated on this machine; that is not permission to treat every new patch as ready.

## External references already used

- [ASUS dedicated EC reset procedure](https://www.asus.com/support/faq/1050239/) — adapter-connected instructions and 20/40-second variants.
- [ASUS boot failure troubleshooting](https://www.asus.com/uk/support/faq/1014276/) — conflicting adapter-disconnection wording discussed with the user.
- [Microsoft system power states](https://learn.microsoft.com/en-us/windows/win32/power/system-power-states) — Fast Startup preserves kernel state; distinguish it from full startup.
- [Anker 747 product information](https://service.anker.com/product-description/a085g000004xGpeAAE) — advertised adapter capabilities.
- [Renesas ISL9241 operation modes application note](https://www.renesas.com/en/document/apn/r16an0002-isl9241-operation-modes-application-note) — comparison for charger sequence and register meanings, not live chip identification.
- [Chromium EC ISL9241 definitions](https://chromium.googlesource.com/chromiumos/platform/ec.git/+/08f5a1e6fc2c9467230444ac9b582dcf4d9f0068/driver/charger/isl9241.h) and [USB PD definitions](https://chromium.googlesource.com/chromiumos/platform/ec/+/master/include/usb_pd.h).
- [PawnIO modules](https://github.com/namazso/PawnIO.Modules) — interface restrictions and synchronization caveats.
- [KM003C library](https://github.com/okhsunrog/km003c-rs) — passive monitor protocol reference; investigated commit `c063388ecab1a169326de1b57e650a3a45fc94f6`.

The immediate unresolved engineering question is: **which cached value, transition failure, or charger condition explains why the same Anker can draw about 67 W in one state and about 29 W in another despite a successful 100 W contract?** The next useful work should answer that question rather than repeat deployment or declare the charging restriction solved.
