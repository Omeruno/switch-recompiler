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

## Play without updating Horizon

The user has a PC. [Moonlight Switch](https://github.com/XITRIX/Moonlight-Switch)
can stream a PC browser or locally installed game to the Switch from a Sunshine
host. See [PLAY_WITH_PC_RU.md](PLAY_WITH_PC_RU.md). This is a separate homebrew
client and is not a standalone Switch browser;
it requires a PC, a suitable network, and games or services available on the PC.
Keldurn and NFS Underground have not been verified in this setup. A Switchroot
Android browser is another separate-OS option, dependent on console model and
installation; it is outside this NRO project.
