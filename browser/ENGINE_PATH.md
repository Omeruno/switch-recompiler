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

The next block embeds QuickJS and runs bounded classic scripts. Small
same-origin HTTPS scripts are now downloaded and executed in document order;
basic CSP directives distinguish them from blocked inline scripts. The bridge
currently covers only title, body text
and console output. A local demo exercises this on real hardware. Modules,
DOM APIs, WASM, graphics, audio and media remain open; this milestone
does not imply that Keldurn can run.
An HTTPS host probe of `https://keldurn.com/ru/` returned HTTP 200 and fetched
all three same-origin scripts (`acquisition.js`, `mega.js`, `lang.js`) with zero
download failures. Its `script-src 'self'` policy correctly blocks inline code.
The navigation bridge now exposes URL parsing, query parameters, anchor-prefix
queries, link address changes, the document language and Russian navigator
preferences. A host probe executed two of the three landing-page scripts;
`mega.js` stops at `ReferenceError: 'Intl' is not defined`. General DOM mutation,
events and the game engine APIs remain unimplemented.

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
