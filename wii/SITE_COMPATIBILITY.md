# Website survey — 3 October 2026

## Scope and settings

Tested the eleven requested HTTPS homepages, then the exact requested
`https://www.reddit.com/domain/old.reddit.com/` and the supplemental
`https://old.reddit.com/` comparison. These are logged-out, read-only page
observations from the Wii browser, not desktop-browser or host curl results.
No account login, forms, catalog expansion, checkout, purchasing or playback
was exercised. An observed completed fetch is not a compatibility pass.

The primary runs used GX, a 640x480 32-bit display, JavaScript enabled, the
existing three-second script deadline and 8 MiB per JavaScript heap budget.
Background images remained disabled, as in normal Wii defaults; missing image
observations below must be interpreted with that setting. Foreground images
were enabled. Each page had a 25-second load limit, followed by actual presenter
captures at the top and after a one-screen scroll where possible. Long pages
were not exhaustively inspected.

## Observations

| Site / URL | Observed result |
| --- | --- |
| YouTube | Both targets render placeholders and a large black region rather than a usable homepage. Initial Wii runs hit the load deadline. After the readiness fix, Wii reaches Done in 19.8 seconds with the same incomplete layout. |
| Startpage | Displays “Verifying your request…”; no usable search homepage in either target. |
| Bing | Renders a badly arranged gray page with a white field and fragments such as “Copilot.” Search was not verified. |
| Yahoo | Renders fragments of the search/feed UI, a black header region, and a largely blank scrolled viewport. |
| Amazon | Blank viewport despite the browser reaching Done. |
| Reddit homepage | Blank viewport despite the browser reaching Done. |
| Crunchyroll | Blank viewport despite the browser reaching Done. |
| Twitch | Mostly empty shell with a small outlined control; not a usable homepage. |
| Rumble | Blank top and one-screen scrolled captures despite a long document and Done status. |
| RockAuto | Readable logo, catalog/search tabs and manufacturer list. Scrolling reveals more manufacturers. This is the strongest homepage result; catalog interaction and shopping remain untested. |
| McMaster | Category names, search field and account/order controls render, with missing category images. Part selection/search remain untested. |
| `www.reddit.com/domain/old.reddit.com/` | Both targets render readable legacy-style post listings and links, but the header overlaps. After the readiness fix, Wii reaches Done in 12.0 seconds. |
| `old.reddit.com/` | Redirects to `old.reddit.com/login/?reason=lor2&dest=…`, then a mostly blank login viewport, on both Dolphin and Wii. No login attempted. |

The Wii homepage captures broadly reproduce Dolphin's visual limitations.
The eleven-site Wii run and Reddit run returned to HBC 1.9.3 with no new crash.

## Comparisons and uncertainty

A second eleven-site Dolphin run with JavaScript disabled produced the same
broad visual limitations. Enabling the current engine alone did not make the
modern homepages usable. This does not identify which scripts, browser APIs,
styles or server responses cause each failure.

A software-renderer Dolphin comparison of YouTube, Bing, Yahoo, Rumble and
RockAuto reproduced their incomplete or readable layouts. These observations
do not implicate GX alone. They are visual comparisons, not byte-identical
network replays or performance benchmarks; live responses and timing vary.

The initial Wii survey started its page clock before asynchronous networking
was necessarily ready. Its first request repeatedly stayed on `about:blank`
until the 25-second deadline. Those records remain as evidence, but they are
not proof that the corresponding website is inaccessible. The final harness
waits up to 60 seconds for wiisocket readiness before starting the first URL.
`WII_SITE_SECONDS` can increase the per-page limit (5–90 seconds); the standard
limit remains 25. A readiness-only retest retains 25 seconds to isolate startup
ordering from a longer timeout. That retest loaded YouTube, the requested Reddit
listing and RockAuto successfully within the original page budget, and returned
to HBC without a crash. No longer-timeout run was necessary. This establishes a
startup ordering issue in the initial survey; it does not explain the visual
compatibility failures.

## Reproduction and retained evidence

`site-list.txt` contains all thirteen URLs. See the survey commands in
[README.md](README.md). The checker validates the manifest, completion marker
and full-size top/scroll captures, then writes `results.json`. It deliberately
does not turn a Done status or a blank screenshot into a website success.

Initial survey DOL SHA-256:

```
0b7ab61acbe613db506e2ae169d7e236604bda0d005f45dcbb6d6e2036e48aff
```

Final readiness-aware survey DOL SHA-256:

```
9b58f9d255132c216bd651360a82468f607007c8b5cbba5683e0ef875cc8d993
```

Both packages use devkitPPC r50 / GCC 16.1.0, libogc 3.1.0 and the development
agent with the deliberate crash probe disabled. Matching DOLs/ELFs and raw
evidence are retained under ignored `wii/.deps/runs/`:

- `dolphin-XB72ax`: eleven sites, GX, JavaScript enabled.
- `dolphin-4PYQWR`: eleven sites, GX, JavaScript disabled.
- `dolphin-VLuqcx`: five-site software-renderer comparison, JavaScript enabled.
- `hardware-QQvAW5`: eleven sites, lease `20261003-112323-316972`.
- `dolphin-nB4SHQ`: requested Reddit listing and old-interface comparison.
- `hardware-ycEcyW`: Reddit pair, lease `20261003-113129-8af9ba`. Its test
  completed, but a filename-padding bug interrupted collection. The corrected
  collector covers manifests with fewer than ten entries. Read-only lease
  `20261003-113456-fee0cd` recovered all seven files and validated the results.
