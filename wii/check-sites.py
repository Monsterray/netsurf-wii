#!/usr/bin/env python3
"""Check survey completeness; a completed load does not establish site usability."""

import json
import re
from pathlib import Path
import sys

directory, manifest = map(Path, sys.argv[1:3])
urls = manifest.read_text().splitlines()
complete = dict(
    line.split("=", 1) for line in (directory / "complete.txt").read_text().splitlines()
)
assert int(complete["sites"]) == len(urls), "Survey did not visit every requested site"
rows = []
for index, url in enumerate(urls, 1):
    fields = dict(
        line.split("=", 1)
        for line in (directory / f"{index:02}.txt").read_text().splitlines()
    )
    assert fields["requested"] == url, "Mismatched requested URL"
    for suffix in ["top", "scroll"]:
        capture = (directory / f"{index:02}-{suffix}.ppm").read_bytes()
        assert capture.startswith(
            b"P6\n640 480\n255\n"
        ), "Missing actual screen capture"
        assert len(capture.split(b"\n", 3)[3]) == 640 * 480 * 3, "Truncated capture"
    if url == "file:///sd:/apps/netsurf/wii-test.html" and complete["javascript"] == "1":
        assert fields["title"] == "NetSurf Wii JavaScript PASS", "JavaScript regression did not pass"
    if url.rstrip("/") == "https://html5test.co":
        text = (directory / f"{index:02}-dom.txt").read_text()
        score = re.search(r"Your browser scores\s*(\d+)\s*out of\s*(\d+)\s*points", text)
        assert score, "HTML5test did not produce a score in the live DOM"
        fields["html5test_score"], fields["html5test_maximum"] = map(int, score.groups())
        assert 0 <= fields["html5test_score"] <= fields["html5test_maximum"]
        print(f"HTML5test: {fields['html5test_score']}/{fields['html5test_maximum']}")
    rows.append(fields)
    print(f"{url}: done={fields['done']} {fields['title']} | {fields['status']}")
(directory / "results.json").write_text(
    json.dumps({"javascript": complete["javascript"], "sites": rows}, indent=2)
)
print(
    f"COMPLETE: {len(rows)} homepage observations; inspect captures for compatibility"
)
