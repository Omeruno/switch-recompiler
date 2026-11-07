# Visual browser and touch requirements

## Product target

A standalone Horizon browser must show website layout and images, support Russian
text, and work with the Switch touchscreen and controllers. The text reader is
an intermediate diagnostic tool, not the final browser interface. Work stays on
feature/switch-browser. No Horizon update or PC streaming is part of this plan.

## Current status — visual 0.11.1, 2026-11-06

- The reader remains a separate 0.3.2 diagnostic browser; visual pages use
  `browser/visual` and its independent `switch-visual-browser.nro`.
- The user verified visual 0.1.0 on Switch: Google and Keldurn root/ru pages load;
  touchscreen gestures and left-stick pointer work. On 0.4.0 the user confirms
  fields accept input but some remain invisible on Google; VK/YouTube still fail.
  Version 0.4.1 corrections await device checks; a network restriction is unproven.
  VPN is deferred by the user.
- Visual 0.11.1 retains the native document through HTML/CSS layout and QuickJS
  mutations. It supports flex/media queries, PNG/JPEG/WebP, gradients, viewport
  width modes, right-stick scrolling, keyboard text inputs, native GET/POST forms,
  checkbox/radio groups, scrollable single/multiple selects, basic validation/reset,
  events, timers, buffered Fetch/body requests, asynchronous XHR, anonymous CORS, TLS WebSocket and buffered Blob. Version 0.4.1 adds native field
  frames, independent control sizing, text fitting/wrapping and shared JS/native focus.
  See the visual README for limits.
- Twenty host suites now cover rendering/forms, scheduling, dynamic scripts and
  observers. Flex geometry has 120 Chromium reference rectangles; the shared
  dynamic DOM fixture also runs in Chromium. ARM64 CI supplies the NRO;
  host results do not establish device performance or arbitrary-site compatibility.
- Still required for general browsing: broader DOM/events and JavaScript APIs,
  Grid and additional CSS, navigation/focus/selection work, import maps/workers,
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

## Implemented candidate — visual 0.1.0–0.10.0

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

## Architecture audit — 0.5.0

See [visual/CORE_AUDIT.md](visual/CORE_AUDIT.md) for the researched architecture and
coverage matrix. Confirmed defects fixed: missing flex gaps, unregistered flex DOM
geometry, auto-margin distribution and accumulating paint counters. L/status tap
opens a bounded core report for HTTP/TLS/transport/limits/JS/image/paint categories.
Full browser engine evaluation now has explicit Horizon build, surface/input,
HTTPS, mutation, RAM/frame and GPU/media acceptance gates; no alternative engine
is selected or claimed to work on Switch. Further work prioritizes reproducible
site failures, event-loop/script scheduling and targeted DOM/network coverage.

## Scheduling block — 0.6.0

Separate task/microtask/rAF phases, initial async/defer scheduling on the fully
parsed DOM, lifecycle transitions, currentScript and tracked Promise rejection
events are implemented. CSS/image/nonvisual resource revisions prevent unrelated
completions from rebuilding CSS. Native DOM/input identity is tested through
late images/styles and navigation. See the visual README and core audit for
budgets and limits: streaming parser scheduling, dynamic scripts, observers,
modules/workers and fine-grained style invalidation remain open.

## Dynamic DOM block — 0.7.0

Dynamic classic script preparation/execution, async=false ordering, currentScript,
inert innerHTML, bounded native MutationObserver records and DocumentFragment/bulk
mutations are implemented together. Shared behavioral fixtures have a Chromium
reference; HTTPS tests cover loader ordering, policy/cache/size/error cases and
navigation cancellation. Detached fragments defer computed style until the normal
refresh, fixing a sanitizer-reported uninitialized font-style read. Device checks
are documented in visual/TESTING.md; ResizeObserver, IntersectionObserver, modules,
workers and the other network/layout/graphics gates remain separate.

## Device-directed compatibility fix — 0.7.1

The user confirms faster 0.7.0 browsing. A photo exposes the missing
HTMLScriptElement global; site security verification and Google sign-in still fail.
Typed DOM wrappers/prototypes and script/element descriptors are now implemented
and checked with a shared Chromium/native fixture. noscript fallback is hidden and
its scripts inert in the JS-enabled configuration. Service acceptance and broader
Web API compatibility remain unverified; no UA spoof or successful sign-in is claimed.

## Responsive layout block — 0.8.0

Native ResizeObserver/IntersectionObserver, typed media query events, coalesced
resize/scroll and real view scrollTo/scrollBy are implemented in the documented HTML
subset. Geometry now includes padding/borders and fixed scroll coordinates; expired
renderer weak references are pruned after CSS refresh. A shared Chromium/native
fixture and real Browser integration cover delivery/lifecycle/clips/scrolling;
fifteen host suites are the release gate. Device checks use the offline observer
demo. Grid, broader DOM/CSS, modules/workers, Fetch/CORS/XHR/WebSocket, media/GPU
and game integration remain open. See visual/README.md for quotas, unsupported
geometry and the explicit percentage rootMargin reference-browser difference.

