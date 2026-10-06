# HTML5test results — 6 October 2026

The live [HTML5test.co](https://html5test.co/) release 9 suite now completes with
**70 out of 588 points** on both Dolphin and the physical dev Wii. The score
and full feature table were read from the page's live DOM, without modifying
the site's scripts, injecting results, or substituting a local benchmark.

**The screen still shows a blank results area.** JavaScript inserts the score
and table successfully, but NetSurf's static box tree does not repaint the
changed DOM or apply the subsequent visibility changes. Completing the test
engine does not establish that the results UI, or modern websites generally,
work correctly.

## Fixes that allowed the suite to complete

- `Window.screen` supplies actual framebuffer display dimensions and depth
  (previous commit `57cb40118`). The initial missing `screen` exception
  prevented the browser detector from starting the suite.
- Element rectangle snapshots expose existing rendered box bounds, borders,
  and viewport scroll offsets. Boxless elements return empty rectangles.
  This removes the missing `getBoundingClientRect` exception. The binding is
  limited by static layout: newly inserted nodes, inline fragments, CSS
  transforms and fully updated layout still require further work. Unsupported
  MathML/SVG geometry is not inferred from test attributes.
- The nsgenbind dependency patch omits methods and properties without a
  binding. Previously `input.validity` existed but returned `undefined`, so
  feature detection reached `element.validity.valid` and aborted. Implemented
  methods and reflected DOM attributes remain exposed.
- `Image` constructs a real HTML image element. Detached image requests use
  the existing image cache/decoders, expose intrinsic dimensions and completion,
  and dispatch load/error events to the node. Replacing a source cancels the
  previous request and suppresses its events. Libdom dispatches attribute
  mutation events before committing values; the loader uses the event's new
  source instead of reading the old attribute.
- External scripts respect their declared type. Unsupported modules and
  non-JavaScript data blocks are skipped; an empty type means classic JavaScript.
  Before this correction HTML5test reported 73/588 because module scripts ran
  incorrectly as classic scripts. The final 70/588 removes that mistaken credit.

## Validation

GX, 640×480, 32-bit framebuffer, JavaScript enabled, hosts filter enabled.
The manifest contains the local regression followed by HTML5test only;
BrowserAudit and GPUScore remain deferred.

- Reviewed Dolphin package `dolphin-4gs81y`: local regression PASS and
  HTML5test **70/588**; the isolated instance closed.
- Physical `hardware-JQXKXQ`, queue job `20261006-010320-5bbf67`: local
  regression PASS, HTML5test **70/588**, successful exit in 137 seconds.
  HBC 1.10.0 answered after exit, no crash was reported, the agent was absent,
  settings cleanup completed, and the logging target was cleared.
- The physical receiver used `/usr/bin/python3`: `netlog_init=0` and 2,058
  network log bytes received. An earlier attempt timed out connecting the
  receiver; it returned safely to HBC. The harness now collects SD diagnostics
  before checking network log delivery, so a receiver failure cannot discard
  available browser results. See [HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md).
- Physical DOL SHA-256:
  `4e5563121962edf28b2910772b08e25e14868a819f52211379f5e70ec87dca85`.
- Matching ELF SHA-256:
  `dc5f2180e7af9136bbd9afcc2c060fc84af16ab6d7b8d96add6a7cf666d24a47`.
- The reviewed package also initializes script-type pointers before error cleanup,
  after the physical run. Final Dolphin DOL: `0962508f10a8c01f9797d56b36a1ea5ac07e4f5104a9c7240bae2a8baaca8bd7`;
  matching ELF: `26c653c978ab3c1a96d466bd3eb09f685b8dee054cfd2fbc8c0a0c6a58588aef`.
- devkitPPC/GCC 16.1.0 and libogc 3.1.0; agent enabled, crash probe disabled.

The regression checks actual screen information, rendered border geometry,
empty detached bounds, rectangle mutation, image constructor identity,
decoded dimensions, load/error events, source replacement, and script types.
Host checks cover actual generated API exposure, bounded DOM-text capture,
JavaScript memory/timeout behavior, log formatting, agent lifecycle and cleanup.
DOM text is streamed with a 2 MiB output limit and depth limit of 128, avoiding
a second allocation containing all document text. Survey success requires a
numeric HTML5test score; a finished download or blank page fails the check.
Raw sources, captures and logs remain in ignored `wii/.deps/runs/`.

## What the score means and what remains

The feature table detects working WebP decoding, parts of Canvas 2D, typed
arrays and JSON. Networking APIs, storage, workers, modern JavaScript syntax,
media and many DOM APIs are absent or incomplete. Some checks test only the
presence of an interface: generated interface constructors can still create
false positives (for example, MutationObserver). Missing DOM bindings can
also hide capabilities of the underlying parser. This is a measured test score,
not a standards-conformance certification.

Priorities from this run:

1. Update layout/styles after DOM mutations so the live results render, then
   finish geometry for inline fragments, detached nodes, scrolling/zoom and
   updated layout.
2. Implement DOM collection indexing and accurate remaining document bindings
   before interpreting tokenizer/tree-builder failures as parser defects.
3. Provide real asynchronous networking, storage and scheduling APIs; introduce
   a bounded modern JavaScript engine before supporting modules and promises.
4. Continue GX acceleration through the existing renderer boundary. A GX-backed
   framebuffer does not supply WebGL, WebGPU or modern video decoding APIs.

The CSP subframe still logs an unsupported parent/postMessage error. Its test
finishes via the site's timeout and reports no CSP support. No CSP enforcement
or cross-frame messaging support is claimed.
Hardware priorities remain in [HARDWARE_PLAN.md](HARDWARE_PLAN.md).
