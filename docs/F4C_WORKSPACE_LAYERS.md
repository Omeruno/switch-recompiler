# F4c: retained native and guest workspace planes

Follows 1f0572a (exact CI 40136895969, all six jobs successful). Part of the
ongoing [whole-F closure](F_ACCEPTANCE.md), not a declaration of hardware acceptance.

Switch workspace no longer disables window raster layers. ShellSession keeps the
existing canonical CPU composition for readback/flat software fallback and adds
an optional scene ownership buffer. The scene combines actual retained guest
window surfaces and a separate native chrome plane, with a collision-free token,
independent session generation and exact overlay ownership rectangles. Native
pixels never contaminate guest GDI GetPixel/window surfaces. Presentation owns
its snapshots before guest/session/window teardown; no source view outlives the
synchronous Submit call.

The existing 48-layer/16 MiB scene budget includes the native plane. Guest layers
that do not fit are omitted from retention and their pixels reconcile to canonical
RGB, preserving output. Failure of the optional ownership allocation selects the
flat scene rather than a blank frame or failed native session. The three required
F4a RGB buffers retain their allocation-failure behavior. GPU upload/draw failure
still uses the ordinary CPU/software fallback and persistent output lifecycle.

This integration uses conservative ownership copying and layer comparisons
(writeThrough=false) when combining scenes. Presentation retains owned layers
and GPU textures/revisions, but this is not a claim of reduced CPU work, faster
Switch output or complete E7 epoch optimization for native overlays. I/O/HLE
calls that do not yield remain a responsiveness boundary.

The extended actual-x86 workspace harness now compares CPU layered output and
production EGL/Mesa pixels against the real canonical composition, including
Start overlays, guest minimize/restore, native Explorer, normal exit/abort/restart,
owned replay after complete GuestSession destruction and poisoned output padding.
Optional ownership allocation failure verifies working flat fallback; prior
required-allocation cases remain. The full host runner executes both ordinary
and SR_OPENGL_TEST variants alongside all earlier guest, C and E oracles.

Complete clean rootless host/MinGW/Mesa suite passed final runtime/header/fixture
sources, including new EGL workspace scenes, all 28-scene guest GL oracles,
file-save full-media failures and prior native/workspace/input/lifetime checks.
Strict target builds and exact new-SHA six-job CI remain publication gates.
Switch-only main now submits the combined scene while retaining canonical CPU
fallback; local devkitA64/libnx remains absent and compilation is checked by CI.
Physical Paint/GPU/fallback/Lite/input/relaunch remain deferred; SD stays closed.

Remaining baseline F: bounded folder tree, persisted Desktop/Start shortcuts and
integrated acceptance audit. Installer/archive extension remains separately
tracked after baseline F5. No multiple guest Processes or general NT/Windows
compatibility is delivered by this renderer integration.
