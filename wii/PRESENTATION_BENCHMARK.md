# Presentation comparison

Fixture: `wii-test.html`, one run per configuration. All four modes passed
rendering, PDF export, downloads, cache round trips, and cursor checks.
The optimized modes additionally validated actual GPU EFB colors.

Toolchain: devkitPPC r50 / GCC 16.1.0, libogc 3.1.0. Local test fonts: Arial.
Frozen DOL SHA-256: `bc0e58f2d61d483596b4dbf8696f68732a7606618d63855b934a930ddbc511f2`.

## Dolphin measurements

| Plugin | Depth | Heap used (bytes) | Converted tiles | Texture bytes flushed | Presentation time (µs) |
|---|---:|---:|---:|---:|---:|
| soft | 32 | 7158096 | 63314 | 4052096 | 120577 |
| soft | 16 | 5314800 | 63314 | 2026048 | 84855 |
| soft-legacy | 32 | 5306144 | not instrumented | not instrumented | not instrumented |
| soft-legacy | 16 | 4077344 | not instrumented | not instrumented | not instrumented |

The optimized presenter produced eight frames, including two cursor-only
frames. Cursor-only motion converted zero page tiles. RGB565 flushed exactly
half the texture bytes and used 1,843,296 fewer heap bytes (about 1.76 MiB)
than XRGB8888 in this fixture. It reduces color precision.

Times include vsync waiting and are single emulator samples. They do not
establish a real-Wii speedup or a whole-browser performance improvement.
Legacy presentation counters are not instrumented; zero in its raw report
means unavailable, not free presentation. The optimized path currently uses
additional texture/XFB storage while SDL retains its buffers, so it uses more
heap than the legacy plugin at the same depth.

## Physical Wii

The optimized 32-bit mode passed page and GPU output, PDF, download, cache,
and cursor checks, returned to HBC, and reported no crash.

| Depth | Heap used (bytes) | Converted tiles | Texture bytes flushed | Presentation time (µs) |
|---|---:|---:|---:|---:|
| 32 | 7158036 | 63314 | 4052096 | 114888 |
| 16 | 5314956 | 63314 | 2026048 | 90448 |

Both optimized depths passed on the physical Wii and returned to HBC with no
crash reported. These are single-fixture samples; presentation time includes
vsync waiting. The 16-bit sample used about 1.76 MiB less heap and half the
texture-flush bytes. Repeat representative pages before changing defaults.
Raw reports, GPU/CPU captures, frozen DOLs and matching ELFs remain under
the ignored `wii/.deps/runs/` directory. Reproduction and remaining work are
documented in [HARDWARE_PLAN.md](HARDWARE_PLAN.md).
