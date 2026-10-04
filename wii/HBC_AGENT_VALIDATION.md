# HBC agent validation — 2026-10-02

Built against devkitPPC r50 / GCC 16.1.0 and libogc 3.1.0. The external
HBC-Reborn SDK was rebuilt with the same toolchain inside ignored deps.

| Check | Result | Frozen artifacts under `wii/.deps/runs/` |
|---|---|---|
| Dolphin GX32 smoke | PASS | `dolphin-MBiUHm/` |
| Wii GX32 smoke | PASS | `hardware-4fJeJi/` |
| Wii live tools and exit | PASS | `hardware-p2G5Vd/` |
| Wii deliberate DSI and recovery | PASS | `hardware-4epzQH/` |
| Final probe-free Dolphin GX16 smoke | PASS | `dolphin-nL7ix7/` |
| Final probe-free Wii GX16 smoke | PASS | `hardware-SSAl13/` |

The smoke checks covered native GX output, CPU fallback coherence, glyphs,
cursor, PDF export, download and SD cache. The live-tools test retained
`agent-status.json`, verified a staged SD file byte-for-byte, captured
`agent-screen.png` and requested exit through HBCX. The Wii returned to HBC 1.9.3.

The explicit crash build recorded exception 3 (DSI), DAR `00000010`, and PC
`8019e934`. Its matching ELF resolved the PC to `wii_agent_test_crash` at
`frontends/framebuffer/wii_agent.c:18`. The Wii returned automatically to HBC;
no manual recovery or power-off was used. Existing crash evidence was retained.
The deliberate probe must be removed from ordinary development packages.

The 32-bit agent smoke reported MEM2 high `0x917c6f40` and 24,923,968 arena
bytes remaining. This was the initial conservative agent layout, subsequently replaced by
an allocation-based reservation that restores both sides of MEM2. See the development-memory tradeoff in `README.md`.

Host checks passed for memory reservation, network ordering, single agent
initialization, cooperative exit, disabled builds, foreign-app refusal and
crash evidence. Existing blitter, presenter and browser regression checks also
passed. Dolphin validates DOL rendering; the physical Wii validates installed
HBC return and persistent exception recovery. These runs are functional checks,
not new rendering speed measurements. Registered PC logging is integrated but
was not exercised with an active PC log receiver in this series.

## Frozen DOL hashes

- Dolphin GX32 smoke: `ab43f9e41f3d2ffb18d3d0eab9fce6854f6dd9dd38f652ab0f2cb2a9f171546c`
- Wii GX32 smoke: `ab43f9e41f3d2ffb18d3d0eab9fce6854f6dd9dd38f652ab0f2cb2a9f171546c`
- Wii live tools and exit: `ab43f9e41f3d2ffb18d3d0eab9fce6854f6dd9dd38f652ab0f2cb2a9f171546c`
- Wii deliberate DSI and recovery: `a96f8c4078f4bf9a6bcfb33da17ae7732b2c4a53b7545fde2344c9af061dee0b`

## Initial development package

The final package uses `HBC_AGENT=1`, `HBC_AGENT_CRASH_TEST=0`; its ELF
contains no `wii_agent_test_crash` symbol. Both final GX16 smoke tests passed.
The final Wii run returned to HBC without a new crash. The preceding probe
report remains in `hbc-before.json`; the SDK cleared its console record when
the new app initialized, and `hbc-after.json` reported no crash.

- Final DOL: `60e3881121a719d72dba1e4fa3d4e397b3a0806a2a392468b1eee1bd6dadcc7d`
- Final ELF: `d49476286766c3e1aa27ca90cbd866257859ace8723057863f942d50c630825a`

## Restored MEM2 capacity — current layout

The Arena2 cap has been removed. Startup retains one normal 1 MiB malloc
allocation that contains HBC's complete 4 KiB record page, then frees the other
temporary allocations. Both surrounding MEM2 regions remain usable by newlib;
SDK arena highs and IOS reservations are unchanged by application code.
Compile-time checks require the SDK's log/crash records to fit that page.

| Check | Result | Frozen artifacts under `wii/.deps/runs/` |
|---|---|---|
| Dolphin GX32 + MEM2 capacity | PASS | `dolphin-zKbtrr/` |
| Wii GX32 + MEM2 capacity | PASS | `hardware-UjV07w/` |
| Wii capacity then deliberate DSI/recovery | PASS | `hardware-bjO7MO/` |
| Final Dolphin GX16 + MEM2 capacity | PASS | `dolphin-iVdQRT/` |
| Final Wii GX16 + MEM2 capacity | PASS | `hardware-rJtYMZ/` |

