# Switch visual browser — 0.11.1

Experimental HTML/CSS rendering and touchscreen candidate, separate from the
reader and WASM probe. It uses pinned litehtml with its bundled Gumbo parser and
an SDL2/SDL_ttf/SDL_image drawing adapter. litehtml upstream explicitly does not
recommend it as a full browser engine. This prototype tests a native rendering
path; it is not a production engine selection or a playable Keldurn client.

## What is implemented

- Persistent TLS WebSocket connections (`wss:`), text/binary messages, subprotocols,
  buffered sends, ping/pong and close events. The event loop cancels old sockets
  on navigation. A buffered immutable Blob supports binary messages and HTTP bodies.
  See the 0.11.0 section below for transport, credentials and memory limits.

- URL-based ES modules with static/dynamic imports, live exports and top-level await;
  sources load asynchronously through the browser module map. Import maps, workers,
  WASM/game bindings and graphics remain separate milestones.
- Persistent QuickJS session bound to the same native litehtml document that is
  painted/hit-tested. Resource completion and viewport changes preserve element
  identity, handlers and JavaScript state. A checksum-locked renderer patch rebuilds
  CSS/render state without replacing the document or accumulating old font caches.
- Retained HTML document, external/inline CSS, engine cascade and layout,
  flex layout (including gap/row-gap/column-gap), font metrics and engine link hit testing.
  Flex gaps participate in wrapping, flexible lengths and alignment; row/column,
  reverse, wrapped lines, length units and definite-base percentages are covered.
  Grid/multicol gaps and complete intrinsic-sizing/calc cases are not implemented.
- Cyrillic DejaVu text, solid backgrounds with rounded fills, rectangular solid
  borders, rectangular nested clipping and PNG/JPEG/WebP resources.
- Cached linear, radial and conic gradient tiles (premultiplied sRGB, bounded
  512-pixel tile axes / 8 MiB cache). Advanced color spaces and rounded gradient
  clipping remain unsupported.
- Taps on painted links; drag scrolling that suppresses taps; decaying inertia;
  anchored two-contact pinch zoom (100–250%); contact/focus cancellation.
- Native input/textarea boxes with values, placeholders, readonly/disabled states,
  password masking and the Switch keyboard. Control width and height are independent
  (no image aspect-ratio scaling), with native min/max and border-box sizing.
  Borderless text fields get a thin native frame; the active field has a blue frame.
  Long values fit at UTF-8 boundaries; an active single-line field shows the tail,
  and textareas wrap unless wrap=off. Values stay in memory for the current
  navigation and survive image/style-driven relayout and viewport changes.
- Native GET/POST forms with URL-encoded text, textarea, hidden fields, checked
  checkboxes/radio groups, single/multiple selects and the chosen submitter.
  Select menus support touch dragging, right-stick scrolling and D-pad/A selection;
  B closes the menu. Disabled options cannot be chosen. Labels activate controls.
  Reset restores markup defaults; required/email/number checks catch basic errors.
  This is a subset of HTML constraint validation, not the full standard.
- Form POST redirects handle 301/302/303 as GET and preserve POST for 307/308.
  HTTPS and the supported CSP form-action rules are checked before each hop.
  Cookies are shared with page resources. POST bodies are bounded to 64 KiB,
  omitted from history/diagnostics, and released after transport. Reload does not
  replay a POST response; back skips POST response entries. A GET response reached
  by redirect can be reloaded. Password fields require POST.
- Separate native chrome hit regions, an HTTPS address keyboard on Switch,
  back/reload, left-stick pointer + A, D-pad scroll and ZL/ZR zoom. The right stick
  scrolls vertically with an 18% dead zone and quadratic speed up to 2,200 screen
  pixels/second, using elapsed frame time. Touch interaction takes priority.
- Minus cycles desktop (1280 CSS px), tablet (960) and mobile (640) layout widths.
  Each width reruns media queries/layout; pinch zoom remains separate. Rendering,
  hit testing, drag and pinch coordinates use the combined layout/zoom scale.
  This does not implement CSS Grid or repair every site's layout.
- Up to four concurrent libcurl-multi requests pumped by the UI loop, HTTPS certificate
  validation and the reader's tested root importer. One slot is reserved for
  styles/scripts/fetch instead of being occupied by image requests. Relative stylesheet/image
  URLs resolve against the document, stylesheet base or redirect target. Page
  navigation cancels pending transfers and clears the page caches. This NRO
  uses its own SD profile `/switch/switch-visual-browser`.
- Bundled offline document with an external stylesheet, PNG/JPEG/WebP assets, flex
  cards, editable search, a JavaScript counter and async JSON-loading button, and a
  below-the-fold anchor. A second form demo exercises checkboxes, radio groups,
  scrollable single/multiple selects, validation and reset without sending data.
  A visible input/textarea section also exercises borderless responsive fields.
  It works without Wi-Fi (the optional Google search requires a connection).

## Build

Host dependencies: CMake 3.19+, C/C++17 compiler, Python, Git, libcurl, SDL2,
SDL2_ttf and SDL2_image and libwebp development packages.

```sh
python3 browser/visual/tools/fetch_litehtml.py browser/visual/build/litehtml
cmake -S browser/visual -B browser/visual/build/host \
  -DLITEHTML_ROOT_DIR="$PWD/browser/visual/build/litehtml"
cmake --build browser/visual/build/host -j4
ctest --test-dir browser/visual/build/host --output-on-failure
browser/visual/build/host/visual-host browser/visual/romfs browser/romfs/DejaVuSans.ttf
# Optional real HTTPS page; no account data or game binaries are needed.
browser/visual/build/host/visual-host browser/visual/romfs browser/romfs/DejaVuSans.ttf https://keldurn.com/ru/
```

