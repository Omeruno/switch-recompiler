# F7 — сохранённые привязки кнопок Switch к гостевой клавиатуре

Реализован один общий профиль для программ в новом USER через рабочую оболочку.
Это настоящие нажатия/отпускания клавиш через общий путь ввода, без исключений по
имени игры. Смена языка клавиатуры, IME, USB-клавиатура, DirectInput/XInput,
комбинации/макросы и профили отдельных EXE остаются отдельными задачами.

## Как пользоваться

1. Открыть **Пуск → Настройки оболочки → Кнопки Switch…**.
2. Выбрать кнопку: ↑/↓ на крестовине либо стрелки на экране.
3. Выбрать клавишу: ←/→ либо кнопки «Предыдущая»/«Следующая». `Unbound` отключает
   клавиатурное действие. Доступны буквы,цифры,F1–F12,Space/Enter/Escape/Tab,
   навигация,редактирование,Shift/Ctrl/Alt с выбором стороны и OEM-знаки,включая `/`.
4. **Применить** меняет профиль в текущем запуске. **Сохранить** также записывает
   его в постоянный профиль. Ошибка записи показывается как ошибка, а изменения
   в памяти остаются доступными. Состояние «применено,не сохранено» видно отдельно.
5. **По умолчанию** выбирает стандартные значения в редакторе; затем нужно нажать
   «Применить» либо «Сохранить». «Назад» отбрасывает ещё не применённый черновик.
   Y переключает действие, X подтверждает выбранное действие; работает и тач/мышь.

Привязываются12 клавиатурных кнопок: B/X/Y,L/ZL,Minus,крестовина и нажатия стиков.
R остаётся «Пуском»,Plus — выходом,A/ZR — мышью; аналоговый стик и тач сохраняют
управление указателем. Эти зарезервированные действия показаны в редакторе.
В оболочке B/X/Y/крестовина сохраняют привычную навигацию независимо от гостевого
профиля. Старый режим (L при запуске) сохраняет прежний ввод; его переназначение
не заявлено. Стартовые L legacy/ZL software и карантин удержанных кнопок сохранены.

Стандартный гостевой профиль: B=Escape,X=Enter,Y=Tab,L=левый Alt,крестовина=стрелки;
ZL/Minus/нажатия стиков отключены. Пример общего профиля для клавиатурной игры:
назначить одной кнопке F2,другой Space,ещё двум Z и `/`. Имена EXE не проверяются.

## Shared core and actual production routing

source/ui/controller_bindings.* contains a platform-independent bounded owner core:
12 physical slots,typed validated VK profile,12-transition maximum release batch.
The supported key choices use the existing fixed-US D2 scan/name tables. Generic
modifiers normalize to their actual physical side before per-key reference counting.
Two buttons bound to the same key emit one down and one final up; releasing just
one owner does not release the key. Physical duplicate downs do not synthesize
repeats. Unbound/reserved/invalid choices have explicit behavior; Windows/Apps/mouse
keys cannot replace the protected native controls in this first profile.

InputEvent carries host-only padButton metadata; guest MSG layout is unchanged.
The Switch adapter supplies real gated pad down/up edges and their native navigation
key. ShellSession maps guest presses through the same shared core,then the normal
QueueGuest/ObservePhysicalInput path. USER creates real scan/extended/side/system/
previous/up state and TranslateMessage characters. Ordinary non-pad keyboard events
retain their previous path. main.cpp contains the libnx physical-button adapter,
not a second custom guest binding table. Standalone new-UI diagnostics without a
ShellSession retain their fixed historical inputs; configured production launches
use the real shell/profile route.

A press owns its original context. Opening native Start/settings or applying a
changed profile releases active guest keys and quarantines held owners until their
physical release. A native B press that closes a modal cannot become a guest Z
press on the way out. Native key-up ownership is retained across modal closure.
Closing child bindings settings keeps native focus in the parent preferences;
closing the final native panel returns keyboard routing to the guest. Editor IDs
are bounded separately from directory/program row IDs,so new controls cannot be
mistaken for file rows by pointer or controller activation.

