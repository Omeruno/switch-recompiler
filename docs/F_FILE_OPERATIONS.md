# Functional F: shared file-operation foundation

Based on `975f393` (CI run 40085203929, all six jobs successful). This package
implements real storage operations and browser model APIs. The follow-up [native file actions](F_FILE_ACTIONS.md) now exposes folder/name
entry, delete confirmation and properties. The original foundation alone did
not deliver that end-to-end shell workflow; context menus/copy remain open.

## Shared storage contract

`rt::Vfs` now supplies CreateDirectory, DeleteFile, RemoveDirectory, Move and
QueryProperties. ShellBrowser, KERNEL32 A/W delete/create/remove/move and CRT
remove/mkdir/rmdir/rename use those same operations over the F1 mappings. They
retain the F1 case resolver, per-drive cwd, UTF-8 host paths, readonly intent,
linked-component rejection on Linux and optional cumulative case-search budget.
Ordinary guest calls retain the existing unbudgeted resolver policy.

- Creation creates exactly one directory; it does not recursively manufacture
  missing parents, choose an automatic name or overwrite an existing object.
- DeleteFile and CRT remove reject directories. The old POSIX remove call could
  remove an empty directory; that incorrect behavior is eliminated.
- RemoveDirectory deletes only an empty directory. No recursive delete, recycle
  bin or success-with-no-storage-effect is implemented.
- Move renames real files/directories. An existing destination, including another
  spelling of the source's name, fails by default. Explicit replacement (used by
  MoveFileEx with REPLACE_EXISTING) is limited to regular files. Case-only rename
  is currently an explicit EEXIST result, not a successful no-op. Cross-device
  failure propagates; there is no implicit copy/delete fallback.
- Mutations reject mounted roots and their ancestors, even through overlapping
  C/S aliases. Metadata remains mounted at its configured backing location.
- Errors propagate through errno, existing Win32 error mapping or CRT errno.
  Win32 adds explicit disk-full, cross-device, I/O and busy mappings. This does
  not implement all MoveFileEx flags, NT sharing or complete Windows file APIs.

The no-replace check followed by native rename is **not atomic against another
native writer**. F1 trusted-root/symlink race limitations remain. These calls do
not yield into another guest while mutating. Access rights of already-open
handles, inode aliases/hard links and renaming a current directory still have
the existing host-backed limitations; this is not an NT filesystem/security model.
[Checked CopyFile](F_FILE_COPY.md) now shares the provider and browser model,
with staged publication and checked I/O. [Native single-file Copy/Paste](F_FILE_PASTE.md) is now connected; unsupported
directory copying and overwrite controls are not advertised.

## Properties and browser model

FileProperties is a fresh stat snapshot: file/directory kind, regular-file byte
size, native modification/access timestamps in epoch seconds, mount readonly
policy and the native owner-write mode bit. Directory size is zero meaning
**not recursively measured**; it is not a claim of empty contents. There is no
invented Windows creation time, physical capacity or effective-write permission.
A failed query clears the previous snapshot. Volume capacity remains F1's
separate actual statvfs query with its explicit ROMFS limit.

ShellBrowser adds CreateFolder, RenameSelected, DeleteSelected and
SelectedProperties. Operations re-resolve/revalidate the selected path; cached
listing size/type is not authority. Creation/rename accepts one name of up to
255 UTF-8 bytes, rejecting separators, control characters, Windows punctuation,
trailing spaces/dots and dot traversal. This is a conservative shell name policy,
not a complete Win32 Unicode/reserved-device-name implementation.

After mutation, browser refreshes and retains the new selection where present
in the bounded listing. A three-way result distinguishes failure, applied and
applied-with-stale-view. A listing failure after an actual rename/delete must
never encourage retrying an already-applied operation. OperationErrno describes
the mutation; LastError can describe the later refresh. History and associations
are not rewritten to follow renamed objects. There are no retained native file
handles or guest pointers. DeleteSelected is permanent; the native UI caller now obtains an explicit
user deletion confirmation before executing it.

## Checks and next package

`test_file_operations` uses real temporary overlapping C/S and readonly R mounts.
It compares bytes/stat timestamps, tests Unicode/case resolution, missing paths,
collisions/replacement, wrong object type, nonempty directories, roots/aliases,
symlinks, readonly paths, lookup exhaustion and changed/stale browser selections.
The public `filesystem_mounts.c` fixture also exercises real x86 KERNEL32 A/W and
CRT mutations, rejected directory-as-file deletion, rename collision and actual
file bytes in each of its four isolated sessions. The complete
`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` and exact published-SHA six-job
CI remain required. No proprietary fixture is introduced.

The native-action follow-up connects these operations, confirmation and properties
with pointer/controller routing. Native Copy/Paste now uses the common resumable engine; next:
right-click/context actions. Then persisted preferences/associations,
TXT/images and F5 save -> find -> reopen. Luna polish and G optimization remain
deferred. Local Switch SDK is unavailable; CI compiles the console build.
Physical Paint/GPU/fallback/input/relaunch acceptance remains deferred, SD closed.