## Buffered network block — 0.9.0

Independent native Fetch requests, Headers/Request/Response, UTF-8 codecs,
AbortController and asynchronous XMLHttpRequest are implemented with body/header
limits and realm-safe cancellation. Anonymous CORS includes OPTIONS preflight and
response header exposure; every redirect is checked before connecting. Same-origin
cookies and omit work; cross-origin include remains blocked pending a proper
SameSite/partition policy. Streams, complete HTTP/cache/charset/CORS semantics,
WebSocket, modules/workers and media/GPU remain separate gates. Sixteen host suites
include real TLS multi-origin and shared Chromium interface fixtures. Check the
offline «Загрузка и отмена» demo on Switch; account/security/game compatibility
is not established by the host results.

## URL-based module block — 0.10.0

Browser module-map loading now connects QuickJS's real ES module parser/linker to
the asynchronous TLS/CORS transport. Static/dynamic imports, re-exports, live
bindings, cycles, import.meta.url and top-level await are covered by native and
Chromium fixtures. nomodule/async/defer, redirects/policies/MIME, errors, quotas
and navigation cancellation are part of the block. Offline device checks use
«Модульная страница». Seventeen host suites are the release gate; full site/game
compatibility is unverified. Import maps/attributes and non-JS module types,
Workers, WebSocket, JS↔WASM and GPU/media remain separate milestones.


## Persistent socket block — 0.11.0

Implemented native TLS WebSocket transport compatible with Switch curl 7.69.1,
RFC handshake/framing/control/close checks, bounded send/receive queues, JS
WebSocket/MessageEvent/CloseEvent, buffered Blob and HTTP Blob bodies. The real
Browser event loop rotates socket events with Fetch/modules/timers and cancels
old connections on navigation. A network echo demo complements the offline module
demo. Nineteen host suites cover the combined implementation; Switch testing is
pending. Cross-origin sockets remain anonymous, without complete SameSite cookies.

Remaining large gates: broader DOM/CSS and service compatibility, Worker plus
JS-to-WASM imports/shared memory, GPU rendering (WebGPU/WebGL), media/audio, input
mapping and hardware performance/memory validation. Completing sockets does not
establish account/security-check compatibility or a playable game client.


## Device regression follow-up — 0.11.1

Fixed scripting-aware noscript parsing, HTTP HTML charset propagation and classic
CSP nonce sources with attribute/selector hiding. Added bounded primary navigation
timeout/error UI and final-origin/bytes/completion diagnostics. Twenty host suites
cover parser, charset, negative CSP and partial TLS response cases. Google no longer
reproduces as blank in the cloud check, but redirects to a service traffic check;
normal search/CAPTCHA and restored jutsu.ru access await device validation. Keldurn
fails its actual WebGPU capability check; implementing GPU and JS↔WASM remains
required rather than changing Horizon or claiming a supported graphics adapter.

## Game foundation — 0.12.0

Completed a first executable JS↔WASM + Canvas + input path, including an owned
local playable framebuffer game and native Browser integration tests. WAMR remains
an interpreter with strict quotas; no JIT or fake GPU/shared-memory capabilities.
See visual/README.md for supported import types and limitations.

Next game gates, in dependency order:
1. Broaden the Emscripten host/DOM surface against a minimal owned EmulatorJS-like
   fixture, including imported memories/tables and correct cross-instance refs.
2. Worker/message/event lifecycles and safe SharedArrayBuffer/shared Wasm memory.
3. Real GPU backend and WebGL/WebGPU binding sufficient for a chosen open test
   workload; then the actual selected game renderer (Keldurn needs WebGPU).
4. WebAudio/media timing, save lifecycle and service-specific compatibility.
5. On-device memory/performance, suspend/resume and long-session tests; then real
   game startup/login/loading/gameplay acceptance. Each game requires its own gate.
The current local demo does not complete those gates or establish gam.onl/Keldurn
playability. Full browser compatibility is still a substantial engineering effort.

## Unshared imported game state — 0.13.0

Added canonical imported Memory and mutable numeric Global bindings across modules,
including re-export identity, limits, growth detachment, nested calls, initialization
traps, failure recovery and an owned two-module game. The 0.12 base demo has now been
confirmed working by the user on a physical Switch; the changed 0.13 module path
needs its own device check. Emscripten-host gate remains partial: imported tables and
cross-instance function references in tables are still rejected. Those, Workers /
shared JS memory, real GPU and audio remain the next major game-compatibility gates.
