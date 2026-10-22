# Архитектура switch-recompiler v2

Срез кода: 2026-10-21, ветка `claude/wonderful-hawking-10mpr0`.
Это карта реализации, а не заявление о полной совместимости с Windows.
Историческое описание v1 сохранено в [архиве](archive/ARCHITECTURE_v1.md).
Результаты проверок конкретных коммитов — в [AI_HANDOFF.md](AI_HANDOFF.md).

## Путь исполнения

1. `runtime/process.*` загружает PE32 EXE и DLL в `PagedGuestMemory`:
   секции, релокации, экспорты по имени/ординалу и forwarded exports.
   Системные DLL связываются с HLE; прочие DLL можно исполнять как x86.
2. `win32/iat_patcher.*` связывает импорты с адресами гостя либо с
   host-сентинелами. `host_functions.*` маршрутизирует их в модули HLE,
   соблюдая размер аргументов и соглашение вызова.
3. `Process` создаёт главный `GuestThread`: CPU, стек, TEB, TLS и общий PEB.
   `CreateGuestThread` выделяет независимые состояния рабочих потоков.
4. Основные циклы в `main.cpp` и `tests/winrun.cpp` используют
   `GuestExecution`: он выбирает поток через `GuestScheduler::Next()` и
   возобновляет его сохранённый нативный стек. Checkpoint переключает
   исполнение примерно через 256 операций, включая вложенный гостевой код.
   Интерпретатор получает FS/GS выбранного TEB.
5. `cpu/interpreter.cpp`, `fpu.cpp`, `sse.cpp` исполняют IA-32, x87 и
   MMX/SSE/SSE2. Старый `decoder/x86_decoder.*` сохраняется для листинга
   и общих описаний инструкций; он не является главным ядром исполнения.
6. HLE Win32 меняет память/ресурсы либо вызывает гостевой callback.
   USER32/GDI формируют программную поверхность; Switch-фронтенд выводит
   её и преобразует тач/контроллер в события ввода.

## Память, ABI и исключения

- `cpu/paged_memory.h`: гостевые 32-битные адреса, отображение страниц,
  защиты и проверка диапазонов. `FlatGuestMemory` остаётся для старых тестов.
- `CpuState` хранит целочисленные и FP/SIMD регистры и ленивые флаги.
  Ошибка `Interpreter::Step` возвращается как `CpuException` с адресом
  инструкции и параметрами ошибки доступа.
- `win32/seh_dispatch.*` обслуживает CPU faults и `RaiseException` через
  FS:[0], x86 EXCEPTION_RECORD/CONTEXT и интерпретируемые обработчики.
  Есть target/exit unwind и проверяемые подмножества nested/collided unwind.
- Состояние активных SEH-обработчиков принадлежит гостевому потоку.
  Точность x87 ограничена внутренним double; полный NT SEH ABI не заявлен.
- `CallGuestFunctionStdcall` остаётся синхронным для своего вызывающего,
  но его нативный стек сохраняется `runtime/guest_execution.*` и
  `host_fiber.*`. Ожидание, Sleep, квант или busy-spin внутри WndProc/SEH
  могут отдать исполнение другим гостям без рекурсивного исполнения peers.
- Основной поток получает 2 MiB нативного стека, рабочие — 512 KiB каждый.
  Linux x86 использует ucontext; AArch64 (Switch и Linux/QEMU) — собственное
  сохранение callee-saved GPR/FP, SP и FPCR/FPSR по AAPCS64. Это программные
  continuations на одном OS thread; нативные блокирующие syscalls остаются
  отдельной проблемой. Есть canary и проверка запаса стека на safepoints,
  но аппаратного guard page на Switch пока нет.
- При переключении восстанавливаются USER32 GuestCtx, TEB binding и host
  errno. Глубина callback и активные SEH frames принадлежат GuestThread.
  Отдельная копия CpuState SEH-обработчика остаётся на сохранённом стеке.
- При ExitThread исходный вложенный гостевой код больше не исполняется,
  нативные обёртки возвращаются обычным путём. Затем на том же guest fiber
  выполняются TLS/DLL detach callbacks. При остановке всей сессии pending
  continuations дренируются с флагом выхода, чтобы выполнились деструкторы.
  Завершённый нативный стек освобождается после возврата на стек scheduler.
