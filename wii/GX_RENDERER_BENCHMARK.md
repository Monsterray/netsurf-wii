# GX renderer validation and profiling — 2026-10-02

The `gx` plugin performs native GX page rasterization, image scaling/blending,
FreeType glyph compositing, toolbar bitmap drawing, fills, one-pixel axis-aligned
lines and solid outlines. Unsupported operations use synchronized software
plotters. Select `fb_renderer:gx` in `sd:/apps/netsurf/Choices` and restart.
`soft` remains the default pending representative real-Wii A/B measurements.

## Build and method

Built with devkitPPC r50 / GCC 16.1.0 and libogc 3.1.0. This local package uses
Arial regular/bold fonts supplied through the build variables; the fonts are
local inputs and are not committed. The survey records their hashes.

One package and matching DOL/ELF were frozen for all six Dolphin runs:
soft/GX/soft at each depth. The runner, checker and reporting scripts are also
frozen. `source.json` records source hashes, font hashes and build information;
`results.json` records run paths and artifact hashes. Profiling uses guest
`gettime()` counters, following Wii64's frozen-build comparison workflow.

Scene metrics stop after the local fixture finishes rendering, before contract
checks, capture, PDF export and downloads. Sum **scene redraw plus presentation**:
GPU batch uploads occur inside redraw, whereas software uploads occur in
presentation. Presentation includes vsync; elapsed figures are not CPU or GPU
utilization, and guest timings in Dolphin are not Wii speedups.

```
DOL SHA256: 0e346f1e8bd6a3f392afd0503d35dd223c2d75ed54bff60e07c702fca6510fc8
ELF SHA256: b2a95c1196e935f38945780d9227f1ff31a6600050cc7f9937e6dc7bb843d468
```

## Dolphin A/B/A results

All six runs passed rendered output, CPU/GPU shadow agreement, cursor, clipping,
alpha blending, software fallback, scrolling, mutable images, offscreen
rendering, complete PDF, download and SD-cache checks. Every scene had five
redraws and five presentations.

| Depth | Run | Scene redraw (µs) | Presentation (µs) | Combined (µs) | Uploaded bytes | Scene readbacks |
|---|---|---:|---:|---:|---:|---:|
| 32 | soft-before | 4,592 | 70,679 | 75,271 | 3,124,032 | 0 |
| 32 | gx | 15,883 | 40,948 | 56,831 | 1,228,800 | 0 |
| 32 | soft-after | 4,592 | 70,679 | 75,271 | 3,124,032 | 0 |
| 16 | soft-before | 3,959 | 54,628 | 58,587 | 1,562,016 | 0 |
| 16 | gx | 14,029 | 26,155 | 40,184 | 614,400 | 0 |
| 16 | soft-after | 3,959 | 54,628 | 58,587 | 1,562,016 | 0 |

For this single fixture, combined scene elapsed time was about 24% lower with GX
at 32 bits and 31% lower at 16 bits than the average of the two software controls.
Software redraw alone was faster, so comparing only that counter is misleading.
These are one A/B/A survey in Dolphin, with vsync included; they do not establish
a general browser speedup or a real-Wii performance advantage.

GX uploaded the 640×480 CPU page texture once: 1,228,800 bytes at 32 bits or
614,400 at 16 bits. The page scene needed zero GPU-to-CPU readbacks. Later
intentional CPU fallback/capture checks did synchronize, proving that the CPU
shadow and scroll source receive current GPU pixels. The texture cache occupied
under 80 KiB on this fixture; its limit is 4 MiB and 96 entries.

A pixel comparison of the 32-bit page area against `soft` found 49 differing
pixels, each with maximum channel error 1. The toolbar had small differences
from texture blending and its animated indicator. Actual GPU captures were
visually inspected. Capture validation also compares each renderer's GPU output
with its own CPU shadow, allowing cursor and RGB565 expansion differences.

Dolphin uses CPU EFB access, immediate copies to RAM, and full texture hashing.
Deferred copies and sampled texture hashing previously left stale test drawing
visible despite correct CPU pixels; the harness now disables those shortcuts.
It cleans up only its isolated instance on success, failure or interruption,
including a bounded forced termination if normal shutdown fails. The four stale
Dolphin windows reported during development were closed.

## Physical Wii

The same DOL/ELF passed at both depths through the central HBC lease queue.
Both returned normally to HBC and reported no crash. The output and contract
checks passed, including GPU/CPU capture agreement. No software control with the
new scene checkpoint was run on hardware, so this table does not measure a
physical-Wii speedup.

| Depth | Scene redraw (µs) | Presentation (µs) | Combined (µs) | Uploaded bytes | Scene readbacks | Heap used after smoke (bytes) |
|---|---:|---:|---:|---:|---:|---:|
| 32 | 14,960 | 67,004 | 81,964 | 1,228,800 | 0 | 7,257,452 |
| 16 | 11,033 | 42,682 | 53,715 | 614,400 | 0 | 5,414,244 |

Both reports retained SDK MEM2 high bound `0x933b6f60`, with 54,218,592 arena
bytes remaining. GX allocations use libogc/newlib allocation within those bounds;
no IOS-reserved memory is reclaimed or overwritten. The 16-bit run used about
1.76 MiB less heap than 32-bit. GPU cache limits are additional to decoded images
and SDL's retained buffers; they are not a promise that every large page fits.

## Artifacts and reproduction

Local ignored artifacts:

- Dolphin survey: `wii/.deps/runs/gx-profile-5Y1Xqd/`
- Wii 32-bit: `wii/.deps/runs/hardware-U6mx0r/`
- Wii 16-bit: `wii/.deps/runs/hardware-Ft5SCC/`

```sh
python3 wii/test-regressions.py
./wii/test-render.sh
python3 wii/test-presentation.py
./wii/profile-renderers.sh
WII_RENDERER=gx WII_DEPTH=32 ./wii/hardware-test.sh
WII_RENDERER=gx WII_DEPTH=16 ./wii/hardware-test.sh
```

Next work: broader real-Wii A/B scenes, a glyph atlas/batched quads, bitmap
revision tracking to avoid hashing unchanged large images for every repeated
quad, GPU scrolling, and more accelerated primitives. Preserve the software
renderer as the correctness reference. The implementation and hardware roadmap
are in [HARDWARE_PLAN.md](HARDWARE_PLAN.md).
