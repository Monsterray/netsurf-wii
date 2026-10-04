# Wii content blocking and Firefox extensions

Research date: 3 October 2026. This note recommends a design; it does not
report a working extension host or measured blocker speedup.

## Recommendation

Start with an optional native C network blocker using bounded hostname lists,
plus the existing cosmetic CSS setting. Give it a small decision interface
(`allow` / `block`) so a richer native matcher can replace it later. Keep the
module linked into the browser like the current renderer plugins. Loading a
Firefox XPI is a separate browser-runtime project, not the smallest way to
reduce advertising traffic on this Wii.

Use a conservative bundled list, a user blocklist and an allowlist, with a
single disable switch. Block HTTP(S) subresources; preserve explicit top-level
navigation. Match canonical hostnames at label boundaries: a rule for
`ads.example` matches that host and its subdomains, never `badads.example` or
an occurrence in a query string. Allowlist matches win. Normalize case and an
optional final dot, and reject malformed/oversized list entries.

The first version should accept plain hostnames rather than advertise full
EasyList/ABP compatibility. A later importer can accept the exact limited forms
`||host^` and `@@||host^`, rejecting other syntax explicitly. ABP has URL anchors,
wildcards, exceptions, resource types, domain restrictions and cosmetic rules;
discarding an unsupported option silently can overblock legitimate resources.
[ABP's filter specification](https://help.adblockplus.org/hc/en-us/articles/360062733293-How-to-write-filters)
also describes page breakage caused by blocking expected scripts and false
positives from generic cosmetic rules. A hostname-only blocker cannot remove
ads served from the same hostname as essential content.

## Existing project support and integration

- [resources/adblock.css](../resources/adblock.css) is a broad, legacy cosmetic
  stylesheet dated 2004. [HTML stylesheet loading](../content/handlers/html/css.c)
  loads it when `block_advertisements` is enabled. The Wii defaults enable this
  setting in [gui.c](../frontends/framebuffer/gui.c). This setting is not a
  network-request filter: CSS matching alone does not guarantee preventing
  downloads, redirects or script execution.
- [llcache_handle_retrieve](../content/llcache.c) is the natural place to check
  before an object is returned from RAM/disk cache. Also recheck redirect
  destinations before reuse/start, and retain a network-start guard in
  [fetch.c](../content/fetch.c). A filter only in `fetch_start` misses already
  cached resources. Preserve normal asynchronous content-error delivery;
  never call a content callback synchronously from inside retrieval or leave
  its active-fetch accounting waiting forever.
- Existing retrieval flags distinguish verifiable loads, but that is not a
  complete resource-type/initiator model. Verify the actual main-frame,
  subframe, script and image call paths before treating it as navigation
  metadata. Accurate `$third-party` needs an explicit initiating document and
  registrable-domain handling; a string suffix comparison alone is insufficient.
- Use the already parsed URL host rather than scan the whole URL. Avoid per-
  request allocation and JavaScript callbacks. Load/compile rules once; keep
  bounded counters for allowed/blocked decisions, rejected entries and bytes
  occupied. Retain source attribution and licenses for any redistributed list.

An illustrative design budget is 4,096 rules and a 256 KiB character arena,
not a measured requirement. Packed strings with 32-bit sorted offsets add
16 KiB for that many rules, plus metadata and a separately bounded allowlist.
Binary-search each hostname suffix: at most O(labels × log rules) comparisons,
each comparison bounded by hostname length. This avoids a full list scan on
every request and heap churn. Sorting costs O(rules log rules) once at load.
Enforce limits while reading, and preserve the last valid ruleset on failed
reload. Measure actual PPC time, peak memory and browser load behavior before
increasing those proposed limits.

## Why current Firefox extensions will not run as-is

Duktape targets ES5/ES5.1 with incomplete later-language support; it is an
embeddable language engine, not a Firefox browser host.
[Duktape's guide](https://duktape.org/guide) documents that boundary. The local
[dukky.c](../content/handlers/javascript/duktape/dukky.c) supplies NetSurf DOM
bindings and an 8 MiB Wii heap budget. There is no implemented WebExtensions
host in this integration.

Firefox extensions need manifest processing, privileged browser APIs,
permissions and extension lifecycle. Background pages have their own context,
browser APIs and message communication with content scripts.
[Mozilla background-script documentation](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Background_scripts).
Content scripts need injection scheduling, isolated execution and messaging;
sharing the ordinary page heap does not provide that contract.
[Mozilla content-script documentation](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Content_scripts).

For a concrete blocker, uBlock Origin's
[Firefox manifest](https://raw.githubusercontent.com/gorhill/uBlock/master/platform/firefox/manifest.json)
requests storage, tabs, navigation, blocking web requests and other APIs, runs
a background page and injects content scripts into frames at document start.
Its [background source](https://raw.githubusercontent.com/gorhill/uBlock/master/src/js/background.js)
uses ECMAScript modules and additional modern language/library facilities.
Transpiling syntax would not implement the missing browser services. There is
no verified Wii memory/CPU benchmark for this extension; claiming it fits or
does not fit based on desktop marketing would be unjustified. The current
runtime incompatibility is already enough to rule out installing it directly.

Mozilla's
[declarativeNetRequest API](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/declarativeNetRequest)
shows the useful architectural idea: the browser evaluates declarative rules
without asking an extension about each request. A limited data-only rule
importer could eventually reuse compatible blocking rules without running the
extension, but must publish supported conditions/actions and reject unsupported
ones. Such an importer is not Firefox extension compatibility.

[Brave's adblock-rust](https://github.com/brave/adblock-rust) is a native engine
supporting network/cosmetic filtering and ABP/uBO syntax. It is a future richer
matcher candidate, not an immediately verified Wii dependency: its PowerPC
toolchain integration, binary growth, dependencies and peak memory need a
separate port/build evaluation. Its README's performance claims are not Wii
measurements. Start with the small C path while those costs are unknown.

## Hardware and website compatibility boundaries

Hostname matching, network policy, parsing, JavaScript and CSS layout remain
CPU work. The current Wii [hardware plan](HARDWARE_PLAN.md) documents GX fills,
scaled/blended images and cached glyph drawing. Filtering before download can
avoid subsequent decoding/layout/drawing work; that saving must be measured.
Using GX to compare hostname strings would add transfers and synchronization
without providing a suitable native text-matching operation.

Continue GX scrolling/copy and glyph-cache work separately. Ad blocking cannot
supply missing modern JavaScript/DOM APIs, CSS layout modes, anti-bot verification,
media decoding, streaming or DRM. The [website survey](SITE_COMPATIBILITY.md)
already reproduces most failures across JavaScript settings and renderers.
First collect exact HTTP/script/CSS failures and reduce them to replayable
fixtures. Then test blocker-off/on against those fixtures and the real pages;
check false positives, redirect handling, warm-cache behavior and completion
accounting, not just smaller download totals.

## Implemented native filter

`fb_request_filter:hosts` in SD `Choices` enables the compiled C hostname-filter
module; `off` is the default. Restart to reload the lists. Bundled
`adblock-hosts.txt` contains five conservative advertising hostnames;
`adblock-allow.txt` provides exceptions. Files accept one hostname per line,
blank lines and `#` comments. ABP/EasyList syntax, hosts-file address columns,
regular expressions and Firefox XPIs are unsupported.

The deny file is bounded to 256 KiB / 4,096 entries and the allow file to
32 KiB / 512 entries. On Wii, fixed pointer tables add 18 KiB; maximum policy
storage is approximately 306 KiB plus allocator metadata. Startup rejects an
invalid or missing policy and disables filtering with a diagnostic. There is
no live reload or retention of a previous policy. Matching uses binary searches
of DNS label suffixes, allocates nothing per decision, and honors allowlist
precedence and exact initiating-host exemptions.

The shared low-level cache retrieval boundary checks HTTP(S) loads with a
referer and without the verifiable flag, including redirect retrievals, before
cache reuse or a network fetch. This preserves explicit navigations. It is a
conservative use of existing retrieval metadata, not complete resource-type,
registrable-domain or WebExtensions compatibility. The legacy cosmetic CSS
setting remains independent. Reports expose active policy, bytes, rules,
checked decisions and blocked decisions. Host tests cover malformed/oversized
input and label boundaries; the opt-in browser smoke includes a real cache
retrieval probe that must return permission denied before network activity.

The implemented starter policy occupies 18,736 bytes (about 18.3 KiB). Final
Dolphin and dev Wii GX/JavaScript smoke reports both record one checked probe,
one blocked probe, and `request_filter_test=PASS`. This proves the request
boundary on both targets; it is not a measured real-site bandwidth or speed
improvement. Hardware tests back up and restore existing user policy files
while exercising the bundled rules. Invalid-policy startup remains fail-open;
the test checker fails if a requested policy did not actually load.
