# Switch visual browser — 0.1.0

Experimental HTML/CSS rendering and touchscreen candidate, separate from the
reader and WASM probe. It uses pinned litehtml with its bundled Gumbo parser and
an SDL2/SDL_ttf/SDL_image drawing adapter. litehtml upstream explicitly does not
recommend it as a full browser engine. This prototype tests a native rendering
path; it is not a production engine selection or a playable Keldurn client.

## What is implemented

- Retained HTML document, external/inline CSS, engine cascade and layout,
  flex layout, font metrics and engine link hit testing.
- Cyrillic DejaVu text, solid backgrounds with rounded fills, rectangular solid
  borders, rectangular nested clipping and PNG/JPEG resources.
- Taps on painted links; drag scrolling that suppresses taps; decaying inertia;
  anchored two-contact pinch zoom (100–250%); contact/focus cancellation.
- Separate native chrome hit regions, an HTTPS address keyboard on Switch,
  back/reload, analog-stick pointer + A, D-pad scroll and ZL/ZR zoom.
- Four concurrent libcurl-multi requests pumped by the UI loop, HTTPS certificate
  validation and the reader's tested root importer. Relative stylesheet/image
  URLs resolve against the document, stylesheet base or redirect target. Page
  navigation cancels pending transfers and clears the page caches. This NRO
  uses its own SD profile `/switch/switch-visual-browser`.
- Bundled offline document with an external stylesheet, PNG/JPEG assets, flex
  cards and a below-the-fold anchor. It works without Wi-Fi.

## Build

Host dependencies: CMake 3.19+, C/C++17 compiler, Python, Git, libcurl, SDL2,
SDL2_ttf and SDL2_image development packages.

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
switch-sdl2_image and CMake, then `make -C browser/visual`. Copy
`switch-visual-browser.nro` to `/switch/` and launch through a game with R.
It opens the visual demo automatically. Tap the page or use X to enter an HTTPS
address; B goes back, Y reloads and + exits. Tap the demo link to check a link
below the first screen. This build never invokes Nintendo's WebApplet/update.

## Verification and boundaries

Host tests exercise CSS-driven column positions, document overflow, actual
PNG/JPEG paint calls, Russian glyph rendering, painted-link fragment navigation,
and gesture state/coordinate behavior. CI saves the host frame as a BMP and
cross-builds a separate NRO. A build/host pass does not prove device input,
font rendering, frame rate or compatibility with arbitrary websites.

Limits: 1 MiB HTML, 8,192 '<' characters before parsing, 512 KiB per stylesheet,
8 MiB per encoded image, 64 requests/page and 16 MiB total response bytes;
4-megapixel/4096-axis decoded images, 32 MiB decoded image textures and 8 MiB
text textures. DejaVu is the only font family and font size is clamped 6–144 px.
These quotas do not make this an isolated or standards-complete web runtime.

Not implemented: JavaScript/QuickJS DOM synchronization, forms/editable document
fields, selection, full accessibility/focus navigation, WebGPU/WebGL, SVG/video,
CSS grid/transforms, gradients, rounded image/overflow clipping, rounded border
strokes or complete border styles. Text case conversion is ASCII-only. Ordinary
page buttons/forms do not gain functionality from being painted. Pinch zoom
magnifies a fixed 1280-CSS-pixel layout instead of changing the layout viewport.
The native address keyboard does not implement web forms. No long-press action
menu is supplied. CSS/script compatibility and performance remain separate gates.

The game requires WebGPU and a substantial JS/WASM bridge. Neither is provided
by litehtml or this milestone. Cloud video gaming also needs its own media and
network stack. The previous browser remains version 0.3.2 reading mode; the
engine memory probe remains a separate NRO.