- `hardware-kTyzKW`: control/YouTube repeat, lease `20261003-113501-46dafc`.
- `dolphin-bulF0j`: final readiness-aware package, all thirteen URLs.
- `hardware-MlCXwq`: readiness-only retest of YouTube, the exact Reddit listing
  and RockAuto, lease `20261003-113952-4c66d7`; completed in 142 seconds. Matching
  DOL hash is the final hash above. No new crash; returned to HBC 1.9.3.

`python3 wii/test-sites.py` exercises the actual hardware collector loop with
two-, eleven- and thirteen-site manifests to prevent the filename-padding
regression. It runs in CI without contacting websites or hardware.

PNG conversions and contact sheets are inspection copies of the target's PPM
captures. Profiles, logs, screenshots, cookies and generated reports stay out
of Git. Dolphin runs close their owned profiles; Wii runs use the shared lease
queue and return to HBC without powering off the console.

## Next compatibility work

Capture per-request HTTP outcomes and specific JavaScript errors for blank
pages, then create minimal fixtures for the failing DOM/API or layout behavior.
Compare background-image-enabled captures separately. Use RockAuto and the
Reddit domain listing as readable real-page regression fixtures, while keeping
the software plugin available for rendering comparisons. Media, account and
shopping functionality require separate implementations and tests; homepage
loads do not establish them.

## Compatibility fixes and follow-up diagnostics

Added site-mode HTTP status/URL logging, bounded page-source capture (2 MiB),
and controlled cosmetic/background-image settings. Logs and page source remain
ignored local artifacts. `dolphin-g07xgM` retested Amazon, Reddit, Bing and Rumble
with both network filtering and legacy cosmetic filtering off. Each homepage
returned HTTP 200; completion still did not establish usable content.

- Amazon returned a small verification interstitial whose script needs
  `XMLHttpRequest`, which this integration does not implement. It was not an
  ordinary shopping homepage.
- Reddit returned a challenge page containing `async`/`await`, form operations
  and flex layout. The engine reports a syntax error. Adding one DOM method or
  changing the renderer cannot complete that page.
- Bing's strict scripts lost shared variables because `js_exec` compiled page
  scripts as eval. The actual Duktape host regression failed before the fix and
  passes with global-program compilation. The final `dolphin-cRmcQ7` retest
  removes the repeated `_d` errors and most `_w` cascades, exposing additional
  missing screen/history/image/collection APIs. One early `_w` reference still
  fails. The captured layout remains incomplete; search is still unverified.
- Rumble continues to hit syntax errors in its inline and external scripts.
  Video playback remains unsupported. Its listener-options rejection no longer
  appears after the binding fix.

The two four-site runs have the same settings but are live network observations,
not a byte-identical replay. Disabling the old cosmetic stylesheet did not make
these homepages usable. Their blank/partial results must not be labelled a GX
or adblock failure based only on these captures.

Implemented shared fixes:

1. Page scripts execute as global programs, preserving strict-script globals
   across separate script elements. Direct JavaScript `eval` retains the
   engine's own semantics.
2. Listener bindings accept boolean or dictionary options; registration/removal
   use callback identity plus capture, and removal clears the vacated slot.
3. The pinned libdom dispatcher excludes the target from the ancestor path and
   honors the event's bubbling flag. This prevents duplicate target callbacks
   and spurious ancestor callbacks. The bootstrap applies a small tracked
   patch; the browser build rejects a missing/stale dependency patch.
4. `document.title` reads and mutates the title element, allowing the existing
   DOM mutation handling to update the browser caption.
5. Binding generation runs once and invalidates all generated binding objects,
   avoiding parallel-generator races and stale objects when an unchanged header
   accompanies changed generated C.

The event-path change follows the ancestor/target and bubbling distinction in
[WHATWG DOM dispatch](https://dom.spec.whatwg.org/#concept-event-dispatch).
These fixes do not implement complete modern DOM or JavaScript support.
Final browser fixtures check strict globals, duplicate registration, removal
and re-registration, non-bubbling versus bubbling delivery, title mutation,
GX/CPU agreement, PDF/download/cache, and actual request blocking. Both Dolphin
`dolphin-L1xbpT` and Wii `hardware-tO6auo` pass; the latter returns to HBC with
no crash. See [HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md) for the freeze
follow-up and the separate stalled-cleanup test.

Next compatibility work, in order:

1. Reduce missing screen/history/image/collection and timer-argument failures
   to local fixtures, then implement their real behavior. Avoid success-shaped
   stubs that let scripts continue with incorrect state.
2. Implement bounded asynchronous XHR with cancellation, same-origin/CORS,
   redirect checks and request-filter integration before enabling dependent
   sites. A challenge page is not evidence that the entire site will then work.
3. Evaluate a modern JavaScript engine and the binding migration against Wii
   heap/timeout budgets. Current async/module syntax failures require language
   support, not another GX primitive.
4. Test CSS layout/background-image compatibility on reduced pages. Streaming
   needs a separately budgeted codec/network pipeline; DRM/login requirements
   remain additional constraints.
