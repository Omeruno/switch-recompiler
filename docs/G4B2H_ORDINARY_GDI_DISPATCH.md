# G4b.2h — bounded ordinary solid GDI dispatch and first-frame measurements

Implemented software subset, 2026-10-27. Not full G4b.2 activation, a physical
Switch result, broad Paint acceleration, dynarec or Direct3D. Prior authority
contract: [host access audit](G4B2G_HOST_PIXEL_AUDIT.md); owning output/device:
[mixed workspace](G4B2E_MIXED_WORKSPACE.md), [Switch adapter](G4B2F_SWITCH_MIXED_OUTPUT.md).

## Admission and authority

The ordinary USER32 FillRect and GDI32 PatBlt paths now offer a solid operation
before acquiring a CPU target. A session-owned weak SolidRasterBackend returns
NotHandled, Accepted or Failed. Accepted never performs the same raster on CPU.
Failed never falls through to a second CPU draw; it latches CPU access and USER
output faults. FrameRect/FillRgn/FrameRgn propagate a failed child fill.

WindowGpuBindings owns the shared CPU gate/solid backend. Explicit
EnableSolidDispatch requires a healthy coherent current USER session and active
GPU. Removal checkpoints before detaching both interfaces; foreign-session
teardown cannot detach the suspended owner. Expired installed interfaces fail
closed. A CPU gate cannot be removed while its solid backend is installed.
Normal session exchange moves weak bindings with the GDI store, not into the
inner guest. No window/DC/guest pointer is retained by an operation.

Eligible operations:

- FillRect with BS_SOLID, including dynamic system-color brushes; its reference
  behavior ignores the DC ROP2. Inverted MM_TEXT rectangles normalize exactly as
  the existing CPU reference does.
- PatBlt with exactly PATCOPY `0x00f00021`, positive bounded extent and selected
  solid brush. Other ROPs remain CPU reference operations.
- Actual non-desktop top-level window DC, MM_TEXT, layout0, current HWND/DC
  identities, actual visible/paint/selected clip and window/viewport origins.
- Four images, maximum side512, combined512×512 pixels, ≤128 clip/dirty rects.
  Logical coordinates and DC origins within ±16384 for this admission only.

Memory/DIB/screen/child/large DCs, unsupported maps/brushes/ROPs, exhausted budgets
and pre-draw allocation refusal use the original guarded CPU path. Native seed
and target allocation finish before accepting raster. A seed refusal changes no
logical pixels; a later operation may retry. Lost GPU after import uses CPU before
raster. A failure during an admitted operation is a fault, not a retry after a
possibly partial write. Accepted counts logical operations; driver failure may
recover them through the existing checkpoint/journal. They are not GPU-completion
or draw counts. Real target counters continue to distinguish these.

Private seed/import/dirty tracking, CPU read/ROP/DIB/layout/lifetime checkpoints,
resource retirement and mixed ownership retain their previous contracts. GPU
writes advance the mixed producer epoch without falsely changing CPU screen
bytes. Native overlays and cursor redraw still need no target readback solely
for output. No new EXE-name selection or proprietary payload is introduced.

## Explicit Switch experiment

Default remains CPU GDI. The existing native experiment flag
`sdmc:/switch-recompiler/gpu-gdi-native.flag` with first byte ASCII1 now also calls
EnableSolidDispatch when new UI, GPU and native mixed workspace are available.
L legacy and ZL software overrides remain. Large Paint windows exceed this
subset; no Paint speedup is promised. Do not enable by default from host timing.

Renderer report v10 adds candidates, accepted logical operations, pre-draw
refusals and faults, captured before binding teardown. Candidates count only
eligible solid/common entry offers, not every GDI call. Raster duration labels
now say **Host GDI including native submission**: these synchronous intervals
include queue/preparation and are not exclusive CPU raster or GPU execution time.
Other native device/swap/cursor/fallback and authority-fault fields remain.

Local devkitA64/libnx is absent. Host tests execute the common backend; CI must
compile the actual libnx adapter. Switch pixels/FPS/input/relaunch remain deferred.

