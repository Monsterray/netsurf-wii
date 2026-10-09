# NetSurf on Wii

Wii port maintained by quatric <quatricsoftware@gmail.com>.

This is an experimental port of the complete NetSurf framebuffer browser to
the Nintendo Wii. It cross-compiles NetSurf and its support libraries for
PowerPC/Gekko, uses SDL 1.2 and libnsfb for display, and uses the Wii curl and
mbedTLS packages maintained by rw-r-r-0644 for HTTPS.

The resulting application is under `wii/dist/apps/netsurf/`. Copy that whole
directory to `sd:/apps/netsurf/` and start it from the Homebrew Channel. The
CA bundle, Messages catalogue, CSS, and built-in pages must remain beside
`boot.dol`. With a USB keyboard, Ctrl+P exports the current page to
`sd:/apps/netsurf/netsurf.pdf`. Open
`file:///sd:/apps/netsurf/js-smoke.html` for a small JavaScript/DOM diagnostic
page.

## Controls

Connect a standard USB HID keyboard or mouse to either Wii USB port (a powered
hub is recommended when the SD/USB storage device is also in use). Devices are
hot-plugged, so they may be connected before or after NetSurf starts.

- USB keyboard: text entry, browser shortcuts, arrows, Home/End, Page Up/Down,
  function keys, and modifier keys work normally. Ctrl+P writes the current
  page to `sd:/apps/netsurf/netsurf.pdf`.
- USB mouse: relative motion moves the browser pointer; left, middle, and
  right buttons map to the corresponding browser buttons; the wheel scrolls.
- Wii Remote: aim with IR; A and B are left and right click. Without IR, use
  the D-pad to move the pointer. Home exits.

USB HID support targets boot-protocol keyboards and mice. It is experimental;
there is no compatibility guarantee or end-user support for particular USB
devices.

### Wii Remote troubleshooting

The Wii Remote cursor requires a visible Sensor Bar. Aim the Remote at the
screen, keep the bar within its field of view, and remain within the usual
Bluetooth range. If the cursor disappears, point the Remote at the Sensor Bar
again; the D-pad remains available as a fallback while IR is unavailable.
D-pad movement is intentionally slower than IR and is best used only to
recover the pointer or make small adjustments. Slow page loading is separate
from pointer movement and is expected on complex modern sites.

When testing in Dolphin, install the complete `apps/netsurf` directory into
Dolphin's emulated SD card. Opening `boot.dol` directly does not make sibling
host files visible as `sd:/apps/netsurf`, so the browser will start without
its Messages, CSS, or welcome page. Runtime progress is written to Dolphin's
OSReport log under the `NetSurf Wii:` prefix.

## Prerequisites

- Current devkitPro packages: devkitPPC r50 (GCC 16.1.0) and libogc 3.1.0
  or newer, with `wii-dev`. Run `dkp-pacman -Syu` before building;
  `check-toolchain.sh` rejects older SDKs.
- `wii-sdl`, `ppc-zlib`, `ppc-libpng`, `ppc-libjpeg-turbo`, `ppc-libwebp`,
  `ppc-freetype`, and `ppc-libexpat`
- Git, GNU Make, GNU flex, and a recent GNU bison
- Licensed `FOT-RodinNTLGPro-M.otf` and `FOT-RodinNTLGPro-B.otf` files in
  `~/Library/Fonts`, or another directory selected with `RODIN_FONT_DIR`

## Build

```sh
cd /path/to/netsurf-wii
./wii/bootstrap-network.sh
./wii/bootstrap-browser-deps.sh
./wii/build-browser.sh -j8
```

`build-browser.sh` uses cross-built NetSurf support libraries under
`wii/.deps/netsurf-workspace/inst-powerpc-eabi` and GNU libiconv under
`wii/.deps/iconv`. WebP comes from devkitPro, while libharu 2.4.6 is
cross-built under `wii/.deps/optional/prefix`.
The pinned FIX94 libwupc source is adapted to current libogc and installed
under `wii/.deps/input/prefix` by `bootstrap-input.sh`.
The nsgenbind patch omits unimplemented properties and methods from JavaScript
prototypes, so feature detection does not mistake placeholders for working APIs.
`bootstrap-browser-deps.sh` creates the local prefixes. They are intentionally
untracked. The
rw-r-r-0644 packages are also extracted locally because installing the older
libwiisocket package globally conflicts with socket headers now supplied by
current libogc.

