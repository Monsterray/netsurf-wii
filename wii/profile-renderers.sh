#!/bin/bash
# Guest-side timing A/B/A, following Wii64's frozen-build profiling workflow.
set -euo pipefail
# Freeze this runner in memory: edits during a survey must not change its tail.
if [ "${NETSURF_PROFILE_FROZEN:-0}" != 1 ]; then
    exec env NETSURF_PROFILE_FROZEN=1 bash -c "$(cat "${BASH_SOURCE[0]}")" "${BASH_SOURCE[0]}"
fi
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
mkdir -p "$SCRIPT_DIR/.deps/runs"
SURVEY=$(mktemp -d "$SCRIPT_DIR/.deps/runs/gx-profile-XXXXXX")
SURVEY=$(cd "$SURVEY" && pwd -P)
mkdir "$SURVEY/package"
cp -R "$SCRIPT_DIR/dist/apps/netsurf/." "$SURVEY/package/"
cp "$SCRIPT_DIR/../nsfb" "$SURVEY/boot.elf"
cp "$SCRIPT_DIR/dolphin-test.sh" "$SURVEY/dolphin-test.sh"
cp "$SCRIPT_DIR/check-smoke.py" "$SURVEY/check-smoke.py"
cp "$SCRIPT_DIR/profile-report.py" "$SURVEY/profile-report.py"
python3 - "$SURVEY" "$SCRIPT_DIR/.." <<'METADATA'
import hashlib, json, pathlib, subprocess, sys
survey, root = map(pathlib.Path, sys.argv[1:])
names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-co', '--exclude-standard', '-z']).decode().split('\0')
files = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
         for name in sorted(set(names)) if name and (root / name).is_file()}
metadata = {'head': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD']).decode().strip(),
            'files_sha256': files, 'build_info': (survey / 'package/build-info.txt').read_text(),
            'fonts_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in (survey / 'package/fonts').glob('*') if p.is_file()}}
(survey / 'source.json').write_text(json.dumps(metadata, indent=2) + '\n')
METADATA
printf 'Profiling artifacts: %s\n' "$SURVEY"
for depth in 32 16; do
    for run in soft-before gx soft-after; do
        renderer=${run%%-*}
        WII_SCRIPT_DIR="$SCRIPT_DIR" WII_SMOKE_CHECKER="$SURVEY/check-smoke.py" \
            WII_PACKAGE="$SURVEY/package" WII_ELF="$SURVEY/boot.elf" \
            WII_RENDERER="$renderer" WII_DEPTH="$depth" \
            "$SURVEY/dolphin-test.sh" > "$SURVEY/$depth-$run.log" 2>&1 || {
                cat "$SURVEY/$depth-$run.log" >&2; exit 1;
            }
        printf 'PASS: depth=%s renderer=%s\n' "$depth" "$run"
    done
done
python3 "$SURVEY/profile-report.py" "$SURVEY"