- USER32 v2 хранит отдельные очереди и маршрутизирует cross-thread SendMessage
  владельцу HWND; GetMessage при живых peers периодически отдаёт исполнение.
  MsgWait объединяет HANDLE и очередь, user APC исполняются в alertable waits.
  Полной изоляции input/focus/hooks пока нет.

## Процесс, потоки и HANDLE

`Process` владеет модулями, главным потоком, устойчивым хранилищем рабочих
потоков, планировщиком и таблицей HANDLE. После завершения потока
GuestExecution разворачивает его оставшиеся нативные кадры, удаляет continuation
и освобождает принадлежащие потоку stack/TEB/TLS mappings. Адреса выделений
хранятся на хосте, независимо от изменяемых гостем указателей TEB/TLS.
У главного потока освобождается страница TEB, но сохраняется соседняя PEB.
Метаданные потока остаются в Process/Scheduler для HANDLE и exit-code queries;
это ещё не полная очистка объектов NT.

- До первой инструкции worker исполняются TLS callbacks и
  `DllMain(DLL_THREAD_ATTACH)` в порядке завершённой инициализации модулей;
  `CREATE_SUSPENDED` откладывает их до фактического запуска. EXE получает TLS
  thread callbacks, но не DllMain. При штатном выходе порядок модулей обратный,
  внутри модуля TLS callbacks предшествуют DllMain. Detach заканчивается до
  смены exit code, сигнализации HANDLE, abandonment mutex и освобождения памяти.
- Recursive loader lock принадлежит guest thread, а не native OS thread.
  Ожидающие гости паркуются на внутреннем scheduler token. Lock охватывает
  LoadLibrary/инициализацию, GetProcAddress, GetModuleHandle и уведомления;
  новые потоки ждут завершения текущего DLL callback перед стартом.
  Вызов из DllMain может повторно войти в loader на том же guest thread.
- `Module` различает initializing/initialized/failed; завершённый порядок
  хранится отдельно от порядка загрузки. Callback работает с локальной копией
  CPU и текущим TEB, сохраняя EIP/ESP/регистры вызывающего HLE или entry point.
- Статический TLS при LoadLibrary выделяется всем живым потокам, включая
  suspended; существующие потоки не получают ретроспективный THREAD_ATTACH.
  `DisableThreadLibraryCalls` отключает thread notifications для DLL без
  статического TLS; неверный handle и DLL со статическим TLS отклоняются.

- `GuestScheduler` чередует runnable-потоки, учитывает suspend count,
  Sleep и тайм-ауты ожиданий по монотонным абсолютным срокам.
- Блокирующий HLE wait оставляет EIP/ESP нетронутыми (`customReturn`),
  переводит поток в Waiting и повторяет вызов после пробуждения.
- Сигнал будит кандидатов; ресурс потребляет только успешный повтор HLE.
  Wait-all сначала проверяет всю группу, затем потребляет её состояния
  внутри одной кооперативной операции. Повтор не продлевает тайм-аут.
- `runtime/handle_table.*`: единое пространство файлов, потоков, событий,
  мьютексов, семафоров и waitable timers. Числовой HANDLE — отдельная ссылка на общий объект,
  дубликат получает новое значение. В пределах Process номера не повторяются.
- `FileTable` — совместимый фасад над той же таблицей для Win32 и CRT.
  Общая ссылка на OpenFile сохраняет файловую позицию между дубликатами.
- Сигнал объекта будит ожидания через все его HANDLE. Закрытие удаляет
  только одну ссылку. Закрытый ожидаемый HANDLE повторно проверяется и
  даёт WAIT_FAILED: это защитная политика рантайма; Windows такое действие
  во время активного ожидания определяет как неопределённое поведение.
- При завершении потока сигналятся все его HANDLE; принадлежащие ему
  мьютексы становятся abandoned. Получивший владение возвращает
  WAIT_ABANDONED с нужным индексом. Незавершённый wait-all владение не забирает.
