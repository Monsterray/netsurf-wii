#!/usr/bin/env python3
"""Compile the native plugin and check matching, overrides, limits and hot-path allocation."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "deny").write_text("# policy\nAds.Example.COM.\ntracker.example.net\n")
    (p / "allow").write_text("safe.ads.example.com\n")
    (p / "test.c").write_text("""#include <assert.h>
#include <stdlib.h>
static unsigned allocations;
static void *tracked_malloc(size_t n) {allocations++; return malloc(n);}
static void *tracked_calloc(size_t n,size_t s) {allocations++; return calloc(n,s);}
#define malloc tracked_malloc
#define calloc tracked_calloc
#include "content/request_filter.c"
#undef malloc
#undef calloc
int main(void) {
 assert(request_filter_init("deny","allow"));
 assert(request_filter_active());
 unsigned before=allocations;
 for(unsigned i=0;i<100000;i++) {
  assert(request_filter_blocked("AD.ads.example.com.","www.example.com"));
  assert(!request_filter_blocked("safe.ads.example.com",NULL));
  assert(!request_filter_blocked("ads.example.com.evil.net",NULL));
  assert(!request_filter_blocked("notads.example.com",NULL));
  assert(!request_filter_blocked("ads.example.com","ADS.EXAMPLE.COM."));
 }
 assert(allocations==before);
 assert(!request_filter_blocked("bad..example.com",NULL));
 request_filter_finalise(); assert(!request_filter_active());
 assert(!request_filter_blocked("ads.example.com",NULL));
 assert(!request_filter_init("missing","allow"));
 assert(!request_filter_init("unsupported","allow"));
 assert(!request_filter_init("embedded-nul","allow"));
 assert(!request_filter_init("oversized","allow"));
 assert(!request_filter_init("too-many","allow"));
 assert(!request_filter_active());
 return 0;
}
""")
    (p / "unsupported").write_text("||ads.example.com^$script\n")
    (p / "embedded-nul").write_bytes(b"ads.example.com\x00tracker.example.net")
    (p / "oversized").write_bytes(b"#" * (256 * 1024 + 1))
    (p / "too-many").write_text("ads.example.com\n" * 4097)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-I" + str(root),
            str(p / "test.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], cwd=p, check=True)
print(
    "PASS: suffix boundaries, allowlist, same-host exemption, malformed/oversized policies, zero hot-path allocations"
)
