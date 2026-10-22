# Этап E — графическая граница и ускорение вывода

Статус: план реализации после принятого Stage C; 2026-10-22.
База исполняемого кода: `6aacc4e`; Stage C прошёл на Switch и в CI.
Простой ARM64 JIT probe возвращал 42 по сообщению пользователя; повтор этого
probe не блокирует E. Это не утверждение о готовом dynarec.

## Что означает 100%

Общий интерфейс графического backend, проверенный software reference,
корректные dirty updates и lifetime, рабочий Switch GPU output/compositor,
ограниченные кэши и измеренная аппаратная приёмка без регрессии Paint/input.
CPU GDI остаётся источником Win32-семантики: ROP, палитры, clipping, DIB и
чтение пикселей не заменяются приближёнными GPU-операциями.
JIT, shell, DirectDraw/D3D и полная Win32-совместимость — другие этапы.

Проценты ниже — фиксированные веса проверяемых пакетов этого этапа, не сроки
и не процент всего проекта. Пакет засчитывается только после критериев и
CI его точного SHA. Аппаратные пакеты требуют реального Switch. Одного
интерфейса или зелёной сборки GPU недостаточно для 100%.
Первый программный пакет E0–E2 реализуется; итоговые 35% не засчитываются до exact-SHA CI и аппаратного smoke. Аудит, границы и инструкции: [STAGE_E_RENDERER_AUDIT.md](STAGE_E_RENDERER_AUDIT.md).

## Текущая архитектура

- `source/ui/surface.h`: borrowed Surface и OwnedSurface, DIB форматы
  1/4/8/16/24/32 bpp, палитра, pitch (включая отрицательный).
- `source/ui/ui_gdi.cpp` и `source/win32/gdi*.cpp`: программная растеризация
  и Win32 bridge. Гостевая DIB-память может изменяться напрямую.
- `source/win32/user32.h`: State.screen; Platform.present уже существует.
- `source/win32/user32_message.cpp::PresentIfNeeded`: ScreenChangeCounter
  и ограничение частоты показа; это ещё не прямоугольники повреждений.
- `source/main.cpp::newui::Present/Blit`: заимствованная ссылка на screen,
  полное CPU-масштабирование/конвертация в framebuffer и курсор поверх кадра.
  Ввод умеет вызывать Blit при перемещении курсора без изменения GDI.

Это предварительная карта по чтению кода; E0 должен проверить все пути,
включая legacy UI, cursor-only redraw и прямые изменения DIB.

## Пакеты и критерии завершения

| Пакет | Доля / итог | Работа | Критерий |
| --- | --- | --- | --- |
| E0: аудит и исходные измерения | 10% / 10% | Полная карта записи пикселей/present/ввода, контрольные сцены, счётчики и baseline | Воспроизводимые кадры и замеры; учтены оба UI, DIB, cursor-only redraw; определены бюджеты памяти/производительности для сравнения |
| E1: контракт backend | 10% / 20% | Форматы, размеры, pitch, кадр/слои, damage, resize, ошибки, ownership/lifetime; явная граница с platform output | Есть один подключённый интерфейс; отсоединение/ошибка backend безопасны; нет libnx зависимостей в Win32/GDI |
| E2: software reference | 15% / 35% | Вынести текущие конвертацию, scaling, letterbox и курсор из frontend за контракт | Побитовое совпадение RGB контрольных кадров; палитры/отрицательный pitch/clip/resize; прежние GUI и Stage C regressions проходят |
| E3: dirty updates | 15% / 50% | Собирать, ограничивать и объединять изменённые области; full redraw как корректный резерв | Нет потерянных пикселей после ROP/scroll/окон/курсора/DIB; no-op не вызывает лишнюю работу; несколько output buffers получают все нужные обновления |
| E4: ресурсы и жизненный цикл | 10% / 60% | Устойчивые идентификаторы/поколения surface, resize/destroy/переподключение, очереди uploads и отмена кадра | Нет use-after-free/устаревших кадров/неограниченного роста; выход из callback/ExitProcess/host abort очищают renderer; бюджет соблюдён |
| E5: Switch GPU present | 15% / 75% | Выбрать подтверждённый API, texture upload, масштабирование и вывод с независимым курсором; software fallback | Обычная NRO работает на Switch; цвета/геометрия совпадают с reference; сбой создания GPU ресурсов даёт работоспособный fallback; CPU GDI сохранён |
| E6: композиция окон | 10% / 85% | Слои окон с правильными z-order/clip/перекрытиями; GPU-композиция там, где совместима; screen DC/сложные ROP через корректный CPU путь | Покрытие show/hide/move/resize/destroy, всплывающих окон/меню, прямой экранной отрисовки; итог сравнивается с CPU reference; ввод не зависит от backend |
| E7: кэши и batching | 5% / 90% | Кэши bitmap/glyph/upload только по измеренным горячим местам; объединение обновлений | Ключи учитывают формат/палитру/поколение/изменение; eviction/лимиты/cleanup проверены; есть измеримая польза либо ненужный кэш обоснованно исключён |
| E8: производительность | 5% / 95% | Сравнить CPU/GPU пути на тех же сценах, устранить подтверждённые лишние копии/показы | Записаны время растеризации/конверсии/upload/present, объём обновлений, input latency и память; соблюдены бюджеты E0 и показана реальная польза ускоренного пути |
| E9: аппаратная приёмка | 5% / 100% | Один автоматический пакет сцен и короткий Paint smoke на точном SHA | Все CI jobs зелёные, Switch отчёт PASS, Paint рисует/open/save, ввод/диалоги/окна корректны, software fallback проверен; ограничения записаны |

