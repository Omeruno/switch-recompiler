# D1 — bounded real registry keys, values and HKEY lifetime

2026-10-28. Continues D compatibility after
[explicit CPU/native routing](G4B2O_CPU_ROUTING.md). Removes the established
RegCreateKeyExA host blocker through common registry behavior, not an EXE-name
exception. This does not accept all Stage D, full NT registry/security or Pinball.

## Implemented family

New registry_module.cpp registers11 actual stdcall exports: RegCreateKeyExA/W,
RegOpenKeyExA/W, RegOpenKeyA/W, RegSetValueExA/W, RegQueryValueExA/W and RegCloseKey.
The old duplicate implementations are removed. The old raw UTF-16 narrowing
helper remains for legacy USER callers only; checked registry APIs never use it.

Creation stores genuine normalized keys/intermediate parents. Opening cannot
create a missing key. Relative/empty/root and case-insensitive ASCII lookup,
created/opened disposition, named/default values and typed data use one store.
Empty predefined-root OpenKeyEx refreshes that root handle; old OpenKey with an
empty path returns its input handle, avoiding invented duplicate grants.
Host values survive guest-session teardown; transient path/access HKEY metadata
is exchanged with LegacySession, preserving suspended owners and grants. Close
actually invalidates a non-root handle; predefined roots remain valid. No closed
slot is reused in a session.65536 handle allocations/session and4096-byte full
paths are explicit bounds; exhausting handles accepts no new key. Path components
are limited to255 characters. Invalid/closed/unknown parents cannot create an
"unknown" hive. Root support remains HKCR/HKCU/HKLM/HKU.

Granted masks enforce QUERY_VALUE and SET_VALUE. Read-only handles cannot set;
write-only handles cannot query. Generic READ/WRITE/ALL normalize to the supported
registry rights. Root grants are unrestricted within this host provider. Security
descriptors, ACL/token checks, remote registry, inheritance and alternate WOW64
views are not implemented. WOW64 views, volatile options and nonempty key-class/
security-attribute requests reject explicitly. Unsupported masks/options do not
quietly grant capabilities. This is not the full Windows access-control model.

Supported names are bounded printable ASCII A/W, with no UTF-16/ACP truncation.
Unsupported characters return1113; invalid control characters/path components
reject. Full Unicode names/ACP conversion are a later contract. Data supports
REG_NONE/BINARY/DWORD/DWORD_BIG_ENDIAN/QWORD and SZ/EXPAND_SZ/MULTI_SZ. String data
is canonical UTF-16: ASCII A setters/readers convert actual bytes and sizes;
W data retains its bytes. A conversion outside ASCII rejects explicitly. No fake
terminator is appended: caller's specified data/endings are preserved. Maximum
canonical value input is1MiB; typed widths and mapped guest ranges are checked
before accepting data. Reserved fields, invalid pointers and missing values
return real LSTATUS errors. MORE_DATA reports required size without overwriting
the short data buffer; size-only queries work. Registry calls preserve last error.
Existing guest page-right enforcement remains separate from mapped-range checks.

Enumeration/deletion are still unsupported and now return ERROR_CALL_NOT_IMPLEMENTED
instead of fabricated empty-enumeration/success results. No support is claimed
for those operations or arbitrary Windows registry APIs.

## Persistence boundary

This package proves live host-store values across fresh guest sessions. The
existing SD snapshot provider remains separate: non-atomic replacement, no fsync,
legacy line/name/large-value/default-value round-trip limitations and incomplete
checked loading. SaveToFile now checks stream/flush/close failure, including real
/dev/full, but RegSetValueEx success means in-memory acceptance; its existing
snapshot save remains best effort. No RegFlushKey, power-loss-safe persistence,
full SD round-trip or durable installer support is claimed. Checked versioned
persistence/migration without losing the old registry is an explicit follow-on.

## Evidence and actual application advancement

Public CRT-free registry_keys.exe runs four fresh normal-exit0 x86 sessions.
Independent native inspection checks stored DWORD and exact UTF-16 bytes across
writer/reader/rewrite/reader, intermediate/case/relative/default lookup, cross-A/W
queries, rights, actual close/stale handles, unchanged short buffers, size queries,
invalid outputs/data/parents, excessive sizes/names, unsupported options/types/
encodings and unchanged last-error. Suspended-session grants/values are checked
separately. Native budget exhaustion cannot create the refused key, and real
provider flush failure must return false.

The historical native test10 had requested samDesired=0 and then expected writes
and queries to succeed. Its owned instruction fixture now requests QUERY_VALUE|
SET_VALUE before the same stdcall/stack/value round trip; no permission bypass is
added to the implementation. Existing legacy/session/graphics/file suites remain
mandatory alongside strict/no-exception and focused sanitizer checks.

A user-provided Pinball x86 copy was probed outside public git. It passes the old
missing RegCreateKeyExA entry, performs registry calls and creates a helper USER
window, then stops at missing USER32!GetKeyNameTextA. This is host startup
advancement, not a displayed playable game or physical Switch acceptance. No
private binary/frame/trace is committed or uploaded as public CI evidence.

Next concrete application block: correct key names and VK/scancode translation
with public A/W/input tests, including audit of existing MapVirtualKey behavior.
Checked registry persistence/full encoding/enumeration/deletion, page protection/
SEH/COM, full ARM64 dynarec, D3D9/audio and target games remain distinct open work.
Local Switch SDK absent; NRO builds through exact-SHA CI. Hardware checks stay
deferred; SD recovery diagnosis remains closed.
