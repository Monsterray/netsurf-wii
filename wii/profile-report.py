#!/usr/bin/env python3
"""Compare guest-side renderer timings; emulator numbers are not Wii speedups."""

import hashlib
import json
import pathlib
import os
import subprocess
import statistics
import sys

survey = pathlib.Path(sys.argv[1])
rows = []
for depth in (32, 16):
    for label in ("soft-before", "gx", "soft-after"):
        log = (survey / f"{depth}-{label}.log").read_text()
        directory = pathlib.Path(
            next(
                line.split(": ", 1)[1]
                for line in log.splitlines()
                if line.startswith("Dolphin smoke artifacts: ")
            )
        )
        report = (directory / "Load/WiiSDSync/apps/netsurf/wii-test.txt").read_text()
        fields = dict(line.split("=", 1) for line in report.splitlines() if "=" in line)
        row = {
            "depth": depth,
            "run": label,
            "directory": str(directory),
            "dol_sha256": hashlib.sha256(
                (directory / "boot.dol").read_bytes()
            ).hexdigest(),
            "elf_sha256": hashlib.sha256(
                (directory / "boot.elf").read_bytes()
            ).hexdigest(),
        }
        row.update(
            {
                name: int(fields[name])
                for name in (
                    "redraws",
                    "redraw_us",
                    "present_us",
                    "upload_us",
                    "sync_us",
                    "texture_us",
                    "gx_readbacks",
                    "converted_tiles",
                    "flushed_bytes",
                    "texture_cache_hits",
                    "texture_cache_misses",
                    "texture_cache_bytes",
                    "gx_rectangles",
                    "gx_images",
                    "gx_glyphs",
                    "present_frames",
                    "heap_used",
                    "scene_redraw_us",
                    "scene_present_us",
                    "scene_upload_us",
                    "scene_sync_us",
                    "scene_texture_us",
                    "scene_frames",
                    "scene_redraws",
                    "scene_tiles",
                    "scene_flushed_bytes",
                    "scene_readbacks",
                    "texture_hash_bytes",
                )
            }
        )
        row["sync_sources"] = []
        addr2line = (
            pathlib.Path(os.environ.get("DEVKITPPC", "/opt/devkitpro/devkitPPC"))
            / "bin/powerpc-eabi-addr2line"
        )
        for name, value in fields.items():
            if not name.startswith("sync_source_"):
                continue
            pc, count, elapsed = value.split(",")
            source = {"pc": pc, "count": int(count), "elapsed_us": int(elapsed)}
            if addr2line.exists():
                source["symbol"] = subprocess.check_output(
                    [str(addr2line), "-f", "-e", str(directory / "boot.elf"), pc],
                    text=True,
                ).strip()
            row["sync_sources"].append(source)
        rows.append(row)
assert len({r["dol_sha256"] for r in rows}) == 1, "Build changed during survey"
assert len({r["elf_sha256"] for r in rows}) == 1, "ELF changed during survey"
(survey / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
for depth in (32, 16):
    values = [r for r in rows if r["depth"] == depth]
    control = statistics.mean(
        r["scene_redraw_us"] + r["scene_present_us"]
        for r in values
        if r["run"].startswith("soft-")
    )
    gx = next(r for r in values if r["run"] == "gx")
    elapsed = gx["scene_redraw_us"] + gx["scene_present_us"]
    print(
        f"{depth}-bit page scene: soft A/A mean {control:.0f} us; GX {elapsed} us "
        f"(redraw {gx['scene_redraw_us']}, presentation {gx['scene_present_us']}); "
        f"GX scene readbacks {gx['scene_readbacks']}"
    )
print("Scene checkpoint excludes contract probes, captures, PDF export and download.")
print("Guest elapsed timings include waits within redraw; presentation includes vsync.")
print(
    "One A/B/A per depth is a smoke comparison, not a representative performance benchmark."
)