The build copies FOT-Rodin NTLG Pro into the ignored application package at
`apps/netsurf/fonts`; the licensed source fonts are not copied into the source
tree. The framebuffer frontend does not currently consume downloaded CSS
webfonts, so Rodin is used for generic and named page font requests.

`RODIN_REGULAR_SOURCE` and `RODIN_BOLD_SOURCE` can override the two input font
paths. The GitHub Actions build uses those overrides with DejaVu solely to
produce a redistributable CI test package; local builds continue to use the
licensed FOT-Rodin NTLG Pro faces by default.

## Continuous integration

`.github/workflows/wii-build.yaml` builds in the current official devkitPPC
container and updates its SDK packages on pushes, pull requests, and manual dispatches. It bootstraps every
Wii dependency, verifies the DOL and package metadata, audits build provenance,
and uploads a checksummed `netsurf-wii-ci.tar.gz` artifact for 14 days.

Release publishing is intentionally not part of the build workflow. Releases
for `quatric/netsurf-wii` must be started separately and only after an explicit
approval to publish.

For a quick hardware/display/network diagnostic independent of the full
browser, `./wii/bootstrap-deps.sh && make -C wii package` builds the small
`netsurf-wii-smoke` application.

## Port architecture

```text
NetSurf core -> framebuffer frontend -> libnsfb -> SDL 1.2 -> libogc/GX
NetSurf fetcher -> libcurl -> libogc BSD sockets -> Wii network interface
```

## Current limitations

- Physical Wii testing has verified page/GX rendering, cursor presentation,
  PDF export, downloads, and SD cache reads/writes with the optimized renderer.
  An earlier PDF test was interrupted; the current test returned normally with
  no crash reported. USB device coverage remains limited.
- The Wii low-memory profile reserves memory for rendering: the in-memory cache
  is capped at 6 MiB, disk cache at 16 MiB, font cache at 512 KiB, and no
  decoded bitmap may exceed 4 MiB or 2048 pixels on either side. Oversized
  images fail to load instead of exhausting MEM2.
- JavaScript, background images, and image animation are disabled by default.
  The browser also limits itself to four active fetches (two per host) and
  blocks advertisements by default. Users may override these defaults in
  `sd:/apps/netsurf/Choices`, but doing so can reduce stability.
- USB keyboard and mouse input uses libogc's boot-protocol HID drivers. It is
  intended for ordinary wired devices; wireless receivers and composite HID
  devices need hardware testing and are not supported on request.
- Wii Remote channel zero's IR pointer and its A and B buttons are handled by
  SDL-wii's own event pump, which already emits absolute mouse motion and left
  and right mouse buttons for them. The `libnsfb` patch deliberately does not
  synthesise those a second time. It does call `WPAD_ScanPads()`, because that
  is what keeps SDL's handling supplied with fresh data, and it rate limits
  every hardware poll to 16 ms. Remotes two to four contribute their buttons
  through the patch's own path.
- `MALLOC_MEM2=1` lets libogc's malloc start in MEM1 and grow into MEM2
  when MEM1 cannot satisfy further heap growth. Both banks are available;
  `SYS_GetArena1Hi()` and `SYS_GetArena2Hi()` protect the SDK and IOS
  reservations. The port never extends those bounds to physical RAM limits.
  Cache and bitmap limits control allocations rather than partitioning RAM.
  A simple Dolphin page left 9.2 MiB of unclaimed MEM1 and 51.7 MiB of
  unclaimed MEM2, with MEM2 capped at `0x933b6f60`. Startup logs report arena
  remaining bytes separately from malloc's used/free heap bytes.
- The framebuffer defaults to 640x480x32; 16-bit RGB565 is available as a
  comparison setting. It reduces framebuffer/texture storage and conversion
  traffic, at the cost of color precision. Page decoding and layout memory
  are unchanged. Real-Wii profiling remains required before changing defaults.
- SDL owns the destructive USB HID queues; libnsfb no longer reads the same
  reports. The Wii blitter reads RGBA source pixels directly without a
  full-image conversion allocation, including scaled and tiled images.
