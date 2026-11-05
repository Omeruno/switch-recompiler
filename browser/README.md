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
install Python 3 for embedding the DOM source, then run `make -C browser`.
Copy `browser/switch-browser.nro` to `/switch/` on the SD card, then launch it
through Homebrew Menu with title override for full memory. Connect the Switch
to Wi-Fi before opening a site. The NRO does not invoke Horizon update services.

- Home: D-pad selects Keldurn, its game URL, a typed HTTPS address or an offline
  JavaScript demo; A opens. The demo updates a counter and adds text while open.
- Page: Up/Down scroll; ZL/ZR select a link or button; A activates it; B goes back; X edits
  the address; Y reloads; + quits.

TLS verification is enabled. Only HTTPS pages and HTTPS redirects are accepted.
Responses are capped at 2 MiB and requests time out after 25 seconds. The page
screen reports the HTTP status or TLS/network error, which is useful for the
first real-Switch test on firmware 21.1.0.

## Runtime and DOM

The graphical browser keeps one QuickJS session alive until navigation, reload,
return to Home, or exit. The main loop pumps timers and Promise jobs, then
updates layout when text, titles, button labels or link addresses change.
The offline demo includes a second counter, a clickable button, DOMContentLoaded
and a delayed element insertion. ZL/ZR select links or buttons; A dispatches a
cancelable click, including bubbling to document, before following a link.

The bounded reader DOM supports element/text creation, append/prepend,
replaceChildren/remove, textContent, attributes, dataset, classList, closest,
querySelector(All), getElementById, and basic tag, ID, class and attribute
selectors (including comma lists). It parses up to 128 KiB of source with a
2048-node allocation budget per turn. It does not implement the HTML parsing
standard, CSS layout, innerHTML, forms, images or video rendering. Detached
nodes and event callbacks are reclaimed with the page's entire runtime on
navigation; they do not survive into the next page.

setTimeout/setInterval, clearTimeout/clearInterval and requestAnimationFrame
run on the main loop with at most 64 registered timers. Callbacks must be
functions. Delayed intervals coalesce rather than replaying missed ticks.
Event listeners support once/removal, bubbling and preventDefault; capture,
keyboard/pointer events and full browser event semantics remain unimplemented.
Only 64 Promise jobs are drained per turn. Initial scripts have 300 ms budgets;
timer turns have an 8 ms interrupt budget, clicks have 30 ms, and the QuickJS
heap is capped at 16 MiB. These are reader limits, not a game execution budget.

Up to eight same-origin classic HTTPS script files are fetched, limited to
16 KiB each with a 10-second timeout and no redirects. Basic CSP directives
control script execution; nonce/hash policies are not implemented. URL parsing,
query parameters, location, document language and Russian navigator preferences
are exposed. Cookies and storage are not persisted. Intl.DateTimeFormat and
Intl.RelativeTimeFormat provide a limited ru/en subset using UTC only.

## Remaining engine work

ES modules, standards-compliant DOM/CSS, WebAssembly, WebGL, media, WebRTC,
storage and the Keldurn game are not implemented. A host probe of the Russian
landing page executed all three scripts and pumped its interval without an
exception. This confirms the page reader APIs; it does not verify gameplay or
Switch performance. The graphics path still needs a hardware check.

The older WebApplet NRO requested
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
