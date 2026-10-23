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
| F0 | Bounded startup picker; owned core Process/memory/HLE state; sequential x86 exit/abort/restart regression | GUI/legacy resource isolation before launch-return-launch; physical picker/session smoke |

E8/E9 hardware acceptance remains deferred. Do not award final graphics-stage
percentages or claim Switch speedup from host tests. The consolidated checklist
and measurement definitions are in [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
The combined fixture instructions are in [STAGE_E_ACCEPTANCE.md](STAGE_E_ACCEPTANCE.md).

Next concrete step: audit/isolate GUI and legacy window/class/menu/image/GDI
resources, then prove sequential real-x86 window and dialog sessions with normal
exit and host cancellation. Core Process/memory/heap/CRT/file state is now owned
per session and tested at reused native and guest addresses; see
[F0_SESSION_CORE.md](F0_SESSION_CORE.md). The NRO still exits after one guest:
return-to-launcher remains gated on GUI teardown. The bounded picker is described
in [F0_LAUNCHER.md](F0_LAUNCHER.md). No desktop-shell or multiprocess completion is
implied by this work.

After F0, choose a small reachable desktop/launcher milestone in the one-Process
model. Stage G CPU profiling/dynarec and Stage H DirectDraw/D3D remain separate
programs. Refer to ROADMAP.md for their global scope, not its historical next-E1
queue. Continue independent software work without waiting on physical checks.
