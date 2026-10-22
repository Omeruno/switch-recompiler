# Startup input and cursor speed follow-up

2026-10-22, after the E3 implementation (`3b48605`). User confirmed the previous
drawing/menu issues no longer occur, then reported an open File menu and missing
toolbar on startup (both normalize after clicking elsewhere), plus faster stick
movement while holding A.

## Startup edges

L selects Paint's new UI, but is also mapped to guest Alt. The native adapter
previously forwarded pad button-up edges independently of guest button-down
delivery. Releasing the launch-held L could therefore generate an orphan
WM_SYSKEYUP. DefWindowProc intentionally enters the keyboard menu on Alt-up,
explaining an unsolicited initial File menu. Its modal loop can in turn defer
startup WM_PAINT for controls. Hardware confirmation of the toolbar symptom
remains necessary; it is not independently proven by the input test.

PointerInputGate captures held buttons and touch at BeginPresentation. Each
launch-held input remains suppressed until physically released; that release
also generates no guest edge. Subsequent presses have paired down/up edges.
Inputs that were not held initially remain usable while another launch input
is still held. The gate is applied before all guest mouse/key mappings, and
drag-redraw classification uses accepted held buttons rather than raw state.
The renderer exit report includes the initial held mask and touch state.

## Motion independent of poll frequency

The old stick curve advanced up to nine source pixels per Wait poll. Hover
can block on framebuffer scanout; dragging skips its cursor-only frame and
can poll substantially faster. Work reduction in E3 exposed this rate coupling.

StickPointerMotion integrates the same squared deadzone curve using monotonic
elapsed microseconds, with a maximum of 540 source pixels/second (the previous
nine-pixel step at 60 Hz). It uses a double-precision accumulator so tight polls
do not lose small increments to float rounding. External touch positioning
rebases that accumulator. MouseMove is emitted only when integer coordinates
change, and a clamped cursor does not generate a stream of identical moves.
A sample uses at most 50 ms to prevent large jumps after stalls; keyboard
return resets the time anchor. Legacy input behavior is unchanged.

## Validation and acceptance

The native adapter uses the same portable helpers exercised by
test_pointer_input. It checks launch-held L/A/touch, independent releases,
fresh ordinary button pairs, repeated held samples, equal hover/drag travel
at 0.5/1/8/16.667 ms and jittered polling, low-stick precision, fractional
movement, touch reposition, deadzone, clipping, long pauses, clock rollback
and extreme signed stick input. The targeted optimized and ASan+UBSan runs
passed (LSan disabled in this traced environment). Full runtime/guest tests
and the native adapter compile run in exact-SHA CI.

Switch check: choose Paint's new UI with L as usual, release launch buttons,
and observe the initial File menu/toolbar without clicking. File should remain
closed and toolbar should paint normally. Then compare the same stick tilt
without A and with A held, and verify a fresh L press/release still opens the
menu intentionally. Finish with a short stroke and normal exit; preserve the
renderer report before another EXE. E3's hardware milestone remains pending
these newly reported input/startup regressions and the updated summary.


Acceptance update, 2026-10-22: the user confirmed no remaining problems after
c5e1871, including the launch/menu/toolbar and drag-speed follow-up. All five
jobs in exact-SHA run 39847105724 succeeded. This closes the functional E3
hardware gate at 50% Stage E. No fresh renderer timing report was supplied;
this acceptance does not establish a measured FPS or input-latency result.