Switch: install devkitA64/libnx, switch-curl, switch-sdl2_ttf,
switch-sdl2_image, switch-libwebp and CMake, then `make -C browser/visual`. Copy
`switch-visual-browser.nro` to `/switch/` and launch through a game with R.
It opens the visual demo automatically. Tap the page or use X to enter an HTTPS
address; B goes back, Y reloads, Minus changes layout mode and + exits. Tap the demo link to check a link
below the first screen. This build never invokes Nintendo's WebApplet/update. Startup/layout failures
are appended to `/switch/switch-visual-browser/visual-report.txt`; native graphics
initialization failures keep a console message on screen until + is pressed.

## Verification and boundaries

Host tests exercise CSS-driven column positions, document overflow, actual
PNG/JPEG paint calls, Russian glyph rendering, painted-link fragment navigation,
and gesture state/coordinate behavior. A loopback TLS fixture verifies asynchronous
resource loading, shared cookies, hostname/expiry/unknown-root failures, HTTPS
downgrade refusal, quotas, transfer cancellation and reuse. Gradient pixel tests verify directions, transparency, radial aspect and conic angles.
Input tests cover stick response and mobile hit-test/pinch coordinates. CI saves the host frame as a BMP and
cross-builds a separate NRO. A build/host pass does not prove device input,
font rendering, frame rate or compatibility with arbitrary websites.

Limits: 1 MiB HTML, 8,192 '<' characters before parsing, 512 KiB per stylesheet,
8 MiB per encoded image, 64 requests/page and 16 MiB total response bytes;
4-megapixel/4096-axis decoded images, 32 MiB decoded image textures and 8 MiB
text textures. DejaVu is the only font family and font size is clamped 6–144 px.
These quotas do not make this an isolated or standards-complete web runtime.

Not implemented: JavaScript workers/import maps, complete DOM/web forms, multipart/file
uploads, text selection, full accessibility/focus navigation, WebGPU/WebGL, SVG/video,
CSS grid/transforms, advanced gradient color spaces, rounded image/overflow clipping, rounded border
strokes or complete border styles. Text case conversion is ASCII-only. Buttons can dispatch JavaScript click/submit handlers; compatibility depends on the APIs their scripts use. Pinch zoom magnifies the selected layout width; Minus explicitly changes that width.
Text/search fields use native keyboard editing and emit input/change when a committed edit changes the value; per-keystroke keyboard events are not supplied. No long-press action
menu is supplied. CSS/script compatibility and performance remain separate gates.

The game requires WebGPU and a substantial JS/WASM bridge. Neither is provided
by litehtml or this milestone. Cloud video gaming also needs its own media and
network stack. The previous browser remains version 0.3.2 reading mode; the
engine memory probe remains a separate NRO.

## Host capture

`visual-host ASSET_ROOT FONT --capture HTTPS_URL FRAME.bmp` saves a frame after
bounded page/resource loading settles (30-second limit). Remote sites and their
assets are not committed. Live captures are observations, not reproducible CI
fixtures or evidence that the same network works on the console.

## Device feedback — 2026-11-06

The user confirms 0.1.0 loads Google and the Keldurn website; both the root and
`/ru/` load when entered directly. Touch, drag/pinch gestures and the left-stick
pointer work on the console. Website composition is imperfect; web inputs were
missing. VK reports an outdated browser and YouTube did not open. The precise
cause of those two failures is not established by that feedback. Version 0.1.0 had no page JavaScript, media playback, MSE or WebRTC, so no compatibility claim
is made for VK, YouTube or cloud gaming. Changing the user agent does not supply
those capabilities. The user subsequently tested 0.4.0: fields can be tapped/edited but some Google
fields are still visually missing; VK still reports an outdated browser and
YouTube still fails to open. A live host capture received a different Google
variant with visible fields, so the exact device-page failure is not reproduced.
The 0.4.1 field rendering changes require a new device check. A regional network
restriction is a hypothesis, not an established explanation for YouTube's failure.
VPN support is deferred at the user's request.

## JavaScript bridge — 0.3.0

Classic scripts and ES modules use one QuickJS realm per navigation. Preparation starts after
initial stylesheets settle (images do not block startup). Initial external async
scripts run when ready; ordinary classic scripts retain source order, and external
defer scripts run in source order after the interactive transition. Inline async/defer
attributes do not change inline ordering. This is scheduling on an already-parsed
native DOM, not a streaming HTML parser or full parser-blocking/module implementation. Up to 16 sources,
128 KiB/source, 32 MiB JS heap, 256 KiB JS stack, 64 timers and 256 listeners are
allowed. Initial scripts and scheduled callbacks have a 50 ms interrupt budget;
UI task/frame batches have an 8 ms soft quantum and stop between callbacks. Host
interrupts use thread CPU time where available; Switch uses monotonic elapsed time.
Checkpoints execute at most 32 jobs before yielding, preserving pending jobs before
starting another task. Expensive native calls cannot be preempted by the JS hook. A navigation destroys the realm before its document,
resources or profile can be replaced.

