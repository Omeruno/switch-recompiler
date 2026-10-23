# Active implementation map — 2026-10-23

Use current branch commits and [AI_HANDOFF.md](AI_HANDOFF.md) before the historical
queues in ROADMAP.md and STAGE_E_PLAN.md. Their architecture/stage criteria remain
references; this short map is the current implementation queue.

| Block | Implemented software | Still required |
| --- | --- | --- |
| E6 | Retained window raster, ownership reconciliation, GL layers and 28-scene CPU/GL oracle | Physical output acceptance |
| E7 | Revision-driven snapshots, bounded optional caches, unchanged-tile work tests | Console workload evidence |
| E8 | CPU raster/upload/draw, asynchronous GPU intervals, transfer/memory counters, bounded real-clock input/output correlation (report v7) | Console CPU/GPU comparison, total driver/process memory and physical input-to-display latency |
| E9 | Six unattended software groups in one Process, fixture SHA and fail-closed report, CI artifact | Actual Switch GPU/fallback and real Paint smoke |
| F0 | Bounded restartable picker; per-launch output/mode/input ownership; owned core/new USER/legacy UI+MFC sessions; sequential x86 regressions | Physical picker/session/GPU/fallback/exit-failure-relaunch smoke |
| G0 | Bounded opt-in opcode/host-call census and deterministic public native x86 fixture; see [STAGE_G_PROFILING.md](STAGE_G_PROFILING.md) | Representative public PE phase measurements and profiling overhead before optimization; console evidence deferred |

E8/E9 hardware acceptance remains deferred. Do not award final graphics-stage
percentages or claim Switch speedup from host tests. The consolidated checklist
and measurement definitions are in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
The combined fixture instructions are in [STAGE_E_ACCEPTANCE.md](STAGE_E_ACCEPTANCE.md).

Production launch-return-picker integration is implemented. See
[F0_LAUNCH_LOOP.md](F0_LAUNCH_LOOP.md) for failure recovery, cleanup order and
host-test limits. Session foundations and regressions remain documented in
[F0_SESSION_CORE.md](F0_SESSION_CORE.md), [F0_GUI_SESSIONS.md](F0_GUI_SESSIONS.md)
and [F0_LEGACY_SESSIONS.md](F0_LEGACY_SESSIONS.md). Physical relaunch is unaccepted.

Next concrete software step: representative public PE workload measurements with
independently checked results, startup/steady phases and enabled/disabled overhead.
G0's synthetic fixture establishes counting correctness, not application hotspots.
See [STAGE_G_PROFILING.md](STAGE_G_PROFILING.md). Do not block on hardware smoke.

After F0, choose a small reachable desktop/launcher milestone in the one-Process
model. Stage G CPU profiling/dynarec and Stage H DirectDraw/D3D remain separate
programs. Refer to ROADMAP.md for their global scope, not its historical next-E1
queue. Continue independent software work without waiting on physical checks.

## Implementation order and gates

1. G0 measurement foundation, then public PE workload/phase measurements. Select
   one small fast path from measured work; preserve CPU oracle, SEH and all six CI
   gates. Host frequency alone cannot select the most expensive instruction.
2. Keep F0 reliable while defining one small reachable host-shell milestone in
   the one-Process model: launcher/catalog and file navigation before taskbar,
   shortcuts and larger desktop integration. Do not mix shell with CPU backend.
3. Extend D compatibility only for reproduced application failures, with public
   minimal guests; keep startup/DLL/TLS/SEH teardown and session isolation intact.
4. Dynarec is a separate G program: block cache/invalidation, exact flags/faults,
   executable memory/icache lifetime, interpreter fallback and differential tests.
   The accepted minimal ARM64 probe is a prerequisite already met, not full JIT.
5. H starts separately with bounded DirectDraw 2D surfaces/palette/flipping, then
   chooses a D3D generation from actual application needs and measurements.
6. Consolidated physical E8/E9/Paint/input/fallback/relaunch acceptance stays
   deferred. Final CPU speedup needs matching workloads on Switch, not host CI.

Every implementation package updates this map and AI_HANDOFF.md, runs its required
checks, publishes only to claude/wonderful-hawking-10mpr0 without force, and checks
all six jobs for that exact SHA. A passing older SHA is not a new package result.
