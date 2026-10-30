# D5 — real bounded legacy Win32 file I/O

This common compatibility package follows the observed missing KERNEL32!OpenFile
import, with a public reproducer independent of proprietary Pinball. It does not
certify that game, MIDI, sound, main-frame rendering or playability.

## Implemented behavior

Nine real stdcall exports: OpenFile(12), _lopen(8), _lcreat(8), _lclose(4),
_lread/_lwrite/_hread/_hwrite/_llseek(12 each). Handles refer to the existing
Process-owned kernel File objects; there is no separate untracked FILE map.
CloseHandle, DuplicateHandle, ReadFile/WriteFile and FlushFileBuffers interoperate
with them, including access rights, protected close, shared cursor and aliases.
Handle values are opaque 32-bit HFILE values; only HFILE_ERROR (-1) is failure.
Do not infer failure from the runtime's reserved high-bit HANDLE namespace.

OpenFile validates the complete 136-byte OFSTRUCT before any filesystem action.
It reads an aliased input/reopen path before writing outputs, sets cBytes=136,
fFixedDisk from the actual mounted drive type and nErrCode on failure. A full
absolute ANSI path must fit 127 bytes plus NUL; no truncating the returned reopen
path to create/open some other file. OF_PARSE returns0 and fills the structure
without opening/creating the file. OF_EXIST really opens and closes, returning1.
OF_DELETE really deletes a regular file and returns1. OF_REOPEN takes its path
from the same OFSTRUCT. OF_CREATE creates/truncates a read-write regular file;
ordinary READ/WRITE/READWRITE enforce actual rights. OF_CANCEL is ignored as
specified. File creation occurs only after all mapped-output/path/style checks.

A bare OpenFile name searches the application directory, CWD, C:\WINDOWS\system32,
C:\WINDOWS\system, C:\WINDOWS, then the Process-owned PATH (bounded32KiB).
Explicit paths and OF_REOPEN use the supplied path; _lopen uses CWD without the
OpenFile search. OF_CREATE/OF_PARSE resolve relative to CWD. Search does not change
native/global cwd. Mount validation, case lookup, readonly R and host symlink
rejection are the shared VFS rules. ANSI filenames/OFSTRUCT use actual CP1252
conversion to/from native UTF8; unrepresentable returned paths refuse1113.
PATH uses the existing environment's ANSI byte representation; full environment
A/W encoding unification and general Windows search policies are outside scope.

Legacy and modern transfers share the existing checked8KiB scratch implementation,
range/32-bit wrap validation, access checks, actual byte counts and I/O errors.
_lread/_hread return bytes,0 at EOF,-1 on failure; signed _hread/_hwrite reject
negative lengths. No request-sized native allocation or borrowed guest pointer.
A zero-byte legacy write sets EOF at the current cursor through the real checked
flush/ftruncate path: it truncates or extends with filesystem-provided zero fill.
Zero-byte modern WriteFile remains an ordinary zero transfer. _llseek accepts
signed relative offsets/BEGIN/CURRENT/END, preserves position on invalid origin/
negative/out-of-subset target and supports nonnegative signed32 positions (<=2GiB-1).
It does not fabricate successful large-file legacy positioning. Native disk I/O
is synchronous, not a bounded latency/async/page-protection mechanism.

## Explicit remaining boundaries

- OF_SHARE_COMPAT / OF_SHARE_DENY_NONE are admitted. Deny/exclusive share modes
  refuse ERROR_NOT_SUPPORTED: existing CreateFile/CRT lack common share locks.
  No fake exclusivity across independent openers/processes.
- OF_VERIFY timestamp verification and OF_PROMPT UI refuse ERROR_NOT_SUPPORTED.
  Unknown flags/Win16 search requests refuse; invalid access3/conflicting actions
  fail ERROR_INVALID_PARAMETER. Reserved OFSTRUCT timestamp words stay zero.
- _lcreat supports normal attributes0; hidden/system/readonly attributes refuse
  until real shared attribute behavior exists. Device/pipe/directory legacy opens
  are outside the regular-file contract. Wildcards/invalid name characters refuse.
- Close releases a handle reference. As in [F5](F5_FILE_SAVE.md), durable save
  success requires explicit checked FlushFileBuffers; final RAII close is best
  effort, not a power-loss/SD transaction guarantee. General share/delete locks,
  complete Win16, UNC/network/device semantics and full file API coverage remain.

## Public evidence

`tests/guest/legacy_file.c` builds CRT-free x86 legacy_file.exe. Its native inspector
runs four normal exit0 sessions and one genuinely parked Sleep(INFINITE) abort.
It checks >8KiB authored bytes across mixed _l/_h reads, EOF/seek, actual truncate/
extend bytes, OFSTRUCT/reopen/exist/delete/parse, application-before-CWD search,
_lopen CWD, system/PATH, CP1252 café, readonly errors, alias lifetime, unsupported
styles and invalid mapped buffers. Unclosed read/deleted-file descriptors expire
with the owning GuestSession after continuations drain; abort is not normal exit.
Fresh sessions reopen persisted bytes independently. No private assets are used.

`test_legacy_file` additionally checks exact stdcall stack cleanup, OFSTRUCT
canaries/short mapped ranges, 127/128-byte path limit, unterminated reopen input,
CP1252 Euro native filename, symlink rejection, wrong object/protected close and
real /dev/full failure. Existing modern F5 file-save tests remain required because
they exercise the same shared transfer/truncation code. Negative premature EOF
and zero-write-no-truncate mutations must fail the public byte oracle.

The existing host CI job publishes stage-d-legacy-file (PE/native+x86 logs/public
source/this contract), retaining all six jobs and previous audio/GPU artifacts.
Full host/MinGW/Mesa, strict changed modules, focused ASan/UBSan and exact-SHA
CI/NRO/Pages/source-manifest audits are publication gates. Local Switch SDK is
absent; physical Paint/GPU/input/fallback/relaunch/sound remain deferred. SD closed.

Next audio work remains actual multi-source mixing/per-source state, followed by
target-version DirectSound/MIDI work. Other D compatibility blockers require
public reproducers. F6c player effects/double-click remain unchanged.

## API references

- [Microsoft OpenFile / OFSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-openfile)
- [Microsoft _lwrite](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-_lwrite)
- [Microsoft _llseek](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-_llseek)