The facade directly delegates to native nodes: selectors, IDs, reflected attributes,
textContent/innerHTML, append/prepend/insertBefore/remove/create, classList/dataset/inline styles,
input values and bounding rectangles. CSS and render items are recomputed from
the retained native tree. There is no separate JS document snapshot to overwrite
native user input. The tree/handle traversal cap is 8,192; dynamic creation has a
4,096-node lifetime quota, fragments are capped at 16 KiB/128 '<' characters and
cumulative mutation strings at 8 MiB/navigation. The renderer patch also fixes an
upstream negative shift when parsing text-decoration:none, found by UBSan. This is a DOM subset, with snapshot
node lists, partial CSSStyleDeclaration/getComputedStyle and text-layout limits;
it is not a standards-complete DOM or CSSOM.

Implemented events include capture/bubbling, once/removal, stopPropagation,
preventDefault, DOMContentLoaded/load, native click, input/change on keyboard
commit, and cancelable form submit/reset. Checkboxes/radios activate before click
handlers run; canceling click restores their checked state. Form properties include
checked/defaultChecked, selected/defaultSelected, selectedIndex, options and
selectedOptions. Native submit(), requestSubmit(submitter) and reset() are supplied.
checkValidity()/reportValidity() use the basic native submission validator and may
also reject unsupported destinations/encodings; they are not full constraint validation. Promise jobs, function-only timers and rAF
run on the UI loop. Native control activation and JS focus()/blur() share focus
state, document.activeElement, CSS :focus/:focus-within and focus/blur/focusin/focusout
events. Disabled, hidden and detached nodes cannot retain focus. Full focus order,
:focus-visible, editor selection/caret and IME events are not supplied.
URL/URLSearchParams, origin-scoped storage/cookies, and the
existing ru/en UTC Intl subset are available. Pointer/touch/keyboard web events,
programmatic activation defaults, selection and complete focus semantics remain
unimplemented; native gestures continue to work independently.

The initial GET-only fetch facade has been replaced by the buffered network APIs
in 0.9.0: independent GET/HEAD and body requests, Headers/Request/Response,
cancellation and asynchronous XHR with a documented anonymous CORS subset. See
«Buffered network APIs» below for credentials, redirect, streaming and quota limits.

The script loader intersects final HTTP CSP headers and initial meta policies for its
supported script directives, checks redirect destinations, and blocks unsupported
source expressions conservatively. Inline handlers require inline permission;
string code generation is disabled for restricted policies. Connect restrictions
are checked for fetch. This is not complete CSP enforcement across resource types,
nonce/hash/strict-dynamic policies or arbitrary navigations. Native forms separately
intersect initial HTTP/meta and current meta form-action policies and check each
redirect destination. Unsupported source expressions are rejected conservatively.

Tests exercise the actual rendered native tree, stable node identity across
resource/viewport refresh, CSS selector rematching, live style changes, events,
form cancellation, timers, infinite-loop interruption, authenticated external
script loading, cookie visibility, async fetch and real HTTPS failures. The NRO
still does not establish VK/YouTube/game compatibility: import maps/workers, broader web APIs,
Grid, GPU and media remain separate work.

A live host capture of `https://keldurn.com/ru/` on 2026-11-06 completed the
acquisition.js / mega.js / lang.js script phase without a JS error after the DOM
reflection/prepend fixes; all three WebP images decoded. This is a landing-page
observation, not game-client execution, a device test or a guarantee for future
versions of the site. CSS Grid composition remains incomplete.

## Form verification — 0.4.0

Eight host suites cover successful controls, Cyrillic/special characters, CRLF
normalization, fieldset/legend and external form ownership, dirty/default state,
CSS :checked reflection, reset, JS submission/cancellation and form-action policy.
A loopback HTTPS server verifies POST payloads, cookies, 303/307 redirects, server
error pages, redirect blocking, redirect limits, cancellation and no idle replay.
The actual UI test taps radio/checkbox controls, drags a select popup independently
of the page, chooses single/multiple values, validates and resets the offline form.
CI publishes page/popup BMP previews alongside the ARM64 NRO build.

Select size/listbox presentation, full dynamic option normalization, date/color/range
widgets, validity patterns/step, form target browsing contexts, IME composition and
per-keystroke web events remain incomplete. Fetch/XHR arrive separately in 0.9.0;
WebSocket, media and GPU remain separate gates.

## Field rendering verification — 0.4.1

Nine host suites include pixel assertions for borderless/active frames, independent
input dimensions, CSS min sizes and border-box sizing, Cyrillic value fitting,
textarea wrapping, CSS focus matching and native/JS focus transitions. The fixture
frame is published as visual-preview-fields.bmp. Validation messages now survive
resource/focus relayout instead of disappearing on the following frame.

Native frames intentionally add a visible affordance to borderless text fields;
they are not complete CSS outline/box-shadow rendering. Textarea editing still
uses the system keyboard, without a full inline editor or internal text scrollbar.
Host Google captures do not prove that a different server response on Switch is
fixed. No VK, YouTube or media compatibility improvement is claimed by this release.

## Core audit and diagnostics — 0.5.0

Read [CORE_AUDIT.md](CORE_AUDIT.md) for the Russian architecture review, primary
sources, coverage matrix, engine migration gates and remaining work.

Press **L** (host: F1), or tap the status line, to open a frozen core report.
Tap either half to change pages; B or the heading closes it. On Switch opening
also overwrites `/switch/switch-visual-browser/core-report.txt`. The report separates
HTTP/TLS/transport/limit/policy failures, failed image decodes, skipped paint operations
in the last frame and up to 24 JS issue groups (saturating repeat counts).
JS groups contain only category/class and origin, never message/stack, URL path/query,
HTML, cookie, input values or POST body. Repeated failures can share a group and
these counters alone do not identify a site's precise compatibility failure.

