#!/bin/bash
# Isolated full-browser smoke test, following Wii64's MMU/SD-sync setup.
set -euo pipefail
SCRIPT_DIR=${WII_SCRIPT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}
PACKAGE="${WII_PACKAGE:-$SCRIPT_DIR/dist/apps/netsurf}"
[ -s "$PACKAGE/boot.dol" ] || { echo 'Build the browser package first.' >&2; exit 1; }
mkdir -p "$SCRIPT_DIR/.deps/runs"
PROFILE=$(mktemp -d "$SCRIPT_DIR/.deps/runs/dolphin-XXXXXX")
PROFILE=$(cd "$PROFILE" && pwd -P)
mkdir -p "$PROFILE/Config" "$PROFILE/Load/WiiSDSync/apps/netsurf"
cp -R "$PACKAGE/." "$PROFILE/Load/WiiSDSync/apps/netsurf/"
printf 'selftest=1\n' > "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-test.cfg"
if [ "${WII_MEM2_TEST:-0}" = 1 ]; then
    printf 'mem2test=1\n' >> "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-test.cfg"
fi
if [ "${WII_JS_TEST:-0}" = 1 ]; then
    printf 'javascript=1\n' >> "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-test.cfg"
fi
if [ -n "${WII_SITE_LIST:-}" ]; then
    printf 'selftest=0\nsites=1\njavascript=%s\nsite-seconds=%s\n' "${WII_JS_TEST:-0}" "${WII_SITE_SECONDS:-25}" > "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-test.cfg"
    cp "$WII_SITE_LIST" "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-sites.txt"
    printf 'cosmetic=%s\nbackground=%s\n' "${WII_COSMETIC:-1}" "${WII_BACKGROUND:-0}" >> "$PROFILE/Load/WiiSDSync/apps/netsurf/wii-test.cfg"
    rm -rf "$PROFILE/Load/WiiSDSync/apps/netsurf/site-results"
fi
printf 'fb_renderer:%s\nfb_depth:%s\n' "${WII_RENDERER:-soft}" "${WII_DEPTH:-32}" > "$PROFILE/Load/WiiSDSync/apps/netsurf/Choices"
printf 'fb_request_filter:%s\n' "${WII_REQUEST_FILTER:-off}" >> "$PROFILE/Load/WiiSDSync/apps/netsurf/Choices"
if [ -f "$HOME/Library/Application Support/Dolphin/Config/WiimoteNew.ini" ]; then
	cp "$HOME/Library/Application Support/Dolphin/Config/WiimoteNew.ini" "$PROFILE/Config/"
fi
# Copy matching build artifacts before launching; later builds cannot change this run.
cp "$PACKAGE/boot.dol" "$PROFILE/boot.dol"
cp "${WII_ELF:-$SCRIPT_DIR/../nsfb}" "$PROFILE/boot.elf"
# Stop only this isolated profile, leaving other projects' Dolphin instances alone.
find_pid() {
	pgrep -f '/Applications/Dolphin.app/Contents/MacOS/Dolphin' 2>/dev/null | while read -r pid; do
		line=$(ps -p "$pid" -o command= 2>/dev/null || true)
		[[ "$line" != *"$PROFILE"* ]] || printf '%s\n' "$pid"
	done
}
cleanup() {
    local pid
    for pid in $(find_pid || true); do kill -TERM "$pid" 2>/dev/null || true; done
    for _ in {1..10}; do
        [ -n "$(find_pid || true)" ] || return 0
        sleep 1
    done
    # A hung GUI must not survive a failed test. Recheck ownership before KILL.
    for pid in $(find_pid || true); do kill -KILL "$pid" 2>/dev/null || true; done
    sleep 1
    [ -z "$(find_pid || true)" ]
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
open -n -a /Applications/Dolphin.app --args -b -e "$PROFILE/boot.dol" -u "$PROFILE" \
	-C Dolphin.Core.MMU=True -C Dolphin.Core.DSPHLE=False \
	-C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardAllowWrites=True \
	-C Dolphin.Core.WiiSDCardEnableFolderSync=True \
	-C Dolphin.Interface.ConfirmStop=False -C Dolphin.Interface.UsePanicHandlers=False \
	-C Dolphin.Analytics.PermissionAsked=True -C Dolphin.Analytics.Enabled=False \
	-C Graphics.Hacks.ImmediateXFBEnable=True \
	-C Graphics.Hacks.EFBAccessEnable=True -C Graphics.Hacks.EFBToTextureEnable=False \
	-C Graphics.Settings.SafeTextureCacheColorSamples=0 \
	-C Graphics.Hacks.DeferEFBCopies=False \
	-C Logger.Options.WriteToFile=True -C Logger.Options.Verbosity=4 \
	-C Logger.Logs.MASTER=True -C Logger.Logs.BOOT=True
printf 'Dolphin smoke artifacts: %s\n' "$PROFILE"
for _ in {1..15}; do
	pids=$(find_pid || true)
	[ -z "$pids" ] || break
	sleep 1
done
[ -n "$pids" ] || { echo 'Dolphin did not start.' >&2; exit 1; }
remaining=${WII_DOLPHIN_SECONDS:-40}
# Keep waits bounded; SD folder results are synchronized when Dolphin stops.
while [ "$remaining" -gt 0 ]; do
    interval=$((remaining < 30 ? remaining : 30))
    sleep "$interval"
    remaining=$((remaining - interval))
    [ -n "$(find_pid || true)" ] || break
done
cleanup
trap - EXIT
if [ -n "${WII_SITE_LIST:-}" ]; then
    python3 "$SCRIPT_DIR/check-sites.py" "$PROFILE/Load/WiiSDSync/apps/netsurf/site-results" "$WII_SITE_LIST"
    exit
fi
python3 "${WII_SMOKE_CHECKER:-$SCRIPT_DIR/check-smoke.py}" "$PROFILE/Load/WiiSDSync/apps/netsurf" \
	--dolphin-log "$PROFILE/Logs/dolphin.log"

if [ "${WII_MEM2_TEST:-0}" = 1 ]; then
    grep -qx 'mem2_test=PASS' "$PROFILE/Load/WiiSDSync/apps/netsurf/mem2-test.txt"
    cat "$PROFILE/Load/WiiSDSync/apps/netsurf/mem2-test.txt"
fi
