# F3c: bounded directory tree and persistent Desktop/Start shortcuts

Part of [whole-F completion](F_ACCEPTANCE.md), after 4db5236 (six successful jobs
in CI 40137277767). Completes the baseline's supported navigation/settings/link
scope; arbitrary Windows settings, COM and universal .lnk compatibility are not
implied.

## Directory tree

Explorer's **Folders** button opens an actual F1 directory tree. Up/Down selects,
Right/Expand reads the selected directory, Left/Collapse removes descendants,
Open navigates the same browser and Close/B/Escape dismisses. Pointer rows and
Y/Tab/A reach these controls. It enumerates actual directories without retaining
native directory handles or creating a guest. Missing/readonly/linked/error
paths use the same mapped filesystem rules; readonly navigation is supported.

There are at most 128 retained nodes, depth 32 and 4096 scanned entries/case
lookup work per expansion. Oversized/truncated trees show a warning rather than
pretending all folders were discovered. Expanded children are sorted and only
explicit expansion reads a level; no recursive whole-SD scan or fake directories.

## Actual persisted shortcuts

EXE File/right context offers **Shortcut on desktop** and **Shortcut in Start**.
The platform keyboard accepts the explicit EXE command/arguments; cancellation
is inert. The shared validator checks the target, creates a name from the EXE
stem, defaults CWD to that EXE's directory and publishes a new shortcut without
overwriting an existing name. Recreate through delete/new to change this subset;
there is no misleading editable-properties Save or COM control panel.

Files really live in the persistent profile's Desktop and Start Menu/Programs.
Desktop enumerates up to 24 links; Start appends actual root-program links to
the existing bounded EXE catalog. Enumeration is sorted, warns on unreadable/
truncated inventories and refreshes on frontend entry. Link/catalog selection is
retained by path where still present. Generic licensed program artwork is used;
external icon location is metadata, not a claim to decode arbitrary EXE icons.
Subfolder links remain accessible through the ordinary directory browser.

Profile Start Menu/Programs is created additively in F1. SHGetSpecialFolderPath
supports user Programs/Start Menu and logical Program Files; Program Files and
the environment's ProgramFiles agree on C:\Programs. Existing Desktop/Documents
and standalone path rules remain. No old files are migrated or deleted.

Open validates the link, EXE, arguments and actual directory anew. One shared
ShellLaunchRequest transports target, canonical quoted args and CWD; production
applies CWD after loading EXE and before preparing the guest. This preserves DLL/
module identity beside the EXE while relative document access uses the requested
CWD. No link-to-link cycle, guessed shell execution or second Process is added.
Missing/moved targets, invalid CWD and unsupported links fail visibly.

## MS-SHLLINK subset and checked storage

The files are actual 76-byte ShellLinkHeader/CLSID records with LinkInfo, local
Unicode absolute path, counted Unicode name/CWD/arguments/icon strings and the
terminal ExtraData block. Supported flags are the explicit Unicode local-LinkInfo
subset; ID lists, network targets, environment/tracker/data blocks and other
unsupported flags are refused. LocalBasePath includes the complete local target;
common suffix is parsed metadata, not concatenated into a duplicated filename.
The logical volume's serial/type are unknown (zero), not invented physical data.
ANSI fallback replaces non-ASCII characters; Unicode offsets carry actual paths.

Records are bounded at 64 KiB, guest paths at 4096 bytes, names at 255 bytes and
command data at 16 KiB. UTF-16 pairs/ranges/offsets/lengths/termination and target
syntax are validated. Executable support remains checked by the normal loader,
not inferred from record validity. Path validation is not a native-writer lock
or security/authentication boundary.

An exclusive sibling stage, checked writes/fsync/consuming close and no-overwrite
publication preserve existing destinations. At most 64 stage-name attempts and
16 interrupted data retries; failed stage cleanup may leave a .sr-link temp.
No power-loss, directory-fsync or atomic native-writer no-replace guarantee.
Switch keyboard entry is bounded at 500 UTF-16 units; an oversized initial
shortcut path is refused rather than silently truncated. Larger valid records
from other supported writers can still load within the record/path limits.

`test_shell_links_tree` covers real UTF/args/CWD records, malformed/truncated
input, missing targets/CWD, collisions, short/full/flush/close/publish faults,
Desktop/Start reconstruction, controller commands, real tree expansion/collapse/
navigation and fan-out/symlink limits. An independent pylnk3 0.4.3 reference
(outside the project/dependencies) also parsed the written header/string metadata;
no external parser is shipped or executed by the product. Native screenshots use
production rendering and the real licensed font.

The x86 BMP integration now adds fresh Desktop and Start link launches with
space/Unicode arguments, a deliberately different CWD, real folder APIs and
ProgramFiles environment checks, in addition to actual GDI/save/native view/read.
Full clean host/MinGW/Mesa and exact-SHA six-job CI remain required. Local SDK is
absent; physical input/Paint/GPU/fallback/relaunch stays deferred and SD closed.
