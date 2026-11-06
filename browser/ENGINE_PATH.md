# Browser path without a Horizon update

## Observed blocker

The `webPageCreate` / `webConfigShow` probe builds and starts, but the user's
Switch requests a system update for all three URL choices before navigation.
The console reports 21.1.0, KEF-872 and Atmosphere 1.11.2. The user does not
authorize a firmware update. Treat the Nintendo WebApplet route as blocked for
this target device; changing the URL or whitelist cannot fix a prompt that
occurs before page load. We do not know the exact origin of the update check
without a screenshot or console trace.

The first replacement NRO uses direct libcurl/libnx HTTPS, address input and
links. The user tested it on the target Switch: site information appeared and
there was no update prompt. English text displayed normally; Russian text was
garbled in the libnx console view. The next build replaces console output with
SDL2 and a bundled DejaVu Sans font that supports Cyrillic. Its Unicode display
and navigation still need a real-Switch check. It is not yet game-capable.
The user's next hardware photo showed HTTP 200 from Keldurn in the text fallback
and `romfsInit: 0x00001559` before SDL started. The downloaded NRO had no asset
header at all. `elf2nro` ignores a lone `--romfsdir` when deciding whether to
write asset data. Packaging now also supplies NACP metadata, forcing the asset
header, and CI verifies the RomFS size; the graphical path must be checked on
the target Switch again.

The reader now owns a persistent QuickJS runtime per page, with a bounded DOM,
timers, animation-frame callbacks, Promise jobs and click dispatch. Text and
interactive element changes propagate back to SDL layout between frames.
Navigation destroys the runtime and its callbacks before overwriting the page.
A ru/en UTC Intl subset supports the landing page's calendar functions.

A host probe of `https://keldurn.com/ru/` returned HTTP 200, fetched all three
same-origin scripts, executed all three without exceptions and pumped the
calendar interval. Its `script-src 'self'` policy blocks inline scripts while
allowing same-origin external files. The offline demo and host integration tests
cover delayed updates, button clicks, lifecycle events, Promise continuations,
cancellation, navigation cleanup and interruption/heap limits. These checks do
not establish that the actual game client can execute.

The next engine stages need standards-level HTML/CSS and DOM behavior, module
loading, full network APIs, IndexedDB, WASM and GPU rendering. The page reader remains
text-based and cannot run WoW or a cloud gaming video client. Hardware rendering
and input latency must still be verified on the target console.

The profile milestone bundles and verifies a Mozilla/curl CA snapshot, configures
TLS for pages and scripts, shares HTTP cookies, and exposes bounded origin-scoped
localStorage/sessionStorage plus document.cookie. The SD profile saves persistent
cookies and local storage, reports write failures and allows user-initiated data
clearing. Loopback TLS tests verify unknown CAs, hostname mismatch, expiry and
HTTPS-to-HTTP rejection on the host. Native libnx root import and SD persistence
still require a console check. Authentication remains limited by the reader's
missing form, navigation and cross-origin APIs.

## Actual client inspection

The uploaded 2026-11-05 WASM client is now inspected and validated statically.
See [the evidence and next gates](research/KELDURN_CLIENT.md) and its reproducible
JSON summary. It has 1,041 imports, shared 32-bit memory, exception tags,
reference tables and both WebGPU/WebGL bindings. Its module minimum is 16.625 MiB
of linear memory; 4 GiB is its maximum, not a measured RAM requirement. This
version validates without SIMD. The subsequent JS/entry-page upload confirms a
WebGPU-required normal launcher, default initial shared memory of 16.625 MiB,
optional 0–4 compute helpers and an intentionally stubbed WASI contract. The
supplied glue covers every core import name and its static WASM export references.
See the report's loader follow-up for source coverage and remaining dependencies.
The supplied capture reaches character creation; in-world traffic/assets and
performance have not been observed. Actual GPU limits, peak working set, full
module/resource graph and Horizon
engine/backend feasibility remain unresolved. The reader cannot execute this
client by simply raising resource limits.

## Engine compatibility implementation

`browser/engine` now contains a separately pinned WAMR compatibility candidate,
an interpreter-only libnx platform layer and an executable host/Switch probe.
The supplied client uses legacy `try`/`catch`, which this WAMR mode supports.
Twelve host probes execute features (including a real two-thread shared-memory
counter), and WAMR successfully loads the actual client without instantiating it.
See [the build, device checks and boundaries](engine/README.md).

This is not a final engine selection. Classic interpreter performance is not
sufficiently characterized, host results do not establish libnx behavior, and
the Switch memory shim only backs small reservations. A 4 GiB VA reservation
probe is separate from physical commit/growth; neither supplies WebGPU, the JS
binding graph or gameplay. Those remain explicit implementation gates.

