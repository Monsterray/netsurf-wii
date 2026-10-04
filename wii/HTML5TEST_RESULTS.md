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
