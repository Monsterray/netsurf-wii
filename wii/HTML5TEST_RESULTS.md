# HTML5test results — 6 October 2026

The live [HTML5test.co](https://html5test.co/) release 9 suite renders its score
and full feature table. Dolphin measures **73 out of 588 points**, up from
70/588. The checker reads the actual live DOM and independently requires the
same score in visible render boxes. Top and scroll captures confirm the panel
and table; a DOM-only result no longer passes.

## Results rendering and capability fixes

- Connected DOM mutations coalesce into one layout update after 50 ms. The
  existing box converter builds a replacement tree without yielding; the
  previous tree stays allocated until conversion succeeds. Style selection
  caches are invalidated, newly arrived stylesheets are included, widgets and
  retired image requests are released, and scheduled work is cancelled on
  close/destruction. Detached feature probes do not rebuild the page.
- `element.style` keeps its identity and reads/writes the live inline style
  attribute. Supported property aliases, priorities and quoted/function values
  cover the page's display and visibility changes. This is a partial CSSOM:
  full value validation, canonical serialization and shorthand expansion remain.
- `dataset` keeps its identity and reads/writes real `data-*` attributes, with
  camel-case conversion, deletion and current-key enumeration. HTML5test awards
  two additional points for custom data. The regression verifies storage and
  enumeration, beyond the site's presence check.
- The boolean `hidden` property reflects its attribute; the default stylesheet
  supplies its display behavior. Hidden elements have no render boxes, and
  clearing the attribute lays them out again. This adds one point.
- `document.compatMode` reads libdom's real document mode. Collection proxies
  handle canonical numeric string keys and return `undefined` out of range.
  Tokenizer/tree-builder checks still fail and earn no additional credit.

The earlier screen, image, geometry, script-type and truthful-method-exposure
fixes are retained. Unsupported modules are still skipped; the new 73 points
come from dataset/hidden support, rather than executing modules as classic JS.

## Validation

GX, 640×480, 32-bit framebuffer, JavaScript enabled and hosts filter enabled.
Only the local regression and HTML5test were tested; BrowserAudit and GPUScore
remain deferred. Sources, screenshots and logs stay in ignored `wii/.deps/runs/`.

- Dolphin `dolphin-98FFGO` established visible results at 70/588;
  `dolphin-7LcfWW` measured 72/588 after dataset; `dolphin-icWXNf` measured
  73/588 after hidden support. Each local regression passed and the owned
  Dolphin instance closed.
- Physical `hardware-rj3oiW`, queue `20261006-083305-c33266`, passed the local
  regression and visible 73/588 result in 128 seconds. It returned to HBC
  1.10.0 without a crash or running agent; settings were restored and the
  log target cleared. `netlog_init=0`; 1,629 network log bytes were captured.
- The first physical attempt (`hardware-zCnzJJ`, queue
  `20261006-083036-8660f4`) lost its connection during package upload, before
  launching NetSurf. Cleanup obtained HBC status without an agent/crash and
  restored configuration. The retry used the shared queue normally.
- A subsequent review keeps ordinary input/textarea edits in the existing
  widget synchronization path, avoiding replacement of the typing caret.
  Dolphin `dolphin-TFbUBl` passed that change at 73/588. The final package,
  with readability-only formatting afterward, passed physical
  `hardware-NNRf3u`, queue `20261006-084025-f4e250`, in 139 seconds: local
  regression PASS, visible 73/588, `netlog_init=0`, 1,630 network log bytes,
  HBC 1.10.0 return, no agent/crash, and configuration/logging cleanup complete.
- Final physical DOL SHA-256:
  `4d1acc62a29fe3e9043aadd5eecc7f7c003b831f1326b1fbb44dca48a4f930a6`.
- Matching ELF SHA-256:
  `991109f845b9554a9c6b4e69b9f913a4d03b34be6488a960e4c000408da69ff6`.
- devkitPPC/GCC 16.1.0 and libogc 3.1.0; agent enabled, crash probe disabled.

The local regression checks actual rendered dimensions after insertion,
show/hide and removal; style/dataset identity and live attribute storage;
collection indexing; screen information; real image decoding/events; and
script types. Host checks run the actual Duktape and nsgenbind, bounded capture
helpers, allocator/timeouts, log formatting and agent lifecycle/cleanup.
Visible-box text is an additional rendering check; screenshots remain necessary
for clipping, alignment and other visual defects.

## Limits and next work

- Layout updates rebuild the whole document synchronously. Incremental subtree
  updates and preserving caret/selection across structural or style changes
  are the next performance/usability work. Pages with existing live frame
  windows or framesets retain their static layout until those window lifetimes
  can be updated safely. Dynamic frame creation/removal remains incomplete.
- Rectangle snapshots update after scheduled layout. Synchronous layout reads,
  inline fragments, transforms, zoom and removed-node reads before that update
  still need work.
- Tokenizer/tree-builder failures require proper fragment context and Unicode
  binding diagnostics. Some bindings pass character counts to byte-string APIs.
- Networking, storage, workers, modern syntax and media remain incomplete.
  Unimplemented interface constructors can still cause presence-only false
  positives (for example, MutationObserver/EventSource). No new placeholder
  interfaces were added to increase the score.
- The CSP subframe still reports an unsupported parent/postMessage error and
  finishes through the site's timeout. Cross-frame messaging and CSP are not
  claimed. GX presentation does not provide WebGL, WebGPU or video codecs.

The hardware plan remains in [HARDWARE_PLAN.md](HARDWARE_PLAN.md).
