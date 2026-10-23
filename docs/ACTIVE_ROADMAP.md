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

E8/E9 hardware acceptance remains deferred. Do not award final graphics-stage
percentages or claim Switch speedup from host tests. The consolidated checklist
and measurement definitions are in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
The combined fixture instructions are in [STAGE_E_ACCEPTANCE.md](STAGE_E_ACCEPTANCE.md).

Production launch-return-picker integration is implemented. See
[F0_LAUNCH_LOOP.md](F0_LAUNCH_LOOP.md) for failure recovery, cleanup order and
host-test limits. Session foundations and regressions remain documented in
[F0_SESSION_CORE.md](F0_SESSION_CORE.md), [F0_GUI_SESSIONS.md](F0_GUI_SESSIONS.md)
and [F0_LEGACY_SESSIONS.md](F0_LEGACY_SESSIONS.md). Physical relaunch is unaccepted.

Next concrete software step: bounded interpreter opcode/host-call workload
profiling and a repeatable public fixture measurement for Stage G. Establish
where work occurs before selecting an optimization; console performance remains
unmeasured. Do not block this independent work on hardware smoke.

After F0, choose a small reachable desktop/launcher milestone in the one-Process
model. Stage G CPU profiling/dynarec and Stage H DirectDraw/D3D remain separate
programs. Refer to ROADMAP.md for their global scope, not its historical next-E1
queue. Continue independent software work without waiting on physical checks.
