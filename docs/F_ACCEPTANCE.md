# Stage F completion ledger

User instruction 2026-10-25: finish the remaining F work as one objective, rather
than stopping after each small feature. Baseline `b0858f0` is clean and its exact
CI 40136047193 passed all six jobs. This ledger records real gates; it does not
award completion to future work or replace physical Switch acceptance.

## Programmatic F baseline

| Gate | Concrete behavior | State at start of closure work |
| --- | --- | --- |
| F0 | Fresh one-Process guest scopes, launch/normal exit/error/abort/return | Implemented, regressions exist |
| F1 | Persistent C/S/R, real profile/space/Unicode/readonly paths | Implemented, regressions exist |
| F2 | Native desktop/Start/taskbar and real actions | Implemented; persisted user shortcuts need completion |
| F3 navigation | Directory list, history/address/roots/sort and bounded directory tree | List implemented; tree open |
| F3 state | Persisted validated settings and document associations | Sort/associations implemented; final scope audit open |
| F3 shortcuts | Persisted target/arguments/CWD, actual Desktop/Start discovery and validated launch | Open |
| F4 input/lifetime | Native service independent of guest USER, real owner commands, one output across sessions | Implemented, public busy/session regressions exist |
| F4 layers | Retained native + guest planes, exact CPU/GL ownership/fallback oracle, owned replay | Open; current shell path flattens and disables window layers |
| F5 save correctness | Real guest read/write/flush/fsync/size/truncate errors, HANDLE access/alias lifetime | [Implemented synchronous subset](F5_FILE_SAVE.md); integrated acceptance pending |
| F5 whole workflow | Public guest Start -> save -> return -> find -> reopen after fresh frontend, supported failures | TXT/BMP slices exist; combined acceptance pending |
| Regression/artifact | Full host/MinGW/Mesa, exact-SHA six-job CI, revision-matched NRO and current documents | Required after every completed code package |

Closure order: real file-save APIs and faults; tree/persisted shortcuts and scope
of settings; retained workspace composition/oracle; integrated F acceptance and
final map audit. Keep all open gates visible throughout this task. Do not return
a final “F complete” based solely on the first completed package.

## Separate physical acceptance

Current native shell, Paint save/find/reopen, GPU/fallback, Lite/touch/controller
and repeated launches require the existing consolidated Switch checklist in
[STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md). They remain deferred. No local
devkitA64/libnx is available; exact-SHA CI compiles Switch adapters. No SD diagnosis.

## Planned extension after the baseline

[Installer and archive import](F_INSTALL_AND_ARCHIVES.md) is explicitly a planned
extension after baseline F5: real supported installers, program folders and
shortcuts, ZIP/RAR/other formats. Its D/COM/child-process/format dependencies are
not silently moved into “implemented” by baseline shell acceptance. The request
to finish F does not authorize success stubs or claiming universal installation.
Keep this extension visible and report its actual state separately. Full Windows,
multiple guest Processes, DirectDraw/D3D and G optimization are separate programs.
