# Switch Recompiler — полная дорожная карта
Дата обновления: 2026-10-21
Статус: инженерный план, НЕ заявление о полной Windows-совместимости.

## Цель и границы
Нативная среда запуска 32-битных x86 Windows XP/NT 5.1 user-mode приложений на Nintendo Switch (Horizon/libnx). Не эмулируем всё ядро Windows и не делаем исключения под конкретные EXE. XP-подобный рабочий стол — собственный host-shell, не обязательный запуск настоящего explorer.exe.

Два независимых измерения качества:
- Совместимость: правильность x86, Win32 ABI, SEH, потоки, объектная модель, GUI.
- Производительность: сначала эталонный интерпретатор, затем опциональный dynarec, GPU backend. Ускорение НЕ должно менять семантику приложений.

## Подтверждённые факты / текущая точка
- Paint открывает и сохраняет файлы на физическом Switch — проверено пользователем; держать NRO базового коммита `2c9a1fa` как известную рабочую копию.
- Интерпретатор x86, PE EXE/DLL loader, импорты, Win32 HLE, файловые диалоги, USER32/GDI существуют, но совместимость неполная.
- SEH: RaiseException, dispatch CPU traps, FS:[0], CONTEXT32 частично включая x87/SSE, RtlUnwind, nested/collided subset; не подтверждать полноту SEH без пограничных тестов.
- GuestThread/TEB/stack/TLS и GuestScheduler исполняют x86 по квантам в winrun и Switch. CreateThread/worker return, Sleep, single/multiple waits и critical sections существуют. Базовый HEAD `83b299b` имеет зелёный CI `39650590532`; новые изменения проверять отдельно.
- Добавлены общая process-owned HANDLE table, независимое дублирование объектов, alias wake, abandoned mutex и ExitThread главного потока. Наличие кода не заменяет CI и аппаратную проверку.
- Визуальный desktop-shell ещё не существует как законченная функция.

## Этап A — корректность и регрессионная база [постоянно]
- Версионировать known-good NRO и тестовые EXE без распространения proprietary Windows DLL/бинарников.
- Host tests, x86 CPU oracle, Switch CI; редкие проверки на настоящей Switch отдельно.
- Каждый баг: воспроизводящий минимальный Win32 guest, проверка стеков/регистров/импортов и crash log.
- Матрица программ: запуск, GUI, ввод, открытие/сохранение, потоки, стабильность, производительность.

## Этап B — завершить кооперативную многопоточность [сейчас, блокирует shell]
B1. Выполнять инструкции по квантам GuestScheduler для выбранного GuestThread; переключать FS и TEB, хостовые поля текущего потока. Проверка: два гостя увеличивают независимые счётчики, без перекрёстных записей.
B2. CreateThread, ExitThread, GetExitCodeThread, SuspendThread, ResumeThread, GetCurrentThreadId; законный worker return trampoline завершает только рабочий поток, не процесс.
B3. Для каждого потока: lastError, SEH активный стек, TLS, stack guards, DLL_THREAD_ATTACH/DETACH, TEB self and ClientId.
B4. Отделить HWND/очередь сообщений/таймеры от host render-thread. Проверка: оконный процесс с фоновым worker и без зависания интерфейса.
Критерий: Paint baseline + 2 worker guest EXE + корректное завершение/обработка exceptions на разных потоках.

## Этап C — NT объекты и ожидания
- Единая таблица типизированных 32-битных HANDLE с refcounts: thread, file, event, mutex, semaphore.
- WaitForSingleObject/MultipleObjects, finite/infinite timeout, auto/manual reset, WAIT_ABANDONED; guest wait освобождает квант, не блокирует render.
- CriticalSection рекурсивность/владелец, Interlocked, таймеры и очередь сообщений.
Критерий: producer/consumer guest, контеншн и тайм-ауты, отсутствие deadlock.

## Этап D — loader и системные Win32 API
- PE32 ABI (stdcall/cdecl/thiscall), экспорт по имени/ordinal, forwarded exports, delay-load, DLL refcount and attach/detach.
- VirtualAlloc/Protect/Query/Free, page faults and stack growth, файловые пути, кодировки, реестр, загрузка ресурсов, COM/Shell.
- Полные необработанные исключения, unwind, x87 precision limitations, FPU/SSE; тесты MSVC-подобного поведения.
Критерий: реальные приложения без адресных hardcodes и чистое завершение.

## Этап E — графическая граница (начать интерфейс сейчас, ускорять после C)
Формат: Win32 GDI/DirectDraw/Direct3D commands → независимый графический backend.
E1. Ввести границу поверхности: host-owned textures/surfaces, pixel format, clip, dirty rect, lifetime, update, blit/composite, present. Детерминированный software backend — эталон.
E2. Для GDI: выполнять семантику на CPU где это выгоднее/проще; отправлять изменённые прямоугольники в GPU, аппаратно компоновать окна, курсор и масштабирование; cache glyphs/bitmaps.
E3. Native Switch GPU backend только после проверки доступного API и режима памяти; определить подходящий libnx/deko3d/совместимый графический путь для целевого homebrew runtime. Факт наличия GPU не означает прямой аппаратной совместимости с DirectX.
E4. Измерять latency/CPU/GPU/memory на Switch, fallback на software если несовместимый путь.
Критерий: пиксельное сравнение CPU и GPU вывода на GDI-сценах + Paint visual regression.

