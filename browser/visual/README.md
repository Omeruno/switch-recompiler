# Switch visual browser — 0.2.0

Experimental HTML/CSS rendering and touchscreen candidate, separate from the
reader and WASM probe. It uses pinned litehtml with its bundled Gumbo parser and
an SDL2/SDL_ttf/SDL_image drawing adapter. litehtml upstream explicitly does not
recommend it as a full browser engine. This prototype tests a native rendering
path; it is not a production engine selection or a playable Keldurn client.

## What is implemented

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
- Four concurrent libcurl-multi requests pumped by the UI loop, HTTPS certificate
  validation and the reader's tested root importer. Relative stylesheet/image
  URLs resolve against the document, stylesheet base or redirect target. Page
  navigation cancels pending transfers and clears the page caches. This NRO
  uses its own SD profile `/switch/switch-visual-browser`.
- Bundled offline document with an external stylesheet, PNG/JPEG/WebP assets, flex
  cards and a below-the-fold anchor. It works without Wi-Fi.

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

Not implemented: JavaScript/QuickJS DOM synchronization, POST and complete web forms, selection, full accessibility/focus navigation, WebGPU/WebGL, SVG/video,
CSS grid/transforms, advanced gradient color spaces, rounded image/overflow clipping, rounded border
strokes or complete border styles. Text case conversion is ASCII-only. Buttons requiring JavaScript do not work merely because they are painted. Pinch zoom magnifies the selected layout width; Minus explicitly changes that width.
Text/search fields use native keyboard editing, without DOM input/change events. No long-press action
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
cause of those two failures is not established by that feedback. This runtime
has no page JavaScript, media playback, MSE or WebRTC, so no compatibility claim
is made for VK, YouTube or cloud gaming. Changing the user agent does not supply
those capabilities. The 0.2.0 additions require a new device check.