Ten host suites now include 120 geometry rectangles from 20 own flex-gap cases
at 1280/640 CSS px, independently generated with Chromium 151.0.7922.34 and compared
within 0.1 CSS px. Expected coordinates, browser version and fixture SHA are checked
in; optional regeneration: `python3 browser/visual/tools/reference_flex.py` with
Playwright/Chromium installed. Tests also cover native flex geometry registration,
auto margins, report interaction, error categorization, bounded JS issue grouping
and non-accumulating paint counters. This is a narrow reference suite, not WPT conformance.

Device testing of 0.5.0 is pending. This release does not establish VK/YouTube/game
compatibility or select a replacement full browser engine.

## Scheduling and invalidation — 0.6.0

- Timers and rAF use separate queues. Each timer/fetch task and rAF callback is
  followed by a QuickJS microtask checkpoint. Native queueMicrotask uses the job
  queue directly. A bounded unfinished checkpoint resumes before the next task.
  Timer callbacks receive window as this and retain extra arguments.
- A render opportunity snapshots rAF callbacks; cancellation works between
  callbacks, newly requested callbacks wait for a later frame, and callbacks in
  the same batch receive the same timestamp relative to the realm's time origin.
  Opportunities are throttled to at most one every 16 ms; this is not proof of
  60 FPS or a dedicated compositor.
- document.currentScript identifies executing initial classic scripts and is null
  in their microtasks. readyState becomes interactive before defer; DOMContentLoaded
  follows ordered/defer scripts, while complete/load additionally wait for async
  scripts and registered image/stylesheet requests. Script load/error does not bubble
  into window load. Timers/fetch continue while external scripts are pending.
- The host rejection tracker defers unhandledrejection until a checkpoint finishes,
  suppresses it for early catch handlers, supports preventDefault and emits
  rejectionhandled for tracked late handlers. At most 32 rejected promises/reasons
  are retained per realm; overflow is reported. Values are freed at navigation.
  This supplies Event.promise/reason, not full PromiseRejectionEvent interfaces.
- Typed resource completion counters distinguish styles/images from scripts/JSON.
  Images rerun layout without discarding CSS/fonts; nonvisual completions do not
  rebuild the document. DOM/style changes coalesce at the end of a turn. Explicit
  geometry/style reads still force a synchronous refresh when dirty. The core
  report now shows cumulative CSS and layout pass counts for the current document.

Twelve host suites cover the previous renderer, task/microtask/frame ordering,
cancellation, lifecycle and delayed loopback HTTPS script/image/CSS responses.
The actual Browser test verifies preserved native DOM/input, no layout for JSON,
image-only relayout, late stylesheet application, coalesced mutations and navigation
cancellation of the old realm. Function-only timers, coarse timer clamps, bounded
checkpoints/callbacks, post-parse script discovery, missing dynamic script execution,
workers/modules/observers and incomplete lifecycle/load-blocking remain limitations.
The same native document still needs a full style refresh for generic DOM mutations;
this does not add fine-grained style dependency tracking.

## Dynamic classic scripts and native MutationObserver — 0.7.0

See [TESTING.md](TESTING.md) for a Russian device checklist. The offline demo's
"Проверка динамики" button loads dynamic-demo.js on demand: two clicks should show
"Запуск: 2", "Изменений: 4" and "Готово".

Created inline scripts execute synchronously on connection/text insertion with
currentScript restored across nested evaluation; their microtasks wait for the
outer script's checkpoint. External dynamic scripts default to async; setting
async=false opts into a separate ordered queue. defer on a dynamic script does
not create a parser-defer job. Script state is per element: moving an executed
script or changing its src does not restart it; new elements can execute the same
cached URL. Removing an already-started external script does not cancel it;
navigation destroys its realm/transfers/events. Script text set after connection,
late src, connected subtrees and DocumentFragment insertion are handled. Scripts
parsed through innerHTML remain inert even after reinsertion.

MutationObserver records are generated by committed native attributes, CharacterData
and child-list operations. Options include subtree, attributeFilter, old values,
validation/defaults, re-observation, takeRecords and disconnect. Delivery uses the
QuickJS microtask queue in observer creation order. Detached subtree observations
survive until notification; callback mutations schedule another notification.
Bulk append/prepend/replaceChildren and fragments preserve grouped child-list
records; added/removed lists are frozen snapshots with item(). Generic DOM wrappers
are still not a complete Web IDL/NodeList implementation; comments, namespaces,
Shadow DOM, ranges/custom elements and every cross-document/fragment edge case are
not implemented. Native value/checked property changes are not attribute mutations.

Limits: 64 prepared scripts per navigation (including up to 16 initial jobs),
128 KiB/script (also checked on reused cache entries), nested inline depth 8;
64 active observers, 32 targets/observer, 64 filter names, 128 transient roots per
observer, 256 queued records/observer, 1,024 total queued records and a conservative
1 MiB record budget. Overflow is reported in the core issue log; it does not provide
an unlimited standards runtime. The trusted bundled bootstrap has a 500 ms startup
budget; page callbacks retain their existing 50 ms limit.