- Wii U Pro Controllers are detected through libwupc before SDL initializes
  WPAD. GlowWii-style four-channel aggregation gives them precedence over
  GameCube pads. A/B click, the D-pad sends arrows, Plus/Minus send `+`/`-`,
  X/Y send Page Down/Page Up, and Home exits.
- WebP image decoding is enabled; JPEG XL is excluded to keep the browser and
  its dependency set smaller. PDF export uses libharu and a fixed output path;
  a Wii-native filename picker has not been implemented.
- JavaScript remains available through the bundled Duktape engine when enabled
  in `Choices`; `js-smoke.html` is a target-side JavaScript/DOM diagnostic.
  Modern sites can still exceed the Wii's memory or depend on browser APIs
  NetSurf does not implement.
- Cookies, Choices, the CA bundle, and the 16 MiB persistent HTTP cache live
  under `sd:/apps/netsurf/`. Downloads use `Downloads/`, sanitize filenames,
  and preserve existing files by choosing a numbered name. Partial transfers
  use a temporary file; PDF export keeps the previous file on write failure.
- Network startup is asynchronous so a missing Dolphin network configuration
  does not prevent the UI from appearing. The initial page is local; network
  requests wait while socket startup is in progress; initialization failure
  still produces a normal network error.

The `libnsfb` patch adds devkitPPC/newlib endian detection, Wii input
polling, and optional GX presentation and synchronization hooks. It is kept separate so it can be proposed upstream.
`bootstrap-browser-deps.sh` only applies it when it is not already applied, so
after editing `wii/.deps/netsurf-workspace/libnsfb` regenerate the patch with
`git -C wii/.deps/netsurf-workspace/libnsfb diff > wii/patches/libnsfb-wii-endian.patch`.

## Renderer plugins and display depth

The compiled renderer plugin is selected at startup in `sd:/apps/netsurf/Choices`:

```
fb_renderer:soft
fb_depth:32
```

`soft` uses software page drawing with batched GX presentation and a separate
GPU cursor. `soft-legacy` retains the original SDL presentation and software
cursor for comparisons. Both support 16-bit RGB565 with `fb_depth:16`; 32-bit
remains the default. Restart after changing either setting. Plugin callbacks
live in `frontends/framebuffer/renderer.h`, with the registry in `framebuffer.c`
and the software plugin in `renderers/soft.c`. Future plugins are linked into
the DOL and added to that registry. The GX drawing plugin is implemented and
selectable with `fb_renderer:gx`.

See [the hardware roadmap](HARDWARE_PLAN.md) for the architecture, memory rules,
remaining hardware opportunities, and validation requirements, and the
[presentation comparison](PRESENTATION_BENCHMARK.md) and
[GX validation/profile results](GX_RENDERER_BENCHMARK.md) for measured results. To compare
renderers in isolated Dolphin profiles:

```sh
WII_RENDERER=gx WII_DEPTH=32 ./wii/dolphin-test.sh
WII_RENDERER=gx WII_DEPTH=16 ./wii/dolphin-test.sh
./wii/profile-renderers.sh
WII_RENDERER=soft WII_DEPTH=32 ./wii/dolphin-test.sh
WII_RENDERER=soft WII_DEPTH=16 ./wii/dolphin-test.sh
WII_RENDERER=soft-legacy WII_DEPTH=32 ./wii/dolphin-test.sh
WII_RENDERER=soft-legacy WII_DEPTH=16 ./wii/dolphin-test.sh
```

## Verification and future work

See [UPSTREAM_REVIEW.md](UPSTREAM_REVIEW.md) for the commit review order,
dependency ownership, validation commands, and remaining upstream checks.

```sh
python3 wii/test-regressions.py
./wii/test-render.sh
python3 wii/test-presentation.py
python3 wii/test-javascript.py
python3 wii/test-bindings.py
python3 wii/test-survey.py
./wii/dolphin-test.sh
./wii/hardware-test.sh
```

