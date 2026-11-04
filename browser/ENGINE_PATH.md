# Browser path without a Horizon update

## Observed blocker

The `webPageCreate` / `webConfigShow` probe builds and starts, but the user's
Switch requests a system update for all three URL choices before navigation.
The console reports 21.1.0, KEF-872 and Atmosphere 1.11.2. The user does not
authorize a firmware update. Treat the Nintendo WebApplet route as blocked for
this target device; changing the URL or whitelist cannot fix a prompt that
occurs before page load. We do not know the exact origin of the update check
without a screenshot or console trace.

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
