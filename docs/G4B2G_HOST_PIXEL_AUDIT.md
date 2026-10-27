# G4b.2g: host pixel authority and coherence-fault audit

Incoming `dc254b204a8ba9f77a7a51872a655bacd1cc86aa` passed
[CI40442170369, six jobs](https://github.com/Omeruno/switch-recompiler/actions/runs/40442170369).
Extends the [mixed device adapter](G4B2F_SWITCH_MIXED_OUTPUT.md) and
[window coherence contract](G4B2C_CPU_COHERENCE.md). Ordinary GDI remains CPU.

## Findings and fixes

The public Gdi::Screen getter returned the canonical Surface without acquiring CPU
authority. It is now a guarded const view. ScreenForWrite explicitly acquires CPU
authority, marks a conservative CPU output change and returns the mutable view.
Replacing an existing screen or window provider also requires authority before
changing the binding. Successful provider replacement invalidates layout metadata.
These views are synchronous native borrows, not pointers to retain across GPU or
session exchanges. Surface's raw bytes are not a memory-protection mechanism:
trusted callers must use the write entry for mutation, even when a C++ pointer
could bypass const intent. No arbitrary pointer/memcpy interception is claimed.

Failed/expired/reentrant CPU preparation now latches a Gdi-store-owned fault.
Owner expiration is visible through CpuAccessFault even before a pixel operation.
Removing a gate, changing its later return value, screen rebinding or installing
another gate cannot erase lost authority. Reentrant failure cannot be masked by
an outer callback returning success. Fault and gate ownership move with the Gdi
store during GUI/session exchange; fresh stores start clean.

The coherent window owner now retains the first failure reason and immediately
marks its own USER output fault on dirty-import/window-description/CPU-commit/
detach failure. A request from another current USER session rejects without
poisoning the suspended owner. Acquire/Prune/CheckpointCpu/DisableCoherence no
longer turn a nested session's rejected operation into an outer-owner failure.
Explicit Shutdown still revokes any surviving weak gate before owner destruction;
an expired suspended gate subsequently blocks CPU access rather than dereferencing
that owner. The same native GL owner-thread/context contract still applies.

ShellSession gains TryCompose: a CPU authority failure returns null and latches
output failure. The compatibility Compose entry returns an invalid Surface on
failure, never the previous valid output image. Switch direct clock/fallback
presentation checks this result; CPU-only direct output also acquires authority.
USER selection and Switch OutputHealthy/Blit/Present observe CPU authority faults,
so a cursor or direct fallback cannot queue an old flat frame after owner loss.
The existing drain/return path remains responsible for ending the guest. The v9
summary additionally records CPU access fault and the first native coherence cause.

Two lifecycle boundaries were missing explicit pre-mutation authority: ScrollCommon
could move child geometry after a CPU authority failure inside blit, and OnGuiThreadExit could
retire dirty windows before checkpointing. Scroll now refuses that authority failure before mutation.
Thread-exit fallback acquires authority while windows still exist; on failure it
still retires native handles/queues but latches output failure. This permits native
continuation cleanup without presenting fabricated pixels. BeginPaint likewise
acquires authority before consuming update state/calling paint callbacks. Exposure
repaint policy and the limited thread-termination compatibility scope are unchanged.

## Registered production access inventory

Audit used current source, not just screen accessor names. Recheck this inventory
when introducing a new host pixel consumer or GDI dispatch path.

| Access family | Authority / boundary |
| --- | --- |
| ui_gdi Target/MakeCtx, CPU source/destination raster | PrepareCpuAccess before pixels; stale HWND/DC identities still reject |
| ui_gdi Screen / ScreenForWrite | Guarded synchronous read / conservative write view; no retained raw pointer |
| SetScreen / SetWindowDcProvider | Fresh setup needs no old transfer; replacement checks old authority first |
| SnapshotWindow / CommitWindow | Internal owner exchanges, intentional gate bypass to avoid recursion; identity/layout/visibility/dirty validation and coherent owner sequencing |
| USER Init | Fresh session allocates CPU screen/provider before any GPU authority exists |
| USER RepaintDesktop / paintDesktop callback | CPU gate precedes direct canonical Surface writes; shell paints only the supplied gated region |
| USER EnableWindowLayers | CPU gate precedes capture-pointer/retained-storage changes |
| USER create/destroy/position/show/style/parent/activation | Existing pre-layout barriers; added scroll and fallback thread-exit boundary |
| USER BeginPaint and ordinary paint/control GDI | BeginPaint barrier before consuming updates; actual raster uses gated GDI contexts |
| USER PresentIfNeeded CPU branch | Real CPU gate before canonical output; native selected branch deliberately uses owned mixed producer instead |
| ShellSession TryCompose / Compose | Checked CPU gate before canonical copy; failure cannot return an old valid frame |
| ShellSession native chrome/background/document surfaces | Separate host-owned CPU buffers, not resident guest images; NativeOverlay has no guest CPU read |
| WindowGpuBindings SubmitMixed | Deliberate stale-under-resident CPU base only; validated dirty window owners cover GPU-authoritative pixels, real CPU base selected elsewhere |
| MixedPresentation retained snapshots / resident views / software recovery | Owning CPU copies/resource pins; explicit manager recovery and borrowed views only during synchronous consumption |
| Switch direct Present / clock / cursor / software queue | CPU fallback checked; mixed uses owning producer; complete private RGBA staging copied to libnx output |
| Generic Presentation/OpenGlPresentation | Borrowed CPU inputs must already be authoritative or owned snapshots; no hidden live-USER bypass inside renderer |
| gdi32 BitmapObj/GetDIBits/SetBitmapBits/DIB sections | Memory bitmap pixels remain CPU-authoritative and never alias window GPU textures; actual window-to-DIB raster is gated |
| Legacy GDI/MFC frontend | Separate explicit L path/store; no coherent new-GDI GPU resources installed there |
| Metrics/window dimensions/clip/ownership descriptors | Metadata only; no added pixel checkpoint solely for a query |
| Public inspectors' direct S().screen snapshots | Test-only explicit stale/canonical oracle observations; not production authority consumers |

State/Surface storage remains accessible to native internals. This is a review of
registered paths plus executable boundaries, not proof against arbitrary future
C++ access. The single native owner thread, no-yield exchange, supported mapping/
clip limits, four-image/512-side/512x512 total budget and CPU-only DIB policy remain.
No guest page-protection or general asynchronous memory tracking is introduced.

## Evidence

New native test_cpu_access checks guarded read/write views, write revision,
expired owner detection, sticky failure despite later callback success/detach,
failed rebinding/reinstallation, reentrant failure masking, and fault moving with
the Gdi store while another store stays healthy.

Four fresh public-x86/EGL inspector sessions extend the original20 stages and
seven device modes without replacing their pixel/transfer/lifecycle gates:

1. Two real guest windows receive native GPU fills. A nested GuestSession tries
   Acquire/Prune/Checkpoint/Disable against the suspended owner and must leave it
   healthy, dirty and unread. Guarded host read commits both images exactly once;
   repeated clean read adds no target readback. A raw host write through the write
   entry must be reimported into the actual GPU image, survive full CPU/production
   GL comparison, and be consumed by the guest's real GetPixel checks at stage2.
2. Deliberately stale native layout while dirty makes the first CPU read fail.
   Canonical pixels stay unchanged, the first commit cause is retained, both fault
   latches set, TryCompose rejects, Compose is invalid and no output callback runs.
   A nested fresh guest stays healthy; restoring the outer store restores its fault.
3. Actual OnGuiThreadExit fallback entry checkpoints both dirty windows before
   retiring handles; their accepted pixels reach CPU and resources can be pruned.
4. The same fallback with deliberately invalid layout still retires native handles
   but leaves canonical pixels unchanged and output faulted. No stale frame is queued.

These are bounded host-inspected real-x86 sessions. The read/write case reaches
actual guest stage2; inspector sessions then abort/drain rather than claiming full
guest completion. Thread-exit cases invoke the real native cleanup entry while
parked, not a new full arbitrary forced-thread-termination acceptance.

Full COMPOSITION_GL_TEST=1 and strict O2/no-exceptions/no-RTTI checks are package
gates. Focused ASan/UBSan instruments changed GDI/bridge/shell/USER and mixed/GPU/
renderer/inspector modules with other runtime objects ordinary. Driver leak scan
is disabled; the preexisting unused ScrollCommon CPU parameter warning is allowed
in that focused build, with no source warning suppression. Existing native-fiber
warning is retained. Negative controls restore nonsticky CPU preparation or poison
the suspended owner in Prune; each must fail its specific new invariant.

The existing stage-gpu-window-coherence artifact includes CPU-access log, actual
x86/EGL log, public PE32 fixture and this contract. Core CI remains six jobs. Local
Switch SDK is absent; Switch-only direct output/fault diagnostics are CI compiled,
not host libnx execution. Physical Paint/GPU/fallback/input/relaunch remain deferred.

## Next

Admit a bounded ordinary solid-window GDI subset through the shared dispatch,
using metadata eligibility before any CPU Target barrier. Preserve unsupported
ROP/pattern/memory-DIB paths on CPU and distinguish refusal-before-work from an
accepted/faulted native operation. Public actual-x86 dispatch, CPU/EGL pixels,
readback/ROP/DIB/layout/cleanup and work-count gates precede default activation or
performance claims. No per-EXE shortcut, dynarec, D3D or game acceptance is added.
