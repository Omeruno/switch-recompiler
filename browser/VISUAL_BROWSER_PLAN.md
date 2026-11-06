# Visual browser and touch requirements

## Product target

A standalone Horizon browser must show website layout and images, support Russian
text, and work with the Switch touchscreen and controllers. The text reader is
an intermediate diagnostic tool, not the final browser interface. Work stays on
feature/switch-browser. No Horizon update or PC streaming is part of this plan.

## Current status — visual 0.4.0, 2026-11-06

- The reader remains a separate 0.3.2 diagnostic browser; visual pages use
  `browser/visual` and its independent `switch-visual-browser.nro`.
- The user verified visual 0.1.0 on Switch: Google and Keldurn root/ru pages load;
  touchscreen gestures and left-stick pointer work. Later additions await device checks.
- Visual 0.4.0 retains the native document through HTML/CSS layout and QuickJS
  mutations. It supports flex/media queries, PNG/JPEG/WebP, gradients, viewport
  width modes, right-stick scrolling, keyboard text inputs, native GET/POST forms,
  checkbox/radio groups, scrollable single/multiple selects, basic validation/reset,
  events, timers and bounded same-origin GET fetch. See the visual README for limits.
- Eight host suites and ASan/UBSan checks pass for 0.4.0. ARM64 CI supplies the NRO;
  host results do not establish device performance or arbitrary-site compatibility.
- Still required for general browsing: broader DOM/events and JavaScript APIs,
  Grid and additional CSS, navigation/focus/selection work, modules/workers,
  broader network APIs and media. VK/YouTube compatibility is not established.
- The separate WASM probe's original feature tests passed on Switch. Its new
  stable-memory backend needs device verification. WebGPU and the game bridge
  are still missing; neither creating a character nor entering the world works
  in this browser yet.

## Delivery order

### 1. Choose a viable rendering engine integration

Evaluate an existing HTML/CSS engine against Horizon build requirements, memory,
font shaping, rendering surfaces, JavaScript/DOM integration, resource loading,
threads and event loop requirements. Prove a rendered local HTML/CSS page in an
NRO before claiming that engine is viable. Engine choice must also account for
the eventual WebGPU-required Keldurn launcher; a successful static page is not
proof of game compatibility. NetSurf's missing graphics APIs make it insufficient
by itself for the game's final runtime.

A handcrafted CSS subset may be useful for experiments, but must be labelled as
such. Styling the reader's own interface does not implement website CSS. The
current QuickJS DOM cannot simply be attached to another layout engine without
coherent node ownership, mutations, events and lifecycle integration.

### 2. Deliver visual pages and direct interaction together

The first visual milestone must demonstrate:

- A document tree retained through layout, painting and hit testing.
- Stylesheet loading, cascade/inheritance, box sizes, margins, padding, borders,
  backgrounds, fonts and normal text flow, with documented feature coverage.
- Local and HTTPS PNG/JPEG images with intrinsic dimensions, bounded decode/cache
  memory, missing-image handling and layout updates after loading.
- Links, buttons and editable fields that match their painted hit regions.
- Russian text, wrapping, selection/focus visibility and on-screen keyboard input.
- Touch tap to activate, drag to scroll, and controller navigation of the same
  elements. A scroll gesture must not accidentally activate a link.
- Scrolling bounded by document size, with clipping and correct coordinate
  transforms after scroll/zoom. UI controls retain distinct hit regions.
- Navigation/reload cleanup and a usable interface during resource requests.

Verify host layout/input behavior and cross-build, then verify touch coordinates,
keyboard and rendering on the actual Switch. Do not require completion of the
WASM/game pipeline before implementing this milestone.

### 3. Expand modern layout and gestures

Verify flexbox, grid, positioning, overflow, media queries and responsive pages
against reference browser fixtures. Add inertial scrolling, pinch zoom around
the fingers' anchor, multi-touch cancellation and long-press link actions.
Translate native contacts into the selected engine's real pointer/touch event
model, including capture/cancel and default actions. Test transitions between
one and two fingers, lost contact, page navigation during a gesture, and focus
changes. Do not claim all gestures or arbitrary-site compatibility from a demo.

### 4. Connect the game runtime and graphics

Integrate WASM/JavaScript object and memory bindings, workers, WebGPU, asset
loading, network transport, audio and game input. Verify touch/controller input
on the game's canvas separately from ordinary page links. Test character creation
and entering the world separately, measuring memory and frame time on the device.
Cloud video gaming additionally needs its service's video/audio/network APIs;
HTML/CSS support alone does not supply those.

## Acceptance rule

A screenshot of a native toolbar or formatted extracted text is not completion
of the visual milestone. Record the actual supported web features, engine,
reference fixtures, build results and device results. Keep unverified capabilities
explicit. The current reader remains labelled as reading mode until website
layout and direct interaction are implemented.

## Implemented candidate — visual 0.1.0–0.4.0

`browser/visual` ports pinned litehtml/Gumbo layout to SDL painting and binds
QuickJS directly to that retained native tree. Host rendering, input, DOM and
HTTPS form/resource fixtures accompany the independent NRO. The reviewed renderer
patch and hashes are checked during builds. See its README for exact API and CSS
coverage. litehtml is a feasibility candidate, not a final full-browser/game engine
selection; complete CSS/DOM, modern web APIs, GPU and media remain substantial work.

### Next acceptance gates

1. Check the new controls, POST/redirect behavior, right-stick scrolling and
   viewport modes on Switch when the user is available; record failures precisely.
2. Expand layout/API compatibility with reproducible fixtures for the specific
   Google/Keldurn/VK/YouTube failures. A changed user agent alone is not a fix.
3. Evaluate the missing modules/workers/network/media stack against device memory
   and the rendering-engine choice before claiming general modern-site support.
4. Continue the separate WASM/shared-memory and WebGPU integration gates, then
   measure character creation and world entry separately on hardware.

There is no evidence-based completion percentage or fixed finish date for the
full game browser. Native visual browsing is implemented in a limited subset;
the GPU/media/runtime portions still require implementation and device proof.
