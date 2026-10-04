# Startpage compatibility — 4 October 2026

Startpage currently serves this Wii an Anubis v1.26.4 proof-of-work page,
not an image CAPTCHA. Its visible message stays at “Verifying your request”.
The captured HTML loads `main.mjs` as a module; its fallback bootstrap uses
arrow functions. The module uses async functions, Promise jobs, fetch,
workers, Blob URLs and URL/query-parameter operations. The current browser
cannot execute that combination.

The documented GET search route was also tested with the browser's normal
user-agent and received the same verification page. It is not a working
alternative to the homepage on this connection. See Startpage's
[GET/POST documentation](https://support.startpage.com/hc/en-us/articles/5265503371412-POST-versus-GET),
its [JavaScript requirements](https://support.startpage.com/hc/en-us/articles/4455571405204-Why-are-we-requiring-users-to-enable-JavaScript),
and Anubis's [browser challenge design](https://github.com/TecharoHQ/anubis/blob/main/docs/docs/design/how-anubis-works.mdx).

## Corrected cookie capability

`Navigator.bnd` reported `navigator.cookieEnabled === false` unconditionally,
although the fetcher and URL database accept cookies. Anubis explicitly checks
this flag. It now reports the actual enabled capability. Cookie storage,
HttpOnly visibility and the existing per-cookie rules were not changed.

The actual getter regression failed before the change and passed afterward.
The ordinary Wii HTML smoke also asserts the flag before its JavaScript PASS
title. This is a necessary compatibility fix, not a completed Startpage fix.

The packaged `web-features.html` diagnostic reports observed syntax and
behavior. It distinguishes constructor exposure from functionality: Worker
is exposed, but execution is untested; URL resolution and URLSearchParams/Blob
operations fail despite some exposed interface names. An empty first diagnostic
capture exposed its reliance on a load event; the final fixture reports
directly and updates when a module actually executes.

## Target validation

| Check | Artifacts/job | Result |
| --- | --- | --- |
| Host JavaScript regression | `test-javascript.py` | PASS, including actual cookie getter |
| Full physical GX/JS/filter smoke | `hardware-OBlp4j`, `20261004-102141-b8b656` | PASS |
| Wii homepage and documented search route | `hardware-ihlwfb`, `20261004-102356-7ba8ca` | Both remain on verification |
| Matching Dolphin comparison | `dolphin-R2EUD7` | Same verification result |
| Final capability fixture, Wii | `hardware-ZEiFCH`, `20261004-103129-df0866` | Cookies enabled; module/modern syntax/Promise/fetch/URL operations unavailable |
| Final capability fixture, Dolphin | `dolphin-cSbNBk` | Same inspected capability results |

All three physical jobs returned normally to HBC 1.9.3, reported no crash and
completed settings cleanup. All owned Dolphin processes closed. DOL SHA-256:
`57b64d626341d887a74835f31ad98148cd52b21ba3cbc833cdaea8a67f2beeeb`.
Matching ELF SHA-256:
`09df4e6b64a07bc213e069330a146066ce856df75da5da3f1bc9d167ecb1b3db`.
Raw HTML, challenge metadata and captures stay ignored.

## Modern engine feasibility and required work

An ignored local probe of official
[QuickJS 2026-06-04](https://bellard.org/quickjs/) passed host checks for arrow
and async syntax, Promise job execution, an 8 MiB heap limit, interruption and
teardown. It compiled the captured Startpage module without executing it.
The heap after those probes was 205,280 bytes; this is not a peak or a browsing
memory measurement.

Its core also cross-compiled with devkitPPC 16.1.0 for Wii. The object archive
contains 1,037,221 bytes of text/data before final linking. Three local changes
were needed: exclude OS-thread Atomics on GEKKO, include newlib's malloc header,
and use the existing portable timezone path rather than `tm_gmtoff`.
Neither the archive nor its browser integration has run on the physical Wii.
QuickJS is not linked into the browser and no engine setting is exposed yet.

Probe source, portability patch, archives and logs are under ignored
`wii/.deps/quickjs-probe/`. Official source archive SHA-256:
`b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a`.

The next required work is broader than a CAPTCHA widget:

1. Validate the modern engine's heap, stack, interruption and Promise/module
   behavior on Wii before replacing the active runtime.
2. Port the real DOM/window bindings and service Promise jobs from the browser
   scheduler, preserving script globals, teardown and execution limits.
3. Implement module retrieval plus bounded asynchronous fetch with origin,
   redirect, cancellation and request-filter checks.
4. Provide real URL/query-parameter and Blob/object-URL behavior, and isolated
   worker execution with cancellation and a small per-page resource budget.
   The Wii's single CPU does not justify a large worker pool.
5. Retest the complete Startpage verification, search submission and readable
   results on Wii. Completing a challenge alone would not establish that the
   actual search application works.