- `ExitThread` завершает только выбранный поток, включая главный.
  При завершении последнего потока циклы выходят с его кодом; `ExitProcess`
  и возврат из точки входа EXE по-прежнему завершают процесс целиком.

HANDLE table хранит отдельные access mask и INHERIT/PROTECT_FROM_CLOSE flags
для каждой ссылки. Create/Open sync A/W, CreateThread/CreateFile и DuplicateHandle
задают эти свойства; Get/SetHandleInformation меняют флаги. Защищённый CloseHandle
возвращает ERROR_ACCESS_DENIED и сохраняет объект/ожидания. DuplicateHandle
с SAME_ACCESS сохраняет права, но задаёт наследуемость из аргумента и не копирует
защиту. Runtime также отвергает CLOSE_SOURCE для защищённого источника без
публикации дубликата (защитная политика, не полная эмуляция NT/debugger исключений).
SYNCHRONIZE требуется всем объектным waits, MODIFY_STATE — событиям/семафорам;
ReleaseMutex определяется владением потока, MUTEX_MODIFY_STATE не нужен.
Query/suspend/APC проверяют права thread HANDLE. Read/WriteFile, WriteConsole,
SetEndOfFile и CRT data I/O проверяют права ссылки, не только общий OpenFile.
Файловый DuplicateHandle не повышает права исходной ссылки.

Process-owned weak namespace разделяется event/mutex/semaphore/timer: имя чувствительно
к регистру, A декодирует ACP1252, W сохраняет UTF-16. Local/default совпадают,
Global отдельно; это разделение имён внутри единственного emulated process,
не настоящий межпроцессный namespace Horizon. Повторный Create того же типа
возвращает новый HANDLE и ERROR_ALREADY_EXISTS, не меняя состояние/владельца/
лимит объекта; другой тип даёт ERROR_INVALID_HANDLE. Последний Close удаляет
объект, weak name не удерживает его живым. Open не создаёт отсутствующий объект.

Ограничения: нет межпроцессного DuplicateHandle/named namespace, передачи
INHERIT handles дочерним процессам, token/DACL/security descriptor enforcement,
полного аудита прав всех metadata/legacy Win32 API и I/O completion APCs. Security descriptor в SECURITY_ATTRIBUTES пока игнорируется; INHERIT
сохраняется и доступен через HandleInformation. Нет DLL unload/refcounts, rollback отображений неудачной
загрузки, полноценного PROCESS_DETACH/ExitProcess shutdown и TerminateThread.
Инициализация до GuestExecution остаётся синхронной; этот путь не поддерживает
блокирующие ожидания между гостевыми потоками из startup DllMain.
Стандартные HANDLE консоли
пока сохраняют прежнюю политику CloseHandle. Для standalone/v1 HLE
используется отдельная резервная таблица, когда нет `rt::g_process`.

## Win32 и файловая система

`win32/kernel32_*.cpp` реализуют загрузку, память, файлы, строки, систему
и синхронизацию; `msvcrt_*.cpp` — CRT, форматирование, строки и stdio.
`runtime/files.*` отображает гостевой C: на каталог EXE и ищет существующие
компоненты путей без учёта регистра. Это ещё не изолированная Windows ФС.
`runtime/heap.*` и `resources.*` обслуживают выделения и ресурсы модулей.
Реестр остаётся в `virtual_registry.*`.

`comdlg32_module.cpp` реализует общие Open/Save диалоги на новом USER32.
SHELL32, OLE32/OLEAUT32, COMCTL32, GDI+ и UXTHEME имеют частичные HLE-модули;
наличие экспорта не означает полной реализации его Windows-контракта.

## Два графических пути

| Путь | Код | Назначение |
|---|---|---|
| Прежний v1 | `win32/gdi.*`, `window_manager.*`, MFC в `host_functions.cpp`, оверлей `main.cpp` | Совместимость с проверенными Сапёром и Paint через MFC-заглушки |
| Новый v2 | `win32/user32_*.cpp`, `gdi32_module.cpp`, `source/ui/*` | Общие окна/controls, GDI, настоящая гостевая MFC DLL |

