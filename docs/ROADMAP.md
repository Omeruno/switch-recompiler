> Текущая веха F0–F5: **PASS_SOFTWARE_PENDING_HARDWARE**.
> [Критерии, отчёт и оставшиеся аппаратные проверки](F_ACCEPTANCE.md).
> Установщики/архивы — отдельное запланированное расширение; полной Windows-совместимости нет.

# Switch Recompiler — полная дорожная карта
Дата обновления: 2026-10-25
Статус: инженерный план, НЕ заявление о полной Windows-совместимости.

## Цель и границы
Нативная среда запуска 32-битных x86 Windows XP/NT 5.1 user-mode приложений на Nintendo Switch (Horizon/libnx). Не эмулируем всё ядро Windows и не делаем исключения под конкретные EXE. XP-подобный рабочий стол — собственный host-shell, не обязательный запуск настоящего explorer.exe.

Два независимых измерения качества:
- Совместимость: правильность x86, Win32 ABI, SEH, потоки, объектная модель, GUI.
- Производительность: сначала эталонный интерпретатор, затем опциональный dynarec, GPU backend. Ускорение НЕ должно менять семантику приложений.

## Подтверждённые факты / текущая точка
- **Мы здесь: C принят в ограниченном scope; E0–E7 и F0 программно реализованы, G0 добавил census. Пользовательская веха F не завершена: F1 постоянный SD-backed корень/тома и F2 native idle desktop/theme реализованы; F3a добавил реальный проводник/path-only Run; F3b добавил приложения документов/аргументы; F4a добавил desktop/taskbar/Start при активном EXE и новый UI Paint по умолчанию. По последнему запросу F4b.1 добавил независимое обслуживание native-ввода. F4b.2 сохраняет frontend/output между сеансами. Реальные create/rename/delete/properties и проверяемый copy provider реализованы; native Copy/Paste с прогрессом/отменой подключён; context menu/ПКМ/controller focus подключены; постоянная сортировка/ассоциации реализованы ограниченным F3c-пакетом; ограниченный native TXT read/edit/save реализован; ограниченный просмотр BMP и публичный save/browse/reopen slice реализованы; следующая цель — корректность сохранения/ошибок гостевых API в F5; Luna-полировка отложена. См. [активную карту](ACTIVE_ROADMAP.md), [аудит](PROJECT_AUDIT_2026_10_23.md) и [план F1–F5](F_SHELL_PLAN.md).**
- Принятая реализация: `6aacc4e013e55be84a6f7860dfb34e810c6fbda0`; все пять CI jobs прошли в [39810630356](https://github.com/Omeruno/switch-recompiler/actions/runs/39810630356). Пользователь запустил standalone input и combined acceptance v2 на Switch и прислал отчёты: PASS 1–8, `PASS: ALL 8 SUITES`, `input passed`, без FAIL. [Запись приёмки](STAGE_C_ACCEPTANCE.md#результат-приёмки-2026-10-22).
- Paint открывает и сохраняет файлы на физическом Switch — проверено пользователем; держать NRO базового коммита `2c9a1fa` как известную рабочую копию.
- Дополнительная аппаратная проверка пользователем: Paint на NRO `8b0ad70` работает без изменений/регрессии (2026-10-22). Это проверка Paint, а не всех новых loader/concurrency-сценариев.
- Интерпретатор x86, PE EXE/DLL loader, импорты, Win32 HLE, файловые диалоги, USER32/GDI существуют, но совместимость неполная.
- SEH: RaiseException, dispatch CPU traps, FS:[0], CONTEXT32 частично включая x87/SSE, RtlUnwind, nested/collided subset; не подтверждать полноту SEH без пограничных тестов.
- GuestThread/TEB/stack/TLS и GuestScheduler исполняют x86 по квантам в winrun и Switch. CreateThread/worker return, Sleep, single/multiple waits и critical sections существуют. Базовый HEAD `83b299b` имеет зелёный CI `39650590532`; новые изменения проверять отдельно.
- Добавлены общая process-owned HANDLE table, независимое дублирование объектов, alias wake, abandoned mutex и ExitThread главного потока. Наличие кода не заменяет CI и аппаратную проверку.
- USER32 v2: focus/active принадлежат очереди потока; GetGUIThreadInfo позволяет проверить чужую очередь. Capture один на desktop, но Get/ReleaseCapture учитывают владельца. AttachThreadInput явно не реализован; объединённые очереди не имитируются успешным stub.
- Визуальный desktop-shell ещё не существует как законченная функция.

## Этап A — корректность и регрессионная база [постоянно]
- Версионировать known-good NRO и тестовые EXE без распространения proprietary Windows DLL/бинарников.
- Host tests, x86 CPU oracle, Switch CI; редкие проверки на настоящей Switch отдельно.
- Каждый баг: воспроизводящий минимальный Win32 guest, проверка стеков/регистров/импортов и crash log.
- Матрица программ: запуск, GUI, ввод, открытие/сохранение, потоки, стабильность, производительность.

## Этап B — кооперативная многопоточность [база реализована, поддерживать регрессиями]
B1. Выполнять инструкции по квантам GuestScheduler для выбранного GuestThread; переключать FS и TEB, хостовые поля текущего потока. Проверка: два гостя увеличивают независимые счётчики, без перекрёстных записей.
B2. CreateThread, ExitThread, GetExitCodeThread, SuspendThread, ResumeThread, GetCurrentThreadId; законный worker return trampoline завершает только рабочий поток, не процесс.
B3. Для каждого потока: lastError, SEH активный стек, TLS, stack guards, DLL_THREAD_ATTACH/DETACH, TEB self and ClientId.
B4. Отделить HWND/очередь сообщений/таймеры от host render-thread. Проверка: оконный процесс с фоновым worker и без зависания интерфейса.
Критерий: Paint baseline + 2 worker guest EXE + корректное завершение/обработка exceptions на разных потоках.

## Этап C — NT объекты и ожидания [ограниченная приёмка пройдена]
- Единая таблица типизированных 32-битных HANDLE с refcounts: thread, file, event, mutex, semaphore.
- WaitForSingleObject/MultipleObjects, finite/infinite timeout, auto/manual reset, WAIT_ABANDONED; guest wait освобождает квант, не блокирует render.
- CriticalSection рекурсивность/владелец, Interlocked, таймеры и очередь сообщений.
- MsgWaitForMultipleObjects/Ex объединяют HANDLE и очередь USER32 v2; wait-all не расходует объекты до готовности всего набора и input. Поддержаны новые/уже просмотренные сообщения, QS masks, MWMO_INPUTAVAILABLE.
- QueueUserAPC и alertable SleepEx/WaitForSingleObjectEx/WaitForMultipleObjectsEx/MsgWaitEx выполняют callbacks на CPU/TEB/TLS получателя, включая очередь до старта и вложенные alertable waits.
- Именованные event/mutex/semaphore: Create/Open A/W, общий типизированный namespace внутри Process, lifetime до последнего HANDLE, Local/default и отдельные Global имена. HANDLE содержит собственные access/INHERIT/PROTECT_FROM_CLOSE; права проверяются в waits, sync mutation, thread/APC и file/CRT data I/O.
- SignalObjectAndWait: однократный signal event/mutex/semaphore, общий continuation wait, alertable/nonalertable APC, timeout/abandonment/access checks. Переполнение семафора возвращает ERROR_TOO_MANY_POSTS; signal-object lifetime не продлевается ожиданием.
- Waitable timers: Create/Open A/W, общий named/type/access контракт, manual/auto reset, relative/absolute deadlines, periodic coalescing, Set/Cancel, timer APC с FILETIME и отменой при rearm/последнем Close/выходе setter. Scheduler учитывает armed timers при простое всех потоков; native regression покрывает cancel-at-dispatch checkpoint.
- Регрессии конкуренции: auto-timer через aliases отдаёт одно срабатывание одному из трёх waiters; manual-timer будит всех; periodic wait-all сохраняет исходный deadline и timer/semaphore tokens. APC проверены на self-cancel/rearm/последний Close и перенос setter/TLS. Исправлена утечка retry deadline после ошибочного wait-all: новый независимый wait получает чистое состояние.
- Смешанные waits/teardown: нативная регрессия закрытия HANDLE после отменённого APC выявила и закрыла недостающую повторную валидацию до consume/park. Win32-гости проверяют 12 MsgWait/timer/APC циклов с блокирующим callback и sent/posted сообщениями, а также ExitThread из timer APC с удалением HWND, abandonment mutex и отменой таймера.
- Остановка исполнения: последний executor Process дренирует native frames до отмены всех потоков/waits/APC/timers и освобождения stack/TEB/TLS; nested executor не завершает внешнюю сессию. ExitProcess сохраняет заданный код, host abort использует STATUS_UNSUCCESSFUL; HANDLE tombstones и ранее полученные exit codes сохраняются. Host abort остаётся без guest callbacks; orderly process detach описан ниже, обычная выгрузка DLL — в этапе D. Добавлены native stop/ExitProcess регрессия и Win32 ExitProcess из APC при заблокированном timer callback.
- Orderly process exit: ExitProcess/CRT exit/entry RET внутри executor дренируют peers и вызывают EXE TLS/DLL_PROCESS_DETACH на requester CPU/TEB/TLS, в обратном initialization order, с ненулевым reserved и loader lock. Thread HANDLE/память requester завершаются после callbacks; host abort уведомлений не вызывает. Добавлен bridge последнего ExitThread/worker return, включая повторную проверку peers после loader lock и сохранение жизни процесса при suspended peer. Добавлен startup на main fiber и cleanup при attach failure (STATUS_DLL_INIT_FAILED), включая failed-load TLS/DllMain detach. Обычная выгрузка DLL и динамический failure rollback реализованы в D; добавлена ограниченная обработка startup SEH/CPU faults; полный NT failure contract и hardware acceptance остаются открыты.
- **Ограниченный этап C закрыт 2026-10-22**: программные gates и полный аппаратный `stage_c_acceptance.exe` v2 прошли; последний отчёт содержит `PASS: ALL 8 SUITES`. Бывшая оценка ~95% заменена результатом приёмки. Scope: один Process, HANDLE/waits/APC/timers, startup/exit/cancellation и GUI ownership. Cross-process namespace/наследование, token/DACL, I/O completion APCs, TerminateThread и полный NT-контракт остаются отдельным расширением модели. Подробности и границы — [STAGE_C_ACCEPTANCE.md](STAGE_C_ACCEPTANCE.md).
Критерий: producer/consumer guest, контеншн и тайм-ауты, отсутствие deadlock.

## Этап D — loader и системные Win32 API [существенно реализован, расширять по приложениям]
- PE32 ABI (stdcall/cdecl/thiscall), экспорт по имени/ordinal, forwarded exports, delay-load, DLL refcount and attach/detach.
- DLL unload: явные ссылки LoadLibrary/GetModuleHandleEx, PIN/UNCHANGED_REFCOUNT/FROM_ADDRESS; достижимость от EXE/закреплённых модулей/явных ссылок удерживает общие и циклические зависимости. FreeLibrary вызывает TLS/DllMain PROCESS_DETACH с reserved=0 в обратном initialization order, затем освобождает образы и host-owned static TLS у живых потоков (включая suspended), переиспользует TLS-индексы. HLE-модули закреплены. Метаданные выгруженных модулей сохраняются для безопасности snapshots. Добавлен rollback обычных динамических LoadLibrary: отклонённые/частично отображённые DLL и ненужные новые зависимости освобождаются, уже существующие и независимые вложенные загрузки сохраняются, повторная попытка начинается с чистого образа/TLS. Отказ в циклической вложенной зависимости распространяется на новые зависящие модули. Guest GetProcAddress инициализирует ленивые forwarded targets и откатывает неудачные новые edges/images. FreeLibraryAndExitThread не возвращается в выгруженный код. Ошибки отсутствующего модуля, повреждённого PE, TLS setup и отказа DllMain различаются. Необработанные guest SEH/CPU faults в attach/TLS теперь дают ошибку загрузки (1114; startup C0000142), динамический отказ проходит обычный rollback. SEH получает первую попытку; аварийный attach не получает PROCESS_DETACH, FALSE-return сохраняет отдельный cleanup. Сохранены явные ExitThread/ExitProcess и host abort; FS:[0] восстанавливается после аварийного callback. Добавлены native/PE проверки вложенного handler fault, yield и межпоточной изоляции. Полный startup image rollback/NT failure contract, guest finally unwind и LoadLibraryEx datafile/image flags ещё впереди.
- VirtualAlloc/Protect/Query/Free, page faults and stack growth, файловые пути, кодировки, реестр, загрузка ресурсов, COM/Shell.
- Полные необработанные исключения, unwind, x87 precision limitations, FPU/SSE; тесты MSVC-подобного поведения.
Критерий: реальные приложения без адресных hardcodes и чистое завершение.

## Этап E — графическая граница [реализованы E6/E7; E8/E9 открыты]
Подробный план до 100%: [STAGE_E_PLAN.md](STAGE_E_PLAN.md). Пакеты аудита/backend/software/dirty/lifetime и GPU/layers/cache уже реализованы; принятая отметка — 60%, дальнейшие аппаратные критерии остаются открытыми. Проценты — веса пакетов, не оценка сроков.
Формат: Win32 GDI/DirectDraw/Direct3D commands → независимый графический backend.
E1. Ввести границу поверхности: host-owned textures/surfaces, pixel format, clip, dirty rect, lifetime, update, blit/composite, present. Детерминированный software backend — эталон.
E2. Для GDI: выполнять семантику на CPU где это выгоднее/проще; отправлять изменённые прямоугольники в GPU, аппаратно компоновать окна, курсор и масштабирование; cache glyphs/bitmaps.
E3. Native Switch GPU backend только после проверки доступного API и режима памяти; определить подходящий libnx/deko3d/совместимый графический путь для целевого homebrew runtime. Факт наличия GPU не означает прямой аппаратной совместимости с DirectX.
E4. Измерять latency/CPU/GPU/memory на Switch, fallback на software если несовместимый путь.
Критерий: пиксельное сравнение CPU и GPU вывода на GDI-сценах + Paint visual regression.

## Этап F — XP-подобная оболочка [F0/F1/F2 idle-shell программно реализованы; F3a browser реализован; F4a active workspace реализован; F4b.1 independent input реализован; остальная функциональная F открыта]
- Общий SD-backed root/mount/path/volume contract и idle desktop/F3a browser реализованы; далее F4b/F3c/F5 по [F_SHELL_PLAN.md](F_SHELL_PLAN.md).
- Свой desktop-shell host UI: фон, taskbar, Start menu, часы, shortcuts, запуск и переключение EXE, сохранение на SD, управление Joy-Con/тач.
- Собственный файловый менеджер. Настоящий explorer.exe — необязательная отдельная цель с огромными зависимостями.
- Процессы: начать с одного адресного пространства/нескольких окон, затем полноценная изоляция нескольких экземпляров с отдельными Process и ресурсами.
Критерий: открыть Paint из «Пуска», рисовать, сохранить, найти через проводник, переключиться между окнами.

## Запланированное расширение F: установка приложений и архивы

Запрос 2026-10-24: EXE-установщики программ/игр должны писать в реальные
постоянные папки и создавать рабочие ярлыки Desktop/Start. Нужны согласованные
Program Files/profile paths, действительные Win32/registry/shortcut API и явные
ограничения child-process/MSI/COM. Затем импорт ZIP/RAR/других архивов с реальным
распаковыванием, выбором папки, сохранностью файлов и проверкой ошибок.
Подробные зависимости/критерии: [F_INSTALL_AND_ARCHIVES.md](F_INSTALL_AND_ARCHIVES.md).
Это будущий блок после базового F5, не заявление о текущей поддержке произвольных
установщиков или игр. Текущая очередь — [ACTIVE_ROADMAP.md](ACTIVE_ROADMAP.md).

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

## Актуальная очередь 2026-10-23 (после аудита)

1. **F1 реализован:** постоянные C/S/R, per-drive CWD/readonly, согласованные
   loader/CRT/dialog/profile пути и реальные statvfs space APIs. Контракт,
   совместимость старых запусков и пределы — [F1_FILESYSTEM.md](F1_FILESYSTEM.md).
2. **F4b/F3c/F5:** retained-композиция и взаимодействие поверх F4a,
   затем постоянные настройки и действия Start/tree поверх F3a/F3b, сохранение/поиск/повторное открытие документа.
   F0 session/picker foundation не означает готовую оболочку.
3. **D:** файловые недостачи, нужные F1; отдельно page-permission enforcement,
   bounded SEH/unwind/LoadLibraryEx и воспроизводимые API incompatibilities.
4. **E:** сохранить реализованные software/GPU/layers/cache/telemetry/tests;
   E8/E9, реальный Paint/GPU/fallback/input/relaunch остаются одним отложенным
   аппаратным списком. Не переоткрывать SD и не засчитывать host PASS как Switch.
5. **G/H:** G0 сохранить; timing/overhead/fast paths после рабочей F-вехи или
   измеренного blocker. Dynarec и DirectDraw/D3D — отдельные программы.

Сводка доказательств: [PROJECT_AUDIT_2026_10_23.md](PROJECT_AUDIT_2026_10_23.md).
Пакеты и критерии F, предложенный layout и лицензируемые visual sources:
[F_SHELL_PLAN.md](F_SHELL_PLAN.md). F1 mounts, desktop/Start/explorer и F4a/F4b.1 уже подключены; create/rename/delete/properties подключены; Copy/Paste UI подключён; контекстные меню подключены; сортировка/ассоциации сохраняются в профиле; документы и полная непрерывность среды остаются открыты.

## Историческая очередь (сохранена для контекста)
1. **Зафиксировать базу:** Stage C принят на `6aacc4e`; сохранить рабочую NRO. Paint hardware baseline остаётся `8b0ad70`: приёмка C сама по себе не доказывает Paint open/save на новом SHA. Host/MinGW, CPU oracle, ARM64/QEMU и Switch build остаются обязательными регрессиями.
2. **Следующий пакет E1:** проследить Surface/GDI → композицию окон → platform present; ввести явный renderer-backend interface с правилами ownership/lifetime, формата пикселей, clip, dirty rects и resize. Вынести существующий software путь за интерфейс, сохранив CPU GDI эталоном. Добавить измерения времени/числа present и объёма обновлений. Приёмка: детерминированное сравнение пикселей до/после, teardown/resize/clip, существующие GUI/input tests и Paint smoke. Это ещё не GPU backend.
3. **Базовый аппаратный JIT probe уже выполнен:** 2026-10-22 пользователь подтвердил, что в прежней переписке до переноса проекта запускал probe на Switch и получал 42. Повторный запуск минимального probe не является текущей блокирующей задачей. Старое фото/точный SHA, режим запуска, Result-коды и результат cleanup в репозитории не найдены; их не выдумывать. Это подтверждение исполнения минимальной ARM64-функции со слов пользователя, а не приёмка dynarec. При интеграции проверить реальный code-cache: rewrite/invalidation, icache, lifetime и безопасный fallback; окружение при следующем таком тесте записать.
4. **После E1:** выбирать E2/E3 по измерениям и доступному Switch API: dirty uploads и композиция с сохранением software fallback; доказать пиксельную эквивалентность и реальное улучшение на консоли. D дорабатывать по конкретным несовместимостям и минимальным воспроизводящим тестам, не ждать абстрактной полной Win32-совместимости.
5. **Следующие вехи:** F — минимальный host-shell в модели одного Process (запуск Paint, окна, файлы, ввод); G — профилирование и первый ограниченный x86→ARM64 backend с oracle/fallback, используя подтверждённую базовую JIT-возможность и отдельные проверки code-cache при интеграции. Не запускать полную оболочку, GPU и dynarec одной неделимой переделкой. H DirectDraw/D3D — самостоятельный последующий блок.

Правило: изменения исключительно в ветке `claude/wonderful-hawking-10mpr0`. Не объявлять CI-успех для коммита, пока нет зелёного workflow с тем же SHA.

## Architecture review: external recommendations (2026-10-21)
Independent review found four actionable risks; refined against existing design:
1. **Instruction quantums already preempt pure guest spin loops.** Actual starvation danger is nested guest callbacks / blocking HLE calls that monopolize the host thread, so test an infinite-looking busy spin waiting on worker and audit HLE callbacks. Eventually implement MsgWaitForMultipleObjects and APC alongside real per-thread message queues; do not emulate as ordinary WaitForSingleObject.
2. **JIT feasibility probe moves earlier**, before implementing ARM64 dynarec: use libnx `jitCreate`, `jitTransitionToWritable/Executable`, and `jitGetRwAddr/jitGetRxAddr`; run a minimal generated ARM64 function on physical Switch in actual target homebrew launch mode. Confirm permissions and I-cache synchronization, code-memory lifetime, and W^X without assuming `svcMapProcessCodeMemory` must be called manually. Keep current interpreter fallback. libnx API docs: https://switchbrew.github.io/libnx/jit_8h.html
3. GDI remains synchronous CPU rasterizer and pixel-correct reference; GPU used for dirty-rect upload, batching and desktop composition; DirectDraw/D3D translator is a separate backend. Never force every GDI call through GPU.
4. Expand old-compiler SEH ABI tests (MSVC/Delphi/CRT) with nested, collided unwinds, register/flags/FP restoration. Apply targeted compatibility matrix, not unbounded demand for "100% Win32".
Acceptance gates: a native spin-lock guest test completes under scheduler quanta; no host API call starves render loop; JIT probe works on user's actual Switch before dynarec milestone is scheduled; GDI image comparison passes software vs composited output.


## Уточнение аппаратного JIT результата (2026-10-22)
Пользователь сообщил: probe уже проверяли в другой переписке до переноса,
сгенерированный ARM64-код возвращал `42`. Прежняя запись «нет физической
проверки» устарела. Найдены коммиты реализации `bb33f1e`, перехода на Pad API
`904347f` и изоляции CI `3d7081e`; отдельного прежнего коммита с аппаратным
результатом при просмотре истории/документов не обнаружено. Эта запись
сохраняет текущее подтверждение пользователя, не подменяет отсутствующие
Result-коды/фото и не обещает JIT в любом режиме запуска.


Native TXT create/read/line-edit/save/Save As is now implemented with bounded
UTF/BOM preservation, retained backups and real conflict/error reporting; see
[F_TEXT_DOCUMENTS.md](F_TEXT_DOCUMENTS.md). Next: supported image viewing and F5.


Крупные блоки и ближайший проверяемый рубеж для пользователя: [PROJECT_STATUS_RU.md](PROJECT_STATUS_RU.md).
Просмотр BMP: [F_IMAGE_DOCUMENTS.md](F_IMAGE_DOCUMENTS.md).
