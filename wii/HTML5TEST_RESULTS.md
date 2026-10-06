# HTML5test investigation — 4 October 2026

The live `https://html5test.co/` page produced no score on either the physical
Wii or Dolphin. The header renders, but the results area stays blank. This is
an aborted test, not a zero score or an HTML5 compatibility pass.

Both targets fetched the HTML, stylesheet, `base.js`, `scripts/9/engine.js`
and `scripts/9/data.js` successfully with JavaScript enabled. The browser log
then reports:

```text
ReferenceError: identifier 'screen' undefined
at loadWhichBrowser (https://html5test.co/scripts/base.js:69)
```

The site's browser detector reads `screen.width` and `screen.height` before
calling its completion callback. That callback creates `new Test(...)`, so
the feature suite never starts. NetSurf currently has no Screen binding or
Window.screen property. The first required fix is real display information
through that binding, followed by an HTML5test-only rerun. Viewport or document
dimensions must not be substituted for screen dimensions. Further failures
may become visible after this first blocker is fixed.

The site's [description](https://html5test.co/) also explains that some checks
detect feature presence without testing functionality. Generated placeholder
interfaces can therefore inflate a future score; behavioral checks and actual
site usability remain necessary, especially for workers and graphics APIs.

## Evidence

- Physical run: `hardware-heVnYE`, queue job `20261004-104147-b469a3`.
  HTML5test was first in the manifest, reached Done in 2.1 seconds and was
  captured after 2.6 seconds. Inspected top and scrolled captures match the
  blank result area. The app returned to HBC with no crash and cleanup finished.
- Dolphin run: `dolphin-S1TXj8`. HTML5test was first, reached Done in 1.1
  seconds and was captured after 2.3 seconds, with the same error and layout.
  Its isolated Dolphin instance closed.
- GX, 640x480, 32-bit display; JavaScript enabled; hosts filter enabled.
- DOL SHA-256:
  `57b64d626341d887a74835f31ad98148cd52b21ba3cbc833cdaea8a67f2beeeb`.

These runs were queued before the request to proceed one site at a time and
also visited BrowserAudit and GPUScore afterward. Those observations do not
establish completed benchmark runs. Further work is focused on HTML5test.
Raw sources, logs and captures remain under ignored `wii/.deps/runs/`.

## Progress — 6 October 2026

`Window.screen` now exposes a cached, read-only Screen object backed by the
framebuffer's actual display geometry. Width/height and available width/height
report the full output surface. Color depth excludes alpha/padding: 24 for
the 32-bit RGB surface, 16 for RGB565. Other frontends can implement the optional
screen-dimensions callback; it does not substitute document dimensions.
The interface follows the [Screen specification](https://www.w3.org/TR/cssom-view-1/#the-screen-interface).

Host checks passed for live dimension/depth changes. The physical GX32/JS/filter
smoke (`hardware-D0O0kn`, job `20261005-235512-e0a435`) also passed the Screen
identity, dimensions and read-only checks, with all existing regressions
passing and a clean return to HBC 1.10.0. Screen fix commit: `57cb40118`.

HTML5test now fetches its WhichBrowser detector successfully, with the real
640x480 dimensions. Waiting 45 seconds on both targets still produced no
score (`dolphin-yZF8Ee`; `hardware-g2XzFi`, `20261005-235836-1abf39`).
The survey now supports a minimum observation interval so network completion
does not cut off asynchronous test execution.

A minimal bootstrap fixture (`dolphin-xOY3S9`) verified that the detector
callback and a five-second timer both run. Capturing caught page-console
exceptions exposed the next blocker on the live page:

```text
TypeError: undefined not callable (property 'getBoundingClientRect' of [object Object])
```

The first failing call is the test suite's dynamically inserted MathML
`mspace` geometry test. Element geometry is absent from the existing bindings.
Implementing it needs real layout bounds relative to the viewport, including
layout updates, scroll offsets, borders, inline fragments and detached nodes.
Returning invented rectangles would produce misleading feature results.
This method has not been implemented; HTML5test still has no valid score.

The same exception was captured on the physical Wii in `hardware-rHdR1S`,
job `20261006-000944-d617ec`, with clean HBC return and no crash. Its separate
network `agent.log` was empty, so that run does not establish live log delivery.
The preceding `20261006-000713-9f236c` attempt timed out while staging in HBC,
before NetSurf launched; cleanup restored settings and HBC remained responsive.
See [HBC_AGENT_VALIDATION.md](HBC_AGENT_VALIDATION.md) for log delivery validation.
Live delivery subsequently passed in `hardware-rtuATs`, job
`20261006-002217-0f55d9`, using the system Python receiver. Both the SD log and
the received network log contain the same missing-geometry exception, and
the app again returned cleanly to HBC. This does not change the HTML5test
compatibility verdict.