Host checks cover printing setup, borrowed-page layout restoration, PDF write
failure and libfat replacement rollback, scheduler allocation failure, Home
quit propagation, and RGBA scaling/tiling with no scratch allocation.
The GX plugin accelerates page fills, images, and cached FreeType glyph quads.
Set `fb_renderer:gx` in Choices to select it; `soft` remains the default.
Target checks exercise clipping, alpha, mixed CPU/GPU drawing, image mutation,
scrolling and offscreen rendering.
The JavaScript engine check runs actual Duktape with the Wii allocator and
deadline hooks, including nested callbacks and memory-exhaustion recovery.

JavaScript remains disabled by default. Set `enable_javascript:1` in Choices
to try it. Wii heaps have an 8 MiB allocation budget (including accounting
headers, excluding malloc overhead), and the default execution deadline is
three seconds. Nested callbacks share the outer deadline. DOM coverage is
incomplete; enabling JavaScript does not promise modern-site compatibility.
Run the DOM mutation smoke with `WII_JS_TEST=1` on either test launcher:

```sh
WII_JS_TEST=1 WII_RENDERER=gx ./wii/dolphin-test.sh
WII_JS_TEST=1 WII_RENDERER=gx ./wii/hardware-test.sh
```

See [the optimization results](OPTIMIZATION_BENCHMARK.md) for repeat-image
hashing measurements, JavaScript validation, and the next performance work.

For a bounded survey of real homepages, use the checked-in `site-list.txt`:

```sh
WII_SITE_LIST="$PWD/wii/site-list.txt" WII_JS_TEST=1 WII_RENDERER=gx \
  WII_DOLPHIN_SECONDS=420 ./wii/dolphin-test.sh
WII_SITE_LIST="$PWD/wii/site-list.txt" WII_JS_TEST=1 WII_RENDERER=gx \
  WII_JOB_SECONDS=900 ./wii/hardware-test.sh
```

The opt-in survey gives each page 25 seconds, records its final URL, title,
load status and dimensions, then saves actual GX captures before and after a
one-screen scroll. It stops outstanding fetches and separates pages with
`about:blank`. It returns to HBC after the list; the hardware watcher requests
cooperative exit if the overall survey hangs. Site completion means that the
observations were collected, not that forms, login, checkout or media work.
Use `WII_JS_TEST=0` to compare the browser's default JavaScript-disabled mode.
The survey waits for network readiness before the first page. Override the
25-second per-page budget with `WII_SITE_SECONDS` (5–90); increase the launcher
and job bounds accordingly for a longer manifest. Observed compatibility and
capture locations are in [SITE_COMPATIBILITY.md](SITE_COMPATIBILITY.md).
For asynchronous test pages, set `WII_SITE_MIN_SECONDS` (2–90, default 2)
to keep observing after loading finishes. The maximum `WII_SITE_SECONDS`
still applies. Surveys capture page console messages in `browser.log`, with
each entry limited to 4096 bytes. Normal browsing retains release logging.
The target smoke loads `wii-test.html`, captures actual framebuffer colors,
exports a PDF, downloads a fixture, performs an SD cache write/read, and checks the produced
artifacts. It is
opt-in via `--wii-test` or a `wii-test.cfg` containing `selftest=1`.
Normal runs do not auto-export or exit. Test fixtures are included in the
application package. Dolphin uses an isolated profile with MMU and SD folder
sync. The hardware script requires the sibling HBC-Reborn client and the
central `~/.wii-bench/wiibench.py` lease queue documented in Wii64; it freezes
both the DOL and matching ELF before queuing and removes only its own outputs.
Reports remain under the ignored `wii/.deps/runs/` directory.

Priorities for further work: monitor for any recurrence of the earlier physical
Wii PDF interruption, exercise USB hot-plug and controllers on hardware, test persistent HTTP cache
reuse across launches and SD-full failures, and profile complex pages before
changing memory budgets or trying the 16bpp display path. A filename picker
and better download progress UI remain useful product improvements.

## Support

This is experimental hobby software. No end-user support or device-compatibility
guarantee is provided. Project correspondence: quatric
<quatricsoftware@gmail.com>.

Copyright (c) 2026 quatric

## HBC-Reborn development agent

Build the optional SDK from a sibling checkout, using the same devkitPro/libogc
as the browser. SDK objects stay in ignored `wii/.deps/hbc-sdk`; the external
checkout is not modified. `HBC_AGENT_ROOT` overrides its location.

