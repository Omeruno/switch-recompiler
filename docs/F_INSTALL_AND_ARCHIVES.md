> Baseline F update: [real Unicode local .lnk and Desktop/Start integration](F3C_SHORTCUTS_TREE.md)
> and [checked guest file saves](F5_FILE_SAVE.md) are now implemented. Installer
> compatibility/automation, COM and archive extraction below are still planned;
> existing shortcut infrastructure does not certify an arbitrary installer.

# Planned functional extension: application installation and archive import

User request, 2026-10-24: downloaded application/game installers should install
into an appropriate persistent folder and produce usable desktop shortcuts;
ZIP/RAR and other archive extraction should become available later. This is a
**planned milestone**, not existing installer/archive support. It does not
replace the current checked-copy -> context/input -> preferences/TXT/images ->
F5 queue in [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md).

## Application installers

An EXE installer is a real x86 Win32 application. Compatibility depends on its
actual API/loader/GUI requirements; arbitrary EXE/MSI installers are not currently
supported merely because the launcher accepts an EXE. Begin with a reproducible
public installer fixture generated from redistributable test payloads, then
expand based on concrete installer failures. Do not special-case an EXE filename
or report successful installation without real installed data.

Planned work:

- Audit Program Files, Programs, user profile/Desktop/Start Menu paths across
  environment, shell folder APIs, dialogs and the persistent F1 provider. Establish
  one consistent default under persistent C, allow an explicit supported target,
  and preserve adjacent application files/DLLs. Existing C:\Programs is the current
  shell's application location, not proof that every Program Files API agrees.
- Support the file/registry/GUI/loader calls required by the chosen public
  installer. An installer requiring child processes, COM, MSI, services or another
  unsupported subsystem needs real implementation or an explicit failure; the
  current one-Process runtime must not simulate a successful child installation.
- Create/persist native shell shortcuts with executable, arguments, working
  directory and icon information; implement the supported Windows shortcut API/
  .lnk subset explicitly if the installer uses it. Desktop and Start must enumerate
  actual persisted shortcuts and launch through the common validated request.
- Handle real write/read-only/out-of-space/cancel/failure outcomes, define partial
  installation and repair/uninstall scope, and preserve pre-existing application
  data. Do not promise transactional arbitrary installers or automatic rollback.

Acceptance: a public installer writes its payload to the chosen real folder,
creates an actual desktop/Start shortcut, survives a fresh frontend session,
launches the installed EXE with its expected arguments/CWD/resources and can
perform its supported update/remove operation. Host tests and physical Switch
checks remain distinct. Compatibility of installing a game and running that game
are separate; missing DirectDraw/D3D remains Stage H.

## Archive import

Start with a bounded ZIP implementation, then add RAR/other formats through
reviewed open-source libraries with recorded versions/licenses and explicit
format/encryption/multipart support. Do not call ZIP-only support universal
archive support or silently skip unsupported entries.

Planned user flow: select a real archive on C/S, select an extraction directory,
see actual progress/result/errors, then browse and launch/use extracted files.
Create folders/files through the same provider. Define collision/overwrite,
partial extraction/cancellation and available-space behavior. Reject paths that
escape the selected destination (absolute/drive/parent traversal and link
entries), bound entry/path/expanded-byte/nesting work, and test corrupt/truncated
archives and read-only/full storage. Existing user files must not be overwritten
without the chosen explicit policy. No native downloader/browser is implied.

Acceptance: public ZIP/RAR fixtures extract expected names and exact bytes into
real folders, preserve unrelated files and fail visibly on unsupported/corrupt/
out-of-space input. Reopen those files after restarting the environment. Choose
library/limits and require corresponding Switch build and later physical tests
when implementing; no dependencies are installed for this planning entry.

This extension follows the functional F5 baseline, with D/one-Process/shortcut
and filesystem dependencies kept explicit. G optimization may address measured
blockers separately; universal installer compatibility is not a gate that must
be claimed to finish the smaller F5 workflow. See [F_SHELL_PLAN.md](F_SHELL_PLAN.md).