Новый USER32 строит классы/окна, управление фокусом, меню, диалоги,
таймеры и controls. `ui::Gdi` растеризует DC, битмапы, шрифты TrueType,
области и ROP в CPU-поверхности. GPU backend и готовой desktop-shell нет.
`Window::threadId` фиксирует создателя HWND. `State::queues` содержит отдельные
posted/input/sent queues, WM_QUIT, таймеры, keyboard snapshot и last MSG для
каждого guest thread. Очередь создаётся лениво при USER32 API/создании окна;
PostThreadMessage отвергает поток без очереди и завершённый поток. PostMessage
доставляется владельцу окна; NULL HWND относится к текущему потоку. WM_QUIT
извлекается независимо от фильтра, PM_NOREMOVE сохраняет сообщение.

`user32_thread.cpp` хранит shared sent-message requests. В том же потоке WndProc
вызывается непосредственно; между потоками отправитель ждёт, а получатель
обрабатывает запрос в Get/PeekMessage/WaitMessage или при собственном ожидании
SendMessage. Отправитель по умолчанию принимает встречные sent messages, не
извлекая posted messages; обычный kernel wait и Sleep их не обрабатывают.
WndProc исполняется с локальной копией CPU получателя и его TEB/TLS; исходные
аргументы/EIP/ESP внешнего HLE сохраняются. ReplyMessage может освободить
отправителя раньше возврата из WndProc; InSendMessage/Ex отражают вложенный
приём. SendNotifyMessage между потоками ставит неблокирующий sent request.
SendMessageTimeout поддерживает finite deadline и SMTO_BLOCK; ожидающий гость
не блокирует host thread. Не начатый просроченный запрос пропускается; уже
работающий обработчик продолжает выполнение. Hung detection и дополнительные
SMTO flags не реализованы (за пределами NORMAL/BLOCK/ABORTIFHUNG флаги отвергаются;
ABORTIFHUNG пока использует обычный deadline без отдельной диагностики hung).

Штатный ExitThread сначала разворачивает исходные native callbacks, затем
уничтожает собственные окна в контексте владельца, выполняет DLL/TLS detach
и закрывает очередь. Pending sends к уничтоженному HWND/завершённому потоку
завершаются ошибкой. DestroyWindow из чужого guest thread отклоняется.
Логическое удаление HWND немедленно; Window/control state и удалённые WndClass
сохраняются до выхода всех активных native HLE/window frames. Это консервативная
защита Window* при reentrancy, не универсальное решение lifetime всех GDI/hook
объектов. Очереди завершённых потоков остаются как закрытые записи.

MsgWaitForMultipleObjects/Ex и WaitMessage проверяют очередь вместе с объектами
через общий preflight/consume слой KERNEL32. Wait-all проверяет весь набор и
input до получения mutex/auto-event/semaphore; wait-any предпочитает младший
готовый HANDLE. MsgWait не извлекает сообщения и не вызывает WndProc.
Обычный режим ждёт нового input; MWMO_INPUTAVAILABLE видит и уже просмотренный.
GetQueueStatus возвращает high-word available/low-word changed и сбрасывает
запрошенные change bits. Поддержаны posted/quit, sent, key/mouse, timer/paint;
это не полная история Windows raw/system input. Во время MsgWait frontend
опрашивается с отдачей guest fiber через 1 ms, без блокировки других CPU.

QueueUserAPC хранит FIFO в GuestThread и адресно прерывает alertable wait,
не сигналя сам объект. SleepEx, WaitForSingleObjectEx/MultipleObjectsEx и
MsgWaitEx возвращают WAIT_IO_COMPLETION после доставки. Callback получает
локальный CpuState с TEB/TLS получателя; обычные waits внутри APC не становятся
alertable автоматически. Явный вложенный alertable wait допускает рекурсивную
доставку. APC до старта выполняются после DLL_THREAD_ATTACH до user entry;
при завершении pending очередь удаляется. I/O completion APCs пока не реализованы.

