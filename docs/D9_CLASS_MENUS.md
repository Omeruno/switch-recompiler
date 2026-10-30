# D9 — actual class-default menus, checked templates and owning window lifetime

A private application's missing Game/Options/Help bar exposed a common USER gap:
RegisterClass stored lpszMenuName but CreateWindow never loaded that default menu.
The public CRT-free class_menus.exe reproduces it before any private game change:
on incoming f0a6a98, WM_NCCREATE sees GetMenu==NULL and exits at source line7.
The implementation contains no executable-name branch, game ID or synthetic menu.

## Registration and creation

RegisterClass/RegisterClassEx A/W now capture an owning class-menu resource name or
numeric ID. String reads use the shared checked D7 CP1252/UTF16 reader, including
mapping/wrap/length checks. Mutating/freeing the caller's buffer does not change
future default menu resolution. SetClassLongA/W(GCL_MENUNAME) updates the same
snapshot; W is no longer incorrectly routed through the A wrapper for this field.
This package does not redesign every GetClassInfo/GetClassLong returned string
pointer/encoding contract; their existing raw metadata queries retain their limits.

When a top-level window's explicit hMenu is NULL, CreateWindow resolves that class
menu from the **registering class module**, creates a fresh owning menu tree and
attaches it before creation hooks/WM_NCCREATE/WM_CREATE. CREATESTRUCT.hMenu and
GetMenu agree. Separate windows receive independent trees. An explicit valid hMenu
overrides the class resource. A WS_CHILD hMenu remains its integer control ID and
never triggers default menu loading. Existing geometry/menu-bar drawing sees the
real handle and recalculates the client area through the existing USER machinery.

Missing/invalid class resource refuses creation: invalid module6, absent menu1814,
invalid/unsupported template13. This is the bounded runtime error contract, not a
complete Windows last-error precedence oracle. Loading a menu copies resource data
into native owned items; unloading the source DLL does not invalidate an already
loaded tree. Registering a class does not pin that DLL or certify callbacks into
unloaded code. New creation from that unloaded module refuses.

## Attachment and teardown

A real menu can belong to one attached window. SetMenu validates the new handle/
owner before detaching the old menu (invalid/foreign owner1401). SetMenu(hwnd,NULL)
detaches without destroying; the caller can retain/destroy the old tree. Window
retirement destroys the menu tree still attached to that window, including explicit
menus and submenus. This common path covers normal destruction, failed creation,
thread exit and host cancellation after native continuations drain. Detached menus
are not silently destroyed with a former owner. System-menu and arbitrary menu-
mutation/full cross-thread Win32 policies retain their separate existing limits.

## Checked standard and extended templates

The shared LoadMenu/default-menu/dialog parser now validates complete reads, strings,
alignment, header offsets and version0/1 standard/MENUEX templates. Missing MF_END,
truncated words/text/popup data, unsupported versions and invalid offsets refuse
instead of yielding a successful partial tree. Limits:1MiB template,4096 total
items,16 nested popup levels and32767 UTF16 text units plus NUL per item. Partial
allocations roll back transactionally. This is a bounded supported template subset,
not unlimited menu resources or complete owner-draw/bitmap menu compatibility.

Direct LoadMenu A/W uses the same resource/parser/error path; existing encoding
limits of other menu APIs are not claimed fixed by class-name capture. The native
MenuObjectCount inspector is diagnostic only and is not a new guest API.

## Public evidence

- tests/guest/class_menus.c/.rc + public class_menus.dll: real A/W class registration,
  mutable caller menu-name buffer, named/numeric/MENUEX/EXE/DLL resources, independent
  defaults, explicit override, child IDs, menu availability during creation, changed
  GCL_MENUNAME, failed WM_CREATE, source DLL unload and detach/destroy semantics.
- tests/test_class_menus.cpp:4 normal interpreted x86 sessions plus2 host aborts
  parked inside WM_CREATE or WM_INITMENUPOPUP. A native platform keyboard sequence
  selects the real popup entry; the guest receives the actual WM_COMMAND. Attached
  menus and windows drain completely, and the next session starts empty.
- tests/test_menu_templates.cpp: every prefix of an authored valid template, bad
  version/offset/end/NUL,16/17 depth,4096/4097 item and32767/32768 text boundaries,
  size budget, exact live-object rollback and real stdcall DestroyMenu.
- Focused ASan/UBSan covers changed USER/menu/window/thread code and inspectors;
  remaining runtime objects are ordinary. Guest-fiber leak scan is off with the
  ucontext caveat; the no-fiber malformed-parser run enables leak scanning. Strict
  O2/Wall/Wextra/Werror/no-exceptions/no-RTTI excludes existing unused parameters.
  Removing default loading or parser rollback fails independent public assertions.
- Required full host/MinGW/Mesa preserves C/E/F/GPU/D/audio/GUI oracles. CI retains
  six jobs and publishes stage-d-class-menus: authored PE32 EXE/DLL, sources/RC,
  native/parser logs and this contract. Exact-SHA CI/Pages/NRO/public source and
  fixture manifest checks close publication. Local devkitA64/libnx is absent.

## Observed private boundary — later initialization and moving ball on host

Earlier -f12/-f60 snapshots ended too early: wait-adapter steps are not game frames
or an application-ready signal. Even incoming code in a longer no-input host run
shows BALL1/player/score digits and changing lamps. Their absence in an early
snapshot was not evidence of missing font support. The game's score digits use
its own bitmap assets; no replacement score renderer is added here.

With actual class menus implemented, a new-UI real-time host run shows Game/Options/
Help,Player1,Awaiting Deployment and a metallic ball in the launch channel. Late
F2/Space input followed by sequential snapshots of the **same** bounded run shows
the ball at different positions,mission text,and score0→2000→2500. This is observed
host ball launch/movement/score progress,not a complete game acceptance. Flipper
pairs were scripted but their causal physics effect is not independently certified.
Sound/MIDI,full input coverage,saving/endurance,FPS and physical Switch remain open.
The diagnostic wait-step schedule is not a portable readiness/latency/FPS guarantee.

Private original EXE/assets/images/traces and local observer captures stay outside
public git/CI/Pages. Public open-source SpaceCadetPinball Release_1.0 (MIT) was
consulted for initialization/bitmap-score facts; no implementation/assets were
imported. No physical console result is added and no SD diagnosis is reopened.

Next priority remains [F7 controller bindings](F7_CONTROLLER_BINDINGS_PLAN.md),
then broader target-specific input/audio/performance acceptance through common
mechanisms. Legacy still needs a separate LoadIconA resource/lifetime reproducer.
CPU GDI default,L legacy,ZL software and Claude Luna are preserved.

References: [CreateWindowExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw),
[WNDCLASSW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassw),
[SetMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setmenu),
[public reference](https://github.com/k4zmu2a/SpaceCadetPinball/tree/Release_1.0).
