# D3 — real bounded INI/profile files

Implemented 2026-10-28 after D2. Common VFS-backed behavior replaces default-only
reads and successful no-op writes. No application-name exception, private binary,
ABI-only signature or synthetic successful storage is added.

## API surface and storage

Sixteen real stdcall KERNEL32 exports: A/W pairs of GetPrivateProfileString,
GetPrivateProfileInt, WritePrivateProfileString, GetProfileString, GetProfileInt,
WriteProfileString, GetPrivateProfileSectionNames and GetPrivateProfileSection.
The legacy GetPrivateProfileIntW/StringW implementations and old profile-module
registrations are removed so the same provider serves both frontends.

Profile APIs and NULL filenames use `C:\WINDOWS\win.ini`. A bare private filename
uses `C:\WINDOWS`; explicit drive/root/relative-directory paths use the existing
VFS rules. C/S/R mount policy, case lookup, linked-path rejection and readonly
volumes apply. Parent directories are not invented by a read or write. Each call
loads actual file contents; no cache/guest pointer/native descriptor survives a
call or a fresh guest session. Missing files/keys return supplied defaults;
I/O/encoding/size/path failures return0 with LastError. Successful calls preserve
LastError. Zero-capacity reads return0 without dereferencing inputs/output.

ASCII case-insensitive section/key lookup, surrounding spaces/tabs, semicolon
comment lines, empty values, matching single/double value quotes and default
trailing-space trimming are supported. Unknown/unparsed text lines are preserved
on edits. Enumeration returns unique names in first occurrence order, or actual
`key=value` entries, with double-NUL termination and nSize−2 truncation (including
the exact-fit list boundary). Scalar truncation uses nSize−1 and one NUL.
Positive-capacity NULL output fails87; unmapped/wrapping actual output fails998
before writing. Input strings have checked mapped ranges and termination bounds.

Integer reads support optional signs, decimal and 0x hexadecimal, stop at the
first nondigit and wrap modulo2^32; absent/empty values return nDefault. Writes
create/update keys, NULL value deletes matching keys, NULL key deletes matching
sections. First matching value wins reads/updates; deletion removes all matching
keys/sections. Repeated section names are logically merged, and enumerations
suppress duplicate names. These bounded duplicate rules are explicit and do not
claim an exhaustive Windows-version oracle. All-NULL write is a checked uncached
flush request: no pending application-side cache exists, all accepted writes have
already completed checked publication.

## Encoding and limits

Files are ASCII bytes or UTF-16LE with FF FE BOM; writes retain that encoding.
W APIs support valid UTF-16, including non-ASCII values in existing BOM files and
Unicode VFS paths. A API text is explicitly ASCII; unrepresentable requested
output/input or attempts to put non-ASCII into a plain file fail1113. No silent
ACP truncation or conversion of existing files. New files are plain ASCII;
create a UTF-16LE BOM file first to store Unicode values. UTF-8/high-byte ANSI,
UTF-16BE, malformed surrogate sequences and embedded NUL files are rejected.
Case folding is ASCII only; non-ASCII code units compare exactly.

Max file1MiB, line/input32768 UTF-16 units,16384 lines, path4096 UTF-8 bytes,
case-search budget4096 and32 exclusive staging names per write. Refusal reports
an error and preserves the old destination. Section/key newlines, ambiguous
section delimiters, key '=' and embedded newline values are refused. Unsupported
IniFileMapping/registry redirection, ACP/localization, full grammar/Unicode case
folding, WritePrivateProfileSection, profile structs and concurrent external
writers remain outside this contract.

## Checked publication

Writes load and validate first, resolve VFS write permission, refuse a readonly
existing file, then create an exclusive sibling staging file. Short writes and
EINTR are handled. Actual write, fsync, close and replacement rename must succeed
before TRUE. No destination unlink/truncate fallback. Failures clean up the owned
staging file and retain original destination bytes. No-op deletion/update avoids
unnecessary rewrites. Successful edits preserve unrelated logical lines/comments,
normalize line endings to CRLF and publish a new native0600 file; metadata/ACLs
are not preserved. Native replacement rename/fsync unsupported on a backend is
an error, never a success. Directory fsync/power-loss-safe SD transactions and
hostile-host race protection are not claimed. Crashed leftover staging files are
not deleted blindly; collision attempts are bounded.

## Reproducible evidence

`COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh` with documented host/MinGW/Mesa
prerequisites builds and runs the public CRT-free `ini_profile.exe`. Existing
host CI job publishes `stage-d-profiles`: PE32, native provider log, four-session
x86 API log and this contract. No private application or trace is published.

- `test_ini_profile`: actual files, comments/case/quotes, deletion, readonly
  file/mount, oversize/overlong/unsupported/malformed encoding and integer cases;
  short I/O/EINTR and read/write/zero-write/flush/read-close/write-close/rename
  failures. Failures retain original bytes and owned staging is cleaned up.
- `test_profile_api` + public `tests/guest/ini_profile.c`: four fresh normal-exit0
  x86 writer/reader/rewrite/reader sessions with16 real A/W exports. Native
  inspection checks actual files, scores, preserved other sections and Windows
  rather than CWD locations; defaults, signed/hex/trailing integers, lists,
  scalar/list truncation, deletion, UTF-16 BOM, unsupported conversion, unmapped
  buffers/input and readonly writes are actual guest assertions.
- Strict O2/no-exceptions/no-RTTI builds, focused ASan/UBSan and negative controls
  complement the full existing C/E/F/G suite. Sanitized modules are the provider,
  API bridge and inspector; other runtime objects are ordinary. Driver leak
  scanning is disabled and the known native-fiber warning is retained.

Windows/Wine profile test expectations were consulted as reference facts; no
third-party implementation is copied and no Windows execution oracle is claimed.
The early development suite exposed a harness setup error: VFS is per Process,
so configuring it before constructing GuestSession does not configure the guest.
The final fixture mounts and loads its owned copied public PE inside each fresh
session, then verifies persisted data externally. No production isolation bypass.

## Next measured dependency

A private host Pinball x86 trace passes GetPrivateProfileIntA and reaches missing
`WINMM!waveOutGetNumDevs`. Next block: audit waveOut/audio device/backend and
callback/buffer/lifetime requirements with a public PCM fixture. A fixed fake
count or successful silent write would not be device/output acceptance. Current
PlaySoundW behavior is separate and does not imply waveOut support. No accepted
Pinball gameplay/main game frame, Switch launch or FPS is claimed. Registry
persistence migration/full encoding/enumeration/deletion, dynarec/D3D9 and richer
shell remain open. Hardware Paint/GPU/fallback/input/relaunch remains deferred;
SD recovery is closed. Local devkitA64/libnx is absent; exact-SHA CI builds NRO.