Every capacity test allocated **52,428,800 bytes (50 MiB) of MEM2 at once**.
Each 4 KiB page was written and checked; allocations were checked against the
runtime SDK high bound and HBC record page. The record page remained unchanged
and temporary buffers were freed. The retained allocation is 1,048,576 bytes.
The SDK's final high bound was `0x933a6f40`.

The combined hardware pressure/crash run then generated DSI (3), DAR `0x10`,
PC `8019ee40`. The matching ELF identified `wii_agent_test_crash`; HBC 1.9.3
returned automatically. No agent relocation, HBC installation change, manual
recovery, or power-off was required.

The final package is again probe-free (`HBC_AGENT_CRASH_TEST=0`). Its Dolphin
and Wii GX16 smoke tests passed along with the same 50 MiB diagnostic. Browser
heap-used counters now subtract the observed MEM1-to-MEM2 address gap; raw
newlib/SDK agent counters can include that gap. Arena remaining bytes exclude
free chunks already owned by malloc and do not measure all available MEM2.
See `README.md` for that accounting and the reproduction commands.

Current frozen DOL hashes:

- Dolphin GX32 + MEM2 capacity: `44dbca089a50ad667a0e415d083c2b5cfc507cfe443ad0540200907909439e81`
- Wii GX32 + MEM2 capacity: `44dbca089a50ad667a0e415d083c2b5cfc507cfe443ad0540200907909439e81`
- Wii capacity then deliberate DSI/recovery: `20588c956effdb2f6712d84761549e4559faf93835d81aaee40ce57ff62e9002`
- Final Dolphin GX16 + MEM2 capacity: `8088e107e59dae11871f3e112be3125479788232c5f7de38379f130e903f2000`
- Final Wii GX16 + MEM2 capacity: `8088e107e59dae11871f3e112be3125479788232c5f7de38379f130e903f2000`

Final ELF: `294dc8984e671b24402326fa4ce04ae1e5c27d70167aeffe92a981cdc9d73d0e`

## Freeze follow-up — 3 October 2026

The user confirmed a frozen Wii during lease `20261003-132925-d952f5`
(`hardware-IgLt4r`). The host never observed its agent and could not reconnect.
After manual recovery, a read-only leased collection
`20261003-134816-cafa7c` found no smoke report and no crash record. This does not
identify the freeze's exact location or establish that it happened in cleanup.
That run remains a failure.

Added lifecycle checkpoints, agent startup before first navigation, bounded
network readiness, a 250 ms maximum idle event wait, and a development-only
watchdog. Cleanup phases allow ten seconds without progress; startup/main-loop
stalls allow sixty seconds. Emergency returns use `_Exit` to avoid reentering
blocked libc/SDL cleanup. Checks distinguish a graceful final checkpoint from a
forced return. Configuration restoration happens only after verified HBC, and
restoration errors fail the job.

Observed on the physical Wii:

| Test | Lease / artifacts | Result |
| --- | --- | --- |
| Cooperative remote exit | `20261003-135627-7c2b8e`, `hardware-6DE49s` | Agent file read and screen capture worked; final lifecycle checkpoint; HBC 1.9.3; no crash. |
| Deliberately stalled cleanup | `20261003-140038-2554a5`, `hardware-hzQ3aE` | Main thread slept in the probe; watchdog returned to HBC; retained `stage=shutdown stall probe`; no crash. |
| GX + JavaScript + native filter smoke | `20261003-140619-34ea34`, `hardware-cAddXS` | All checks passed; final checkpoint; HBC; no crash. |
| Strict global-script regression + full smoke | `20261003-141227-8521c5`, `hardware-lTUzPx` | All checks passed; final checkpoint; HBC; no crash. |

Strict-global regression DOL SHA-256:
`a5eeba0282fe75ebc703559ed818222956999186a1fed805b01a40360a4612d7`.
Matching ELF SHA-256:
`3c45e1e1196f878698f3041cb9309302ffecc73a2093d86e05b8863acaacc080`.
Matching Dolphin regression: `dolphin-rgPcUo`, all checks passed with the same DOL.
Host tests execute the real watchdog decision, including unsigned clock wrap,
and check that the forced return occurs at the ten-second deadline. They also
exercise watcher rejection of an incomplete cooperative exit and the actual
shell restoration path.

The final build additionally protects DOM title-reference cleanup when a
JavaScript allocation throws and returns through `_Exit` if the HBC memory
reservation fails. Host tests cover both failure paths, including title
allocation beyond the engine's 8 MiB budget.

