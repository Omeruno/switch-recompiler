# Switch Browser: independent prototype

This NRO makes direct HTTPS requests with libcurl/libnx and draws a bounded
page view with SDL2 and DejaVu Sans. It never calls Nintendo's WebApplet. It is
the networking, navigation and Unicode display foundation for a browser that
runs on the Switch itself.
The x86 runtime and main NRO are untouched. All work stays on
`feature/switch-browser`.

## Build and use

Install devkitPro, devkitA64, libnx, `switch-curl` and `switch-sdl2_ttf`;
run `make -C browser`.
Copy `browser/switch-browser.nro` to `/switch/` on the SD card, then launch it
through Homebrew Menu with title override for full memory. Connect the Switch
to Wi-Fi before opening a site. The NRO does not invoke Horizon update services.

- Home: D-pad selects Keldurn, its game URL, or a typed HTTPS address; A opens.
- Page: Up/Down scroll; ZL/ZR select a link; A follows it; B goes back; X edits
  the address; Y reloads; + quits.

TLS verification is enabled. Only HTTPS pages and HTTPS redirects are accepted.
Responses are capped at 2 MiB and requests time out after 25 seconds. The page
screen reports the HTTP status or TLS/network error, which is useful for the
first real-Switch test on firmware 21.1.0.

## Current limit

This build extracts text and links from HTML. It does not run JavaScript,
WebAssembly, WebGL, video or the Keldurn game. The older WebApplet NRO requested
a system update on the user's Switch before opening any URL and was retired.
The user confirmed that the previous console-text NRO loaded site information
without an update prompt on their Switch. English displayed normally, while
Russian text was garbled. This build replaces that console output with a
Unicode font; Cyrillic display needs a new hardware check. Gameplay still
needs a modern browser engine with the APIs listed in
[ENGINE_PATH.md](ENGINE_PATH.md).

Host parser check:

```sh
cc -std=c11 -Wall -Wextra -Werror -Ibrowser/source \
  browser/tests/page_test.c browser/source/page.c \
  -o /tmp/browser-page-test $(pkg-config --cflags --libs libcurl)
/tmp/browser-page-test
```
