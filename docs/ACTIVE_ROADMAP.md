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
| F0 | Bounded startup picker; owned core/new USER/legacy UI+MFC sessions; sequential x86 window/dialog/core/legacy restart regressions | Frontend launch-return-picker integration and physical picker/session smoke; physical picker/session smoke |

E8/E9 hardware acceptance remains deferred. Do not award final graphics-stage
percentages or claim Switch speedup from host tests. The consolidated checklist
and measurement definitions are in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
The combined fixture instructions are in [STAGE_E_ACCEPTANCE.md](STAGE_E_ACCEPTANCE.md).

Next concrete step: connect a bounded one-Process frontend launch/return/picker
loop, with per-launch layer/output rebind, failed-load recovery and launch-held
input quarantine in both UI modes. Lifetime foundations and their regressions:
[F0_SESSION_CORE.md](F0_SESSION_CORE.md), [F0_GUI_SESSIONS.md](F0_GUI_SESSIONS.md),
[F0_LEGACY_SESSIONS.md](F0_LEGACY_SESSIONS.md). The NRO still exits after one
guest; the launch loop itself and physical relaunch are not yet accepted. No
multiprocess or desktop-shell completion is implied by these ownership changes.

After F0, choose a small reachable desktop/launcher milestone in the one-Process
model. Stage G CPU profiling/dynarec and Stage H DirectDraw/D3D remain separate
programs. Refer to ROADMAP.md for their global scope, not its historical next-E1
queue. Continue independent software work without waiting on physical checks.