Existing HTTPS and CSP checks apply at preparation/execution and supported meta
CSP restrictions are retained after removal. Unsupported modules, nonempty SRI
integrity metadata and cross-origin crossorigin mode fail rather than silently
claim support. This does not add modules, complete MIME/charset handling, full CORS,
SRI verification or an isolated browser security model.

The same own JS fixture runs in native QuickJS/DOM and Chromium 151.0.7922.34;
its expected scenario trace and JS hash provenance are in tests/fixtures.
Optional regeneration: `python3 browser/visual/tools/reference_dynamic_dom.py`.
Additional TLS tests exercise slow/fast ordered loaders, default async, cached
URLs, late src, error events, observer-driven updates, policy restrictions,
resource-size limits and navigation cancellation. Host sanitizer checks also
exercise deferred styling of detached innerHTML to avoid reading uninitialized
inherited font properties. Fourteen host suites cover this release.

## DOM interface compatibility — 0.7.1

The user's 0.7.0 device feedback confirms faster browsing and shows
`ReferenceError: HTMLScriptElement is not defined` on play.keldurn.com. Registration
security verification fails and Google's sign-in rejection page reports unsupported
JavaScript/browser support. Those screenshots do not establish the exact cause of
either service's rejection or prove that one missing interface explains them all.

Native wrappers now have distinct EventTarget/Node/Element/HTMLElement and common
HTML element prototypes, including HTMLScriptElement. Script src/text/textContent/
async/defer descriptors are exposed on its prototype with receiver checks;
Element attribute/innerHTML descriptors and Document prototype factories also work.
Text/DocumentFragment construction preserves native wrapper identity. Script.supports
reports classic=true and module/importmap=false, matching implemented capabilities.
These interfaces remain a subset, with some generic inherited facade properties;
they are not a full Web IDL or browser isolation implementation.

The JS-enabled renderer hides noscript fallback content and does not prepare
scripts below noscript. Gumbo's complete scripting-aware tokenizer is still absent.
This fixes fallback visibility/inertness without claiming that security verification
or account sign-in has succeeded.

A shared own interface/prototype fixture passes in native QuickJS/DOM and Chromium
151.0.7922.34. Regenerate its result with tools/reference_dom_interfaces.py. Native
regressions also check unsupported capability reporting and noscript behavior.
All fourteen existing host suites remain part of the release gate. Actual Switch
verification of 0.7.1, Google sign-in and the site's security check remains pending.

## Layout observers and responsive events — 0.8.0

ResizeObserver and IntersectionObserver use the retained renderer, after animation
callbacks and their microtasks. Geometry reads now return DOMRect border boxes
rather than content-only plain objects, including padding/borders and fixed-position
scroll coordinates. Repeated restyling prunes expired renderer references; this
fixes zero geometry after a style change and prevents those weak slots accumulating.

- ResizeObserver supports content-box and border-box, contentRect, contentBoxSize
  and borderBoxSize, with branded readonly prototype accessors for loader feature
  detection. Initial nonzero sizes, display:none, detach/reconnect,
  unobserve and disconnect are covered. Callbacks run after rAF; size changes loop
  through increasing tree depths, with at most 8 rounds/64 callbacks and a soft
  8 ms quantum. Undelivered changes resume on a later frame and issue a window
  error with the standard loop message, plus a bounded diagnostic category.
- IntersectionObserver supports viewport/document or element roots, px/% rootMargin,
  sorted thresholds (including duplicates), edge contact, geometric intersection
  ratio, overflow ancestors, asynchronous task delivery and takeRecords. Records
  are sampled after resize delivery and delivered in a subsequent task; timer tasks
  and intersection tasks alternate when both sources have work.
- MediaQueryList/MediaQueryListEvent provide typed change events, onchange and legacy
  addListener/removeListener. Only lists with listeners are retained; removing the
  last listener releases their registry slot. Queries use litehtml's existing media
  parser and its supported features, not a separate full CSS media implementation.
- Viewport resize and document scroll events are coalesced at rendering opportunities.
  scrollTo/scrollBy (numeric or options with auto/instant behavior) clamp to the native
  document and update the actual touch/controller view. pageXOffset/pageYOffset alias
  scrollX/scrollY. Smooth scrolling is rejected rather than simulated.

There are at most 32 active layout observers and 128 observed targets shared across
both observer types. Intersection queues hold at most 128 records per observer,
512 total. Disconnect releases target capacity; realm destruction drops all callbacks.
At most 32 active media lists with listeners are retained. Limits appear in the core
report without element text, field values or URLs beyond the existing origin policy.

This is an HTML, horizontal writing, single-box layout subset. SVG, transformed and
fragmented box geometry, subframes, shadow trees, device-pixel-content-box, intersection
visibility/occlusion tracking, delay and scrollMargin are not supplied. ResizeObserver
on a non-replaced inline element reports zero sizes. This block does not add Grid,
modules/workers, XHR/WebSocket, media/GPU or prove account/security-check compatibility.