A disconnected pad supplies a released snapshot through the existing edge gate.
Source close/quit releases mapped async state; bounded queue overflow remains an
explicit cancellation condition rather than pretending a key release was delivered.
The existing256-event ring and deferred physical snapshots remain. Forced guest
abort drains continuations/windows before session storage is discarded; a fresh
launch has no old mapper/pressed state. No extra thread or guest callback executes
from profile translation. Per-device USB/keyboard co-ownership is outside this
single-controller profile; no full Windows input-device namespace is claimed.

## Persistent profile version2

The existing dual-slot profile path is unchanged:
C:\\Documents and Settings\\Player\\Application Data\\SwitchRecompiler\\prefs-{0,1}.bin
(the existing VFS maps C to the actual persistent Switch filesystem root).
SRPREF02 retains sort/associations and adds an exact12-key array with validated
32-bit serialized values. SRPREF01 migrates with original values and default keys;
future versions refuse writes. Bounds,CRC,generation selection and real flush/close
results remain the common preference-store contract. Partial/corrupt writes retain
an older valid copy. Flush/close errors are not reported as saved; as with existing
preferences,bytes may already have reached storage,so no filesystem power-loss or
old-value-after-every-I/O-error guarantee is invented. Applied RAM state remains
usable after save failure. Normal later preference saves include current bindings.

## Public evidence and acceptance limits

- test_controller_bindings: known key names,invalid/reserved choices,duplicates,
  generic+sided modifiers,unbound/no synthetic repeat,atomic profile/context change,
  held quarantine and12-key release limit.
- test_controller_settings: independently encoded actual v1 fixture,version2 save/
  reconstruction,preserved sort/associations,partial-write recovery/future refusal,
  real editor pointer/controller draft/apply/save/reset,real saved screenshot,
  actual ShellSession native-versus-guest B/modal ownership,source-close pairs,
  duplicate key owners and mapped-input overflow cancellation.
- test_game_loop --controller: the persisted profile routes physical button tags
  through the **production ShellSession/core**,actual USER queues/scans/async+queued
  states/chars/timers and independently checked moving/held/released pixels. Four
  normal real/fast-forward x86 sessions and two held-key aborts/clean restart use
  the existing public authored game_loop.exe. The original D8 mode remains intact.
- Focused ASan/UBSan covers mapping/store/browser/editor/layout/route/inspectors;
  other runtime objects remain ordinary. Non-fiber native checks enable leak scans;
  x86 fiber checks disable them and retain the ucontext caveat. Strict changed
  modules/inspectors compile O2/Wall/Wextra/Werror/no-exceptions/no-RTTI. Ignoring
  actual saved mappings and premature duplicate up each fail independent oracles.
- Full host/MinGW/Mesa and exact published-SHA six-job CI/Pages/NRO/source/fixture
  checks remain mandatory. CI publishes stage-controller-bindings with the public
  PE32,source/logs,core and actual native renderer screenshot. No private game file,
  profile,image or trace is published. InputEvent/header changes require a full
  runtime rebuild; do not mix old native object layouts.

Status is software implemented/verified within this scope; actual libnx buttons,
touch,docked/disconnect behavior and storage on Switch remain deferred hardware
checks. Local devkitA64/libnx is absent; CI builds the real NRO. This is not full
Pinball/game/DirectInput/audio/FPS acceptance. CPU GDI default,Claude Luna,L legacy,
ZL software and SD recovery's closed status are preserved.

Next technical package: shared MCI/MediaPlayer/PlaySound sources alongside mixed
waveOut,then version-specific DirectSound/MIDI and larger dynarec/D3D9 queues.
Physical consolidated Paint/GPU/input/relaunch/audio/controller evidence is separate.
See [D2](D2_KEYBOARD_LAYOUT.md),[F4b](F4B_INPUT_SERVICE.md),[D8](D8_GAME_LOOP.md),
[previous plan](F7_CONTROLLER_BINDINGS_PLAN.md),[preferences](F3C_PREFERENCES.md).

[F8 view/indexed copies](F8_GAME_VIEW_AND_INDEXED_BLITS.md) adds an explicitly
selected arcade draft profile and bounded live-window aspect-fit view. It also
extends the common CPU fast path to indexed8→32 SRCCOPY with per-call palette
lookup,reference/capture gates and paired native observations;no hardware FPS claim.
