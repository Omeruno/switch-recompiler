# Visual browser and touch requirements

## Product target

A standalone Horizon browser must show website layout and images, support Russian
text, and work with the Switch touchscreen and controllers. The text reader is
an intermediate diagnostic tool, not the final browser interface. Work stays on
feature/switch-browser. No Horizon update or PC streaming is part of this plan.

## Verified starting point

- HTTPS, bounded JavaScript, cookies/storage and Russian UI text exist.
- The current page renderer lays out a flattened text buffer and a link list.
- Site CSS, image layout, forms and pointer/touch events are not implemented.
- The separate WASM probe checks runtime features and memory. It does not add
  website rendering, WebGPU or game compatibility to the reader.

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
