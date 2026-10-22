# Этап C: приёмка на Switch

Этап C относится к одному эмулируемому процессу: HANDLE, sync objects,
кооперативные ожидания/APC/timers и завершение потоков. Это не приёмка
полного ядра NT, межпроцессной безопасности, оболочки или JIT.

## Результат приёмки: 2026-10-22

**PASS: ограниченный этап C принят на физическом Switch.**
База реализации — `6aacc4e013e55be84a6f7860dfb34e810c6fbda0`;
все пять jobs [CI 39810630356](https://github.com/Omeruno/switch-recompiler/actions/runs/39810630356)
завершились успешно. Пользователь сообщил об отдельных запусках
`gui_thread_input.exe` и `stage_c_acceptance.exe` v2 на своей Switch:
оба показали тестовые окна и завершились самостоятельно.

Присланный [полный отчёт](evidence/stage-c-6aacc4e-acceptance.txt) содержит
PASS для всех восьми suites и финальное `PASS: ALL 8 SUITES`.
[Журнал шагов](evidence/stage-c-6aacc4e-details.txt) относится к combined
acceptance, содержит `input passed` и переход к suite 8, без FAIL.
Второй запуск перезаписал детали первого; это ожидаемо и не портит
свидетельство полного прогона. Строка RUNNING в начале — постоянное
предупреждение заголовка; финальный PASS завершает отчёт.

Атрибуция к базе v2 следует из выданного пакета и сообщения пользователя;
отдельный execution-log с Build/UI и хеш самой NRO не предоставлены.
Не выдавать это за криптографическую сверку установленной сборки.
Первоначальный сбой v1 более не блокирует gate; исправление FILE доказано
регрессией, но точная причина старого аппаратного вылета отдельно не установлена.
Инструкции ниже сохраняются для повторной приёмки будущих изменений.

## Программные критерии

| Блок | Обязательные проверки |
| --- | --- |
| HANDLE lifetime/access/aliases | test_handle_objects; gui_thread_named/handles/wait_lifecycle |
| Транзакционные waits, signal/timeout/abandonment | test_signal_wait/test_wait_retry_state; gui_thread_sync/wait_multiple/signalwait |
| APC/timers/MsgWait, повторная валидация | test_waitable_timer/test_alertable_revalidation; gui_thread_timer_contention/mixed_apc/timer_exit |
| Native frames и cancellation | test_guest_execution/test_execution_stop; test_gui_input (host abort и ExitProcess) |
| Startup/exit loader boundary | test_startup_lifecycle/test_process_detach/test_initfault; соответствующие PE fixtures |
| GUI ownership на границе потоков | test_gui_input; gui_thread_input |

Эти проверки обязательны в `sh tools/run_host_tests.sh`; отдельно CI собирает
NRO, исполняет ARM64 fiber ABI под QEMU и сверяет CPU oracle. Успех относится
только к конкретному SHA. Аппаратный gate не заменяется зелёным CI.

## Один аппаратный запуск

1. Сохранить известную рабочую NRO. Последняя подтверждённая пользователем
   Paint-база — `8b0ad70`.
2. Взять обычную `switch-recompiler-nro` из зелёного CI нужного коммита
   и `stage_c_acceptance.exe`, собранный из того же исходного дерева.
   Экспериментальный JIT probe для этого теста не используется.
3. Положить EXE в отдельный каталог на SD. Дополнительные Windows DLL не нужны.
   Запустить свежую сессию NRO **без зажатой L** и выбрать EXE.
   Для этих EXE L включает старый UI; это другой режим, не данный gate.
   Во время автоматического прогона не нажимать кнопки и не переключать окна.
4. При успехе на несколько секунд появится окно `Stage C: PASS`.
   В каталоге EXE появится `stage-c-acceptance.txt`.
5. Нужна последняя строка **`PASS: ALL 8 SUITES`** и запись SHA/способа запуска
   NRO. `RUNNING`, неполный отчёт, `FAIL` или отсутствие файла не означают успех.

Внутри — те же исходные тесты CI, с отдельными translation units и
переименованными main: mutex/semaphore; multiple waits; named/access;
SignalObjectAndWait; timer contention; смешанные MsgWait/APC; GUI focus/capture
и ExitThread из callback; ExitThread из timer APC с abandonment/teardown.
Отчёт сбрасывается на SD после каждого BEGIN/PASS, поэтому незавершённый
прогон оставляет название последнего начатого блока. При повторе начать
с новой сессии NRO; тест перезаписывает собственные отчёт и журнал деталей.

## Диагностика незавершённого прогона (v2)

Полученный аппаратный отчёт v1 прошёл блоки 1–6 и оборвался после BEGIN 7.
Чёрный экран с последующим выходом не устанавливает причину: CHECK мог
вызвать ExitProcess, мог остановиться интерпретатор или нативный фронтенд.
Отдельный запуск с L оборвался в блоке 3 старого UI; эти два отчёта нельзя
объединять в один успешный прогон.

Для следующей проверки взять обычную NRO и EXE из одного коммита v2.
Сначала запустить **gui_thread_input.exe без L** в новой сессии NRO.
Это ровно тело блока 7 с отдельным запуском и журналом контрольных точек;
оно не требует повторения блоков 1–6. После выхода сохранить:

- `stage-c-details.txt` рядом с EXE: последний шаг, точный CHECK, строка,
  поток, tick и сохранённый Win32 last-error;
- `sdmc:/switch-recompiler-execution.txt`: SHA сборки, EXE, UI, ограниченный
  вывод гостя и причина остановки с exit code/счётчиком инструкций;
- `sdmc:/switch-recompiler-crash.txt`, если появился: расширенный stop/fatal
  report. Старый crash-файл может остаться от предыдущего запуска: сверять
  EXE/сборку, а execution-файл перезаписывается при каждом запуске.

Журналы нужно забрать до следующего запуска. Отсутствие STOPPED при наличии
RUNNING означает, что обычный путь завершения не закончен, а не PASS.
Диагностика не гарантирует запись при аварии самого libnx/ОС.
Вывод гостя ограничен 16 KiB полезного текста; журнал не трассирует каждый
кадр или инструкцию. CHECK и условия приёмки не ослаблены.

После успешного отдельного теста всё ещё нужен полный `stage_c_acceptance.exe`
v2 с последней строкой `PASS: ALL 8 SUITES` на той же NRO.

Нативный test_gui_input дополнительно вводит клавиатуру/мышь от платформы,
когда её опрашивает другой поток, и проверяет host-abort cleanup. Эти
инъекции не выдаются за аппаратную проверку тача/стиков/физической клавиатуры.
Он также исполняется с реальным временем и задержкой present в 16 ms;
это приближает тайминг, но не проверяет framebuffer/libnx на самой консоли.
Paint open/save smoke остаётся отдельной проверкой совместимости приложения.

## Что остаётся вне этого gate

AttachThreadInput пока явно возвращает ERROR_CALL_NOT_IMPLEMENTED: чужие
очереди не считаются присоединёнными. GetGUIThreadInfo отдаёт active/focus/
capture; caret/menu/move-size поля пока нулевые. Полная Win32 foreground policy,
implicit input attachment, hooks/menu ownership, TerminateThread,
cross-process namespace/security и I/O completion APC — отдельные задачи.
Успех приёмки закрывает ограниченный этап C, не объявляет весь Win32 готовым.