GPU API не выбирается по предположению о наличии GPU. В E5 проверить
реальный toolchain/API/права/память целевого запуска и только затем принять
решение. Если рабочий GPU путь не подтверждён, этап остаётся на достигнутой
отметке; заменить этот критерий одной программной оптимизацией нельзя.
Не обещать конкретный FPS или ускорение до измерений.

## Первый крупный пакет: E0–E2, до 35%

1. Проследить все записи в screen и DIB, PresentIfNeeded, Blit из input,
   legacy output, пересоздание UI и завершение runtime. Зафиксировать владельца
   каждого буфера и срок действия borrowed Surface.
2. Подготовить детерминированные сцены: текст/палитры/bitmap/ROP/clip,
   отрицательный pitch, перекрывающиеся окна, меню/диалоги, resize,
   курсор по неизменному фону. Получить исходные RGB результаты.
3. Добавить ограниченную диагностическую статистику без записи каждого кадра
   на SD. Замерять CPU стадии и байты отдельно от времени исполнения гостя.
4. Определить минимальный backend contract по найденным путям, затем вынести
   существующий software output. Не создавать параллельный неиспользуемый API.
5. Подключить новый путь к frontend и host inspector. Сохранить частоту
   present, input mapping, pixel conversion и software поведение.
6. Сравнить кадры, проверить отключение/resize/lifetime, выполнить полный
   host/MinGW suite и exact-SHA CI. Затем один осмысленный Switch smoke,
   а не аппаратная проверка после каждого маленького изменения.

## Инварианты реализации

- Backend не исполняет guest callbacks и не захватывает чужую GUI очередь.
  Yield/ExitThread не должны оставлять живую ссылку на уничтоженную поверхность.
- Raw guest DIB writes нельзя считать отслеживаемыми через Surface::Set:
  до надёжного tracking использовать консервативный full-damage fallback.
- Dirty state сбрасывается после принятого обновления, не до неудачного upload.
  Для двойной буферизации сохраняется состояние каждого output buffer;
  первый кадр и resize требуют полного обновления.
- Курсор имеет старую/новую damage области; перемещение по неизменному кадру
  не должно создавать следы и не должно теряться из-за ScreenChangeCounter.
- RGB/alpha/channel order, palette mutations, scaling и letterbox заданы явно.
  Существующее nearest-neighbour поведение — эталон начального переноса.
- GPU lifetime включает завершение команд/fences до освобождения ресурсов;
  аппаратные ожидания не должны безгранично останавливать ввод/guest scheduler.
- Ошибка GPU не меняет Win32 return values/last-error или CPU raster semantics.
  Резервный путь остаётся доступен; не требовать C++ exceptions/RTTI.
- Прямой screen DC/ROP и guest-visible pixels сохраняют правильное CPU
  представление даже при наличии аппаратной композиции окон.

## Проверка прогресса и handoff

Каждый пакет: отдельный reviewable commit/набор коммитов в
`claude/wonderful-hawking-10mpr0`, понятный результат, регрессии по риску и
обновлённый статус в этом файле. Полный suite — на границах согласованных
пакетов, повтор — после исправлений/нового риска. До завершения критериев
статус «в работе», долю не засчитывать частично по объёму написанного кода.

