#!/usr/bin/env python3
"""Export small, independent patches and check each against current upstream.

Fetch upstream first; pass its commit/ref. Outputs are ignored local artifacts.
This does not rewrite branches, change the checkout, or submit anything.
"""
import argparse
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PATCHES = [
    ("framebuffer-scheduler-oom", "720d98b9a", ["frontends/framebuffer/schedule.c"],
     "Return NSERROR_NOMEM when allocating a scheduled callback fails, instead of dereferencing NULL."),
    ("portable-generated-icons", "720d98b9a", ["tools/convert_image.c"],
     "Emit aligned ABGR integer pixels so generated framebuffer icons retain their colors on big-endian targets."),
    ("quote-testament-paths", "720d98b9a", ["tools/Makefile"],
     "Quote the source and output paths passed to git-testament.pl when the checkout path contains spaces."),
    ("external-script-types", "d7e96c633", ["content/handlers/html/script.c"],
     "Skip external modules and data scripts unsupported by the selected handler; treat an empty type as classic JavaScript."),
]


def git(*args, **kwargs):
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", help="freshly fetched upstream commit or ref")
    args = parser.parse_args()
    base = git("rev-parse", args.upstream, stdout=subprocess.PIPE).stdout.decode().strip()
    out = ROOT / "wii/.deps/upstream-review" / base
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="netsurf-upstream-") as tmp:
        archive = Path(tmp) / "source.tar"
        git("archive", "--format=tar", "-o", str(archive), base)
        source = Path(tmp) / "source"
        source.mkdir()
        with tarfile.open(archive) as tar:
            # Trusted archive generated directly by Git, including on Python < 3.12.
            tar.extractall(source)
        for name, commit, paths, description in PATCHES:
            patch = out / (name + ".patch")
            with patch.open("wb") as stream:
                git("diff", commit + "^", commit, "--", *paths, stdout=stream)
            # Each patch must apply independently, without Wii-port changes.
            subprocess.run(["git", "apply", "--check", str(patch)], cwd=source, check=True)
            (out / (name + ".md")).write_text(
                f"{name.replace('-', ' ').capitalize()}\n\n{description}\n\n"
                f"Base: `{base}`. Source commit: `{commit}`.\n\n"
                "Validation: patch applies independently to the stated upstream base. "
                "See wii/UPSTREAM_REVIEW.md for regression evidence and remaining native checks.\n"
            )
            print(f"PASS {patch}")
    print("Independent apply checks only; no upstream runtime or CI pass is claimed.")


if __name__ == "__main__":
    main()
