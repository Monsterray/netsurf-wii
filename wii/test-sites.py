#!/usr/bin/env python3
"""Exercise the real collector loop with short and full site manifests."""

import os
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent / "hardware-test.sh").read_text()
start = source.index("    for index in $(seq 1")
end = source.index('\n    python3 "$RUN/check-sites.py"', start)
loop = source[start:end]
with tempfile.TemporaryDirectory() as directory:
    run = Path(directory)
    (run / "package").mkdir()
    for count in [2, 11, 13]:
        (run / "package/wii-sites.txt").write_text("https://example.invalid/\n" * count)
        names = subprocess.check_output(
            ["bash", "-c", 'hbc() { printf "%s\\n" "${2##*/}"; }\n' + loop],
            env={**os.environ, "RUN": str(run)},
            text=True,
        ).splitlines()
        assert names == [
            f"{index:02}{suffix}"
            for index in range(1, count + 1)
            for suffix in [".txt", "-top.ppm", "-scroll.ppm", "-source.html", "-dom.txt", "-layout.txt"]
        ], names
print("PASS: actual collector uses matching filenames for 2, 11 and 13 sites")
