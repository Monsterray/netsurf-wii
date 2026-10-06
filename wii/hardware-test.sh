#!/bin/bash
# Full-browser smoke through the shared HBC-Reborn lease queue.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
CLIENT=${HBC_CLIENT:-"$ROOT/../hbc-reborn/tools/hbc.py"}
BENCH=${WII_BENCH_CLIENT:-"$HOME/.wii-bench/wiibench.py"}
if [ -z "${WII_HOST_PYTHON:-}" ]; then
    WII_HOST_PYTHON=python3
    # The signed macOS interpreter can receive logs under the existing firewall
    # policy; a separately installed Python binary may need its own permission.
    if [ "$(uname -s)" = Darwin ] && [ -x /usr/bin/python3 ]; then
        WII_HOST_PYTHON=/usr/bin/python3
    fi
fi
if [ -z "${WII_BENCH_JOB:-}" ]; then
	[ -s "$SCRIPT_DIR/dist/apps/netsurf/boot.dol" ] || { echo 'Build the browser first.' >&2; exit 1; }
	mkdir -p "$SCRIPT_DIR/.deps/runs"
	RUN=$(mktemp -d "$SCRIPT_DIR/.deps/runs/hardware-XXXXXX")
	RUN=$(cd "$RUN" && pwd -P)
	mkdir "$RUN/package"
	cp -R "$SCRIPT_DIR/dist/apps/netsurf/." "$RUN/package/"
	cp "$ROOT/nsfb" "$RUN/boot.elf"
	cp "$SCRIPT_DIR/check-smoke.py" "$RUN/check-smoke.py"
	cp "$SCRIPT_DIR/agent-watch.py" "$RUN/agent-watch.py"
    if [ "${WII_AGENT_LOG_TEST:-0}" = 1 ]; then
        grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt" || { echo 'Log test needs agent build' >&2; exit 1; }
        touch "$RUN/log-test"
    fi
    cp "$SCRIPT_DIR/check-sites.py" "$RUN/check-sites.py"
	if [ "${WII_AGENT_EXIT_TEST:-0}" = 1 ]; then
		grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt" || { echo "Exit test needs HBC_AGENT=1" >&2; exit 1; }
		touch "$RUN/exit-test"
	fi
    if [ "${WII_EXIT_STALL_TEST:-0}" = 1 ]; then
        grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt" || exit 1
        [ ! -f "$RUN/exit-test" ] || { echo 'Choose cooperative exit or stalled cleanup test' >&2; exit 1; }
        touch "$RUN/stall-test"
    fi
	if [ "${WII_AGENT_CRASH_TEST:-0}" = 1 ]; then
        grep -qx "HBC_AGENT_CRASH_TEST=1" "$RUN/package/build-info.txt" || { echo "Crash test needs an explicitly built probe" >&2; exit 1; }
        [ ! -f "$RUN/exit-test" ] && [ ! -f "$RUN/stall-test" ] || { echo "Choose crash or exit test" >&2; exit 1; }
        touch "$RUN/crash-test"
    fi
	printf 'selftest=1\n' > "$RUN/package/wii-test.cfg"
	[ ! -f "$RUN/crash-test" ] || printf 'selftest=0\nagent-crash=1\n' > "$RUN/package/wii-test.cfg"
	[ ! -f "$RUN/exit-test" ] || printf 'selftest=0\n' > "$RUN/package/wii-test.cfg"
	[ ! -f "$RUN/stall-test" ] || printf 'selftest=0\nexit-stall=1\n' > "$RUN/package/wii-test.cfg"
	if [ "${WII_MEM2_TEST:-0}" = 1 ]; then
        [ ! -f "$RUN/stall-test" ] || exit 1
        grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt" || { echo "MEM2 test needs agent build" >&2; exit 1; }
        [ ! -f "$RUN/exit-test" ] || { echo "Choose memory or exit test" >&2; exit 1; }
        touch "$RUN/mem2-test"
        printf 'mem2test=1\n' >> "$RUN/package/wii-test.cfg"
    fi
	if [ "${WII_JS_TEST:-0}" = 1 ]; then
        printf 'javascript=1\n' >> "$RUN/package/wii-test.cfg"
    fi
    if [ -n "${WII_SITE_LIST:-}" ]; then
        grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt" || { echo 'Site survey needs agent build' >&2; exit 1; }
        [ ! -f "$RUN/crash-test" ] && [ ! -f "$RUN/exit-test" ] && [ ! -f "$RUN/mem2-test" ] && [ ! -f "$RUN/stall-test" ] || exit 1
        cp "$WII_SITE_LIST" "$RUN/package/wii-sites.txt"
        printf 'selftest=0\nsites=1\njavascript=%s\nsite-seconds=%s\n' "${WII_JS_TEST:-0}" "${WII_SITE_SECONDS:-25}" > "$RUN/package/wii-test.cfg"
        touch "$RUN/sites-test"
        printf 'site-min-seconds=%s\n' "${WII_SITE_MIN_SECONDS:-2}" >> "$RUN/package/wii-test.cfg"
        printf 'cosmetic=%s\nbackground=%s\n' "${WII_COSMETIC:-1}" "${WII_BACKGROUND:-0}" >> "$RUN/package/wii-test.cfg"
    fi
	printf 'fb_renderer:%s\nfb_depth:%s\n' "${WII_RENDERER:-soft}" "${WII_DEPTH:-32}" > "$RUN/package/Choices"
    printf 'fb_request_filter:%s\n' "${WII_REQUEST_FILTER:-off}" >> "$RUN/package/Choices"
	job=$(python3 "$BENCH" add --name 'NetSurf Wii full-browser smoke' \
		--agent netsurf-wii --timeout "${WII_JOB_SECONDS:-240}" --cwd "$ROOT" -- \
		env "NETSURF_WII_RUN=$RUN" "HBC_CLIENT=$CLIENT" \
		"WII_HOST_PYTHON=${WII_HOST_PYTHON:-python3}" \
		bash -c "$(cat "$SCRIPT_DIR/hardware-test.sh")" "$SCRIPT_DIR/hardware-test.sh")
	printf 'Hardware smoke queued: %s\nArtifacts: %s\n' "$job" "$RUN"
	exec python3 "$BENCH" wait "$job"
