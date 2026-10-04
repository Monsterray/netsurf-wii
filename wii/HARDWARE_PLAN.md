# Wii hardware roadmap

## Current architecture and settings

The browser's CPU handles parsing, CSS layout, JavaScript, font rasterization,
image decoding, and PDF generation. The software renderer is a compiled plugin
in `frontends/framebuffer/renderers/soft.c`. The plugin registry and lifecycle
are in `frontends/framebuffer/framebuffer.c`; the shared contract is in
`renderer.h`. All plugins must support offscreen bitmap rendering as well as
screen rendering. The GX renderer retains the software path for unsupported primitives and RAM
targets. PDF export continues to use its own Haru plotters.

`sd:/apps/netsurf/Choices` selects a plugin and pixel depth at startup:

```
fb_renderer:soft
fb_depth:32
```

Available plugins:

- `soft`: software page rasterization, batched GX presentation, GX cursor.
- `gx`: GX fills, scaled/repeated alpha image quads, cached FreeType glyph quads,
  clipped to the browser region. Unsupported primitives and offscreen surfaces
  use the software renderer.
- `soft-legacy`: software page rasterization and original SDL presentation/cursor,
  retained as the comparison and troubleshooting path.

All three support `fb_depth:16` (RGB565) or `fb_depth:32` (XRGB8888). Restart to apply.
Unknown plugin names fall back to `soft` with a diagnostic. The `gx` plugin is now registered; `soft` remains the default. Plugin
callbacks own initialization, presentation, teardown, cursor handling and
surface switching, so a GX plugin can replace the software pipeline without
changing the browser or toolkit drawing calls. Plugins
are linked into the DOL; this interface does not dynamically load code from SD.

## Presentation work

1. Coalesce damage into 4x4 texture tiles once per browser event-loop iteration.
   Convert each dirty tile once and flush only contiguous changed tile ranges.
   Serialize GX ownership: stop SDL's presentation thread before taking over.
2. Draw the pointer as its own alpha-blended GX texture. Pointer motion does not
   change the page framebuffer or reconvert its pixels. Cursor image uploads
   happen only when the image changes. Its position/hotspot remain in browser
   coordinates.
3. Compare RGB565 with XRGB8888 using the same frozen DOL and fixture. A
   640x480 buffer occupies 600 KiB versus 1200 KiB. This saves space in the
   software buffer and presentation texture; it does not halve every browser
   allocation. Watch for gradients, transparency, text quality, and color loss.

The optimized presenter currently keeps its own texture/XFB while SDL retains
its allocated buffers for the input/display lifecycle. This adds memory compared
with `soft-legacy`; counters include heap used/free as well as arena bounds.
A later SDL integration could share/release the unused SDL texture, but must
retain a supported ownership contract rather than access private SDL structs.

Target reports retain plugin/depth, presented frames, converted tiles,
cache-store bytes, presentation time, and arena remaining bytes. Presentation
includes vsync waiting; these counters are not whole-page CPU/GPU utilization.
Dolphin tests enable CPU EFB access and copies to RAM so GPU captures are real.
Dolphin timings are emulator results and must not be described as Wii speedups.
Keep 32-bit as the default until representative real-Wii measurements establish
that 16-bit is worthwhile.

## GX drawing plugin

`renderers/gx.c` uses native libogc GX, rather than the OpenGX compatibility layer.
GX rasterizes filled rectangles, one-pixel axis-aligned lines and solid toolkit
outlines, scales and
alpha blends page images and toolbar icons, and draws
FreeType coverage textures. A content-addressed texture cache holds up to 96
entries and 4 MiB, shared by images and glyphs. Glyphs use separate cached
textures; packing them into an atlas remains future work. Hashing the source
bytes handles images modified in place and FreeType reusing glyph addresses.
Eviction waits for outstanding GX commands before releasing texture memory.
Repeated page bitmap tiles share one full-content hash per plot call. Later
calls hash again, preserving correctness for images modified in place. The
`texture_hash_bytes` counter measures this work; it is not a GPU utilization
counter. See [OPTIMIZATION_BENCHMARK.md](OPTIMIZATION_BENCHMARK.md).
Nearest sampling preserves pixel images; vertices/UVs are cropped to the clip.

The EFB is authoritative while GX draws. Before software access, the public
libnsfb Wii synchronization hook saves the page to its tiled texture and updates
only the stale CPU tiles needed by that operation. CPU writes invalidate their
tile region for upload before subsequent GPU drawing. Toolkit/browser redraws
use an explicit native claim: they draw through plot APIs and do not retain a
raw CPU buffer. This claim does not read back pixels merely to acquire a drawing
region. Ordinary `nsfb_claim` still synchronizes for clients using CPU memory,
and `nsfb_get_buffer` synchronizes before raw buffer access. Every actual software
plotter fallback crosses that boundary before reading or writing pixels. Scrolling synchronizes
both its source and destination; RAM targets have no GPU callback. The cursor is
added after saving page contents, so it never enters scrolling or CPU readback.
This avoids rendering every GPU operation again in software.

Thicker/patterned rectangles, diagonal/patterned lines, arcs, discs, polygons,
monochrome glyphs,
images above GX's 1024-pixel texture dimension, and allocation failures use the
existing software plotters. The existing software path plotter remains
unimplemented; GX does not add vector-path support. Non-native framebuffer
sizes that exceed the EFB retain software rendering. Parsing, CSS layout,
JavaScript, FreeType rasterization and image decoding remain CPU work.

Next steps are to move remaining toolkit primitives/scroll copies to GX, reduce EFB
copy barriers, pack glyphs into an atlas, and benchmark complex pages. Keep the
software plugin selectable: acceleration of individual primitives does not
promise a faster whole page when mixed CPU/GPU drawing requires synchronization.

