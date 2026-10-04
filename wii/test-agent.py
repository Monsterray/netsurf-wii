#!/usr/bin/env python3
"""Compile the real integration against a minimal SDK to check its contracts."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "ogc").mkdir()
    (p / "ogc/lwp_watchdog.h").write_text(
        "#include <stdint.h>\nuint64_t gettime(void);\n#define ticks_to_millisecs(t) (t)\n"
    )
    (p / "gccore.h").write_text("""#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32; typedef unsigned lwp_t;
int LWP_CreateThread(lwp_t *, void *(*)(void *), void *, void *, unsigned, int);
#define usleep agent_sleep
#define _Exit agent_force_exit
#define malloc agent_malloc
#define free agent_free
void *SYS_GetArena1Lo(void); void *SYS_GetArena2Lo(void); void *SYS_GetArena2Hi(void);
void SYS_SetArena2Hi(void *); void SYS_Report(const char *, ...);
""")
    (p / "hbc_agent.h").write_text("""#include <stdbool.h>
#define HBC_CRASH_ADDR 0x91800020
typedef struct {unsigned words[40];} hbc_crash_block;
struct config {const char *name,*version; int priority; bool app_polls_exit;
 unsigned exit_grace_ms; int crash_reload_s; void (*on_exit)(void *);};
typedef struct config hbc_agent_config;
int hbc_agent_init(const hbc_agent_config *); bool hbc_agent_exit_requested(void);
""")
    (p / "hbc_netlog.h").write_text(
        "#define HBC_NETLOG_KEEP_ADDR 0x91800000\ntypedef struct {unsigned words[8];} hbc_netlog_block;\nstatic inline int hbc_netlog_init(void) {return 0;}\n"
    )
    (p / "test.c").write_text("""#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <hbc_agent.h>
static void *hi=(void*)0x933b6f60; static int calls; static bool exiting;
static unsigned allocations, frees;
static uint32_t fake_ms, watchdog_start;
void agent_force_exit(int status) {
 if(getenv("NETSURF_TEST_RESERVATION_FAIL")) {
  assert(allocations==1 && frees==0);_Exit(status);
 }
 assert((uint32_t)(fake_ms-watchdog_start)>=10000 &&
        (uint32_t)(fake_ms-watchdog_start)<11000);
 _Exit(status);
}
static void *(*worker)(void *);
uint64_t gettime(void) {return fake_ms;}
int agent_sleep(unsigned int us) {fake_ms += us/1000; return 0;}
int LWP_CreateThread(unsigned *id, void *(*entry)(void *), void *arg,
 void *stack, unsigned size, int priority) {
 (void)arg; (void)stack; assert(size==8192 && priority==96);
 *id=1; worker=entry; return 0;
}
static uintptr_t mem1lo=0x80400000, mem2lo=0x90002000;
void *agent_malloc(unsigned long size) {
 assert(size==1024*1024); unsigned n=allocations++;
 if(getenv("NETSURF_TEST_RESERVATION_FAIL"))return NULL;
 uintptr_t address=n<2 ? 0x80400000+n*(size+32) : 0x90002000+(n-2)*(size+32);
 if(n<2) mem1lo=address+size+32; else mem2lo=address+size+32;
 return (void*)address;
}
void agent_free(void *p) {assert(p);
 uintptr_t address=(uintptr_t)p;
 assert(!(address<=0x91800000 && address+1024*1024>=0x91801000));
 frees++;}
const char *const netsurf_version="test";
void *SYS_GetArena1Lo(void) {return (void*)mem1lo;}
void *SYS_GetArena2Lo(void) {return (void*)mem2lo;}
void *SYS_GetArena2Hi(void) {return hi;}
void SYS_SetArena2Hi(void *v) {hi=v;}
void SYS_Report(const char *fmt, ...) {(void)fmt;}
int hbc_agent_init(const hbc_agent_config *c) {calls++; assert(c->app_polls_exit);
 assert(c->exit_grace_ms==10000 && c->crash_reload_s==3 && c->on_exit); return 0;}
bool hbc_agent_exit_requested(void) {return exiting;}
#include "framebuffer/wii_agent.h"
int main(int argc, char **argv) {
 (void)argv;
 if(argc>1) {
  fake_ms=0xffffff00u; watchdog_start=fake_ms; wii_agent_stage("test shutdown");
  wii_agent_begin_shutdown(); worker(NULL); assert(!"watchdog returned");
 }
 assert((uintptr_t)hi==0x933b6f60);
 assert(allocations==frees+1 && allocations>20);
 assert(wii_agent_heap_gap()==0x90002000-0x80600040);
 assert(!wii_agent_poll() && calls==0);
 wii_agent_network_ready(-3); assert(!wii_agent_poll() && calls==0);
 wii_agent_network_ready(0); assert(!wii_agent_poll() && calls==1);
 assert(!wii_agent_poll() && calls==1);
 exiting=true; assert(wii_agent_poll()); return 0;
}
""")
    subprocess.run(
        [
            "cc",
            "-DNETSURF_HBC_AGENT",
            "-I" + str(p),
            "-I" + str(ROOT),
            "-I" + str(ROOT / "frontends"),
            str(p / "test.c"),
            str(ROOT / "frontends/framebuffer/wii_agent.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
    assert subprocess.run([str(p / "test"), "watchdog"], timeout=2).returncode == 1
    import os

    assert (
        subprocess.run(
            [str(p / "test")],
            timeout=2,
            env=dict(os.environ, NETSURF_TEST_RESERVATION_FAIL="1"),
        ).returncode
        == 1
    )
    # Disabled builds need neither HBC headers nor the SDK archive.
    (p / "release.c").write_text("""#include <assert.h>
