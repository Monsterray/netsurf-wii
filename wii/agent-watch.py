#!/usr/bin/env python3
"""Observe only the app launched by our leased job; preserve crash evidence."""

import importlib.util
import json
from pathlib import Path
import sys
import time


def observe(client, wii, run, seconds=90):
    before = json.loads((run / "hbc-before.json").read_text())
    require_agent = "HBC_AGENT=1" in (run / "package/build-info.txt").read_text()
    exit_test = (run / "exit-test").exists()
    if exit_test and not require_agent:
        raise RuntimeError("Exit test requires an HBC_AGENT=1 build")
    expect_crash = (run / "crash-test").exists()
    seen = False
    exited = False
    deadline = time.monotonic() + seconds
    # The sender returning is not proof that the old HBC has stopped answering.
    time.sleep(3)
    while time.monotonic() < deadline:
        try:
            status = client.status(wii)
        except (OSError, client.HBCError):
            time.sleep(1)
            continue
        if status.get("agent"):
            if status.get("app") != "NetSurf Wii":
                raise RuntimeError("Another app is running; refusing to control it")
            seen = True
            (run / "agent-status.json").write_text(json.dumps(status, indent=2))
            if exit_test and not exited:
                data = client.get_file(wii, "sd:/apps/netsurf/wii-test.html")
                if data != (run / "package/wii-test.html").read_bytes():
                    raise RuntimeError("Agent SD read did not match staged fixture")
                (run / "agent-read.html").write_bytes(data)
                width, height, pixels = client.screen(wii)
                (run / "agent-screen.png").write_bytes(
                    client.yuyv_png(width, height, pixels)
                )
                client.request(wii, b"HBCX")
                exited = True
        elif seen or not require_agent or status.get("crash") != before.get("crash"):
            (run / "hbc-after.json").write_text(json.dumps(status, indent=2))
            crash = status.get("crash")
            (run / "crash.json").write_text(json.dumps(crash, indent=2))
            text = (
                client.crash_report(crash, str(run / "boot.elf"))
                if crash
                else "no crash reported"
            )
            (run / "crash.txt").write_text(text + "\n")
            if expect_crash:
                if not (
                    crash
                    and crash != before.get("crash")
                    and crash.get("app") == "NetSurf Wii"
                    and crash.get("exception") == 3
                    and int(crash.get("dar", "0"), 16) == 0x10
                    and "wii_agent_test_crash" in text
                ):
                    raise RuntimeError(
                        "Expected DSI probe did not match the frozen ELF"
                    )
                return
            if crash != before.get("crash") and crash:
                raise RuntimeError(
                    "New NetSurf crash; see crash.txt and matching boot.elf"
                )
            if require_agent and not seen:
                raise RuntimeError("App returned before its agent was observed")
            if exit_test:
                lifecycle = client.get_file(wii, "sd:/apps/netsurf/wii-lifecycle.txt")
                (run / "wii-lifecycle.txt").write_bytes(lifecycle)
                if lifecycle != b"stage=return to HBC\n":
                    raise RuntimeError(
                        "Incomplete cooperative exit; cleanup fallback may have run"
                    )
            return
        time.sleep(1 if not seen else 5)
    # A timed-out job must return its own app to HBC before releasing the lease.
    status = client.status(wii)
    if seen and status.get("agent") and status.get("app") == "NetSurf Wii":
        client.exit_app(wii, 20)
    raise RuntimeError("NetSurf timed out; cooperative recovery requested")


def watch(client, wii, run, seconds=90):
    try:
        return observe(client, wii, run, seconds)
    except BaseException:
        # A failed read/capture/check must also release our app back to HBC.
        # exit_app additionally checks the shared lease's start time.
        try:
            status = client.status(wii)
            if status.get("agent") and status.get("app") == "NetSurf Wii":
                client.exit_app(wii, 20)
        except Exception as recovery_error:
            print(f"Agent recovery failed: {recovery_error}", file=sys.stderr)
        raise


if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("hbc", sys.argv[1])
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    run = Path(sys.argv[3])
    watch(client, sys.argv[2], run, 420 if (run / "sites-test").exists() else 90)
