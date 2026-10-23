# F0: bounded startup launcher

The existing Switch startup picker now uses the same ExecutableCatalog exercised
by native tests. This is integrated startup functionality, not an unused shell
model and not a finished XP desktop.

## Inventory boundary

- ROMFS root first, then the SD application root and one subdirectory level,
  preserving current root priority and sorted paths within each root.
- Cumulative caps: 256 EXE paths, 4096 inspected entries, 1024 bytes per path.
  Scan recursion itself is capped at depth 2 regardless of caller input; the
  production SD call still requests the former depth of 1.
- Regular files with case-insensitive `.exe` suffix only; directories named
  `.exe` and other file types are not launch candidates. This is discovery,
  not PE validation; the ordinary loader remains responsible for the binary.
- No list of all subdirectories is retained. At most three directory streams
  are open across bounded recursion. File-path payload is at most 256 KiB,
  excluding vector/string and libc directory-stream overhead.
- Failed reads/stat calls and truncation are surfaced in the picker. A budgeted
  scan may omit candidates; this is not a complete filesystem inventory.
  FAT/ROMFS behavior is the target; there is no new symlink sandbox claim.

## Selection and cancellation

18 candidates are displayed per page. Up/Down wrap selection; Left/Right move by
18 items; A launches. Long labels retain the filename end, avoid cutting into a
UTF-8 continuation sequence and replace control bytes for console display only.
The actual stored launch path is unchanged.

B/PLUS and applet shutdown return from the picker, including its inner input
wait loop. Cancellation cleans ROMFS/audio/console before graphics or the guest
thread is created. No-files state stays readable until exit; it does not attempt
to load an absent default EXE. Single-file autolaunch is preserved when discovery
is not truncated. Existing L launch-mode and ZL software override behavior remain.

Native tests cover extension/type filtering, scan depth/root order, missing and
oversize roots, both cumulative budgets, selection wrap and partial pages.
Switch CI must compile the actual adapter; menu rendering, cancellation/app exit,
long labels and launch quarantine still require the deferred hardware smoke.
The runtime/renderer teardown path after a launched guest is unchanged here.

## Historical next step of the first picker package

The paragraph below records the first package only. Its lifecycle integration
is now implemented; see [F0_LAUNCH_LOOP.md](F0_LAUNCH_LOOP.md). The current next
package is F1 filesystem foundation in [F_SHELL_PLAN.md](F_SHELL_PLAN.md).

Original next F block: audit and make the existing one-Process session initialization/
teardown repeatable before implementing return-to-launcher or a desktop shell.
Current main closes the NRO after its one guest thread, while the interpreter
owns static Process/memory and global UI state. Do not simply loop that path or
claim multiprocess isolation. Add a host regression for two sequential guest
sessions before enabling launch-return-launch on Switch.
