# F5: synchronous guest file-save correctness

Baseline b0858f0, exact CI 40136047193 passed six jobs. This package closes the
false-success FlushFileBuffers gap as part of the ongoing [whole-F task](F_ACCEPTANCE.md).
The other F gates remain in that ledger and work continues after this package.

KERNEL32 synchronous ReadFile/WriteFile now use an 8 KiB transfer buffer rather
than allocating guest-request-sized memory. Mapped buffer/count ranges and 32-bit
overflow are checked before I/O. Partial read/write counts are retained; fread,
fwrite and append-seek failures return FALSE with actual errno-to-Win32 errors.
EOF is successful zero-byte reading. NUL has explicit sink/EOF behavior; wrong
types fail, and nonzero OVERLAPPED is explicitly unsupported rather than a fake
asynchronous success. This does not add guest page-permission enforcement or
bound native disk-call latency. A large valid request can still occupy a host
call; native preemption remains outside this contract.

FlushFileBuffers validates a live file HANDLE, FILE_WRITE_DATA access and writable
stream, then checks fflush and actual fsync. Readonly/denied/closed/invalid and
unsupported non-disk streams fail. Append-only FILE_APPEND_DATA without
FILE_WRITE_DATA is outside this implemented flush subset and fails access.
Unsupported native fsync EINVAL fails explicitly. Actual ENOSPC is
ERROR_DISK_FULL (112), I/O errors map to ERROR_IO_DEVICE (1117). This is file
flushing, not an SD/directory transaction or a power-loss-proof save promise.

GetFileSize/Ex no longer convert failed flush/fstat into a fabricated zero.
SetEndOfFile checks flush, current position and ftruncate before reporting success.
CloseHandle keeps the existing shared HANDLE/object lifetime: closing one alias
never closes the FILE for the other. It reports handle closure, not a substitute
for explicit checked FlushFileBuffers. Final RAII fclose remains best-effort
teardown and does not claim durable-success/error acceptance for user saves.

`test_file_save_api` invokes the actual dispatcher with a Process/TEB and checks
native bytes across >8 KiB transfers, size/truncate/EOF, zero/invalid/massive
buffers, unsupported OVERLAPPED, access and stale/duplicate HANDLEs, NUL/directory
behavior and real /dev/full fflush errors. It verifies GetLastError and counts,
not a separate success-returning mock. The public x86 GDI bitmap writer now calls
the real checked FlushFileBuffers before closing; fresh native/x86 readers still
verify all persisted pixels. Native TXT/copy providers retain their own checked
stage/fsync/close publication contracts.

Complete clean rootless host/MinGW/Mesa passed, including that x86 save/find/
reopen flow and all Stage C/E/GL/lifetime regressions. Strict API/native harness
checks and exact new-SHA six-job CI are required on publication. Local Switch SDK
is absent; physical Paint/GPU/fallback/input/relaunch remain deferred; SD closed.
Remaining baseline F: directory tree, persisted Desktop/Start shortcuts, retained
workspace planes/oracle and integrated F acceptance. See F_ACCEPTANCE.md.

[D5 legacy file I/O](D5_LEGACY_FILE.md) now shares the same checked transfer and
truncate implementation for OpenFile/_l/_h APIs; zero-byte legacy writes set EOF,
while zero-byte WriteFile keeps its ordinary transfer semantics. Existing F5
regressions remain mandatory; this does not expand durable/hardware acceptance.