```sh
HBC_AGENT=1 ./wii/build-browser.sh -j8
WII_RENDERER=gx ./wii/dolphin-test.sh
WII_RENDERER=gx ./wii/hardware-test.sh
WII_AGENT_EXIT_TEST=1 WII_RENDERER=gx ./wii/hardware-test.sh
python3 wii/test-agent.py
```

The agent starts after wiisocket finishes network startup. It answers live
status and mounted-device file requests, supports screenshots and cooperative
remote exit, and installs the SDK's fatal-exception recorder/reload handler.
A logging target registered through `hbc.py log` receives stdout/stderr,
browser warnings, JavaScript errors and page console messages in agent builds.
Wii diagnostic messages also remain available to Dolphin through OSReport.
Survey logs are written to SD and mirrored to the network stream.
The hardware harness automatically registers a receiver before launch,
retains `agent.log` and `agent-log-status.txt` in its frozen run, and unregisters
the receiver afterward. `WII_AGENT_LOG_TEST=1` requires successful SDK log
initialization and a nonempty received log; use it to validate live delivery.
`python3 wii/test-logging.py` checks formatting and duplicate suppression.
The harness uses `/usr/bin/python3` on macOS for the HBC client and log
receiver, and `python3` elsewhere. Set `WII_HOST_PYTHON` to override it.
This avoids changing firewall settings when the system interpreter is already
allowed to receive incoming connections.
Crash handling is active after agent initialization; earlier startup faults
still require Dolphin logs. SDL owns controllers and GX; the agent HOME overlay
is not opened. Local HOME retains the browser's normal exit behavior.

Hardware tests always use the central lease queue. They freeze package, ELF,
watcher and checker, retain before/after HBC status and previous crash evidence,
poll live status every five seconds, and symbolize new crashes with that ELF.
The exit test also verifies an agent SD read and captures `agent-screen.png`.
A timeout requests cooperative exit only from this job's observed NetSurf app.
Transfers/screenshots run outside the rendering benchmark; their CPU/heap cost
would distort timing. The protocol is for the trusted development LAN.

**Development memory layout:** the agent retains one ordinary 1 MiB malloc
allocation containing the complete 4 KiB HBC record page at `0x91800000`.
Temporary startup allocations are freed, so newlib can reuse MEM1 and MEM2 on
both sides of that allocation. SDK arena highs and IOS reservations are never
changed. This replaces the initial 24 MiB cap; the remaining MEM2 capacity is
around 50 MiB. Ordinary builds have no retained agent allocation.

Arena remaining bytes exclude memory already handed to malloc, even after its
chunks are freed. Read them together with heap-free statistics; a lower
`Arena2Hi - Arena2Lo` does not mean the lower MEM2 chunks were lost.
The explicit capacity test allocates 50 MiB of MEM2 simultaneously, checks each
4 KiB page, verifies the HBC record page is unchanged, then frees the buffers:

```sh
WII_MEM2_TEST=1 WII_RENDERER=gx ./wii/dolphin-test.sh
WII_MEM2_TEST=1 WII_RENDERER=gx ./wii/hardware-test.sh
```

