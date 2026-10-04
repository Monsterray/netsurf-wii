# Repeat-image and JavaScript optimization validation

## Implemented changes

GX page bitmap repetition now hashes the image once per plot call, at the first
visible tile. Each later visible tile uses that hash. Entirely clipped draws do
not hash. Later plot calls hash again, so animations and images changed in place
still invalidate the content-addressed texture cache. The cache, its 4 MiB
budget, alpha handling and software fallback contract remain in use.

The presenter host check draws 64 repeats while hashing only one 12-byte image,
then changes that same source address and checks a cache miss. Target contract
checks also cover actual GX output, alpha, clipping, CPU/GPU transitions,
scrolling and offscreen targets. Reports now include `texture_hash_bytes`.

The Wii Duktape allocator limits each heap to 8 MiB including its private
allocation headers, excluding libc chunk overhead. Failed realloc preserves
the original allocation and accounting. Heap prototype initialization runs
inside a protected Duktape call, allowing initialization allocation failure to
return an error. This budget does not cap DOM, image or other browser heaps.

Heap creation honors the requested timeout; the Wii default is three seconds.
Nested callbacks share their outer execution deadline instead of extending it.
The actual bundled engine host test checks arithmetic, infinite loops, nested
C-to-JavaScript callbacks, allocation exhaustion, recovery and zero allocations
after destruction. Deadlines depend on the engine's interrupt checks; this is
not a hard real-time watchdog for native DOM/engine operations.

JavaScript remains opt-in through `enable_javascript:1` in Choices. The target
smoke's `WII_JS_TEST=1` enables a fixture script which changes the title element
through `getElementById(...).textContent`; the browser checks the resulting
content title. This proves actual engine/DOM integration, not just a host engine
build. It does not establish broad modern-site compatibility.

## Frozen validation

The final development package uses devkitPPC r50 / GCC 16.1.0, libogc 3.1.0,
`HBC_AGENT=1` and `HBC_AGENT_CRASH_TEST=0`. DOL SHA-256:

```
54554e0e79fce05dd5306ef9df1fa89d049bc5fc09cc23c7eeb98387fd2c2b44
```

The exact final package passed the 16-bit GX JavaScript smoke on the dev Wii,
including GPU capture, renderer contracts, PDF export, download and SD cache
round trip. Shared lease job `20261003-104437-94cbf2` completed in 40 seconds.
The after-status confirmed HBC 1.9.3 / IOS58 revision 6175 with no crash.
Frozen evidence: `wii/.deps/runs/hardware-Hpusiw`. A preceding 32-bit
JavaScript-enabled hardware smoke also passed, before the final nested-deadline
refinement, in `hardware-jITxX3`.
The exact final package also passed the JavaScript-enabled 16-bit GX smoke in
Dolphin (`dolphin-6C9PGH`), and the final rendering survey passed three 32-bit
GX smokes. Isolated profiles were closed after each run.

Run the checks with:

```sh
python3 wii/test-javascript.py
python3 wii/test-presentation.py
python3 wii/test-agent.py
python3 wii/test-regressions.py
./wii/test-render.sh
WII_JS_TEST=1 WII_RENDERER=gx WII_DEPTH=16 ./wii/dolphin-test.sh
WII_JS_TEST=1 WII_RENDERER=gx WII_DEPTH=16 ./wii/hardware-test.sh
```

Hardware runs freeze the package, ELF and helpers before entering the shared
lease queue. Dolphin runs use separate profiles and clean up their owned
processes. Raw evidence remains in the ignored dependency/run directory.

## Measurement interpretation

The final frozen 32-bit GX reference/candidate/reference survey is in
`wii/.deps/runs/speed-survey-oQz8ft/results.json`. JavaScript was disabled for
this rendering comparison; all packages used the same fixture/assets.

| Scene counter | Reference before | Candidate | Reference after |
| --- | ---: | ---: | ---: |
| Redraw, microseconds | 15,798 | 14,427 | 15,798 |
| Presentation, microseconds | 61,133 | 62,085 | 61,133 |
| Combined, microseconds | 76,931 | 76,512 | 76,931 |
| Frames / redraws | 5 / 5 | 5 / 5 | 5 / 5 |
| EFB readbacks | 0 | 0 | 0 |

Redraw decreased about 8.7%; combined time decreased about 0.5%. The candidate
reported 91,064 hashed bytes over the full smoke (not only the scene).
The reference DOL SHA-256 is
`8088e107e59dae11871f3e112be3125479788232c5f7de38379f130e903f2000`.
These measurements cover one fixture and do not establish general browsing
speed or physical-Wii timing improvements.

Compare scene redraw plus scene presentation time. Presentation includes vsync
and remains the larger cost on this small fixture. Guest `gettime()` counters
follow the Wii64 profiling approach; Dolphin results are not physical Wii speed
measurements. A single reference/candidate/reference sequence is a smoke
comparison, not a representative multi-page benchmark.

Preparing a shared hash now occurs before the presenter's individual-image
texture timer. Consequently `scene_texture_us` has a different accounting
boundary from older builds. Its decrease alone is not evidence of a speedup;
use scene redraw and combined scene time. Older reports lack hash-byte counters;
an absent value is unavailable, not zero hashing work.

## Next work

- Benchmark repeat-heavy and complex pages, including mixed CPU/GPU fallbacks.
  Profile parsing, CSS/layout, decoding and JavaScript separately before changing
  memory budgets or introducing PowerPC-specific routines.
- Reduce EFB copy barriers and implement GPU scrolling, then compare actual
  output against the software path. Pack glyphs into an atlas if measurements
  show texture/state changes matter.
- Add focused DOM capability fixtures. `Document.title` is currently an empty
  binding; title-element text mutation works. Keep timeout and allocation
  regressions while expanding APIs.
- Measure whether sharing/releasing SDL's unused presentation buffers is worth
  changing its supported ownership contract.

The broader hardware plan and MEM2/IOS rules remain in
[HARDWARE_PLAN.md](HARDWARE_PLAN.md). The previously validated simultaneous
50 MiB MEM2 allocation test is separate from these rendering/JavaScript timings.
