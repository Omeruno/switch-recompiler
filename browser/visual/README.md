# Switch visual browser — 0.3.0

Experimental HTML/CSS rendering and touchscreen candidate, separate from the
reader and WASM probe. It uses pinned litehtml with its bundled Gumbo parser and
an SDL2/SDL_ttf/SDL_image drawing adapter. litehtml upstream explicitly does not
recommend it as a full browser engine. This prototype tests a native rendering
path; it is not a production engine selection or a playable Keldurn client.

## What is implemented

- Persistent QuickJS session bound to the same native litehtml document that is
  painted/hit-tested. Resource completion and viewport changes preserve element
  identity, handlers and JavaScript state. A checksum-locked renderer patch rebuilds
  CSS/render state without replacing the document or accumulating old font caches.
- Retained HTML document, external/inline CSS, engine cascade and layout,
  flex layout, font metrics and engine link hit testing.
- Cyrillic DejaVu text, solid backgrounds with rounded fills, rectangular solid
  borders, rectangular nested clipping and PNG/JPEG/WebP resources.
- Cached linear, radial and conic gradient tiles (premultiplied sRGB, bounded
  512-pixel tile axes / 8 MiB cache). Advanced color spaces and rounded gradient
  clipping remain unsupported.
- Taps on painted links; drag scrolling that suppresses taps; decaying inertia;
  anchored two-contact pinch zoom (100–250%); contact/focus cancellation.
- Native input/textarea boxes with values, placeholders, readonly/disabled states,
  password masking and the Switch keyboard. Values stay in memory for the current
  navigation and survive image/style-driven relayout and viewport changes. Basic
  GET forms serialize named text/textarea/hidden fields and the chosen submitter;
  POST, password submission and other control types report that they are unsupported.
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
  below-the-fold anchor. It works without Wi-Fi.

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

Not implemented: JavaScript modules/workers, complete DOM/web forms and POST, selection, full accessibility/focus navigation, WebGPU/WebGL, SVG/video,
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
those capabilities. The 0.2.0 and 0.3.0 additions require their own device checks.

## JavaScript bridge — 0.3.0

Classic scripts use one QuickJS realm per navigation. Inline and external sources
are scheduled in document order after initial stylesheets settle (images do not block startup). This is not
full browser parser-blocking/async/defer/module scheduling. Up to 16 sources,
128 KiB/source, 32 MiB JS heap, 256 KiB JS stack, 64 timers and 256 listeners are
allowed. Script evaluation has a 50 ms interrupt budget; timer/job turns use 8 ms
and drain at most 32 jobs. A navigation destroys the realm before its document,
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
commit, and cancelable form submit. Promise jobs, function-only timers and rAF
run on the UI loop. URL/URLSearchParams, origin-scoped storage/cookies, and the
existing ru/en UTC Intl subset are available. Pointer/touch/keyboard web events,
programmatic activation defaults, selection and complete focus semantics remain
unimplemented; native gestures continue to work independently.

`fetch` is a bounded same-origin GET-only API returning text/JSON Promises and
status/ok/url. HTTP error responses resolve with ok=false; TLS/transport, disallowed
origins and redirects reject. There is no streaming, custom-header/body fetch,
CORS implementation, XHR, WebSocket or media API. Fetch uses the existing bounded
page resource cache, not a complete HTTP cache or arbitrary long-lived polling
implementation.

The script loader intersects final HTTP CSP headers and initial meta policies for its
supported script directives, checks redirect destinations, and blocks unsupported
source expressions conservatively. Inline handlers require inline permission;
string code generation is disabled for restricted policies. Connect restrictions
are checked for fetch. This is not complete CSP enforcement across resource types,
nonce/hash/strict-dynamic policies, navigations and forms.

Tests exercise the actual rendered native tree, stable node identity across
resource/viewport refresh, CSS selector rematching, live style changes, events,
form cancellation, timers, infinite-loop interruption, authenticated external
script loading, cookie visibility, async fetch and real HTTPS failures. The NRO
still does not establish VK/YouTube/game compatibility: modules, broader web APIs,
Grid, GPU and media remain separate work.

A live host capture of `https://keldurn.com/ru/` on 2026-11-06 completed the
acquisition.js / mega.js / lang.js script phase without a JS error after the DOM
reflection/prepend fixes; all three WebP images decoded. This is a landing-page
observation, not game-client execution, a device test or a guarantee for future
versions of the site. CSS Grid composition remains incomplete.
