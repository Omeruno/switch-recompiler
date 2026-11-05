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
version validates without SIMD. The real wasm-bindgen loader, GPU selection,
worker/WASI wiring and total working set remain to be established before engine
selection. The reader cannot execute it by simply raising resource limits.

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
