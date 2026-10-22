# Этап C: приёмка на Switch

Этап C относится к одному эмулируемому процессу: HANDLE, sync objects,
кооперативные ожидания/APC/timers и завершение потоков. Это не приёмка
полного ядра NT, межпроцессной безопасности, оболочки или JIT.

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
   Запустить свежую сессию NRO обычным используемым способом и выбрать EXE.
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
с новой сессии NRO; тест перезаписывает только собственный отчёт.

Нативный test_gui_input дополнительно вводит клавиатуру/мышь от платформы,
когда её опрашивает другой поток, и проверяет host-abort cleanup. Эти
инъекции не выдаются за аппаратную проверку тача/стиков/физической клавиатуры.
Paint open/save smoke остаётся отдельной проверкой совместимости приложения.

## Что остаётся вне этого gate

AttachThreadInput пока явно возвращает ERROR_CALL_NOT_IMPLEMENTED: чужие
очереди не считаются присоединёнными. GetGUIThreadInfo отдаёт active/focus/
capture; caret/menu/move-size поля пока нулевые. Полная Win32 foreground policy,
implicit input attachment, hooks/menu ownership, TerminateThread,
cross-process namespace/security и I/O completion APC — отдельные задачи.
Успех приёмки закрывает ограниченный этап C, не объявляет весь Win32 готовым.
