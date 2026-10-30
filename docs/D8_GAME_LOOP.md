# D8 — public x86 input/timer/frame-progress oracle and generic host key scripts

This package verifies the common path beyond startup rendering. It does not add
Pinball-specific code, a game emulator, synthetic key-state answers or fake timers.
The existing USER/GDI/clock/scheduler/presentation implementations execute the
public authored PE32 program; the new host tooling makes keyboard input repeatable.

## Public executable and independent output oracle

`tests/guest/game_loop.c` is CRT-free source without proprietary code/assets. Its
real busy loop calls PeekMessageA/TranslateMessage/DispatchMessageA/timeGetTime;
window WM_PAINT and WM_TIMER handlers execute interpreted x86. F2 starts, Right
moves a sprite by elapsed update intervals, Z changes its color while held, Space
resets position. Four actual key pairs and two translated WM_CHAR events are
required. The guest asserts scans/extended/up bits, queue-local key state, timer
identity, final key release and a finite elapsed-time budget. No Sleep/GetMessage
is inserted into the main update loop to fabricate progress.

The inspector runs four normal sessions and two host aborts while Right remains
held, alternating real time and explicitly fast-forwarded waits at800x600 and
1280x720. Input is delivered by the actual platform event source, not PostMessage
or edits of guest key-state globals. Native control probes coordinate reproducible
stages and a final visible-frame acknowledgement; they are test instrumentation.
Time advances through Clock, never by incrementing a guest frame/timer counter.

A complete independent96x64 reference checks background, moving sprite, pressed/
released colors and timer pixel against both canonical CPU pixels and the real
production SoftwarePresentation1280x720 output. Required visible stages1–6 and8
must occur; transient stage7 may correctly coalesce before presentation. At least
four elapsed update steps of actual movement are required before Right-up.
Paint-complete probe fields describe the rendered scene separately from mutable
input/update state. The inspector never pretends an unsubmitted intermediate
state is a visible frame. Forced redraw after Process/window/guest-memory teardown
must equal the last owning presentation; every timer/window owner is drained.
Restart requires initially released key state. Native existing FetchMessage-only
and C multi-thread input tests remain separate, unchanged gates.

## General host keyboard diagnostics

`winrun --new-ui -f N --key tick:vk:down --key tick:vk:up ...` injects actual
platform KeyDown/KeyUp events. `tick` counts calls to the platform wait adapter,
not rendered frames or time. Existing -f/-s/-c retain this historical wait-step
meaning. VK accepts strict decimal or0x hexadecimal,1–254. At most256 events,
nondecreasing ticks1–1000000, stable same-tick order and explicit repeat down are
supported. Invalid syntax/overflow/unmatched up/held final keys/out-of-limit events
refuse before loading the EXE. A finite -f and new UI are required. Early guest
termination before all scheduled keys were delivered is a failed diagnostic.

`--real-time` selects the existing monotonic real clock and real waits. Default
fast-forward behavior remains unchanged; it still includes real elapsed time plus
explicitly advanced waits. Neither option measures Switch FPS or input latency.
The helper is host-only and does not change Switch controller mappings. A full
scripted run of the public executable is part of the required host runner; parser
bounds/repeat/order/release tests are separate from real x86 delivery checks.

## Validation and boundaries

Run full COMPOSITION_GL_TEST=1 tools/run_host_tests.sh with MinGW/Mesa. CI retains
six jobs and publishes stage-d-game-loop containing public PE32, C/native/helper/
winrun sources, inspector/CLI/parser logs and this contract. Strict tool/inspector
and focused ASan/UBSan (remaining runtime ordinary, native ucontext caveat/leak scan
off) supplement the suite. A native negative that drops key-up must fail the public
protocol; a corrupted presented pixel must fail the independent output assertion.
No private game binary/image/trace is published. Verify exact-SHA all6 CI, Pages,
NRO and public source/fixture manifests before closing publication.

This proves a bounded public game-like loop, not arbitrary games, DirectInput,
Direct3D, playable Pinball, SFX/MIDI, sustained FPS or physical Switch acceptance.
Hardware remains deferred and local devkitA64/libnx is unavailable. CPU GDI default,
L legacy, ZL software and Claude Luna are preserved. SD recovery remains closed.

Next user-prioritized product package: [F7 controller bindings](F7_CONTROLLER_BINDINGS_PLAN.md),
with a real native settings screen, persistent profiles and safe key ownership.
Shared MCI/MediaPlayer/PlaySound with mixed waveOut, DirectSound/MIDI, real dynarec
and D3D9 remain separate larger milestones. Legacy LoadIconA needs its own public
resource/lifetime reproducer; do not chase imports with successful placeholders.

Private application observation: a bounded new-UI real-time F2/Space/Z/OEM_2 script
exited0 and retained a table snapshot. It did not establish ball/flipper handling
or gameplay; a rendered frame plus delivered host events is insufficient evidence.
Private files stay outside git/artifacts/site. Legacy boundary remains LoadIconA.
