# Switch Browser Probe

Standalone Switch homebrew experiment on `feature/switch-browser`. It opens the
Switch system WebApplet at Keldurn's Russian page, its game page, or a manually
entered HTTPS address. The existing x86 runtime and main NRO are untouched.

**Hardware result (2026-11-04): blocked on the user's 21.1.0 / KEF-872 /
Atmosphere 1.11.2 setup.** Every menu choice asks for a system update as soon
as the WebApplet launches, before any site is loaded. The user will not update
the modified console. This NRO is therefore a rejected compatibility probe,
not a playable browser build. Do not accept the update prompt to test it.

## Build and run

With devkitPro, devkitA64 and libnx installed, run `make -C browser`. Copy
`browser/switch-browser.nro` to `/switch/` on the SD card. Launch through Homebrew
Menu with **title override** (hold R while starting a game) so the application
has full memory and the WebApplet can launch. Use the D-pad and A to open a site;
use + to exit. The native browser applet handles touch and cursor input.

## Further acceptance on real hardware

1. Open `https://keldurn.com/ru/` and verify TLS, page rendering and navigation.
2. Open `https://play.keldurn.com/` and record whether login, WebAssembly,
   WebGL, audio, network transport and game input work. Record firmware version
   and the exact error shown. Do not enter credentials into a page that fails TLS.
3. Enter an unrelated HTTPS cloud gaming site to check general navigation.
   Playing a service requires that service's own account, supported browser
   features and controller support.

This is an applet launcher, not an independent browser engine. The system update
gate makes it unusable on this console. No Keldurn gameplay or cloud gaming
compatibility has been verified. See [ENGINE_PATH.md](ENGINE_PATH.md) for the
independent-engine and streaming requirements.