#include "framebuffer/wii_agent.h"
int main(void) {wii_agent_arm_crash(); wii_agent_network_ready(0);
 assert(!wii_agent_poll()); return 0;}
""")
    subprocess.run(
        [
            "cc",
            "-I" + str(ROOT / "frontends"),
            str(p / "release.c"),
            str(ROOT / "frontends/framebuffer/wii_agent.c"),
            "-o",
            str(p / "release"),
        ],
        check=True,
    )
    subprocess.run([str(p / "release")], check=True)
print(
    "PASS: MEM2 reservation, network ordering, one init, cooperative exit, shutdown watchdog including clock wrap, disabled build"
)

# Exercise the actual watcher without contacting a console.
import importlib.util
import json
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("watcher", ROOT / "wii/agent-watch.py")
watcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watcher)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "package").mkdir()
    (p / "package/build-info.txt").write_text("HBC_AGENT=1\n")
    (p / "hbc-before.json").write_text('{"agent":false,"crash":null}')
    agent = {"agent": True, "app": "NetSurf Wii"}
    client = SimpleNamespace(
        HBCError=RuntimeError, crash_report=lambda c, e: "wii_agent_test_crash"
    )
    statuses = iter([agent, {"agent": False, "crash": None}])
    client.status = lambda w: next(statuses)
    with patch.object(watcher.time, "sleep"):
        watcher.watch(client, "unused", p)
    assert json.loads((p / "agent-status.json").read_text()) == agent
    client.status = lambda w: {"agent": True, "app": "Someone else"}
    try:
        with patch.object(watcher.time, "sleep"):
            watcher.watch(client, "unused", p)
    except RuntimeError as error:
        assert "Another app" in str(error)
    else:
        raise AssertionError("Watcher accepted another app")
    crash = {"app": "NetSurf Wii", "exception": 3, "dar": "00000010"}
    (p / "crash-test").touch()
    client.status = lambda w: {"agent": False, "crash": crash}
    with patch.object(watcher.time, "sleep"):
        watcher.watch(client, "unused", p)
    assert json.loads((p / "crash.json").read_text()) == crash
    # A failed live-tool validation still returns this app to HBC.
    (p / "crash-test").unlink()
    (p / "exit-test").touch()
    client.status = lambda w: agent
    client.get_file = lambda *args: (_ for _ in ()).throw(OSError("read failed"))
    recovered = []
    client.exit_app = lambda *args: recovered.append(True)
    try:
        with patch.object(watcher.time, "sleep"):
            watcher.watch(client, "unused", p)
    except OSError:
        pass
    else:
        raise AssertionError("Watcher hid the read failure")
    assert recovered == [True]
    (p / "package/wii-test.html").write_bytes(b"fixture")
    client.screen = lambda w: (1, 1, b"pixels")
    client.yuyv_png = lambda *args: b"png"
    client.request = lambda *args: None
    for phase in [b"stage=return to HBC\n", b"stage=video shutdown\n"]:
        statuses = iter(
            [agent, {"agent": False, "crash": None}, {"agent": False, "crash": None}]
        )
        client.status = lambda w: next(statuses)
        client.get_file = lambda w, path: (
            b"fixture" if path.endswith("wii-test.html") else phase
        )
        try:
            with patch.object(watcher.time, "sleep"):
                watcher.watch(client, "unused", p)
        except RuntimeError as error:
            assert (
                phase != b"stage=return to HBC\n"
                and "Incomplete cooperative exit" in str(error)
            )
        else:
            assert (
                phase == b"stage=return to HBC\n"
            ), "Fallback exit passed as cooperative"
print("PASS: live status, foreign-app refusal, crash evidence, failure recovery")

# Run the real shell cleanup: never modify files in another running app and
# never report success when restoring our temporary configuration fails.
cleanup_source = (ROOT / "wii/hardware-test.sh").read_text()
cleanup_source = cleanup_source[
    cleanup_source.index("cleanup() {") : cleanup_source.index("\ntrap cleanup EXIT")
]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    prefix = """RUN=$1
hbc() {
 if [ "$1" = --json ]; then printf '%s\\n' "$CLEANUP_STATUS"; return; fi
 printf '%s\\n' "$*" >> "$RUN/calls"
 if [ "$CLEANUP_FAIL" = 1 ] && [ "$1" = rm ]; then return 2; fi
}
"""
    import os

    for status, fail, expected in [
        ("false", "0", 0),
        ("true", "0", 1),
        ("false", "1", 1),
    ]:
        (p / "calls").unlink(missing_ok=True)
        environment = dict(
            os.environ, CLEANUP_STATUS='{"agent":' + status + "}", CLEANUP_FAIL=fail
        )
        result = subprocess.run(
            [
                "bash",
                "-c",
                prefix + cleanup_source + "\ntrap cleanup EXIT\nexit 0",
                "test",
                str(p),
            ],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert result.returncode == expected, result.stderr.decode()
        if status == "true":
            assert not (p / "calls").exists()
        else:
            assert "rm sd:/apps/netsurf/wii-test.cfg" in (p / "calls").read_text()
print(
    "PASS: real cleanup refuses a running app and fails on configuration restoration errors"
)
