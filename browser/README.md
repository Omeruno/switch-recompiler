# Switch Browser: independent prototype

This NRO makes direct HTTPS requests with libcurl/libnx and draws a bounded
page view with SDL2 and DejaVu Sans. A bounded QuickJS runtime runs inline
classic scripts and small same-origin HTTPS script files according to the
page's basic CSP script directives. It never calls Nintendo's
WebApplet. It is the networking, navigation, Unicode and JavaScript foundation
for a browser that runs on the Switch itself.
The x86 runtime and main NRO are untouched. All work stays on
`feature/switch-browser`.

## Build and use

Install devkitPro, devkitA64, libnx, `switch-curl` and `switch-sdl2_ttf`;
run `make -C browser`.
Copy `browser/switch-browser.nro` to `/switch/` on the SD card, then launch it
through Homebrew Menu with title override for full memory. Connect the Switch
to Wi-Fi before opening a site. The NRO does not invoke Horizon update services.

- Home: D-pad selects Keldurn, its game URL, a typed HTTPS address or an offline
  JavaScript demo; A opens. The demo should show a Russian title and text that
  JavaScript wrote after loading.
- Page: Up/Down scroll; ZL/ZR select a link; A follows it; B goes back; X edits
  the address; Y reloads; + quits.

TLS verification is enabled. Only HTTPS pages and HTTPS redirects are accepted.
Responses are capped at 2 MiB and requests time out after 25 seconds. The page
screen reports the HTTP status or TLS/network error, which is useful for the
first real-Switch test on firmware 21.1.0.

## Current limit

This build extracts text and links from HTML and runs bounded classic JavaScript.
It fetches up to eight same-origin HTTPS scripts in document order, with a
16 KiB limit per script, no redirects and a 10-second timeout. A `script-src
'self'` policy permits these files and blocks inline scripts. It exposes
`document.title`, `document.body.textContent` and `console.log`. The navigation
bridge adds `URL` (hostname, pathname, search, searchParams.get/set), `location`,
the HTML language, and `navigator.language/languages` with a Russian preference.
Anchor queries support `a` and `a[href^="prefix"]`; script changes to their
HTTPS addresses are copied back to the browser's link list. Other selectors
return no matches. Event listeners are placeholders and are not dispatched;
cookies and storage are not persisted. A small `Intl.DateTimeFormat` subset
formats numeric dates, short months/weekdays and 24-hour time in Russian or
English. Only UTC is supported. `Intl.RelativeTimeFormat` supports minutes,
hours and days, Russian plural forms and `numeric: 'auto'`. Other Intl classes,
timezone conversion and full locale formatting are not provided. Timers are
not implemented; scripts using `setInterval` still stop there. ES modules,
a real DOM, WebAssembly, WebGL,
video and the Keldurn game are not implemented. Basic CSP script directives
control execution. Each script has a 300 ms execution budget; the runtime
has a 16 MiB memory limit. The older WebApplet NRO requested
a system update on the user's Switch before opening any URL and was retired.
The user confirmed direct HTTPS to Keldurn returns HTTP 200 on their Switch,
without an update prompt. Earlier graphical builds exited to the text fallback:
`romfsInit: 0x00001559`. Inspection of the published NRO found no asset header
or embedded RomFS. The installed `elf2nro` ignores `--romfsdir` alone when
deciding whether to write an asset header, so the build now also supplies an
NACP metadata file. CI checks that the font RomFS is actually present in the
NRO. The text fallback still cannot render Cyrillic correctly; the graphical
font path needs a new hardware check. Gameplay still needs a modern browser
engine with the APIs listed in
[ENGINE_PATH.md](ENGINE_PATH.md).

Host parser and JavaScript check:

```sh
sh browser/tests/run_host_tests.sh
```
