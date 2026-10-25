# Luna integration review — 2026-10-25

Reviewed and fast-forwarded all five Claude commits after G2, preserving them:

| Commit | Change |
| --- | --- |
| 9ca8ff7 | Cached procedural landscape wallpaper, Luna taskbar/Start/tray |
| 5dba613 | Original vector icons, bounded public draw sizes, icon pixel tests |
| 6034b12 | XP-style desktop icon arrangement, labels and shortcut overlay |
| c881653 | Larger Explorer/About/Programs window, toolbar/task pane/status, tiled file view |
| 466bcfd | Two-column Start menu, user picture, separators and Homebrew exit |

Latest incoming `466bcfd818407071e400de31bfac137c615b5554` passed six jobs in
CI 40148776172; all five commits have successful CI runs. G2 `6a15480` separately
passed six jobs in 40143458222, including its ARM64 GDI oracle. The clean G2
comparison artifact was downloaded and its fixture SHA verified. Its tile
baseline fast/reference ratio was about0.812 on the CI host, not Switch FPS.

The visual changes use procedural/original drawings, not copied Microsoft
wallpaper/icon files. Existing Win32/GDI semantics and G2 fast copy are retained.
The native window minimize/maximize caption glyphs are currently decorative;
only the existing close action has a native window command. This review does
not claim full XP shell behavior or finish drag/resize/multitasking/Luna polish.
Wallpaper generation is cached once per size; native shell drawing remains CPU.
No GPU-GDI acceleration is inferred from the new theme.

## Integration defect fixed

Native drawing moved the panel to `(156,8)-(1258,676)`, while ShellSession still
composited, assigned native ownership and filtered active-guest clicks using
`(218,100)-(1166,630)`. Consequently idle-only screenshots and tests comparing
retained GPU output against the same truncated canonical image could pass,
while a real active EXE lost the new caption, toolbar and right/bottom panel
edges and received clicks intended for Explorer.

`DesktopShell::PanelArea()` is now the common source for window drawing,
canonical composition, retained ownership and input ownership. No Claude art
or functional shell flow was removed. The actual-x86 workspace regression checks
**every pixel** of the full panel against a separately rendered native view,
native ownership at expanded corners, and no click-through at the new right
edge over a maximized guest window. Existing CPU and actual-EGL workspace
oracles check the resulting scene across normal/abort/relaunch sessions.

Full clean rootless MinGW/Mesa suite passed, including G1/G2 measurements, icon
checks, all file/text/BMP/link/tree actions,22 F evidence groups, Stage C/E and
CPU/actual-EGL composition/lifecycle. Actual native Start/Programs screenshots
were inspected. Host success does not grant physical Switch acceptance.
Local devkitA64/libnx remains unavailable; CI must build the exact new NRO.

The next independent block remains G3's bounded actual-EGL GPU-GDI prototype:
owned targets/inputs, CPU readback barriers, bounded recovery journal and
reference pixel/fallback comparisons before production activation. User requests
no new hardware tests; all console gates remain deferred.
