# F1: persistent directory-backed guest volumes

This package binds the existing Switch picker/guest launch to stable storage;
it does not implement the desktop, taskbar, Start menu or explorer. Those remain
F2–F5 in [F_SHELL_PLAN.md](F_SHELL_PLAN.md). No console/Paint acceptance is added.

## Actual layout and launch policy

Every production Switch GuestSession configures its own mount/cwd metadata over
the same backing directories:

| Guest view | Native storage | Access |
| --- | --- | --- |
| `C:\` | `sdmc:/switch-recompiler/drive_c/` | Read/write persistent system/user tree |
| `S:\` | `sdmc:/` | Read/write existing SD files |
| `R:\` | `romfs:/` | Read-only packaged resources/EXEs |

Configuration creates only missing directories: Programs, WINDOWS/system32,
TEMP, Shared Documents and `Documents and Settings/Player` with Desktop,
My Documents/My Pictures, Application Data and Local Settings/Application Data.
Existing files, EXE directories, registry.dat and stable NROs are not moved.
A file blocking a required directory produces initialization failure. A failed
mount setup does not replace the session's previous mount metadata. Already
created missing directories may remain after a failed setup; setup is additive,
not a filesystem transaction or migration.

C's root is independent of EXE directory. A selected old SD EXE such as
`sdmc:/switch-recompiler/Game/game.exe` sees its own directory as
`S:\switch-recompiler\Game` and keeps that as CWD; relative files and adjacent
DLLs still resolve there. Absolute C paths now refer to the shared system tree.
Existing saved absolute C paths from the old per-EXE mapping may need reselection
through S; no ambiguous fallback rewrites them. The picker also discovers EXEs
under `drive_c/Programs` and one subdirectory, within its existing global budgets.
ROMFS-first/old-SD ordering and L/ZL mode selection remain unchanged.

Loader module names and GetCommandLine now expose the mapped path with its
subdirectories, rather than C plus basename. LoadLibrary with a guest path uses
the same resolver; bare DLL names retain EXE/search-directory lookup. HLE modules
still report their virtual C:\WINDOWS\system32 names; no proprietary system DLLs
are installed. Each new session gets fresh cwd/env/handle metadata but the saved
file bytes survive. Env USERPROFILE/HOMEDRIVE/HOMEPATH/APPDATA and Shell32 special
folders agree with the profile tree; guest environment edits remain session-local.

## Resolver and write protection

There are at most 26 mounts. Paths are limited to 4096 bytes; each drive has its
own remembered cwd. Relative, drive-relative, rooted, `.`/`..` and mixed separators
normalize through one resolver. Unknown drives fail instead of aliasing C.
`..` at a drive root clamps there. UNC/device/alternate-stream syntax is not
supported and is rejected. Existing components use case-insensitive lookup.
UTF-16 APIs convert to/from UTF-8 host paths; full-path lengths/file-part pointers
use UTF-16 code units for W APIs. This does not claim a complete Windows ACP or
long-path/NT namespace implementation.

Mount read-only intent is checked before CreateFile creation/write/delete-on-close,
CRT writable/update opens, Win32 delete/rename/copy destination/mkdir/rmdir,
CRT remove/rename/mkdir/rmdir and temp creation. Unknown-device/invalid-path/
write-protected failures are reported without calling host mutation functions.
An already-open writable descriptor retains its checked access. The immutable
ROMFS device also rejects writes at the host layer.

On Linux, linked descendant components are rejected for guest path resolution;
configured root paths are explicitly trusted and canonicalized. No global native
chdir occurs. FAT/ROMFS do not have host symlinks. This is not a race-proof security
sandbox against another native process changing directories/links, and the
explicit S mount intentionally exposes the SD root.

## Honest volume information

All supported ANSI/Unicode drive/volume queries share the mount table. Logical
drive strings contain C/R/S and correct multi-string terminators. Free-space Ex
returns actual statvfs available/total/free bytes; libnx fsdev implements statvfs
using fsFsGetFreeSpace/fsFsGetTotalSpace. Legacy APIs report cluster counts rounded
down with reporting geometry chosen to fit DWORD counters. Geometry is not a
measurement of physical SD sectors. Values from independent queries can change.

Volume labels are logical SYSTEM/SD/ROMFS. Filesystem identifiers are provider
names HOST/SDMC/ROMFS, not an invented NTFS/FAT/exFAT detection. Physical serials
are not fabricated. ROMFS reports read-only with zero available/total/free space
because its device does not provide a capacity query here; zero is not a measured
ROMFS image size. This remaining reporting limit is explicit.

## Host reproduction

Ordinary winrun/tests keep their one-C-per-EXE compatibility default. To exercise
the production storage policy with directory-backed volumes:

```sh
./build-host/winrun --system-root /tmp/sr-sd/switch-recompiler/drive_c \
  --sd-root /tmp/sr-sd --romfs-root /tmp/sr-rom \
  /tmp/sr-sd/Apps/example/example.exe
```

Create the sd/rom directories and supply a public EXE first. Omitting optional
sd/rom roots mounts only C, so the EXE must be inside C. Supplying sd/rom options
without system-root fails with usage status. Existing EXEs outside selected
mounts fail loading; they are not silently remapped.

Required regression command:

```sh
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

`test_vfs_mounts` validates C/S/R round-trips, most-specific C mapping inside S,
per-drive cwd, root traversal, unknown/UNC/stream/long/linked paths, Unicode/case,
read-only resolution, actual host capacity and repeatable non-destructive setup.
`filesystem_mounts.exe` is a public MinGW source fixture, not a Microsoft binary.
`test_filesystem_sessions` places it and a sibling real DLL into two SD directories
and runs writer → fresh reader → host-aborted writer → fresh reader in one host
process. It checks actual host Unicode document bytes, clean env/cwd, DLL and
module/command-line paths, A/W drive/volume/profile APIs, write-protected Win32/
CRT operations, actual Unicode Save/Open selection/title/extension offsets,
refused ROMFS save followed by cancellation, failed-load teardown and continuation
cleanup. The ordinary guest
loop skips this fixture because it requires explicit mounts; its dedicated
harness must execute and pass. The rest of the existing suite still runs.

Local validation and exact-SHA CI are recorded in AI_HANDOFF.md/commit reporting.
Switch compilation is a separate gate; physical SD/Paint/GPU/fallback/input/
relaunch checks remain consolidated and deferred in STAGE_E_PERFORMANCE.md.