fi
RUN=${NETSURF_WII_RUN:?Missing frozen run directory}
WII=${WII_BENCH_IP:?The dispatcher must provide the leased Wii address}
hbc() { "${WII_HOST_PYTHON:-python3}" "$CLIENT" --wii "$WII" "$@"; }
hbc wait 60
hbc --json status > "$RUN/hbc-before.json"
python3 - "$RUN/hbc-before.json" <<'PY_CHECK'
import json, sys
assert not json.load(open(sys.argv[1])).get("agent"), "Wii is running another app; refusing to stage"
PY_CHECK
# Preserve evidence from an interrupted preceding browser run before staging.
hbc get sd:/apps/netsurf/wii-test.txt "$RUN/previous-report.txt" > "$RUN/previous-report.log" 2>&1 || true
# Preserve the user's Choices, then restore them when the app returns to HBC.
hbc get sd:/apps/netsurf/Choices "$RUN/original-Choices" > "$RUN/choices-backup.log" 2>&1 || {
    if ! grep -q 'ENOENT' "$RUN/choices-backup.log"; then
        cat "$RUN/choices-backup.log" >&2; exit 1
    fi
}
# Policy files are user configuration; smoke tests temporarily use bundled rules.
for policy in adblock-hosts.txt adblock-allow.txt; do
    hbc get "sd:/apps/netsurf/$policy" "$RUN/original-$policy" > "$RUN/$policy-backup.log" 2>&1 || {
        if ! grep -q 'ENOENT' "$RUN/$policy-backup.log"; then
            cat "$RUN/$policy-backup.log" >&2; exit 1
        fi
    }
done
cleanup() {
    local outcome=$?
    trap - EXIT
    if ! hbc --json status > "$RUN/cleanup-status.json" ||
            ! python3 - "$RUN/cleanup-status.json" <<'PY_CLEANUP'
import json, sys
assert not json.load(open(sys.argv[1])).get("agent"), "App has not returned to HBC"
PY_CLEANUP
    then
        echo "Cleanup blocked: HBC did not return; preserve this run for recovery." >&2
        exit 1
    fi
    hbc get sd:/apps/netsurf/wii-lifecycle.txt "$RUN/wii-lifecycle.txt" > "$RUN/lifecycle-collection.log" 2>&1 || true
    hbc rm sd:/apps/netsurf/wii-test.cfg >> "$RUN/staging.log" 2>&1 || outcome=1
    if [ -f "$RUN/original-Choices" ]; then
        hbc put "$RUN/original-Choices" sd:/apps/netsurf/Choices >> "$RUN/staging.log" 2>&1 || outcome=1
    else
        hbc rm sd:/apps/netsurf/Choices >> "$RUN/staging.log" 2>&1 || outcome=1
    fi
    for policy in adblock-hosts.txt adblock-allow.txt; do
        if [ -f "$RUN/original-$policy" ]; then
            hbc put "$RUN/original-$policy" "sd:/apps/netsurf/$policy" >> "$RUN/staging.log" 2>&1 || outcome=1
        fi
    done
    exit "$outcome"
}
trap cleanup EXIT
hbc sync "$RUN/package" sd:/apps/netsurf > "$RUN/staging.log"
if [ -f "$RUN/sites-test" ]; then
    hbc rm -r sd:/apps/netsurf/site-results >> "$RUN/staging.log" 2>&1 || true