| Пакет | Статус | Подтверждение |
| --- | --- | --- |
| E0–E2 | Геометрия и непрерывное рисование подтверждены пользователем; замечание меню включено в E3 | После 01f36b6 пользователь подтвердил синхронный штрих и остальную работу; новый сводный renderer-журнал не получен, измеренный Paint baseline ещё не заявлен |
| E3 | Принят, Stage E: 50% | Пользователь подтвердил отсутствие оставшихся дефектов после c5e1871; все пять CI jobs 39847105724 успешны. Функциональная приёмка, без новых измерений FPS/latency |
| E4 | Принят, Stage E: 60% | Exact-SHA CI 2f60dac: 6/6 success; ограниченная память/lifetime: STAGE_E_RESOURCE_LIFECYCLE.md и STAGE_E_GPU_OUTPUT.md |
| E5 | GPU работает; финальная приёмка Paint открыта | ChooseColor подтверждён на Switch; проверка остальных диалогов после исправления dispatch ожидается |
| E6 | Реализованы оконные поверхности и GPU-композиция; аппаратная приёмка отложена | GDI пишет в собственные поверхности, screen DC/readback сохраняют CPU-семантику; 28 x86 сцен и poisoned-base GL oracle: STAGE_E_COMPOSITION.md |
| E7–E9 | Частичная подготовка, критерии ещё открыты | Ограниченные uploads/cache подключены; реальные Switch performance/финальный acceptance не приняты |

Требование пользователя: учитывать всю ширину Switch Lite. Новый UI использует
1280x720 desktop/output; фиксированные окна не растягиваются, другие форматы
поверхности сохраняют пропорции. Touch/cursor используют тот же viewport.

## Аппаратная обратная связь 2026-10-22

Пользователь подтвердил правильную картинку в тесте и Paint; прежняя рябь
текста осталась. Сообщил о возможной новой небольшой задержке линии при
рисовании. Открытие/сохранение новым UI в этой проверке отдельно не подтверждены.
Оригиналы отчётов сохранены в `docs/evidence/stage-e-d9ada11-*.txt`.
В отчёте геометрии PASS и 1280x720. В renderer-журнале Build: unknown,
1 submission/render, 0 redraw/reject; snapshot 1.928 ms, conversion 6.632 ms.
Это один кадр без framebuffer wait, не измерение input latency или FPS Paint.
Принадлежность NRO точному SHA этим журналом не доказана.

Ответ на замечание: убрать старый cursor-only present до доставки drag input,
сохранить immediate hover, сделать contiguous 32-bit conversion для native
ширины, передавать SHA из CI явно и записывать executable, суммарные/максимальные
framebufferBegin/End timings и число отложенных drag redraw. Это подготовка
к проверке задержки, а не утверждение об уже измеренном аппаратном ускорении.

Последующее уточнение пользователя: при непрерывном движении пальца/курсора
линия видна только после остановки. Найдено starvation вывода: FetchMessage
возвращает сообщения раньше idle-вызовов present. Добавлен показ изменённого
кадра перед следующей выборкой сообщения с прежним ограничением 16 ms.
Новый test_present_progress воспроизводит сбой на 9428a1b и проверяет вывод
GDI-пикселей при постоянно непустой очереди Get/Peek. Первый exact-SHA CI
9428a1b в итоге прошёл все пять jobs; аппаратный gate остаётся открытым до
проверки нового исправления starvation и Paint open/save.

Пользователь подтвердил: после исправления непрерывный штрих работает как надо,
в остальном всё в порядке. Новое наблюдение — задержка исчезновения подменю при
движении по верхней полосе. В E3 добавлена синхронная перерисовка открывшихся
областей меню с сохранением принадлежности GUI-потоков и безопасным завершением
callback. Совмещённый пакет E3 и menu exposure описан в
[STAGE_E_DIRTY_UPDATES.md](STAGE_E_DIRTY_UPDATES.md); следующая цель — 50%, затем
E4 до 60% и E5 с реальным GPU present до 75%. До проверки новой NRO на Switch
аппаратные проценты не объявляются закрытыми по одному объёму кода.

