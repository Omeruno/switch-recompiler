# Switch Browser Probe

Standalone Switch homebrew experiment on `feature/switch-browser`. It opens the
Switch system WebApplet at Keldurn's Russian page, its game page, or a manually
entered HTTPS address. The existing x86 runtime and main NRO are untouched.

## Build and run

With devkitPro, devkitA64 and libnx installed, run `make -C browser`. Copy
`browser/switch-browser.nro` to `/switch/` on the SD card. Launch through Homebrew
Menu with **title override** (hold R while starting a game) so the application
has full memory and the WebApplet can launch. Use the D-pad and A to open a site;
use + to exit. The native browser applet handles touch and cursor input.

## Acceptance on real hardware

1. Open `https://keldurn.com/ru/` and verify TLS, page rendering and navigation.
2. Open `https://play.keldurn.com/` and record whether login, WebAssembly,
   WebGL, audio, network transport and game input work. Record firmware version
   and the exact error shown. Do not enter credentials into a page that fails TLS.
3. Enter an unrelated HTTPS cloud gaming site to check general navigation.
   Playing a service requires that service's own account, supported browser
   features and controller support.

This is an applet launcher, not an independent browser engine. The Switch system
WebApplet may reject a site or lack APIs used by modern cloud games. No Keldurn
gameplay or cloud gaming compatibility has been verified on hardware. A full
browser would require an independently ported modern engine and substantial
graphics, media, input and network integration.