fi
# Only remove this harness's prior outputs; retain browser settings and downloads.
for path in wii-test.txt wii-test.ppm wii-test-gx.ppm wii-test.pdf Downloads/wii-test.bin mem2-test.txt; do
	hbc rm "sd:/apps/netsurf/$path" >> "$RUN/staging.log" 2>&1 || true
done

"${WII_HOST_PYTHON:-python3}" "$RUN/agent-watch.py" "$CLIENT" "$WII" "$RUN" --launch
if grep -qx "HBC_AGENT=1" "$RUN/package/build-info.txt"; then
    hbc get sd:/apps/netsurf/wii-lifecycle.txt "$RUN/wii-lifecycle.txt"
    hbc get sd:/apps/netsurf/agent-log-status.txt "$RUN/agent-log-status.txt"
fi
# Preserve SD diagnostics even if the separate network log check fails.
check_agent_log() {
    if [ -f "$RUN/log-test" ]; then
        grep -qx 'netlog_init=0' "$RUN/agent-log-status.txt" || {
            echo 'Agent network log initialization failed; SD diagnostics collected' >&2
            return 1
        }
        [ -s "$RUN/agent.log" ] || { echo 'No live agent logs received' >&2; return 1; }
    fi
}
if [ -f "$RUN/stall-test" ]; then
    hbc get sd:/apps/netsurf/wii-lifecycle.txt "$RUN/wii-lifecycle.txt"
    grep -qx 'stage=shutdown stall probe' "$RUN/wii-lifecycle.txt"
    check_agent_log
    printf 'Stalled cleanup watchdog returned to HBC: %s\n' "$RUN"
    exit 0
fi
if [ -f "$RUN/sites-test" ]; then
    mkdir -p "$RUN/site-results"
    hbc get sd:/apps/netsurf/site-results/complete.txt "$RUN/site-results/complete.txt"
    hbc get sd:/apps/netsurf/site-results/browser.log "$RUN/site-results/browser.log"
    for index in $(seq 1 "$(wc -l < "$RUN/package/wii-sites.txt" | tr -d ' ')"); do
        printf -v index '%02u' "$index"
        for suffix in .txt -top.ppm -scroll.ppm -source.html -dom.txt -layout.txt; do
            hbc get "sd:/apps/netsurf/site-results/$index$suffix" "$RUN/site-results/$index$suffix"
        done
    done
    python3 "$RUN/check-sites.py" "$RUN/site-results" "$RUN/package/wii-sites.txt"
    check_agent_log
    printf 'Hardware site artifacts: %s\n' "$RUN"
    exit
fi
check_mem2() {
    if [ -f "$RUN/mem2-test" ]; then
    hbc get sd:/apps/netsurf/mem2-test.txt "$RUN/mem2-test.txt"
    grep -qx 'mem2_test=PASS' "$RUN/mem2-test.txt"
    cat "$RUN/mem2-test.txt"
fi
}
if [ -f "$RUN/crash-test" ]; then
    check_mem2
    check_agent_log
    printf "Crash capture and HBC recovery passed: %s\n" "$RUN"
    exit 0
fi
if [ -f "$RUN/exit-test" ]; then
    check_agent_log
    printf "Cooperative agent exit passed: %s\n" "$RUN"
    exit 0
fi
for artifact in wii-test.txt wii-test.ppm wii-test.pdf; do
	hbc get "sd:/apps/netsurf/$artifact" "$RUN/$artifact"
done
if grep -Eq '^renderer=(soft|gx)$' "$RUN/wii-test.txt"; then
    hbc get sd:/apps/netsurf/wii-test-gx.ppm "$RUN/wii-test-gx.ppm"
fi
hbc crash --elf "$RUN/boot.elf" > "$RUN/crash.txt"
python3 "$RUN/check-smoke.py" "$RUN"
check_mem2
check_agent_log
printf 'Hardware smoke artifacts: %s\n' "$RUN"