SignalObjectAndWait использует те же signal helpers, что SetEvent,
ReleaseMutex и ReleaseSemaphore, затем сохранённую native continuation общего
object-wait loop. Сигнал выполняется один раз, повторные пробуждения проверяют
только wait HANDLE. Alertable режим доставляет APC после сигнала, обычный
оставляет их в очереди. Проверяются SYNCHRONIZE, MODIFY_STATE и mutex ownership;
переполнение семафора возвращает ERROR_TOO_MANY_POSTS. Ссылка на signal object
освобождается до парковки, чтобы не продлевать lifetime его имени. Защитная
политика: неверный wait HANDLE/права отклоняются до signal mutation; закрытие
wait HANDLE во время ожидания даёт WAIT_FAILED. Эти случаи не заявляются как
полная NT failure-ordering fidelity. На Windows signal/acquire не гарантируют
атомарность относительно потоков на других процессорах; runtime не обещает её.
Standalone HLE без GuestExecution умеет только немедленную проверку, как прежние
object waits; рабочие host/Switch циклы используют GuestExecution.

Waitable timers: TimerObject в общей HANDLE table, Create/Open A/W и Set/Cancel.
Manual-reset остаётся signaled до Set; auto-reset потребляется только успешным
wait. Cancel останавливает сроки/APC, сохраняя signaled state. Set очищает
сигнал и предыдущий callback. Relative 100-ns input округляется вверх до us
и считается по monotonic Clock; absolute UTC FILETIME проверяется по wall clock
на каждом опросе. FILETIME API обоих HLE путей использует тот же источник с
fast-forward skew тестов. Periodic deadlines сохраняют фазу, пропущенные периоды
объединяются; таймер не накапливает токены и имеет не больше одной queued APC.

HandleTable хранит только weak timer registry; Process подключает poll/pending
hooks к scheduler. Expiry проверяется до выбора потока и при object/APC waits.
HasPendingTimers учитывает armed timers, поэтому бесконечный wait не объявляется
deadlock при простое всех CPU. Кванты/idle polling задают точность, не 100 ns.
TimerAPC получает data/FILETIME low/high на CPU/TEB/TLS setter thread и исполняется
только alertably. Queue entries содержат weak source + generation; Set/Cancel,
последний Close и выход setter с callback удаляют queued notification. Таймер
без callback переживает выход setter. Диспетчер проверяет deque после yield:
peer может отменить APC на checkpoint. Никакая source shared_ptr не переживает
вызов callback; допускаются cancel/rearm/close из гостевого callback.
Нет аппаратного wake-from-suspend (fResume: success + ERROR_NOT_SUPPORTED),
гарантии XP low-power clock поведения, high-resolution/coalescable Ex API и
проверки реальной точности/сна Horizon. Threadpool timers и I/O APC — отдельно.

Focus/active/capture, async input, меню и hooks ещё имеют общие состояния;
AttachThreadInput и полные правила marshalling сообщений не реализованы.
Старый v1 message loop остаётся отдельным compatibility path.
На Switch новый путь Paint выбирается удержанием L; DLL должна лежать
рядом с EXE. Проверка пользователя подтвердила Open/Save на базовой сборке
`2c9a1fa`; последующие изменения требуют собственной проверки на железе.

## Проверки и дальнейшая граница

- `tools/run_host_tests.sh`: scheduler, 5 исходных x86-регрессий,
  MinGW EXE/DLL через winrun, проверки потоков/SEH/файлов/GUI.
  При наличии `local/` дополнительно запускаются собственные Paint/Winmine.
- `tools/run_cpu_oracle.sh 50000 all`: 3 seed, сравнение с настоящим x86
  CPU, включая integer/x87/SSE. Не доказывает полноту набора инструкций.
- `.github/workflows/ci.yml`: host tests, CPU oracle, основная Switch-сборка
  и отдельная экспериментальная JIT-сборка. Дополнительный job исполняет
  тот же ARM64 context switch под Linux/QEMU и проверяет регистры/FP/RAII;
  это не заменяет теста на настоящем Horizon.
- `tools/jit_probe/`: standalone ARM64 `movz w0,#42; ret` через libnx JIT.
  Сборка не доказывает права выполнения на Switch. Dynarec в ядро не включён.

Следующие архитектурные задачи: аппаратная проверка continuations,
полный process/DLL shutdown, timer contention/edge cases и input ownership,
затем граница compositor/backend. JIT — после стабилизации этих контрактов
и отдельной проверки разрешений на физическом устройстве.