## Independent-browser requirements

Keldurn gameplay must be checked against its actual game client, not only its
landing page. A usable browser engine needs HTTPS, current JavaScript, WASM,
WebGL, sound, input mapping, storage, and whatever network transport the game
uses. Other cloud gaming services may additionally require WebRTC, Media Source
Extensions, a hardware video decoder, low-latency audio, and gamepad support.
Each target service needs a real-console compatibility test.

An existing [NetSurf Switch port](https://github.com/etonedemid/netsurf-switch)
demonstrates independent HTTPS/JS rendering on Switch, but its own README lists
WebGL and Media Source Extensions as missing. It is a useful research baseline,
not a confirmed Keldurn or cloud gaming solution. The browser work should first
establish the exact Keldurn runtime/API failures on a device that can load the
game, then select an engine and implement the missing APIs against those traces.

## Product requirement

The user wants to play Keldurn on the Switch itself while travelling, over any
working Internet connection, without a PC host and without updating Horizon.
PC streaming is explicitly rejected and is not an implementation path for this
project. A standalone browser must execute the web game locally on the Switch.

Switchroot Android with an Android browser is a possible on-device route that
does not use Nintendo's WebApplet. It is a separate OS installation, not an NRO,
and its suitability depends on console model, storage setup, browser features
and a real Keldurn play test. It should not be represented as a completed native
Horizon browser.

## Hardware feedback requiring confirmation

The user reported a system-error termination in switch-engine-probe.nro and
CA import result 0x0001B47B in the 0.3.0 browser. Engine 0.1.1 now starts in an
idle menu with per-case operation checkpoints and explicit local-exec TLS.
Browser 0.3.1 uses NUL-terminated multi-certificate batches instead of exceeding
Horizon's 71 PKI-object limit. Host tests and compilation cannot substitute for
retesting these changes on the user's console.

## Confirmed device baseline — 2026-11-05

The 0.1.1 device report now confirms all 13 distinct small execution cases;
the shared WASM counter with two native threads passed twice. The browser's CA
error is gone according to the user, and its graphical text/link selector is
visible. The graphics are a reader UI, not site CSS/image/WebGPU rendering.
The report does not cover 4 GiB reservation/commit, full game instantiation,
GPU execution or performance. Checkpoint I/O/frame updates contaminate timings.
Browser 0.3.2 corrects a separate SDL_ttf bug: MeasureUTF8 returns a character
count, which was previously treated as bytes and could split Cyrillic strings.

### 0.2.0: shared linear-memory implementation

The engine now has a stable 4 GiB virtual reservation, incremental 64 KiB heap
backing (256 MiB aggregate quota), rollback-safe commit and quarantined partial
release. Host tests exercise client-sized shared memory, unshared failed growth
and injected allocator errors. The libnx implementation maps heap backing into
reserved stack VA with svcMapMemory/svcUnmapMemory; ARM64 build and a new device
run are separate gates. The 13 successful 0.1.1 device cases do not validate this
new backend. No JS host imports or GPU implementation is supplied by this step.

## Visual browser priority

Website CSS/images and touchscreen interaction are required product features.
They must not wait for the entire WASM/game pipeline. The current flattened-text
renderer is an intermediate reader. See [the visual browser delivery plan](VISUAL_BROWSER_PLAN.md)
for engine feasibility, document/layout/paint/hit testing, images/forms, direct
touch interaction, modern CSS/gestures and separate game-integration criteria.

### Native visual candidate

`browser/visual` now has a pinned litehtml/Gumbo HTML/CSS renderer and SDL2 paint
adapter with an independent `switch-visual-browser.nro`. The offline fixture uses
external CSS, flex layout, Russian text and PNG/JPEG images. Tap hit testing,
drag/inertial scrolling and anchored pinch zoom share the rendered document's
coordinates; HTTPS resources use nonblocking libcurl-multi. The reader's QuickJS
DOM is not attached to this document tree. Refer to the visual README for the
supported painting subset and host/device verification boundary. This candidate
does not settle final browser-engine selection or supply gameplay.

### Visual 0.2.0 refinement

The user confirms visual 0.1.0 networking, touchscreen gestures and stick pointer
on the device, including manually entered Keldurn `/ru/`. Refinement implements
native text controls/basic GET forms, WebP, bounded gradient tiles, right-stick
scroll and selectable CSS viewport width. Grid, JavaScript and media playback
remain separate missing capabilities. VK and YouTube feedback is recorded in
visual/device-evidence-2026-11-06.json without attributing their exact failures
to user-agent detection. A CSS viewport toggle does not implement Grid.
