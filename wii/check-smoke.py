#!/usr/bin/env python3
"""Validate target-produced artifacts; never mistake compilation for a smoke pass."""

import argparse
import pathlib

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=pathlib.Path)
parser.add_argument("--dolphin-log", type=pathlib.Path)
args = parser.parse_args()
report = (args.directory / "wii-test.txt").read_text()
print(report, end="")
info = args.directory / "build-info.txt"
if not info.exists():
    info = args.directory / "package/build-info.txt"
if info.exists() and "HBC_AGENT=1\n" in info.read_text():
    if (args.directory / "wii-lifecycle.txt").read_text() != "stage=return to HBC\n":
        raise SystemExit(
            "Browser cleanup did not reach its final HBC-return checkpoint"
        )

for result in [
    "page=PASS",
    "pdf=PASS",
    "download=PASS",
    "cache=PASS",
    "cursor=PASS",
    "gx_contract=PASS",
]:
    if result not in report:
        raise SystemExit("Failed target smoke result: " + result)
choices = args.directory / "Choices"
if not choices.exists():
    choices = args.directory / "package/Choices"
requested_filter = (
    choices.exists() and "fb_request_filter:hosts" in choices.read_text().splitlines()
)
config = args.directory / "wii-test.cfg"
if not config.exists():
    config = args.directory / "package/wii-test.cfg"
requested_js = config.exists() and "javascript=1" in config.read_text().splitlines()
if requested_js and "javascript=PASS" not in report:
    raise SystemExit("Requested JavaScript regression result is missing or failed")
if requested_filter and "request_filter=hosts" not in report:
    raise SystemExit("Requested hostname filter failed to load")
if "request_filter=hosts" in report and "request_filter_test=PASS" not in report:
    raise SystemExit("Request filter did not stop the browser fetch probe")
if "javascript=FAIL" in report:
    raise SystemExit("Page JavaScript did not execute")
pdf = (args.directory / "wii-test.pdf").read_bytes()
if not pdf.startswith(b"%PDF-") or b"%%EOF" not in pdf:
    raise SystemExit("PDF is incomplete")
capture = (args.directory / "wii-test.ppm").read_bytes()
if not capture.startswith(b"P6\n640 480\n255\n"):
    raise SystemExit("Framebuffer capture missing or wrong dimensions")
# The fixture has large pure red/green/blue areas; catch channel corruption.
for color in [b"\xff\0\0", b"\0\xff\0", b"\0\0\xff"]:
    if capture.count(color) < 100:
        raise SystemExit("Framebuffer fixture color missing: " + repr(color))
if any("renderer=" + name + "\n" in report for name in ("soft", "gx")):
    gpu = (args.directory / "wii-test-gx.ppm").read_bytes()
    for color in [b"\xff\0\0", b"\0\xff\0", b"\0\0\xff"]:
        if gpu.count(color) < 100:
            raise SystemExit(
                "Actual GX output is missing a fixture color: " + repr(color)
            )
    # GPU and CPU shadow should agree, apart from the separate pointer and
    # RGB565 expansion rounding. Catch stale EFB copies and lost fallback writes.
    if gpu.split(b"\n", 3)[:3] == capture.split(b"\n", 3)[:3]:
        a, b = gpu.split(b"\n", 3)[3], capture.split(b"\n", 3)[3]
        differences = sum(
            max(abs(a[i + j] - b[i + j]) for j in range(3)) > 8
            for i in range(0, len(a), 3)
        )
        if differences > 512:
            raise SystemExit(f"GPU/CPU shadow diverged at {differences} pixels")
if "renderer=gx\n" in report:
    fields = dict(line.split("=", 1) for line in report.splitlines() if "=" in line)
    for field in ("gx_rectangles", "gx_images", "gx_glyphs"):
        if int(fields[field]) == 0:
            raise SystemExit("GX did not accelerate " + field)
if args.dolphin_log is not None:
    text = args.dolphin_log.read_text(errors="replace")
    errors = [
        line
        for line in text.splitlines()
        if any(
            pattern in line
            for pattern in [
                "Invalid read from",
                "Invalid write to",
                "Unknown ucode",
                "Failed to sync SD card with folder",
            ]
        )
    ]
    if errors:
        raise SystemExit("\n".join(errors[:10]))
print(
    "PASS: rendered colors, complete PDF, download, SD cache round trip, target memory report"
)
