# G4b.2i — persistent-resource measurements and covered journal retirement

Implemented bounded native experiment, 2026-10-27. Builds on
[ordinary solid dispatch](G4B2H_ORDINARY_GDI_DISPATCH.md); default GDI remains CPU.
No Switch hardware/FPS, broader Paint/game compatibility, dynarec or D3D result.

## Measure before choosing the optimization

New public CRT-free `tests/guest/gpu_gdi_warm.c` issues ordinary interpreted x86
FillRect/PATCOPY calls. One128×128 top-level HWND/DC persists through nine frames,
each with64 differently colored opaque tiles. No guest pixel read occurs between
frames. Real GetPixel checks all64 final tile centers, then normal window/DC
cleanup and ExitProcess0. No native-injected raster or EXE-name selection.

The initial reference run found **eight capacity readbacks before the first guest
pixel read**. The fixed64-command recovery journal fills each frame; the next
fill forces CpuRead even though its pixels replace an already executed tile.
Cold target/import/lazy GL preparation is distinct from repeated same-resource
work. This is the measured work source selected for optimization.

## Bounded retirement contract

GpuGdiPrototype may retire fully covered **already GPU-flushed** opaque journal
commands immediately before accepting a new containing fill, under capacity
pressure only. Both existing fills and owned CPU-copy snapshots are eligible.
Rectangles are compared in clipped target coordinates. The old operation cannot
affect the final replay image once the new opaque fill is appended.

- Pending commands are never retired. Their relative order is preserved.
- Partial coverage keeps the old command; if no room is reclaimed, the existing
  real capacity checkpoint remains. No rectangle splitting, increased journal,
  guessed pixels, lost recovery checkpoint or delayed unsupported operation.
- Stable compaction moves the owned commands and adjusts both count and flushed
  prefix length. Copy bytes are subtracted and freed immediately; the final moved
  slot is cleared. No guest/caller pointer is retained.
- There is no allocation, yield, driver operation or fallible step between
  retirement and the accepting append. Removing a covered command creates room;
  therefore no failure can leave old logical pixels discarded without the new
  fill. If nothing is removed, existing Room behavior is unchanged.
- GPU drawing still performs the new commands, including partially overlapping
  pending suffixes, in order. Recovery uses the verified CPU checkpoint plus the
  compacted owned journal, including accepted but unflushed commands.

Same-build native EnableCoveredRetirement control selects reference/optimized
paths. Ordinary solid dispatch enables it in the existing explicit native
experiment. Isolated prototypes/default GDI remain unchanged; target shutdown
resets the manager policy. Turning the control off does not change current pixels.
New counters separately report capacityCheckpoints and coveredRetired. They are
native work counts, not a claim of GPU execution time or physical frame rate.

Limits remain64 commands,4MiB owned copy input, four targets, side512 and combined
512×512 pixels. CPU read/ROP/DIB/host/layout/lifetime authority and real mixed
recovery retain the previous contracts. Pattern/ROP/child/large/unsupported paths
still use CPU. This does not remove a real application GetPixel checkpoint.

## Differential and recovery evidence

`tests/test_gpu_gdi_warm.cpp` runs12 normal-exit sessions, rotating CPU, GPU with
retirement disabled and GPU with retirement enabled. Same PE/build/output path;
initial triplet excluded from timing summary. Each of108 frames has a full
1280×720 actual production-EGL comparison to independent tile/background pixels.
CPU mode also compares canonical pixels. GPU modes keep canonical bytes unchanged
until the actual final guest read; one seed/upload and no intermediate host commit.

Fixed GPU gates:

| Work per nine-frame session | Reference | Retirement enabled |
| --- | ---: | ---: |
| Actual ordinary tile calls |576|576|
| Seed imports/uploads |1|1|
| Capacity readbacks before guest read |8|0|
| Fully covered journal commands retired |0|512|
| Total readbacks after real final GetPixel |9|1|
| Window CPU commits after guest read |1|1|

`tests/test_gpu_gdi_retirement.cpp` independently checks partial coverage requiring
checkpoint, owned-copy bytes actually freed, overlapping pending suffix ordering,
a full unflushed journal that cannot be retired, real GL error recovery and actual
EGL destruction before CPU replay. Every pixel matches an independent CPU surface;
custom allocator storage returns to zero. A negative lost-pending-prefix mutation
must fail that exact pixel oracle. Existing batch work counts, shared state,
coherence/device/fault/lifecycle and ordinary first-frame tests remain gates.

## Timing scope and next work

`tools/benchmark_gdi_warm.py RUNNER PE OUTPUT.json` removes stale reports, requires
all pixels/counts/exits, records exact source identity, binary hashes, host/cache
policy, raw108 frame intervals plus12 final guest-read intervals and summaries.
Existing CI host artifact publishes report/log/public PE/native test log/contract.

Wall phases: real guest raster/admission, owning submit (including target flush),
production driver output submit, then separate final real guest-read phase.
Cold frame0 and warm frames1–8 are not pooled. Warm samples reuse actual resources,
not just a process shader cache. FBO oracle reads, guest sleeps, load and teardown
are excluded. These are host Mesa wall intervals, not GPU timestamps, physical
swap/scanout, a complete application session or FPS. No noisy timing threshold
changes correctness acceptance; fewer readbacks alone do not prove faster Switch
or arbitrary workloads.

Next: quantify owning snapshot/ownership/flush costs and repeated partial/copy
workloads before selecting a further common-path optimization or widening default
activation. Retained producer snapshots still have bounded copy costs; this
package does not pretend they disappeared. Hardware Paint/GPU/fallback/input/
relaunch remains deferred; local devkitA64/libnx absent, actual Switch adapter
compilation is CI-only. SD diagnosis is closed.

Local Mesa sample, shader cache disabled (µs, host wall medians; timing ranges
overlap, no pass threshold):

| Mode | Cold defined cycle | Warm defined cycle | Final guest read |
| --- | ---: | ---: | ---: |
| cpu | 40819.8 | 3102.6 | 93.9 |
| reference_gpu | 113756.0 | 3206.7 | 320.4 |
| retired_gpu | 114376.3 | 2698.9 | 284.0 |

Raw intervals/ranges and exact CI source/PE identity are in the generated
`gpu_gdi_warm.json` artifact. The stable optimization result is the bounded
capacity-read reduction; these numbers are not Switch performance acceptance.
