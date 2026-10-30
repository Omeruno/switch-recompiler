# F8 — enlarged guest-window view, arcade bindings and indexed CPU copies

Incoming c978514d4fec4e803435218f9ce88bc4bbf2c2b5 passed all six jobs in
[CI41131710506](https://github.com/Omeruno/switch-recompiler/actions/runs/41131710506).
Limited application startup has user confirmation on Switch; gameplay/input/FPS,
seek recovery and the remaining hardware acceptance are still separate gates.
No private game assets, screenshots, traces or hardware-report contents are published.

## User controls

Start → Settings → Switch buttons → **Профиль: аркада** selects a draft:

| Switch | Guest key | Example use in a keyboard game |
| --- | --- | --- |
| B | Space | Hold/release a spring |
| L | Z | Left flipper |
| X | `/` (VK_OEM_2) | Right flipper |
| Y | F2 | New game |
| Minus | Escape | The application's Escape action |

Apply changes the active RAM profile. Save also writes the existing persistent
profile. Selecting the preset alone does not overwrite custom bindings or saved
settings. Other default bindings/reserved R/Plus/A/ZR actions stay available;
R opens Start, Plus quits, A/ZR are mouse buttons. Native shell navigation remains
B=Back,X=Accept,Y=focus independently of the guest profile. This is one explicitly
selected global keyboard preset, not an EXE-name switch or per-game patch. Existing
F7 key ownership,duplicate bindings,held quarantine and v1→v2 preference rules apply.

The old default B=Escape can legitimately minimize a guest that handles Escape
that way. The preset assigns Space to B, so this no longer happens as a side effect
of trying to operate that game's spring. Minus still delivers genuine Escape.

## Enlarged view

With a normal visible guest window active, open **R → Окно на весь экран**.
The frontend presents that window's visible rectangle, aspect-fitted to1280x720,
without desktop/taskbar. **R returns to the desktop and opens Start**. The same
native navigation remains available to change bindings or restore ordinary view.
The feature uses actual live pixels/keyboard/mouse, not a screenshot simulation.

This is a frontend display mode. It preserves guest HWND/style/geometry and the
application's WM_SIZE/maximize policy. A grey maximize button remains grey when
the application omits WS_MAXIMIZEBOX. It does not force a fixed-layout program to
resize, implement a virtual display change, or repair that application's own F4
fullscreen mode. Windows display-mode enumeration/change/notification and virtual
resolution remain a separate compatibility task. Do not call this feature true
Win32 maximization or completed ChangeDisplaySettings support.

ShellSession holds only HWND plus native raster identity. Each presentation
revalidates visibility/minimization/lifetime and clips the rectangle to the real
canonical screen. Hidden/minimized/destroyed/reused windows leave enlarged view;
fresh sessions begin in ordinary view. Selection follows the current foreground
root, with bounded child traversal. This first view does not automatically switch
to a newly created owned dialog outside the selected rectangle. R returns to the
ordinary desktop when the application needs it; multi-window focus policies remain
future work. Offscreen pixels are not invented or recovered from a cropped screen.

TryCompose prepares genuine CPU access and lends a pitched cropped surface only
for the synchronous submit. Presentation owns the submitted pixels and clears
stale layered frames before flat cropped submission. No borrowed guest/window
pointer survives redraw or teardown. The ordinary CPU/software and default Switch
OpenGL composition paths share the aspect-fit presentation. Explicit experimental
resident mixed-output routing has a different ownership/coordinate path and keeps
this action disabled; no native experiment is silently activated or reinterpreted.

Pointer coordinates remain absolute guest coordinates. Stick motion is bounded in
the chosen rectangle; touch uses the shared offset-aware Viewport::ToSource and
existing letterbox press rejection/drag clamp. Only the displayed cursor subtracts
the crop origin. Mouse events and USER capture still receive original guest
coordinates. No alternate per-game input protocol is added. The shared overload
validates64-bit dimensions before narrowing, including extreme invalid rectangles.
Opening Start triggers the existing real release/quarantine policy for held keys.

## Measured indexed blit path

Repeated indexed StretchDIBits work motivated a public native measurement. Gdi's
existing reference/fast switch now also admits8-bit source→32-bit destination
SRCCOPY copies at1:1 size. It requires bounded source coordinates,no mirror,
no colour key and a colour source. All other scaling/ROP/mono/unsupported cases
retain the general rasterizer. MakeCtx/CPU access, clipping, origins, damage and
write-capture ownership/revision barriers are retained.

A256-entry RGB palette lookup is rebuilt for every admitted call. Full/short/absent
palettes keep Surface::PaletteColor fallback semantics; updates are never hidden
by a cross-call cache. Signed pitch is honored. Captured destinations still use
Surface::Set, so indexed fast copying cannot bypass retained-window ownership or
capture no-op/revision rules. There is no GPU dispatch change, shader palette hack,
new frame limiter or promised game-specific FPS. The32-bit path keeps its previous
alpha normalization/alias snapshot behavior.

The authored benchmark alternates reference/fast order in one build, seven paired
samples per32x24/640x480 top-down/bottom-up case,48 actual calls per sample with a
changing palette. Every output pixel has an independent expected value. Median
wall intervals and hashes are printed, with **no timing threshold**. The first
local observation was about8–14× faster for this uncaptured native copy workload.
That is not a game FPS or captured-window/GPU/scanout result. The2800-case reference
oracle separately checks complete storage,clips/formats,pitch/palette variants,
X bytes,overlap,ROP/key,capture ownership/revisions and guard bytes. Existing
public actual-x86 indexed DIB/resource/composition suites remain gates. An outside
sound-enabled diagnostic confirmed admission during application drawing; private
profile/capture data remains outside git and public artifacts.

## Evidence and next work

- F7 settings test selects the actual draft preset,saves,reconstructs and checks
  independent key constants without altering old sort/association migration tests.
- `test_game_loop --view` reuses the public authored game_loop.exe. Four normal
  real/fast-forward x86 sessions plus two held-key aborts route the saved arcade
  profile through production ShellSession/core/USER. Actual Start pointer action
  enters enlarged view; original HWND320x180 is asserted unchanged. Independent
  complete96x64 moving/held/released pixels and scaled production output are checked.
  R returns ordinary1280x720; native B closes Start without leaking guest Space.
  Owning redraw after teardown and fresh restart remain checked.
- Offset touch tests cover corners,center,letterbox drag clamp,empty/extreme
  rectangles and unchanged outputs on failure. Existing pointer/generation/GL
  pixel/fallback tests remain applicable; physical touch is not inferred.
- Focused ASan/UBSan covers GDI/presentation/controller/editor/layout/session/view
  and inspectors; remaining runtime is ordinary. Native leak scans are enabled,
  guest-fiber scans disabled with the ucontext caveat. Strict changed modules and
  inspectors compile O2/Wall/Wextra/Werror/no-exceptions/no-RTTI. Corrupted palette
  and removed R exit fail independent native/x86 assertions.

The complete COMPOSITION_GL_TEST=1 host/MinGW/Mesa suite,exact published-SHA all-six
CI,Switch NRO,Pages/browser and public source/artifact audits remain mandatory.
CI publishes stage-game-view with authored PE/source/logs,actual renderer capture,
paired native timing observations and exact changed core/contract sources.
Local devkitA64/libnx remains absent;CI builds the NRO. CPU GDI default,Claude Luna,
L legacy/ZL software and the closed SD recovery status remain preserved.

Next: consolidate fresh Switch control/view/performance evidence and choose further
common CPU/GDI/GPU work from measurements. Audit real virtual display-mode support;
retain the shared audio-source bus,DirectSound/MIDI,dynarec/D3D9 and legacy LoadIconA
queues. Limited startup is not full Pinball acceptance or a broader game guarantee.
See [F7](F7_CONTROLLER_BINDINGS.md),[F6a](F6A_GUEST_WINDOW_GESTURES.md),
[G2](G2_BLIT_AND_PHASES.md),[D10](D10_AUDIO_RECOVERY.md).
