# D11 — old PE raw-backed imports and Doom95 compatibility investigation

Incoming1ff01c9084c8f9a1f93e3bdf71080343fbbbd40e passed all six jobs in
[CI41136440421](https://github.com/Omeruno/switch-recompiler/actions/runs/41136440421).
This package fixes common PE parsing/loading. It does not implement DirectX,
substitute a native game port, or certify Doom95 launch/gameplay on Switch.

## Actual compatibility boundary

The supplied application is Doom95, a PE32/i386 Windows port, not a DOS executable.
Its old linker sets section VirtualSize to0 while SizeOfRawData/PointerToRawData
still describe initialized bytes. The previous RVA converter matched only
VirtualSize, so it silently reported no imports. The loader then left original
IAT values unchanged and execution could jump into an unmapped historical address.
The import-inspection CLI could also misleadingly print an empty missing-API list.

After the common fix, the import table is parsed and patched, the startup splash
is drawn, and the original launcher DLL loads in a private host diagnostic. The
observed ordinary launch exits without reaching a playable menu/game. This is
limited splash/launcher evidence, not a DirectDraw call-path or gameplay acceptance.
Existing Common Controls ordinal fallback was observed; launcher/configuration/API
work remains open. Do not describe every statically missing import as a reached
failure, or an exit code0 as successful gameplay.

Static requirements include DDRAW!DirectDrawCreate, DSOUND!DirectSoundCreate,
DPLAY ordinal1, WinMM joystick/MIDI streams and further Win32/registry/console
compatibility. DirectDraw/DirectSound/DirectPlay are not implemented by this fix;
no success-shaped COM object or fabricated sound/network state is returned here.
These requirements motivate the next shared graphics/audio/API packages. A full
x86 game/frame/input/audio/performance/physical acceptance remains future work.

The uploaded executable,DLLs,WAD,images,traces and archive are kept outside public
git/artifacts/site. Archive extraction used the host's existing libarchive only;
this does **not** implement RAR import in the Switch shell. Test artifacts below
are authored/redistributable fixtures, not modified copies of the supplied game.

## Common RVA and import behavior

RvaToFileOffset now validates a complete requested byte span against image/header/
raw-file bounds with64-bit arithmetic. The section envelope uses
max(VirtualSize,SizeOfRawData), so VirtualSize0 does not hide initialized bytes.
A virtual-only tail is not treated as file data. A zero-pointer section marked
uninitialized has no raw file bytes even if an old linker leaves a nonzero raw
size. Initialized/header-backed low-offset sections retain file-offset handling.
Header RVAs are supported within both SizeOfHeaders and actual file bytes.

MapImageToMemory leaves uninitialized zero-pointer sections zeroed and checks
whole source/destination spans with64-bit arithmetic before copying. Section raw
bytes outside the file/image are skipped, preserving the prior conservative map
policy. This is not complete PE schema/overlap/page-protection/security validation.
Image allocation,relocations,exports/resources and other loader contracts retain
their existing separate limits. The bounded malformed probes are not a proof of
support for every malformed or exotic PE.

File loading resets previous parser state before each attempt,checks declared
optional/section-table spans and copies only available optional-header fields.
Directory presence must fit the declared header. Negative/beyond-DWORD file sizes
refuse rather than narrowing. A failed import parse clears partial imports and
returns NotAValidPeHeader instead of silently running an unbound IAT.

Import descriptors/lookup entries/hints/names are read by checked RVAs, with the
null descriptor inside the declared import directory. OriginalFirstThunk0 still
uses IAT lookup when that IAT contains name/ordinal RVAs. Name imports and ordinal
imports retain their actual IAT slot addresses. IAT slots must fit the image.
Reserved ordinal bits,empty/nonterminated names,missing table terminators and
raw/virtual/EOF/wrap violations refuse. Metadata is bounded to1024 modules,65536
symbols,4096 bytes per name and1MiB aggregate name bytes. Records must fit one
file-backed header/section span; virtual allocation is not permission to read
following file data as an import structure.

## Public fixture and checks

old_pe_imports.c is a CRT-free authored PE. old_pe_api.c exports a real ordinal7
function and checks zero BSS during actual DLL_PROCESS_ATTACH. The DLL deliberately
uses the EXE's preferred base to require genuine relocation. The build-only
make_old_pe_fixture.py changes these authored MinGW outputs to VirtualSize0,
nonzero uninitialized raw size and deliberately stale IAT addresses while retaining
the real lookup tables. It never patches the uploaded application.

The native inspector verifies raw/header/span/zero-size/virtual-tail semantics,
guarded mapping and zero BSS. It creates valid header-directory/name and OFT0
variants,checks real named+ordinal imports,then runs eight normal new/legacy x86
sessions and two deliberately parked aborts. Guest code invokes actual IAT-bound
Win32 functions and the relocated ordinal DLL,writes/flushes a known file and
checks initialized globals. File bytes are inspected independently. Fresh sessions
must reinitialize the DLL/BSS. No private renderer or game payload is required.

Malformed checks cover overflowed DOS/section/import pointers,short directories,
missing descriptor/string/thunk terminators,invalid IAT destinations,raw EOF and
virtual tails.96 bounded header mutations additionally exercise parser safety.
The parse-only sanitizer mode enables leak scans; guest-fiber execution disables
those scans and keeps the ucontext caveat. Focused ASan/UBSan instruments parser
and inspector;remaining runtime objects are ordinary. Strict compilation uses
O2/Wall/Wextra/Werror/no-exceptions/no-RTTI. Removing the raw fallback fails the
public valid-file/import oracle. Existing load/unload/rollback/TLS/CPU/Win32/GDI/
audio/F7/F8/GPU tests remain required.

CI publishes stage-d-old-pe with the authored PE32 EXE/DLL,source/DEF/build-only
transform,inspector/log and exact parser/contract bytes. Full host/MinGW/Mesa,
exact published-SHA all-six CI,Pages/browser,NRO and public source/fixture audits
remain publication gates. Local devkitA64/libnx remains unavailable;CI builds NRO.
No physical Doom95 acceptance is inferred from host evidence.

Next: trace the actual launcher/configuration/Common Controls boundary with public
reproducers,then implement bounded DirectDraw surface/palette/present interfaces,
real DirectSound sources and appropriate unsupported/offline DirectPlay behavior.
Preserve the shared audio bus/WinMM MIDI/joystick,virtual display,dynarec/D3D9 and
legacy compatibility queues. CPU GDI default,Luna,L/ZL launch overrides,F8 mappings/
view semantics and SD recovery's closed status stay unchanged.
See [target games](TARGET_GAMES_RU.md),[F8](F8_GAME_VIEW_AND_INDEXED_BLITS.md),
[D10](D10_AUDIO_RECOVERY.md),[active map](ACTIVE_ROADMAP.md).

Successor [D12](D12_LAUNCHER_CONTRACT.md) traces and fixes the real empty-path/UserName/DirectPlay-ABI boundaries. It reaches dialog message processing,not gameplay. The earlier D11-only observation above is retained as historical evidence.
