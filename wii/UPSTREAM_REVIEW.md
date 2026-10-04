# Reviewing the Wii changes

The changes are grouped into focused commits: printing and file safety,
JavaScript bindings, optional request filtering, Wii rendering and development
support, then build/test tooling and documentation. Review them in that order.
The software renderer was moved from `framebuffer.c` to `renderers/soft.c`;
use `git diff --find-renames --find-copies` to distinguish that move from new
GX code. Original copyright notices remain with the moved code.

## Proposed upstream submissions

| Repository | Scope | Validation |
| --- | --- | --- |
| NetSurf | Printing failure handling, PDF replacement/cache, scheduler allocation failure, endian-independent generated icons | `test-regressions.py`, `test-render.sh`, target PDF/download checks |
| NetSurf | Global page-script compilation, listener identity/removal, title binding, generator dependencies | `test-javascript.py`, local page fixture in Dolphin and on Wii |
| libdom | Exclude the event target from its ancestor path; honor the bubbling flag | `patches/libdom-event-dispatch.patch`, local target/ancestor event fixture |
| libnsfb | Wii endian/input fixes and optional presentation/synchronization hooks | `patches/libnsfb-wii-endian.patch`, `test-render.sh`, `test-presentation.py`, GX target checks |
| NetSurf Wii port | Renderer selection, GX presenter, bounded SD resources, optional HBC agent and native hostname filter | Full browser build and Dolphin/dev Wii smoke |

Dependency patches are against the revisions listed in
`bootstrap-browser-deps.sh`. Submit each to its owning library separately.
The combined libnsfb patch includes the port's earlier endian/input work; split
those from presentation hooks when preparing individual upstream submissions.
The native filter defaults off and requires agreement on the browser retrieval
hook before a general NetSurf submission. HBC integration remains optional and
Wii-specific; its SDK and the hardware lease client are external dependencies.

The shared printing and JavaScript fixes also affect non-Wii builds. Host
regressions exercise their actual C functions, but this work does not establish
full runtime coverage of every NetSurf frontend. Run the owning project's
native test suite before merging there. Likewise, the event fixes do not claim
complete capture/once/passive/AbortSignal conformance.

## Local validation

```sh
python3 wii/test-regressions.py
python3 wii/test-javascript.py
python3 wii/test-request-filter.py
python3 wii/test-presentation.py
python3 wii/test-agent.py
python3 wii/test-sites.py
./wii/test-render.sh
WII_JS_TEST=1 WII_REQUEST_FILTER=hosts WII_RENDERER=gx ./wii/dolphin-test.sh
WII_JS_TEST=1 WII_REQUEST_FILTER=hosts WII_RENDERER=gx ./wii/hardware-test.sh
```

Host rendering tests require bootstrapped libnsfb headers/source. Hardware tests
must use the shared lease queue. Crash, memory-capacity and stalled-cleanup
probes are separate opt-in tests; a normal build never arms them. Licensed
fonts, SDK archives, AI instructions, generated packages and raw test captures
remain ignored. Benchmarks record artifact hashes and distinguish emulator
measurements from physical-Wii observations.

See [HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md) for shutdown evidence and
recovery limits, [SITE_COMPATIBILITY.md](SITE_COMPATIBILITY.md) for remaining
site failures, and [HARDWARE_PLAN.md](HARDWARE_PLAN.md) for future work. These
limitations should stay in a PR description rather than being presented as
completed compatibility or acceleration work.

## Review validation — 3 October 2026

Production source is committed through `4196e539d`. This review expanded dense
C/Python code using the repository formatting rules, named public renderer
parameters, preserved attribution, removed a redundant platform guard, and
corrected the support-library `PKGCONFIG` override and timeout error enum.
AI files and generated artifacts remain excluded from Git. Dependency patch
context is preserved with a scoped whitespace attribute.

All seven host checks above passed. Both `HBC_AGENT=0` and `HBC_AGENT=1` builds
completed with devkitPPC/GCC 16.1.0 and libogc 3.1.0. The non-Wii renderer
dispatch also passed `cc -fsyntax-only -Wall -Wextra -Werror`. This is a syntax
check, not a full non-Wii browser runtime test.

| Build | Dolphin profile | DOL SHA-256 |
| --- | --- | --- |
| Release, agent disabled | `dolphin-UcLppd` | `e020c5a63a9ebf66fcba7cf0359e246251780da9faf985694a3dacaa939ad5ad` |
| Development agent | `dolphin-Rvwdbd` | `eb89c1e856cbe2a82336182c5513de7b5534f60742b7ddbf498c2365df7e774b` |

Both full GX/JavaScript/hostname-filter smoke checks passed. The agent build
reached `stage=return to HBC`; both Dolphin processes were cleaned up. Matching
agent ELF SHA-256:
`3fca33d883a1ad33f0c2972d3253f16a8c40849f9f09e8676393fc8b88976b08`.

A new physical-Wii retest was queued as `20261003-200245-98df44`, but the shared
queue reported a foreign lease and the console as busy or off. The unstarted
job was canceled without touching that lease or the console. This reviewed
artifact therefore has Dolphin validation; earlier physical-Wii results in
`HBC_AGENT_VALIDATION.md` remain evidence for the previous artifact, including
normal exit and the stalled-cleanup watchdog. No fresh hardware pass is claimed.

On 4 October the console became available to the shared dispatcher. All seven
host checks and six additional Dolphin A/B/A runs passed. Eight physical tests
also passed: all six renderer/depth combinations, cooperative exit and stalled
cleanup. The GX 16-bit job allocated 50 MiB of MEM2 simultaneously without
changing the SDK high bound. All eight returned to HBC with no crash and
completed settings restoration.

The website survey failed during pre-launch HBC file operations after staging;
it produced no fresh website results. The console stopped responding, so its
cleanup could not restore temporary settings. The two unstarted follow-up
tests were canceled, and guarded settings restoration was queued. Identifiers,
artifact hashes and the unresolved console/network boundary are recorded in
[HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md). This failure does not establish
a NetSurf browsing crash.

Guarded settings recovery subsequently passed. The unchanged production build
then completed the 13-site physical survey and a matching Dolphin survey. The
separate expected-DSI/MEM2 diagnostic passed with matching symbols and HBC
recovery, followed by another passing production GX smoke that restored the
probe-free app. All eleven planned hardware checks now have passing runs;
the earlier staging failure remains recorded, with its cause unresolved.
Most tested websites still render blank, partial or verification pages. See
[SITE_COMPATIBILITY.md](SITE_COMPATIBILITY.md) for inspected captures and limits.
