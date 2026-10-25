# G4b.1: ordered GPU fill batches and same-build measurements

Incoming 847ba20 passed six jobs in CI 40193307039. This package is the batching
slice of G4b; live-window/raw-DIB/ROP coherence and production activation remain
G4b.2. The experiment is still isolated from Paint/Win32 GDI and the production
Switch presenter. No physical tests are requested or inferred from host results.

## Bounded batching and order

The target flush partitions only the unflushed journal suffix. A batch contains
contiguous solid fills whose clipped destination rectangles are pairwise disjoint.
An intersection or a Copy command ends the current group. Groups remain in
original order and a copy is issued separately. There is no sorting, commuting
of overlapping operations or destructive merging of recovery records.

Two or more compatible fills use one glDrawArraysInstanced call. A procedural
six-vertex quad per instance selects the original clipped rectangle/color from
fixed uniform arrays. Single commands retain the original full-triangle/scissor
path. Integer rectangle edges preserve CPU pixel coverage; instance colors are
flat and opaque. The shader does not sample an attached texture. Unlike the old
three-vertex scissored triangle, each batched rectangle uses six vertices: fewer
submission calls do not imply less GPU work in every possible workload.

The existing 64-command limit bounds each group and the pairwise compatibility
scan (at most 2016 rectangle checks). Fixed native arrays use about 1.75 KiB stack;
no new command allocation or unbounded queue is added. The vertex shader uses
450 uniform components, below GL 3.3's guaranteed minimum 1024. A static assertion
binds the journal limit to the shader's 64-element capacity. Uniform locations for
both programs are resolved/validated once at initialization. The same-build
native EnableBatching(false) control selects single-command submission with the
same cached locations, resources and image semantics; it is not a guest setting.
The additional shader is cleaned up on ordinary shutdown and its name forgotten
without GL calls after actual context destruction, just like existing resources.

## Barriers, counters and recovery

Copy sources retain their original owned snapshots and upload behavior. Queue/
byte capacity checkpoints and explicit CpuRead still synchronize as before;
batching neither suppresses necessary readbacks nor adds new ones. In particular,
this does not fix the cost of repeated full-target capacity checkpoints for an
arbitrary large interactive workload. G4a composition continues to use resident
textures without a CPU frame readback merely for composition.

Recovery replays the original individual journal entries, including already
submitted batches, in CPU order over the last valid checkpoint. A failed batch
never advances the flushed prefix or discards recovery data. This is bounded
logical recovery, not an automatic detector for unsignaled context loss.

Counters are attempted GL API work, never elapsed GPU execution:

* draws now explicitly counts GL draw calls (single or instanced).
* submittedCommands counts logical commands represented by attempted draws.
* batches counts attempted instanced fill groups; batchedCommands counts their
  original entries. Singles/copies remain outside these batch counters.
* uploads/readbacks/checkpoints/recoveries retain G3 semantics.

Zero pending work causes zero new draws. CPU fallback does not increment GPU
work counters. Ordinary non-GL builds continue to report GPU unavailable.

## Reproduction and comparisons

```
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

The native benchmark builds the real GL code with the same-build reference and
batched controls. Each process checks four warm-up cycles then measures 32 offscreen
128x128 cycles. Every measured cycle enqueues 64 logical commands, submits, performs
an explicit synchronous CPU read barrier, and independently checks every pixel.
Oracle/hash work and shader/context setup are outside measured phases. Report
field frames is an offscreen checked-cycle count, not display/scanout frames.

Three workloads provide deterministic work-count gates:

| Workload | Reference draws /32 cycles | Batched draws | Copies/uploads | Meaning |
| --- | --- | --- | --- | --- |
| tiles |2048|32|0|64 disjoint differently colored fills per cycle |
| mixed |2048|96|32|One owned-source copy separates two disjoint fill groups |
| overlap |2048|2048|0|All 64 fills overlap: order is preserved, no batching |

All modes attempt 2048 original commands and 32 readbacks. Source mutation after
Copy cannot change accepted pixels. The report runner checks exact counters,
pixel signatures and paired hashes. It excludes an additional complete process
warm-up per condition, alternates condition order across seven repetitions and
retains 42 raw measured samples, medians and min/max ranges. Wrong executable,
failed assertions, invalid JSON or count/pixel mismatch removes/prevents fresh
success evidence. Timing is never a CI pass threshold.

Host wall phases are enqueue, GL submission and explicit read barrier. Submission
can include driver waits and lazy work; the read barrier includes queued GPU work
and transfer/CPU checkpointing. Their sum excludes oracle/setup/serialization and
is not exclusive CPU time, a GPU timer, guest x86 time or full application time.
The host uses EGL/Mesa software GL. No ratio is Switch FPS or physical GPU speed.
Overlapping-work control may show timing noise despite identical draw work.

The report build-host/gpu_gdi_batches.json includes source SHA/dirty state,
runner SHA256, platform/timestamp, all samples and comparison summaries. CI host
publishes stage-gpu-gdi-batches with the report, log and this contract. Existing
gpu-reference job independently runs the strict O2 benchmark. There are still
six jobs; clean exact-SHA CI evidence is distinct from local precommit dirty data.

Native CPU GDI/EGL regression also checks negative clipping, adjacent and
intersecting fills, copy barriers, exact batch/command counts, repeated flush,
error recovery after earlier batches and actual context destruction with a
flushed instanced batch plus pending copy. G4a's multi-target/direct presentation,
CPU barriers, lifetime and fallback oracle remains mandatory. Full host/MinGW/
Mesa/CPU/filesystem/USER lifecycle tests remain unchanged gates.

## Next stage boundary

G4b.2 still needs window/DC identity binding, raw-DIB/CPU-ROP/read barriers and
state ownership for the existing presenter context. Current dedicated-context
prototypes are not connected to live HWNDs, native Luna or ShellSession canonical
CPU composition. A real texture-aware production bridge and actual-x86 public
regressions must precede activation; simply adding GPU Fill while reading every
frame back or repeating all CPU work would not establish an optimization.
Physical Paint/GPU/fallback/input/relaunch acceptance remains deferred. Local
Switch SDK remains unavailable; exact-SHA NRO builds come from CI. G2 CPU fast
copy and Claude Luna are preserved; no Pinball/installer compatibility is added.


One local full-suite O1/Mesa run observed median measured-cycle phase sums
14.25ms reference /4.20ms batched for tiles,11.71/6.75ms mixed. These aggregate
32 offscreen checked cycles. Overlap control retained identical work counts
while timing varied36.49/33.98ms; do not interpret that difference as a batching
win. The clean exact-SHA CI artifact's raw distributions supersede these rounded
development observations, and no console speedup follows from them.
