# Stage F completion ledger

User instruction 2026-10-25: finish the remaining F work as one objective, rather
than stopping after each small feature. Baseline `b0858f0` is clean and its exact
CI 40136047193 passed all six jobs. This ledger records real gates; it does not
award completion to future work or replace physical Switch acceptance.

## Programmatic F baseline

| Gate | Concrete behavior | Programmatic result |
| --- | --- | --- |
| F0 | Fresh one-Process guest scopes, launch/normal exit/error/abort/return | Implemented, regressions exist |
| F1 | Persistent C/S/R, real profile/space/Unicode/readonly paths | Implemented, regressions exist |
| F2 | Native desktop/Start/taskbar and real actions | Implemented with real persisted Desktop/Start links |
| F3 navigation | Directory list, history/address/roots/sort and bounded directory tree | [List and bounded real tree implemented](F3C_SHORTCUTS_TREE.md) |
| F3 state | Persisted validated settings and document associations | Sort/associations and management implemented; scope audited |
| F3 shortcuts | Persisted target/arguments/CWD, actual Desktop/Start discovery and validated launch | [Implemented Unicode local .lnk subset](F3C_SHORTCUTS_TREE.md) |
| F4 input/lifetime | Native service independent of guest USER, real owner commands, one output across sessions | Implemented, public busy/session regressions exist |
| F4 layers | Retained native + guest planes, exact CPU/GL ownership/fallback oracle, owned replay | [Implemented retained integration](F4C_WORKSPACE_LAYERS.md); canonical software fallback retained |
| F5 save correctness | Real guest read/write/flush/fsync/size/truncate errors, HANDLE access/alias lifetime | [Implemented synchronous subset](F5_FILE_SAVE.md), real storage/access/alias faults covered |
| F5 whole workflow | Public guest Start -> save -> return -> find -> reopen after fresh frontend, supported failures | Public GDI save/find/native-view + fresh Desktop/Start/Run x86 reopen with args/CWD; combined report implemented |
| Regression/artifact | Full host/MinGW/Mesa, exact-SHA six-job CI, revision-matched NRO and current documents | Required after every completed code package |

All baseline programmatic gates above are implemented and passed the complete
clean host/MinGW/Mesa suite. `build-host/stage-f-acceptance.json` records 22 real
evidence groups, public EXE hashes, tested revision/dirty state and explicit
pending hardware. The runner emits it only after the full suite succeeds; missing
case/fixture evidence fails closed and stale reports are removed. CI publishes
`stage-f-software-acceptance`; verify its exact SHA and clean-tree field alongside
all six jobs and the NRO embedded revision.

**Status: PASS_SOFTWARE_PENDING_HARDWARE**, within the explicit bounded contracts.
This is completion of the F0–F5 software baseline, not physical F acceptance,
universal EXE compatibility or the planned installer/archive extension. Supported
settings are the actual sort/association controls; arbitrary Windows Control Panel,
per-app state, themes/input remapping and shell COM are outside this baseline.
TXT/BMP/links/tree/one-Process limits remain in their linked contracts.

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


## Reproduction and device handoff

Run `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` with MinGW and Mesa. Read
`build-host/stage-f-acceptance.json`; local uncommitted runs explicitly set
working_tree_dirty=true and cannot certify a published SHA. CI produces the clean
exact-SHA report and generated public fixtures in stage-f-software-acceptance.

For the deferred physical milestone use the NRO of that exact SHA and the public
shell_bitmap.exe plus existing Stage C/E fixtures. From Start/Run launch the
public writer with an explicit document path, return to My Documents and view
its BMP, create real shortcuts from Explorer, relaunch from Desktop/Start and
repeat normal/error/abort transitions. Current Paint/SD/GPU/fallback/Lite/input
checks stay in the existing consolidated checklist; do not fabricate their result.
The public writer arguments are `write <absolute-document-path>` and reader
arguments `read <absolute-document-path>`; writes use CREATE_NEW and do not
overwrite old documents. The extra `read-cwd` mode is a host fixture assertion
for C:\TEMP and special-folder/environment transport, not a general viewer.