## Этап F — XP-подобная оболочка (пользовательская веха)
- Свой desktop-shell host UI: фон, taskbar, Start menu, часы, shortcuts, запуск и переключение EXE, сохранение на SD, управление Joy-Con/тач.
- Собственный файловый менеджер. Настоящий explorer.exe — необязательная отдельная цель с огромными зависимостями.
- Процессы: начать с одного адресного пространства/нескольких окон, затем полноценная изоляция нескольких экземпляров с отдельными Process и ресурсами.
Критерий: открыть Paint из «Пуска», рисовать, сохранить, найти через проводник, переключиться между окнами.

## Этап G — ускорение CPU
- Профилирование интерпретатора, горячие инструкции, fast paths.
- Опциональный dynarec x86→ARM64 с блоковым кэшем, точными флагами и исключениями; interpreter fallback и differential tests.
- Проверить возможности исполняемой памяти / icache / W^X на Horizon до обещаний полноценного JIT.
Критерий: идентичные CPU oracle и SEH результаты; измеримый speedup на реальной Switch.

## Этап H — DirectDraw и Direct3D
- Начать с DirectDraw 2D/palette/surface flipping; затем выбрать D3D поколения по реальной совместимости и спросу.
- API translation в native GPU backend с текстурами, render targets, shaders, ресурсным кэшем.
- Графику Win32 и графику игр не смешивать с оболочкой: shell — лишь клиент общего compositor.
Критерий: тесты DirectDraw, затем простые D3D сцены и профилированные игры.

## Этап I — выход продукта
- Версионирование NRO, installer/config for SD, error logs, graceful recovery, crash-safe saves, regression suite.
- Таблица совместимости программ, настройка производительности и контроллеров.
- x64 — отдельная архитектурная программа (x86-64 decoder/ABI/PE32+/Win64); не включать молча в текущий XP x86 scope.

## Ближайшая очередь работ
1. Проверить текущий HANDLE/lifecycle пакет: хостовые ABI-тесты, MinGW-гости, CPU oracle и Switch build; отдельно проверить Paint на железе.
2. Saved native continuations для вложенных callbacks реализованы: взаимные ожидания WndProc/SEH, GetMessage от worker, busy-spin и ExitThread внутри callback покрыты тестами. Проверить ARM64 ABI под QEMU и реальный Switch; это не завершает все блокирующие host HLE пути.
3. Довести жизненный цикл потоков: DLL_THREAD_ATTACH/DETACH, TLS callbacks и освобождение stack/TEB/TLS после безопасной точки.
4. Per-thread USER32 queues и MsgWait*, затем APC/alertable waits. Отдельно — named objects и access/inherit/protect HANDLE policies.
5. Ввести renderer-backend interface без переписывания эталонного CPU GDI. Desktop-shell и JIT — после соответствующих acceptance gates.

Правило: изменения исключительно в ветке `claude/wonderful-hawking-10mpr0`. Не объявлять CI-успех для коммита, пока нет зелёного workflow с тем же SHA.

## Architecture review: external recommendations (2026-10-21)
Independent review found four actionable risks; refined against existing design:
1. **Instruction quantums already preempt pure guest spin loops.** Actual starvation danger is nested guest callbacks / blocking HLE calls that monopolize the host thread, so test an infinite-looking busy spin waiting on worker and audit HLE callbacks. Eventually implement MsgWaitForMultipleObjects and APC alongside real per-thread message queues; do not emulate as ordinary WaitForSingleObject.
2. **JIT feasibility probe moves earlier**, before implementing ARM64 dynarec: use libnx `jitCreate`, `jitTransitionToWritable/Executable`, and `jitGetRwAddr/jitGetRxAddr`; run a minimal generated ARM64 function on physical Switch in actual target homebrew launch mode. Confirm permissions and I-cache synchronization, code-memory lifetime, and W^X without assuming `svcMapProcessCodeMemory` must be called manually. Keep current interpreter fallback. libnx API docs: https://switchbrew.github.io/libnx/jit_8h.html
3. GDI remains synchronous CPU rasterizer and pixel-correct reference; GPU used for dirty-rect upload, batching and desktop composition; DirectDraw/D3D translator is a separate backend. Never force every GDI call through GPU.
4. Expand old-compiler SEH ABI tests (MSVC/Delphi/CRT) with nested, collided unwinds, register/flags/FP restoration. Apply targeted compatibility matrix, not unbounded demand for "100% Win32".
Acceptance gates: a native spin-lock guest test completes under scheduler quanta; no host API call starves render loop; JIT probe works on user's actual Switch before dynarec milestone is scheduled; GDI image comparison passes software vs composited output.