После E3 пользователь подтвердил устранение прошлых дефектов; выявлены новые:
самопроизвольное File и скрытый toolbar на старте, ускорение стика при A.
В следующем исправлении входные кнопки/тач отделены от запуска до отпускания,
а скорость стика интегрируется по времени, не по числу опросов. Причина File —
отпускание launch-held L превращалось в Alt-up без Alt-down; toolbar требует
отдельного визуального подтверждения после этой правки. Код и проверки:
[STAGE_E_INPUT_FOLLOWUP.md](STAGE_E_INPUT_FOLLOWUP.md). E3 остаётся на аппаратной
приёмке, следующая крупная реализация после неё — E4.


Последняя аппаратная приёмка: пользователь сообщил «Теперь точно всё, больше
проблем не наблюдаю». Это закрывает File/toolbar, скорость A-drag и предыдущие
регрессии рисования/меню для c5e1871. Exact-SHA CI 39847105724: 5/5 success.
Stage E принят до 50%; следующий пакет E4 описан в
[STAGE_E_RESOURCE_LIFECYCLE.md](STAGE_E_RESOURCE_LIFECYCLE.md). Старые pending
заметки выше отражают последовательность проверок, а не текущий статус.


## Крупный блок E4–E7 по запросу пользователя

Взята цель 50% → 90% одним циклом. Реализованы fallible memory/transactional
resize, EGL/OpenGL native output, независимый курсор, dirty texture uploads,
software fallback и ограниченный texture/history cache. Добавлено побитовое
сравнение реального GL/EGL вывода с software reference, включая GL error/retry
и software rebind. Карта не переписывается задним числом: E5 требует Switch,
E6 требует самостоятельных оконных слоёв и screen-DC reconciliation; одна
текстура готового CPU screen за E6 не засчитывается. Детали, бюджеты и общий
аппаратный checkpoint: [STAGE_E_GPU_OUTPUT.md](STAGE_E_GPU_OUTPUT.md).
До exact-SHA CI и аппаратных gates принятая отметка остаётся 50%.


## Common color dialog follow-up (2026-10-23)

Exact-SHA CI 2f60dac, run 39853177426: all 6 jobs succeeded. E4 memory/lifetime
criteria are accepted; Stage E is at 60%. ChooseColorA/W exports and a standard
modal palette/RGB/custom-color picker are now implemented, including hooks and
cleanup on guest exit/host abort. E5's final application acceptance remains open
until Paint's color selection path is verified. Details and regression coverage:
[PAINT_CHOOSE_COLOR.md](PAINT_CHOOSE_COLOR.md). Original uploaded diagnostics are
not included in the repository.


## Standard dialogs follow-up

Color selection is confirmed on hardware after 1b4c43d. Other Paint dialogs
exposed a general CBT-subclass/DWL_DLGPROC dispatch gap. The defect is reproduced
by real x86 modal/modeless tests and repaired without application-specific
handlers. Lifecycle cancellation and dialog metadata cleanup are covered too.
Physical acceptance of the affected Paint dialogs remains open; Stage E stays
at the accepted 60% until its application gates are complete. See
[PAINT_DIALOG_DISPATCH.md](PAINT_DIALOG_DISPATCH.md).

## E6: composition reference checkpoint

Added 28 unattended real x86 window scenes with independent full-frame CPU and
output validation at 1280x720 and 800x600, triple preserved software outputs and
production EGL/GL replay in CI. The scenes reproduce stale foreground pixels
after activation and stale exposed child siblings under WS_CLIPCHILDREN; both
production invalidation paths are repaired. This is a prerequisite for retained
window layers, not completion of E6. Accepted Stage E stays at 60% while current
Paint hardware gates and the remaining E6 implementation are open. Scope,
reproduction and next architectural boundary: [STAGE_E_COMPOSITION.md](STAGE_E_COMPOSITION.md).

## E6: retained window layers connected

Per user instruction, implementation continues while a combined hardware check
is deferred. GDI writes through to bounded window-owned surfaces; an ownership
plane reconciles window draws and direct screen DC writes. Owned snapshots feed
independent GPU textures: masked base pixels must be supplied by those textures.
CPU ROP/readback remains authoritative, optional allocation failures retain
complete output and GL failures retain software fallback. Both live x86 sizes
and actual GL/lifetime/fault tests pass locally. Report v5 includes layer memory,
work and CPU timing. E6 implementation is ready for exact-SHA CI and the later
Switch check; accepted Stage E stays at 60% until hardware gates are complete.
Next is E7/E8 batching/measurement: [STAGE_E_COMPOSITION.md](STAGE_E_COMPOSITION.md).