The shared own layout-observers fixture runs in Chromium 151.0.7922.34 and native DOM;
reference provenance is recorded beside it. It covers content/border size selection,
visibility/detachment, thresholds, ancestor clips, pixel root margins, fixed coordinates,
scrolling, rectangle types and media/resize changes. A separate normative percentage
margin check follows the [IntersectionObserver specification](https://w3c.github.io/IntersectionObserver/):
all percentages use root width. Chromium 151 instead resolved vertical percentages
against root height in this experiment; that known difference is excluded from the
shared equality assertion. See also the [ResizeObserver delivery algorithm](https://drafts.csswg.org/resize-observer/).
Native tests cover loop protection, takeRecords, quota/release, repeated CSS refresh,
and the real Browser view plus cancellation of both observers on navigation.
The complete release gate now contains fifteen host suites; actual Switch behavior
of 0.8.0 remains to be verified. The offline demo includes «Размеры и видимость».

## Buffered network APIs — 0.9.0

Fetch now creates independent curl-multi requests, with GET/HEAD/POST/PUT/PATCH/
DELETE/OPTIONS, request headers and string/URLSearchParams/ArrayBuffer/typed-array
bodies. It no longer fills or reuses the image/script URL cache. HTTP error statuses
resolve normally; transport/TLS/CORS/policy failures reject. Headers, Request and
Response include normalized headers, status metadata, clone and one-shot
text/json/arrayBuffer consumption. Network response headers are immutable and
Set-Cookie/Set-Cookie2 are never exposed to script. Forbidden request headers,
including Cookie/Host/Origin/Content-Length and sec-/proxy- names, are ignored by
the request guard. UTF-8 TextEncoder/TextDecoder handle Cyrillic, supplementary
characters, isolated surrogates, malformed byte sequences and BOMs.

AbortController/AbortSignal support synchronous abort events, custom/default reasons,
throwIfAborted, static abort and timeout. Abort cancels the native request, removes
its live queue slot and rejects with the signal reason; buffered body reads also
check the signal. Signal timeout uses the bounded timer queue. Network requests
have a native total deadline including queue/preflight/redirect waiting: default
15 seconds, configurable through XHR.timeout up to 60 seconds; timeout=0 uses the
15-second internal ceiling. Intentional cancellation is excluded from failure counters.

Asynchronous XMLHttpRequest supports open/send/abort, request/response headers,
status/URL, readyState events, text/json/arraybuffer responses, withCredentials and
mutable timeout. Generation checks isolate abort/reopen/navigation from stale
completions. Buffered completion emits HEADERS_RECEIVED, LOADING, one terminal
progress event, DONE and load/loadend (or error/timeout/abort). Upload exposes an
EventTarget for listener registration but upload progress is not implemented.
No synchronous XHR, MIME/charset override, document/blob response or incremental
header/body/progress streaming is supplied. Text decoding is UTF-8 only; fatal and
streaming decoder modes reject.

Cross-origin requests support mode=cors with anonymous transport, exact/wildcard
Access-Control-Allow-Origin, a bounded OPTIONS preflight for unsafe methods/headers,
explicit Authorization grants, and safelisted/Access-Control-Expose-Headers response
filtering. Preflights never use cookies. credentials=same-origin/omit are honored;
credentials=include works for same-origin requests. **Cross-origin credentials=include
is rejected** until SameSite/partitioned cookie policy is implemented; the existing
cookie jar alone cannot provide that policy. no-cors/opaque responses and manual
redirect mode are also unsupported. Preflight redirects are rejected and grants
are not cached. This is a tested CORS subset, not complete Fetch/security isolation.

Actual request redirects are manual, at most five. Each destination must pass the
owning realm's current connect-src/default-src policy before a connection starts.
301/302 POST and 303 non-HEAD rewrite to GET and remove body headers; 307/308 preserve
the body. Origin-changing redirects strip Authorization and preserve CORS origin
checks/tainting. Credentials in URLs and HTTPS downgrade are rejected. Anonymous
handles use the same verified CA/TLS configuration without enabling cookie sharing
or accepting response cookies. Bodies are wiped after native completion/cancellation;
logs retain origins/statuses, never payloads, auth headers or query strings.

Quotas: 32 live requests, the shared 64 resource/logical-request entries per page (preflights and redirects reuse entries), 64 KiB
request body, 32/8 KiB request headers, 64/16 KiB response headers (8 KiB per value),
512 KiB buffered response, 16 MiB resident native response/cache bytes. Delivered
network bodies leave native storage after copying to JS; JS keeps its 32 MiB heap
limit. Header/redirect/preflight work and callbacks remain bounded. These APIs do
not provide unbounded polling, complete HTTP caching, streams, FormData/file uploads,
workers/modules, WebSocket, WebGPU or media playback.

Sixteen host suites cover this release. A shared own network-types fixture has
Chromium reference provenance. The local TLS fixture adds a second valid origin;
checks verify POST/JSON/binary data, cookie omission and acceptance, failed preflights
without sending POST, header exposure/immutability, explicit Authorization grants,
fresh repeated GETs, redirects/CSP, TLS expiry, body quotas, signal/native timeouts,
XHR abort/reopen and cancellation on Browser navigation. The offline demo includes
«Загрузка и отмена». Actual Switch compatibility with account login, Cloudflare or
WoW remains unverified.

References: [Fetch](https://fetch.spec.whatwg.org/),
[XMLHttpRequest](https://xhr.spec.whatwg.org/),
[AbortSignal](https://dom.spec.whatwg.org/#interface-abortsignal),
[UTF-8 decoding](https://encoding.spec.whatwg.org/#utf-8-decoder).

## Browser ES modules — 0.10.0

HTML script type=module, URL-based static imports/exports/re-exports and dynamic
import() use QuickJS's real module parser, linker and evaluator. Import clauses
are inspected from the parsed module record, never extracted with regular
expressions or rewritten into classic scripts. Live bindings, cycles, strict
module scope, namespaces, top-level await and import.meta.url are supported in the
shared fixtures. A module map belongs to one navigation: repeated imports share
an instance/namespace, while distinct query URLs retain separate identities.

The host compiles without resolving dependencies, queues HTTPS loads, discovers
imports and links only after the complete reachable graph is ready. QuickJS's
synchronous loader only returns compiled records; it never waits for HTTP. Module
sources use the buffered CORS transport with script-src/default-src (not connect-src),
verified TLS, strict JavaScript MIME checks and policy checks before redirect hops.
Relative imports/import.meta.url use the final response URL. Default credentials
are same-origin; cross-origin use-credentials transfers remain blocked by the
network layer. Root integrity attributes are rejected rather than ignored.

Parser-created modules join the defer sequence unless async is set; dynamic module
scripts are asynchronous and async=false joins the ordered dynamic queue. Inline
modules also execute asynchronously. document.currentScript is null during module
execution. Classic nomodule scripts are skipped, and HTMLScriptElement.supports
now reports module=true, importmap=false. innerHTML-inserted module scripts stay
inert. Initial preparation still occurs on the already parsed native DOM: this
is not streaming HTML parser integration.

Top-level await does not hold DOMContentLoaded/window load until its promise
settles. External module load fires after evaluation starts; deferred rejection
is reported through the existing window error-event subset. HTTP/CORS/MIME failures
produce script error events. Parse/link/runtime failures report window errors and
external script load, matching the Chromium fixtures; inline modules do not gain
external load events. Handled dynamic-import failures do not create spurious
unhandledrejection events. Parse/evaluation failures retain error identity; linking
failures produce fresh SyntaxError objects on repeated imports. The UI can show a
bounded error message; exported diagnostics retain only phase/class/origin.

Limits: 64 module records, 512 parsed dependency edges, depth 64, 128 KiB per source,
4 MiB cumulative source bytes, 32 pending dynamic imports and the existing 32 MiB
JS heap. Module requests share the page's 64-resource ceiling, response/native
buffer limits and 15-second network deadlines. Compilation/evaluation uses the
existing 50 ms interrupt budget; one source/graph-completion action runs per poll.
Native delivered body storage is now freed, including backing allocations, and
compiled source buffers are released after parsing. Navigation cancels transfers,
drops pending imports/timers and frees the entire realm/module map.

Unsupported: import maps/bare package specifiers, nonempty import attributes,
JSON/CSS/WASM modules, data/blob/file module URLs, import.meta.resolve, modulepreload
processing, streaming compilation, Workers/Service Workers and complete ErrorEvent
or CSP/Web IDL behavior. JavaScript module support does not implement WebAssembly,
WebSocket, GPU, audio/video or successful service sign-in/game execution.

Seventeen host suites now include real multi-origin TLS module tests: static and
dynamic graphs, live exports/cycles, redirected bases, MIME/CORS/policies, ordered
scripts, failure caching/events, a 40-import quota burst, and actual Browser
navigation cancellation. The shared own module graph and lifecycle/error traces
have Chromium 151.0.7922.34 provenance in modules.reference.json; regenerate with
tools/reference_modules.py. The offline «Модульная страница» demo uses real static
and dynamic imports and a live exported counter. Actual 0.10.0 Switch behavior
remains to be verified.

References: [HTML module scripts](https://html.spec.whatwg.org/multipage/webappapis.html#module-script),
[HTML script processing](https://html.spec.whatwg.org/multipage/scripting.html#processing-model),
[ECMAScript modules](https://tc39.es/ecma262/#sec-modules).


## Persistent WebSocket — 0.11.0

`WebSocket` is connected to a native RFC 6455 transport, with real TLS and an
asynchronous `open` / `message` / `error` / `close` lifecycle. Connections use
libcurl connect-only HTTPS handles kept in a dedicated multi handle, then
`curl_easy_send/recv`. This works with devkitPro's curl 7.69.1 and does not require
curl's newer WebSocket API. HTTP/1.1 is forced during TLS negotiation. Certificate
and hostname checks use the same root importer as ordinary HTTPS.

- Relative and HTTPS URLs normalize to `wss:`. Plaintext `ws:`/HTTP, URL credentials,
  fragments and redirects are rejected. `connect-src`/`default-src` are checked
  before opening the connection. An `Origin` header identifies the initiating
  page; opaque demo pages send `null`.
- A fresh OS random nonce and fresh client frame masks are used. The upgrade
  verifies status 101, Upgrade/Connection, the RFC SHA-1 accept value, and the
  selected offered subprotocol. Extensions/compression are not requested or accepted.
- Incremental framing supports text and binary messages, fragmented messages with
  interleaved controls, automatic pong, strict UTF-8/close-code checks, canonical
  lengths and partial TLS reads/writes. Invalid/masked server frames fail the socket.
- JavaScript exposes the four states, `url`, `protocol`, `extensions`, `binaryType`,
  `bufferedAmount`, `send` and `close`. Binary messages default to immutable `Blob`;
  `arraybuffer` is also supported. Typed-array views are copied at send time.
  MessageEvent and CloseEvent provide readonly payload/origin/close fields.
- The minimal buffered `Blob` supports `size`, `type`, `slice`, `text` and
  `arrayBuffer`, up to 64 parts / 512 KiB. Fetch Request/Response and XHR request
  bodies accept it; Request/Response provide `.blob()`. Blob streams, object URLs,
  File, multipart FormData and XHR's `responseType='blob'` remain unsupported.
- Same-origin upgrades use the native cookie jar, including HttpOnly cookies;
  validated upgrade responses can set host-bound cookies. These native cookie
  helpers are not exposed to JavaScript. Cross-origin sockets are anonymous and
  ignore Set-Cookie until a complete SameSite/partitioning policy is implemented.
  This differs from general browser WebSocket credential behavior.
- Page limits: 8 live / 32 created sockets; 16 protocols of at most 128 bytes;
  16 KiB upgrade headers; 512 KiB per message. Outgoing application data is bounded
  to 1 MiB/socket, 4 MiB/page and 64 queued frames/socket. Incoming message events
  are bounded to 1 MiB/socket, 2 MiB/page and approximately 32 events/socket, with
  reads paused under pressure. Decoder storage is also bounded; quotas are not
  a replacement for measuring total memory on the Switch.
- Each network pump processes at most 8 frames, 64 KiB reads and 16 KiB writes per
  connection, with a soft 8 ms total processing budget. Fetch, socket events,
  module progress and timer/observer tasks rotate, each followed by a microtask
  checkpoint. Connect/upgrade has a 10-second deadline (5-second TLS connect);
  close handshakes have 5 seconds. There is no automatic reconnect or heartbeat.
- Navigation cancels transports before destroying the JS realm. Reports contain
  bounded failure categories and the socket origin, without messages, close
  reasons, cookies or URL paths/queries. The network log likewise records only
  the origin, stage and status. No payload capture is added.

The demo's **«Сетевая проверка WebSocket»** section opens a connection only when
pressed. Its editable default is a public echo endpoint; availability is outside
our control. Send transmits the fixed Russian test string, and the page displays
a message count. This online check is independent of the offline module demo.

Nineteen host suites include an RFC known-vector/parser suite and a real local
TLS upgrade/echo suite: text, binary/Blob, maximum-size messages, fragmentation,
ping/pong, protocol/Origin/cookies, close, invalid frames/upgrade, TLS failure,
connection cancellation, live quotas and CSP. The actual Browser navigation test
checks that old socket callbacks do not survive navigation. Host and sanitizer
results do not establish Switch performance, successful Google/Cloudflare login,
or playable Keldurn: Worker, JS-to-WASM, GPU and media integration still remain.

Optional Chromium reference (install Playwright/Chromium separately):

```sh
python3 browser/tests/https_fixture.py browser/visual/tests/chromium_websocket.py
```

The shared fixture checks text/binary/Blob, controls/close, API validation and
protocol failures. It explicitly accounts for anonymous cross-origin cookies,
native quotas and certificate rejection. Chromium's CSP failure is checked via
its error event because that path does not deliver close in the reference build.
Chromium 151.0.7922.34 passed this scenario; it is not a general compatibility test.


## Navigation and parser corrections — 0.11.1

- Gumbo receives a scripting-enabled flag for documents and fragments. `noscript`
  in head/body is raw text, so its fallback style/link/image/meta/script markup
  does not become active. This fixes a reproduced case where Google's noscript
  stylesheet hid div/span/p content while JavaScript was enabled. The bundled
  fixture matches Chromium's visible-content behavior. It does not implement a
  user-selectable JavaScript-disabled parsing mode.
- HTML parsing honors the HTTP Content-Type charset through litehtml's encoding
  input. A BOM still takes precedence; an unknown/absent transport charset falls
  back to the existing meta/default sniffing. This includes Windows-1251 pages.
  The change does not add full charset handling to external CSS or Fetch decoding.
- Classic scripts support CSP nonce sources, including explicitly nonced scripts
  in a strict-dynamic policy. All enforced policies must match. Nonces do not
  permit inline event attributes or eval; eval needs its own unsafe-eval keyword.
  Nonce/hash sources disable unsafe-inline fallback. Nonces are snapshotted when
  preparing each script and rechecked before execution; the native nonce value
  is hidden from attribute selectors/serialization, with an HTMLElement.nonce
  property for loaders. Attribute hiding also applies to detached native elements.
  Hash/SRI execution, nonce-authorized modules and strict-dynamic trust propagation
  remain unsupported; scripts depending on those paths are rejected.
- Primary GET/form navigation has a 45-second overall budget (including form
  redirect hops), a 5-second connect deadline and 20-second low-speed window.
  Other resources keep their 15-second budget. Incomplete failed documents are
  discarded, with a localized error and retry/back/address/report controls shown
  in the content area. A 200 response alone does not mark a complete document.
- Reports add the final origin/redirect count, HTTP/curl result, received body bytes,
  elapsed transfer time and completion status. Redirected response origins are
  logged without paths or queries. Cookies, search terms, form values, CSP nonce
  values and message bodies are not added to diagnostics.

Device reports from 0.11.0 establish a jutsu.ru document timeout (HTTP 200,
curl 28), before script startup. The public www.jutsu.ru endpoint redirected to
jutsucdn.ru during diagnosis. The cause/device recovery is not established by a
cloud request or the longer deadline. Google search's cloud reproduction now
renders Google's Russian suspicious-traffic/check page instead of a blank page;
normal search results and CAPTCHA compatibility are not established. The Keldurn
message is its actual navigator.gpu capability failure: WebGPU is absent, and a
Horizon update is not a fix. No graphics-capability flag is fabricated.

Twenty host suites cover the changes, including real TLS partial-document timeout
and successful completion, nonce positive/negative/multiple-policy/eval checks,
parser fallback inertness, delayed content and transport/meta/BOM precedence.
Uploaded account/cookie/storage data is not copied into the repository or fixtures.
