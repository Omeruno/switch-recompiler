# Functional F: bounded native BMP viewing

Incoming `3536ec4a1e8d3b2434f56b84a6bb0046e0daed90` passed all six jobs in
[CI 40134592527](https://github.com/Omeruno/switch-recompiler/actions/runs/40134592527).
This package adds a real native image tool and a public x86 save/browse/read
scenario. It does not complete F5 or certify physical Paint/SD/GPU acceptance.

## User workflow

Explorer **File / right click -> View BMP** opens a selected `.bmp` (ASCII case
insensitive). Folder/File menus remain reachable through Y/Tab and A/X/Enter,
secondary pointer or ZR. Ordinary Open/Choose application retain explicit guest
EXE associations; there is no guessed Paint mapping or extra guest Process.

The viewer shows the real path/dimensions, Fit and 100%. Fit preserves aspect
ratio, reducing large images with nearest-neighbor sampling and leaving small
ones at native size. In 100%, left/right pans horizontally and up/down vertically
in 32-pixel steps, clamped to the actual image. The viewport clips every write;
letterboxing uses a plain background. No editing, image conversion, bilinear
filtering, touch drag or unsupported zoom levels are implied.

The decoded pixels are an owned snapshot, independent of guest memory/windows.
Native modal input cannot click through into the guest, Start or task controls.
Ordinary guest completion/abort and frontend re-entry preserve the open viewer,
its current Explorer panel and pan. Close/B/Escape releases pixels; filesystem
reconfiguration while it is open is refused. Disk changes after opening are not
live reload. A failed open leaves a visible error and opens no blank fake image.

## Exact format and bounds

`BitmapDocument` uses real F1 C/S/R reads, including readonly R. The existing
Win32 DIB/resource/icon decoder remains unchanged: its USER/Process ownership
and broader legacy behavior are unsuitable as an unchecked native file parser.
The native tool deliberately exposes a separate explicit bounded subset:

- Windows BMP with **40-byte BITMAPINFOHEADER**, one plane, **BI_RGB / no
  compression**, at **1/4/8/24/32 bpp**. Positive-height bottom-up and negative-
  height top-down rows, packed palette indices and four-byte padded rows work.
- Full/default or declared indexed palettes; an out-of-table pixel index fails.
  RGBQUAD's reserved byte and BI_RGB 32-bit's fourth byte are not alpha.
  Direct RGB files require zero `biClrUsed` in this subset.
- At most **16 MiB encoded bytes**, **4,194,304 pixels** and **8192 per side**.
  All limits apply independently. File size/type, positive width, signed-height
  magnitude, 64-bit stride/ranges, offset/palette bounds and image-size fields
  are validated before pixel allocation. The BMP declared file size must equal
  actual bytes; image-size may be zero or the exact padded row extent.
- OS/2 CORE, V4/V5 headers, 16-bit/bitfields, RLE, embedded JPEG/PNG and profiles
  are refused. PNG/JPEG/GIF and general image decoding remain future work.

No external decoder/dependency/license import is added. Reads check short I/O,
EOF, data errors and consuming close, allow at most 16 EINTR retries, and stop
if a growing file exceeds the encoded cap. Invalid/failing loads do not replace
an existing model snapshot. Encoded and decoded byte/pixel bounds are not a total
heap measurement or storage/decode latency guarantee. I/O and decode run on the
frontend owner and may pause service; no hardware timing claim is made.

F1 native-writer/symlink race and non-coherent concurrent-read limits remain.
Readonly image viewing never changes original bytes. Native draw uses owned
RGB values and the current Surface renderer, not fabricated guest callbacks.

## Public validation

`test_bitmap_document` generates actual BMP bytes from public source. It checks
all supported depths/orientations/palettes/row padding and reserved-byte policy,
truncation, malformed dimensions/offsets/planes/compression/palettes/size fields,
bounded deterministic header mutations, exact fit/pan/clipping pixels, Unicode/
readonly/missing/linked/oversized paths, short/read/open/close/EINTR faults with
FD accounting, real native menu/controller controls and scope lifetime. The
optional screenshot uses production rendering and rejects font fallback.

`tests/guest/shell_bitmap.c` creates a real DIB, draws six known colors with
SetPixel/GetPixel, and writes its actual pixels/header with CreateFileW/WriteFile
to a Unicode My Documents BMP. `test_shell_bitmaps` launches it through Start/Run,
returns through actual GuestSession/ShellSession teardown, finds/opens the file
in native Explorer, checks all decoded colors, then starts a fresh shell and a
fresh x86 reader on the same bytes. No private application/asset is required.
The fixture deliberately does not call the still-incomplete FlushFileBuffers;
actual reopened bytes are checked independently, without a durability promise.

Seven busy-x86 sessions also open/compose/pan/close the real native viewer while
the guest heartbeat advances, compare exact viewport and composed-dialog pixels,
and check no native events leak to guest USER.

Run the complete clean `COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh`, strict
native/busy/fixture builds, screenshot, Markdown/diff checks, normal publication
and all six CI jobs for its exact SHA. Local devkitA64/libnx remains absent;
Switch compilation is checked by CI. Consolidated physical Paint/GPU/fallback/
Lite/input/relaunch checks stay deferred; SD troubleshooting stays closed.

## Next F5 correctness gate

Do not interpret this happy-path BMP scenario as complete guest file-save error
handling. Audit found `K32_FlushFileBuffers` currently ignores fflush failure,
returns success even for invalid handles and does not call fsync. File-size and
truncate helpers also ignore some flush errors; final close/error propagation
and duplicated file-object lifetime need inspection. These are concrete open
system work, not supported behavior to hide behind the native tools. The next
package is real guest file-save flush/access/error handling with public fault
checks and save/find/reopen coverage, preserving HANDLE alias lifetime and C
acceptance. Physical Paint acceptance remains a separate deferred gate.