Final DOL SHA-256:
`ae4fcb12e14dd1eccf76cb8086ca5bd522b97ab7a4dab28b5b473bb96eb67e6a`.
Matching ELF SHA-256:
`034f5ce53f55f817939a67c961ce17570fb7f8c4e8fba910c4fe8814504b52a5`.
Both `dolphin-L1xbpT` and physical Wii lease `20261003-143218-d85e3e`
(`hardware-tO6auo`) passed the full GX/JavaScript/request-filter smoke. The Wii
recorded `stage=return to HBC`, HBC 1.9.3, and no crash. Its cleanup succeeded
after verifying HBC; the pre-test absence of Choices was restored and existing
filter files were preserved. No Dolphin process remained after testing.

The settings left by the original failed run were separately removed under
lease `20261003-141836-76d7e2`, after comparing their exact bytes against that
run's package and confirming that no Choices file existed before it.

The forced-return test covers a sleeping main thread with interrupts and the
scheduler working. It does not prove recovery from disabled interrupts, a
wedged kernel/IOS, or a stall before the watchdog starts after FAT mounting.
Repeated clean runs and a validated escape path are hardening evidence; the
original freeze's exact cause remains unresolved.

The subsequent readability/upstream review rebuilt both agent-disabled and
agent-enabled variants and passed their Dolphin smoke tests. Its artifact
hashes and checks are in [UPSTREAM_REVIEW.md](UPSTREAM_REVIEW.md). A physical
retest of that reviewed artifact could not start because the shared console
was unavailable under another project's lease; the pending job was canceled.
The physical results above apply to the earlier hashes recorded here.

## Reviewed-build hardware queue — 4 October 2026

The Wii is responding again. Eleven tests were submitted through the shared
lease dispatcher; at submission, Wii64 was running and earlier Wii64 and
WiiXplorer jobs were ahead of them. These are queued tests, not hardware passes.
Each job freezes its package, ELF, watcher, checker and test configuration.
Normal tests require a verified return to HBC before restoring user settings.
The queue serializes execution and does not interrupt another application's
lease. Consult the job's final log and retained captures before changing its
status below.

| Test | Lease queue job | Local artifact directory | Submission status |
| --- | --- | --- | --- |
| GX, 32-bit, JavaScript/filter | `20261004-013314-fcee74` | `hardware-BvpDVu` | Pending |
| GX, 16-bit, JavaScript/filter, 50 MiB MEM2 | `20261004-013411-d0e169` | `hardware-MjMITm` | Pending |
| Software, 32-bit, JavaScript/filter | `20261004-013503-3e55d9` | `hardware-HvidfE` | Pending |
| Software, 16-bit, JavaScript/filter | `20261004-013503-8670de` | `hardware-EYzYQU` | Pending |
| Legacy software, 32-bit, JavaScript/filter | `20261004-013503-8bb3b2` | `hardware-BKE5I8` | Pending |
| Legacy software, 16-bit, JavaScript/filter | `20261004-013503-fd8574` | `hardware-Y30XTi` | Pending |
| Cooperative agent exit | `20261004-013503-cacf9d` | `hardware-7ewHWH` | Pending |
| Stalled-cleanup watchdog | `20261004-013503-55a6d1` | `hardware-GybDOU` | Pending |
| All 13 requested URLs, JavaScript/filter | `20261004-013503-84403f` | `hardware-ZRmM1P` | Pending |
| Intentional crash and 50 MiB MEM2 pressure | `20261004-013545-a5fc22` | `hardware-f8eXfS` | Pending |
| Restore probe-free app; repeat GX 32-bit smoke | `20261004-014016-b5136b` | `hardware-FbUA4b` | Pending |

The first nine jobs and the final restoration job use the reviewed production DOL
`eb89c1e856cbe2a82336182c5513de7b5534f60742b7ddbf498c2365df7e774b`
and ELF
`3fca33d883a1ad33f0c2972d3253f16a8c40849f9f09e8676393fc8b88976b08`.
The crash job uses a separately built, explicitly armed probe: DOL
`319e6a5906db710d88ef32a09295b73aa8aad5fdce68e1845a4febf7de862c2c`,
ELF `e25cb1b4c16aea94d69de1889a31734249e888a9eeb20d4a3d9a747c18df6ac5`.
The normal probe-free package and reviewed ELF were restored locally after
freezing this diagnostic job. Both builds use devkitPPC 16.1.0, libogc 3.1.0
and SDK commit `3b1e9a4e04fbb1afb98f516a2446ef9789877f8f`.
The final job stages the probe-free package back onto the Wii and repeats the
normal smoke after crash recovery; its pass remains required.

All seven host checks passed again. The reviewed production artifact also
passed all six Dolphin A/B/A runs; see
[GX_RENDERER_BENCHMARK.md](GX_RENDERER_BENCHMARK.md). No Dolphin process
remained. Raw captures, test packages and the per-job hash manifest
`wii/.deps/runs/reviewed-hardware-20261004.json` remain ignored.