## Profiling and correctness

`./wii/profile-renderers.sh` freezes one package/DOL/ELF and runs soft/GX/soft
at 32 and 16 bits in separate Dolphin profiles. `profile-report.py` verifies
matching artifact hashes and saves results. Guest `gettime()` counters follow
Wii64's profiling approach; these are elapsed guest times, not host CPU samples
or physical-Wii performance counters. Report redraws and redraw time, texture
preparation/cache hits/misses/bytes, synchronization time, uploads, EFB readbacks,
and presentation time separately. A checkpoint freezes scene metrics before
contract checks, captures, PDF export or downloads. Compare scene redraw plus
presentation, because uploads performed while starting a GPU batch are counted
inside redraw whereas software uploads happen during presentation. Presentation
includes vsync. Synchronization call-site PCs are resolved against the frozen
ELF by the profiling report; SDK library line numbers may be unavailable. A single A/B/A sequence is
a smoke comparison, not a statistically representative benchmark.

The target contract test checks clipping, image alpha over a GPU fill, a CPU
line fallback, a subsequent GPU glyph, scrolling, image mutation, and offscreen
renderer dispatch. Actual GPU captures are compared with the CPU shadow, allowing
RGB565 rounding and the separate pointer. Dolphin must enable EFB CPU access,
copy to RAM immediately, and use full texture-cache hashing; deferred EFB copies
and sampled hashing can hide CPU edits to an EFB copy. Configuration definitions:
[official Dolphin source](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/Config/GraphicsSettings.cpp).
The smoke harness cleans up its own profile on success, failure or interruption,
with a bounded TERM/KILL sequence for hung instances.

## Other opportunities

- Profile CPU hotspots before trying LTO or hand-tuned PowerPC routines.
- Extend JavaScript DOM coverage with small real-page fixtures. The current
  `Document.title` binding is unimplemented; scripts can mutate the title
  element through existing DOM node APIs. Keep Wii allocation limits and
  nested-callback deadlines while expanding functionality.
- Measure allocation lifetimes before explicitly placing hot data in MEM1 and
  bulk image/cache data in MEM2. Always use libogc's safe arena bounds; preserve
  IOS and SDK reservations. Do not claim physical MEM2's full 64 MiB.
- Overlap I/O with rendering where safe; LWP threads share the single main CPU.
  Keep NetSurf's DOM, layout, and frontend callbacks on their owning thread.
- Audio playback could use the DSP, but the current browser has no media playback
  pipeline. Image/video entropy decoding is not automatically offloaded to GX.
- IOS AES support is limited; measure compatibility and IPC overhead before
  attempting TLS crypto offload. Do not repurpose IOS/Starlet as a worker CPU.

## Validation and outstanding issues

Run host tile/blitter regressions, then soft/GX/soft at each depth in
Dolphin with MMU and SD synchronization. Validate actual GX output separately
from the CPU buffer. Exercise pointer motion, clipping, scrolling, and resize
restrictions. Freeze the matching DOL/ELF for each hardware job; use the shared
Wii lease queue. An earlier physical-Wii PDF test stopped answering HBC. The current optimized
32-bit and 16-bit renderer builds subsequently passed PDF export and returned normally,
with no crash reported, under the shared HBC lease. Retain that earlier failure
history when investigating any recurrence. Add complex pages and SD-full failures after the basic fixture passes.

Measured presentation baselines are in [PRESENTATION_BENCHMARK.md](PRESENTATION_BENCHMARK.md).
Native GX implementation results, frozen A/B/A profiling and physical-Wii
validation are in [GX_RENDERER_BENCHMARK.md](GX_RENDERER_BENCHMARK.md).

### HBC development agent

Optional `HBC_AGENT=1` builds integrate the external HBC-Reborn SDK with
network-startup ordering, main-loop exit polling, fatal exception records and
registered PC logging. The SDL/GX HOME overlay integration is deferred because
it changes controller/framebuffer ownership. See `README.md` for the leased
live-status, screen, cooperative-exit and opt-in crash checks.

The agent pins one ordinary 1 MiB allocation containing HBC's 4 KiB persistent
record page at `0x91800000` and frees its other temporary startup allocations.
This leaves both sides of MEM2 usable through newlib, preserves SDK/IOS arena
highs and replaces the original 24 MiB cap. Around 50 MiB remains available;
`WII_MEM2_TEST=1` validates simultaneous MEM2 allocations and record integrity.
Arena remaining bytes alone exclude free chunks already owned by malloc.

Relocating the records would require changing both the SDK and installed HBC.
It is unnecessary for the current capacity target. Reducing the retained chunk
below 1 MiB is future work only if that capacity matters on measured pages.

## Request filtering and modern site compatibility

The optional compiled hostname filter prevents selected cache/network loads
before download. Its matching remains CPU work with no allocation per decision;
GX has no suitable string-search primitive. Firefox WebExtensions need browser
host services and language facilities absent from the current integration;
see [ADBLOCK_EXTENSIONS.md](ADBLOCK_EXTENSIONS.md). Installing an extension
cannot replace missing DOM, networking, layout or media support.

Prioritize recorded HTTP/script failures and reduced fixtures before adding
more GPU drawing. Native GX fills, images and glyphs already cover common
raster operations. Remaining drawing opportunities include glyph atlases,
solid stroke primitives and reducing CPU fallback readbacks; validate visual
agreement and measure whole-frame cost on the same fixture before changing
defaults. GX cannot accelerate JavaScript, TLS, HTML/CSS parsing, video codec
support or DRM. Media capability needs a separately budgeted decoder and
streaming pipeline; rendering a video texture alone does not provide playback.