In agent builds, browser heap-used reports subtract the observed MEM1-to-MEM2
address gap. Raw newlib `uordblks` and the SDK agent's `heap_arena` include that
gap and can exceed physical RAM; they are not physical usage measurements.
The reason is visible in [newlib's allocator source](https://raw.githubusercontent.com/mirror/newlib-cygwin/master/newlib/libc/stdlib/_mallocr.c),
which adds discontinuous `sbrk` addresses to its accounting.

The build helper recompiles the agent and its callers when agent/probe flags change.

A deliberate crash regression requires an explicitly instrumented build and
an explicit queued test. It stores to `0x10`, expects a DSI with DAR `0x10` and
`wii_agent_test_crash` in the matching ELF, and requires HBC to return:

```sh
HBC_AGENT=1 HBC_AGENT_CRASH_TEST=1 ./wii/build-browser.sh -j8
WII_AGENT_CRASH_TEST=1 ./wii/hardware-test.sh
# Remove the deliberate probe from the development package afterward:
HBC_AGENT=1 HBC_AGENT_CRASH_TEST=0 ./wii/build-browser.sh -j8
```

The ordinary build cannot arm this probe. No test powers off the Wii. The harness saves earlier crash evidence in
`hbc-before.json` and never sends a clear request; the SDK may clear its console
record when the next app initializes. Restore an ordinary build with `HBC_AGENT=0` when
measuring release memory capacity or distributing the browser.

Local integration results and frozen artifact hashes are recorded in
[HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md).

### Optional native advertising-host filter

Set `fb_request_filter:hosts` in `sd:/apps/netsurf/Choices` and restart to load
`adblock-hosts.txt` and `adblock-allow.txt`. Default is `off`. Each file accepts
one hostname per line, blank lines, and `#` comments. A hostname also matches
its subdomains; allowlist entries win. Invalid or oversized lists disable the
filter with a diagnostic. Limits are 4,096 deny hosts / 256 KiB and 512 allow
hosts / 32 KiB. Explicit navigation and exact initiating-host requests bypass
blocking. This is separate from the existing cosmetic CSS option, and does not
support Firefox extensions or full EasyList syntax. See
[ADBLOCK_EXTENSIONS.md](ADBLOCK_EXTENSIONS.md) for compatibility and memory limits.

Use `WII_REQUEST_FILTER=hosts WII_JS_TEST=1 WII_RENDERER=gx` with the test
launchers to exercise the network-blocking boundary and DOM regressions.
Site surveys also accept `WII_COSMETIC=0` and `WII_BACKGROUND=1` for controlled
comparisons. Survey artifacts include verbose HTTP/script diagnostics and a
bounded copies of each page's source and streamed live DOM text (2 MiB, depth 128). HTML5test surveys
require a numeric score in the live DOM; a completed download alone fails the
check. Captures are local test artifacts and ignored by Git.

### Bounded development shutdown

Agent builds start their agent before the first page navigation, after a bounded
50-second network-readiness wait. Main-loop event waits are capped at 250 ms so
remote exit requests are polled even when the page is idle. A development
watchdog allows 60 seconds without main-loop progress and 10 seconds without
progress during cleanup. Its emergency `_Exit` path bypasses stalled libc/SDL
cleanup and uses libogc's HBC reload stub. Ordinary builds have no watchdog.

`wii-lifecycle.txt` retains the latest startup/cleanup checkpoint. The host
checker requires `stage=return to HBC` for a successful agent-build smoke or
cooperative-exit test; a forced return is not counted as graceful cleanup.
The hardware launcher verifies HBC before restoring test settings, refuses to
modify files in a running app, and fails when restoration fails.

Run `WII_AGENT_EXIT_TEST=1 ./wii/hardware-test.sh` for cooperative exit, or
`WII_EXIT_STALL_TEST=1 ./wii/hardware-test.sh` for an opt-in stalled-cleanup
probe. The latter intentionally leaves the main thread sleeping until the
watchdog returns to HBC. It does not disable interrupts or power off the Wii.
These are separate tests. See [HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md)
for evidence and limits.

## Live compatibility results

The site survey saves `NN-dom.txt` from the live DOM and `NN-layout.txt` from
visible render boxes, each bounded to 2 MiB. HTML5test checks require matching
numeric scores in both captures; inspect the top/scroll screenshots as well.
For asynchronous suites, set `WII_SITE_MIN_SECONDS=15` or longer rather than
capturing immediately after the document download completes.

The local JavaScript regression covers dynamic layout, inline styles, dataset,
hidden elements and removal in addition to the existing image/geometry tests.
See [HTML5TEST_RESULTS.md](HTML5TEST_RESULTS.md) for measured scores and limits.

### Reviewed HBC-Reborn revision

`hbc-reborn.env` pins HBC-Reborn 1.10.2 (upstream HEAD checked 9 October 2026).
`build-agent.sh` and hardware tests verify the checkout with `check-hbc.sh`;
update the checkout before using the SDK. Hardware tests also reject stale
agent packages and require the leased Wii to report that version before SD
staging. The installed queue launcher keeps its shared state and runs the
selected checkout's current queue source. No channel installation is performed.
CI builds both agent modes using the same pinned SDK. Historical validation
records retain their original versions. See [UPSTREAM_SUBMISSIONS.md](UPSTREAM_SUBMISSIONS.md)
for independent patch exports and the remaining submission checks.
