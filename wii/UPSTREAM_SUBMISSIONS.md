# Submission preparation — 9 October 2026

NetSurf upstream was fetched from its [official GitHub mirror](https://github.com/netsurf-browser/netsurf)
on 9 October 2026: `39da3c3a40af4566d86500ff3052dfdc7f9a0378`
(`fixup options test data`, 19 February 2026). This is already an ancestor of
our branch. There are 38 local commits beyond it, including the original Wii
port, and 144 changed files before this submission-preparation work. Do not
submit the whole branch as one core-browser change.

NetSurf's [developer guide](https://www.netsurf-browser.org/developers/) asks
for patches through its tracker, development mailing list or IRC. The GitHub
repository is a mirror; verify the maintainer's preferred destination before
opening a GitHub PR. No patches have been sent and no PR has been opened.

## First submissions

Each of these patches was extracted and independently checked against the
upstream commit above. None requires the Wii port or another patch in this
list. Exported descriptions explain the trigger and resulting behavior.

| Suggested title | Size | Source | Evidence / remaining check |
| --- | --- | --- | --- |
| Framebuffer: handle scheduled callback allocation failure | 2 added lines, 1 file | `720d98b9a`, schedule.c only | Actual-function OOM regression passed; run upstream framebuffer checks |
| Framebuffer: emit endian-independent generated icon pixels | 1 file | `720d98b9a`, convert_image.c only | Wii rendering evidence; verify generated assets on a little-endian native frontend before sending |
| Build: quote paths passed to git-testament.pl | 1 changed line, 1 file | `720d98b9a`, tools/Makefile only | Independent apply check; this does not claim all Make recipes support spaces |
| HTML: respect declared types for external scripts | 1 file | `d7e96c633` | Classic/empty-type and unsupported module/data fixtures passed in Dolphin and on Wii; port fixture to upstream's native test setup |

Reproduce the exports without rewriting our branch:

```sh
git fetch https://github.com/netsurf-browser/netsurf.git master
python3 wii/prepare-upstream.py FETCH_HEAD
```

The command writes `.patch` attachments and short `.md` descriptions beneath
`wii/.deps/upstream-review/<base>/`, then checks each patch against a clean
archive of that base. Outputs remain ignored. An apply check is not a build,
behavior test or merge approval. Keep the base, relevant regression fixture,
and tested frontend in the eventual submission description; avoid benchmark
scores as a substitute for a reproducible defect.

## Changes to split or hold

- Printing/PDF: `720d98b9a` combines failure handling, Wii's borrowed-content
  export and a PDF image cache. Extract generic failures first, with native
  printer fixtures. File replacement/rollback needs both POSIX and libfat
  checks. Keep borrowed-content export with the Wii port.
- JavaScript: `7533979b7` combines globals, listeners, title bindings and build
  dependencies. Split those before submission. Cookie capability, screen,
  rectangle snapshots, detached images and live style/dataset support each
  deserve a separate change and native tests. Screen needs frontend API
  agreement; geometry does not synchronously flush pending layouts.
- Feature detection: the generator patch belongs to **nsgenbind**, not NetSurf.
  Prove reflected attributes survive while missing implementations disappear;
  coordinate the generator revision with the browser bindings change.
- Event dispatch: the patch belongs to **libdom**. Keep target/bubbling tests
  with the library; do not bundle it into a browser PR.
- Dynamic layout: hold `de7dde94e` pending native frontend/ownership tests and
  maintainer review. The synchronous full-tree replacement temporarily holds
  two box trees, resets selection, and skips framesets/live iframes. This is
  a significant architectural change despite its useful Wii result.
- Wii renderer/agent/filter/tooling: the main `4196e539d` commit is too broad.
  Separate build/input/platform support, the software-renderer move, GX,
  optional agent and developer tooling. Hostname filtering changes retrieval
  behavior and should be discussed independently. Keep run diaries out of
  core submissions; link concise validation and remaining limits.
- libnsfb: split endian/input fixes from presentation/synchronization hooks.
  Submit to that library; its patch must not be hidden in a NetSurf change.
- libwupc: its libogc compatibility patch belongs to that dependency.

Preserve original authorship/copyright when extracting patches. Do not
force-push or rewrite the shared development branch to make this split.
Use a clean upstream base for each submission; existing commit boundaries
are evidence and starting points, not a ready-made review series.

## HBC-Reborn dependency

`wii/hbc-reborn.env` pins latest upstream HEAD verified on 9 October 2026:
**1.10.2**, `0c2e3d9f7f8689d9c1dd733ed2d5ec9c6deb70f7`.
The README in that external checkout still says 1.10.1; the authoritative
channel config and latest commit say 1.10.2. Historical test documents retain
the versions actually tested, including 1.10.0; do not relabel those runs.

SDK builds reject another revision or modified tracked SDK/channel/tool
sources. The hardware harness checks the same source, directs the installed
queue launcher to it while preserving shared queue state, rejects stale
agent packages, and requires the leased Wii to report 1.10.2 before staging.
CI builds both agent-disabled and agent-enabled packages and checks out this
exact SDK revision for the latter. Build metadata records the version and
SDK commit. No SDK source is vendored and no HBC checkout is rewritten.

The guard does not install or replace the console's channel. If the Wii is
running an older HBC, run the current HBC DOL through the shared lease queue
before testing; NAND installation is a separate operation. A future upstream
release needs an explicit pin update and fresh SDK/browser validation.

## Validation of this preparation

The four exports independently passed `git apply --check` against the fetched
upstream archive. Host checks passed: regressions, HBC revision/runtime gates,
agent lifecycle/log cleanup, generated bindings, logging, actual Duktape,
DOM/layout capture and site collection. Shell syntax and whitespace checks
passed. CI configuration was reviewed, but its new matrix has not run here.

The rebuilt 1.10.2 agent package passed isolated Dolphin smoke
`dolphin-vS2vnq`: JavaScript, GX/colors, PDF, download, SD cache and the final
`stage=return to HBC` checkpoint. DOL SHA-256:
`9983e39e9c18661126400941432a31b8887d91b1fdeee52f5b99893f54fb598a`;
matching ELF:
`3ac23bb91267df0ca0778202114619193327ddd12c3397aca48495061ebf212d`.

The first smoke (`dolphin-mw8Q69`) exposed a premature fixture check: the
harness inspected the title on CONTENT_STATUS_DONE, before asynchronous checks
finished. It now waits for the fixture PASS title within the existing bounded
poll loop. The site-collector regression also now expects the DOM/layout
captures actually collected. These are developer-test changes, kept separate
from core upstream patch candidates.

No physical-Wii retest was performed for this artifact: the shared lease was
held by another project's WiiStation job when checked. No console control,
channel replacement or NAND installation was attempted. Earlier hardware
passes apply to their recorded artifacts, not this updated SDK package.