## Public test and fixed work gates

`tests/guest/gpu_gdi_dispatch.c` is CRT-free public PE32. All raster work is issued
by real interpreted x86 Win32 calls, never native-injected GPU fills. Dedicated
`tests/test_gpu_gdi_dispatch.cpp` runs sixteen normal-exit sessions:

- Seven CPU/GPU pairs, same PE/same build and same owning mixed/production EGL
  output path. The first pair warms the process/driver and is excluded from the
  six paired timing samples; remaining order alternates.
- First phase:64 disjoint16×16 tiles in a128×128 window, alternating real FillRect
  and PatBlt. GPU admits64, imports/uploads once, emits one64-fill batch plus one
  seed draw, and performs zero readback before/for output. Canonical CPU bytes
  remain the old seed. CPU reference tile colors also have an independent oracle.
- Compare every1280×720 EGL pixel to the CPU reference at four output phases.
  Real GetPixel checks64 tile centers; exactly one target readback/commit then
  supplies destination-invert ROP, window→DIB, raw guest DIB stores→window and
  GetDIBits. Later phases test clip/origins, hatch, child, memory/screen, side,
  combined-pixel and fifth-image refusal, move/resize/stale leases and cleanup.
- Allocation refusal before first seed: first tile genuinely uses CPU, remaining63
  use GPU; no duplicate raster, one import/batch and63×256 committed dirty pixels.
- Actual EGL context destruction after ordinary GPU work, manager notified with
  no current context: subsequent real x86 reads/raster and full software output
  match reference; no new GPU operations. This is not simulated hardware recovery.

All guests must actually exit0, stage order must complete, CPU/USER faults must
remain clear and custom allocator storage must return to zero. Native CPU-access
regression adds expired/failed solid owner with still-live CPU gate, aggregate
failure propagation and sticky no-retry checks. Earlier coherence/device/host-
audit fixtures and their original work-count gates remain unchanged.

## Measurement interpretation and next decision

`tools/benchmark_gdi_dispatch.py RUNNER PE OUTPUT.json` removes stale output,
requires pixels/counts/exits/recovery checks, records raw pairs, hashes, source
HEAD/dirty status, host/cache policy and medians/range. The full host runner and
existing CI host artifact include this report/log/PE/contract. No timing threshold
changes acceptance.

Measured host wall intervals are real x86 raster phase, owning submission,
production GL output submission, and subsequent real guest read/ROP/DIB phase.
Their sum is a bounded **first-frame path**, not complete application startup.
Fresh target/renderer each session includes import and lazy GL preparation.
Oracle FBO reads, lifecycle/budget cases, initial load and parked guest sleeps
are excluded. Driver output submission is not GPU execution, swap or scanout.
Mesa software GL and shader-cache policy materially affect numbers; no physical
Switch/FPS result is inferred. This is one public workload, not all applications.

Observed first-frame GPU path is slower than CPU on the local Mesa workload,
despite the64→1 fill batch and zero presentation target readback. Therefore no
default activation or speedup claim follows. The next package must separate
cold target/import/lazy-driver costs from repeated warm batches on the same
resource, instrument eligibility/ownership/copy costs, and optimize the measured
bottleneck with the same differential and work-count gates. Do not select a
Paint-specific shortcut or claim G4b.2 universally complete. Physical workload
measurements remain a separate deferred acceptance gate.

Local reproducible sample (Mesa shader cache disabled, host only):

| Median host wall, µs | CPU | Experimental GPU |
| --- | ---: | ---: |
| Guest raster / seed preparation | 235.0 | 27869.5 |
| Owning submit | 780.0 | 19186.5 |
| Driver output submit | 39826.5 | 71263.3 |
| Guest read/ROP/DIB | 134.4 | 406.4 |
| Sum of defined intervals | 40992.9 | 116725.1 |

Fixture SHA256: `021bc4b98af365babce57ab5515b254a6205d98d94fe128909dd779ac8c27d65`. Raw samples and exact
CI source identity are in the generated `gpu_gdi_dispatch.json` artifact.
