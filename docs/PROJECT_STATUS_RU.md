# Где находится Switch Recompiler

[Визуальная карта проекта](https://omeruno.github.io/switch-recompiler/) · [Автономный HTML](roadmap/index.html) · [Как открыть и публиковать](roadmap/README.md)

Актуально на 2026-10-28, после G4b.2o и D1 (bounded real registry family). Проверенный код: `25ec8be3a7f44cee4b7de09cab502dacbb1a5115`,
[CI 40620546240 — 6/6](https://github.com/Omeruno/switch-recompiler/actions/runs/40620546240).
[Глобальный план A–I](ROADMAP.md), [ближайшая очередь](ACTIVE_ROADMAP.md), [handoff](AI_HANDOFF.md).

**База исполнения и ограниченная функциональная оболочка уже есть. В G4b.2h
обычные solid FillRect/PATCOPY подключены к GPU в явном эксперименте. G4b.2i разделяет cold/warm затраты и сокращает capacity readback8→0,
сохраняя настоящее чтение. G4b.2j сохраняет snapshot при допустимых обновлениях; G4b.2k добавляет adaptive exact damage; G4b.2l добавляет bounded capacity replay; G4b.2m сокращает DIB upload bytes655360→66112 при тех же CPU/read boundaries; G4b.2n сводит software acceptance и CPU/native cycles; G4b.2o реализует explicit CPU routing; D1 реализует11 A/W registry API; далее keyboard names/VK-scancode translation; default CPU.**
Это среда для поддержанных 32-битных x86 Win32-приложений на Switch. Полного
ядра Windows, поддержки x64 и универсальной совместимости EXE пока нет.

## К чему идём

**К запуску PC-игр на Switch**, а не только к XP-подобному рабочему столу.
Spore, Mirror’s Edge, The Sims 3 и Lineage 2 Classic / Asterios — будущие целевые нагрузки, пока без
подтверждённого запуска/FPS. Для них нужны полноценный x86→ARM64 dynarec,
игровой графический API (приоритет исследования — D3D9), звук, память,
потоки и совместимость системных вызовов. Paint/Сапёр — ранние проверки общих
механизмов. [Подробная цель и критерии](TARGET_GAMES_RU.md).

## Глобальная карта

| Этап | Фактическое состояние | Что остаётся |
| --- | --- | --- |
| A — корректность | Работает постоянная база host-тестов, CPU oracle, ARM64/QEMU, EGL и Switch CI | Регрессии и матрица приложений нужны при каждом расширении; это постоянная работа |
| B — исполнение x86 | Интерпретатор, кооперативные гостевые потоки, TEB/TLS, кванты, callbacks и завершение | Расширение совместимости и ускорение; полноценный dynarec отсутствует |
| C — объекты и ожидания | Ограниченная модель одного Process принята на Switch: HANDLE, waits, APC, timers, lifecycle и владение GUI | Полная NT-модель, межпроцессная изоляция и другие расширения не входят в эту приёмку |
| D — Windows API | PE EXE/DLL, импорты/выгрузка/TLS, часть SEH, существенный Win32/CRT | Защита страниц, полный unwind/SEH, COM и недостающие API реальных приложений |
| E — графический вывод | CPU GDI, действующий GPU-композитор, software fallback, оконные слои, кэши и метрики | Текущая аппаратная приёмка, замеры реальных Switch-нагрузок, соединение с новым GPU-GDI |
| F — оболочка и файлы | **Ограниченная программная F0–F5 готова**: постоянные диски, Пуск/проводник/ярлыки, реальные файловые действия, TXT/BMP, запуск/выход/ошибка/возврат | Аппаратная приёмка; полноценное поведение окон XP и дальнейшее расширение среды |
| G — ускорение | G1 измерения; G2 быстрый CPU SRCCOPY включён; G3/G4a/G4b.1 GPU-прототипы проверены; G4b.2a/b связывают GL/HWND/DC, G4b.2c проверяет opt-in CPU/GPU-барьеры | **Сейчас G4b.2**: основной texture bridge и ограниченное production-подключение; затем реальные замеры. Dynarec отдельно |
| H — игровая графика | Готовой реализации DirectDraw/Direct3D нет | Отдельный графический API-переводчик и проверки поддержанных игр |
| I — готовый продукт | Сборки NRO, диагностика и публичные тестовые артефакты есть | Матрица совместимости, настройка/доставка, стабильность и приёмка продукта |

A–I — разные направления: продвижение по G не закрывает автоматически D, E или F.
Процент общей готовности и срок без целевой матрицы программ не определены.

## Что уже доступно в среде

- Постоянный C на SD, S как SD-карта целиком, read-only R/ROMFS; реальные
  пользовательские папки, Unicode-пути, аргументы запуска и рабочая папка.
- Desktop, Пуск, taskbar, проводник с деревом, реальные Desktop/Start `.lnk`,
  сохранение сортировки и ассоциаций. Все пять Luna-обновлений Claude сохранены;
  [исправлен стык размеров нового окна и активного workspace](LUNA_INTEGRATION_REVIEW.md).
- Создание папок, rename/delete, свойства, ограниченные контекстные меню/ПКМ,
  Copy/Paste файла с прогрессом и отменой, проверяемые ошибки хранения.
- Native TXT: создать/читать/редактировать строки/сохранить с резервной копией.
  Native BMP: открыть/просмотреть/масштабировать/прокрутить поддержанные форматы.
- Один активный гостевой Process с его потоками и несколькими окнами, изоляция
  последовательных запусков и возврат в оболочку после выхода/ошибки/отмены.
  Независимый native-ввод и постоянный frontend/output между сеансами.

[Критерии F](F_ACCEPTANCE.md) проверяются 22 программными группами. Статус:
**PASS_SOFTWARE_PENDING_HARDWARE**. TXT/BMP — собственные ограниченные инструменты,
а не полная совместимость Notepad/Paint. PNG/JPEG и другие форматы не добавлены.
Несколько одновременно запущенных гостевых EXE ещё не поддержаны.

Внешний вид Luna приблизился к XP, но это не законченная копия Windows:
[F6b](F6B_NATIVE_WINDOW_ACTIONS.md) даёт реальные native min/max/restore/close,
move/resize и адаптивный layout в одной группе инструментов. Произвольная
многооконность и несколько экземпляров native-документов ещё не реализованы. Старые подтверждения
Paint open/save не подтверждают аппаратную приёмку каждой последующей сборки.

## Где именно находится ускорение

Рабочий путь сейчас: **x86-интерпретатор → GDI на CPU → GPU-композиция
(либо software fallback) → экран**.
GPU-композитор уже используется. Явный native experiment теперь допускает
обычные solid FillRect/PATCOPY небольших top-level окон на GPU; остальные
операции сохраняют CPU. Это не включено по умолчанию и не обещает ускорение Paint.

| Пакет | Что сделано | Подключение к обычному приложению |
| --- | --- | --- |
| G0/G1 | Частоты инструкций/API, публичный PE-тест и измерения фаз/накладных расходов | Измерительные инструменты; сами не ускоряют программу |
| G2 | Проверенный быстрый CPU SRCCOPY, опциональный отчёт квантов | Быстрый путь включён; рост FPS Switch не заявлен без измерений |
| G3 | Реальные GL fill/copy, CPU readback и ограниченное восстановление | Изолированный прототип |
| G4a | Несколько GPU-targets с поколениями и вывод из текстур без CPU readback ради композиции | Изолированный прототип; не HWND bindings |
| G4b.1 | Упорядоченные instanced-пакеты: 64 disjoint fills → 1 draw; копии/пересечения сохраняют порядок | Изолированный прототип, сравнение с прежним путём в одной сборке |
| G4b.2a/b | Общий GL-контекст; реальные идентичности окон/DC и bounded native leases/ресурсы | Явный native-мост, без автоматического GDI dispatch |
| G4b.2c | GPU-данные доступны реальным x86 GetPixel/ROP/DIB через opt-in CPU-барьеры; layout/fallback проверены | Native-эксперимент; автоматического GDI dispatch нет |
| G4b.2d/e/f | Владеющие GPU/mixed кадры, живые окна и shell, USER selector и opt-in Switch device/cursor/fallback | Программный opt-in путь; ordinary GDI остаётся CPU |
| G4b.2g | [Аудит зарегистрированных host pixel/fault путей](G4B2G_HOST_PIXEL_AUDIT.md), CPU views, sticky faults, checked fallback/lifecycle | Проверки real x86/EGL; не перехват произвольной памяти |
| G4b.2h | [Обычные solid GDI, public x86/CPU/EGL/count/recovery и первые парные замеры](G4B2H_ORDINARY_GDI_DISPATCH.md) | Явный эксперимент, default CPU; на локальной Mesa первый кадр через GPU медленнее |
| G4b.2i | [Cold/warm same-resource и covered journal retirement](G4B2I_WARM_GDI_JOURNAL.md) |12 normal exits/108 EGL frames; capacity reads8→0, реальный финальный read сохранён; default CPU |
| G4b.2j | [Retained workspace / partial / real CPU DIB](G4B2J_RETAINED_WORKSPACE.md) |24 normal exits/216 EGL frames; snapshot builds9→1 tiles,9→6 partial,9 CPU DIB; default CPU |
| G4b.2k | [Adaptive exact damage / atomic masked commit](G4B2K_EXACT_DAMAGE.md) |36 normal exits/324 EGL frames + odd-window/capture/fault/fallback/recovery; partial commits5→0, snapshots6→1; default CPU |
| G4b.2l | [Bounded capacity replay](G4B2L_CAPACITY_REPLAY.md) |48 normal exits/432 EGL frames; partial capacity reads8→0, private CPU work bounded; real GetPixel/DIB boundaries remain; default CPU |
| G4b.2m | [Измеренный differential CPU import](G4B2M_DIFFERENTIAL_IMPORT.md) |60 exits/540 EGL frames; DIB uploads655360→66112 bytes; полные CPU seeds и реальные чтения сохранены |
| G4b.2n | [Software acceptance / CPU-native cycles](G4B2N_SOFTWARE_ACCEPTANCE.md) |17 software groups/five reports;32 exits/288 EGL frames; DIB и size-refusal costs сохранены; default CPU |
| G4b.2o | [Explicit CPU routing](G4B2O_CPU_ROUTING.md) | Flag2 alternative;48 exits/432 EGL frames; real USER transitions/fault/cached redraw; default CPU |
| **Следующий блок D** | [D1 registry](D1_REGISTRY_KEYS.md) implemented; next keyboard names/VK-scancode translation с public API/error/lifetime gates | **Следующий незавершённый рубеж** |

Host EGL/Mesa проверяет пиксели, порядок, лимиты и восстановление. Это не
физический Switch GPU benchmark. G4-прототипы имеют собственные небольшие лимиты
и ограниченный [shared-context контракт](G4B2_SHARED_CONTEXT.md); это ещё не
готовый GPU-путь обычных приложений.
Минимальный JIT-probe ранее возвращал 42 на Switch, но code cache/dynarec не готов.

## Ближайшие рубежи

1. **Блок D:** после explicit CPU routing перейти к keyboard names/VK-scancode translation после [D1 registry](D1_REGISTRY_KEYS.md), сохраняя GL/CPU/read/ROP/DIB/alias/lifetime/recovery gates.
2. Выбрать и проверить общую оптимизацию по этим данным. Сохранить public x86,
   точные CPU/EGL пиксели, read/ROP/DIB/lifecycle/budget/fallback и Luna/input.
3. Измерения реальных приложений на Switch и объединённая аппаратная приёмка
   E/F — когда пользователь вернётся к проверкам. Сейчас они отложены.
4. Параллельная очередь D: конкретные воспроизводимые несовместимости. У Pinball
   x86 прошёл прежний RegCreateKeyExA; следующий host-барьер — `GetKeyNameTextA`;
   совместимость игры не объявлена. Версия x64 не поддерживается архитектурно.

## Установщики, архивы и дальнейшая среда

[EXE-установщики и ZIP/RAR](F_INSTALL_AND_ARCHIVES.md) записаны в план,
но не реализованы. Реальные папки/ярлыки уже есть; этого недостаточно для
произвольного installer EXE, MSI/COM или запуска установленной игры. Затем
планируется ZIP, позднее RAR/другие форматы с настоящими ошибками распаковки.
H DirectDraw/D3D и универсальная совместимость — отдельные крупные задачи.

Тестов на Switch сейчас не требуется. Локального devkitA64/libnx нет, NRO
собирает CI. SD восстановлена; повторная диагностика SD не входит в очередь.

Предыдущий пакет: [G4b.2c — CPU/GPU-барьеры](G4B2C_CPU_COHERENCE.md).
В native opt-in режиме обычный x86-код читает GPU-изменения, выполняет CPU ROP,
копирует окно в DIB и получает актуальные данные через GetDIBits. Проверены два
окна, ошибки GL и реальная потеря EGL-контекста. Исправлено удаление текущей
шейдерной программы в основном композиторе. Это проверка корректности;
CPU-вывод всё ещё требует checkpoint, автоматическое ускорение обычных GDI
вызовов не включено. Основной renderer принимает resident-текстуры; владеющий mixed frame и host pixel/fault audit готовы. G4b.2h допускает bounded solid GDI только в явном эксперименте; G4b.2i даёт cold/warm и covered retirement; G4b.2k реализует adaptive exact damage; G4b.2l добавляет bounded capacity replay; G4b.2m сокращает DIB upload bytes655360→66112 при тех же CPU/read boundaries; G4b.2n сводит software acceptance и CPU/native cycles; G4b.2o реализует explicit CPU routing; D1 реализует11 A/W registry API; далее keyboard names/VK-scancode translation.


[G4b.2d.1 — владение GPU-ресурсами кадра](G4B2D_RESIDENT_FRAMES.md) реализовано
в native-менеджере: удержание после retirement, лимиты, сброс и fallback.
Этот prerequisite уже использован в native ShellSession/mixed output и
G4b.2h solid dispatch. G4b.2i разделяет cold/warm и сокращает capacity reads8→0; G4b.2k реализует adaptive exact damage; G4b.2l добавляет bounded capacity replay; G4b.2m сокращает DIB upload bytes655360→66112 при тех же CPU/read boundaries; G4b.2n сводит software acceptance и CPU/native cycles; G4b.2o реализует explicit CPU routing; D1 реализует11 A/W registry API; далее keyboard names/VK-scancode translation.


[G4b.2d.2 — GPU-слои в основном GL-композиторе](G4B2D2_RESIDENT_PRESENTATION.md):
явный native-вход рисует удерживаемые текстуры с ownership-масками, CPU-оверлеем
и курсором. Для выбранных GPU-слоёв не нужны CPU-зеркало/readback/upload.
[G4b.2d.3](G4B2D3_MIXED_FRAME.md) уже владеет CPU-слоями и GPU-pins для redraw.
[G4b.2d.4](G4B2D4_LIVE_WINDOW_FRAMES.md) собирает реальную видимость окон в этот кадр.
[G4b.2e](G4B2E_MIXED_WORKSPACE.md) добавляет native-слои shell и выбор mixed-пути в USER до CPU checkpoint.
[G4b.2f](G4B2F_SWITCH_MIXED_OUTPUT.md) добавляет Switch device/swap/cursor adapter как opt-in; ordinary GDI ещё CPU.
[G4b.2g](G4B2G_HOST_PIXEL_AUDIT.md) добавляет защищённый CPU-доступ, фиксацию отказов, checked fallback и lifecycle barriers.
[G4b.2h](G4B2H_ORDINARY_GDI_DISPATCH.md) допускает обычные solid FillRect/PATCOPY в явном native experiment и измеряет первый кадр. G4b.2i добавляет cold/warm и retirement. Default остаётся CPU; G4b.2k реализует adaptive exact damage; G4b.2l добавляет bounded capacity replay; G4b.2m сокращает DIB upload bytes655360→66112 при тех же CPU/read boundaries; G4b.2n сводит software acceptance и CPU/native cycles; G4b.2o реализует explicit CPU routing; D1 реализует11 A/W registry API; далее keyboard names/VK-scancode translation.


Пользовательский приоритет — настоящее управление окнами.
[F6a: гостевые окна Win32](F6A_GUEST_WINDOW_GESTURES.md) получают модальное
перемещение, все стороны/углы resize, min/max/restore/close по отпусканию,
ограничения и отказ приложения, корректный захват/отмену/завершение.
[F6b: native-окна](F6B_NATIVE_WINDOW_ACTIONS.md) реализуют min/max/task restore,
безопасное закрытие, move/resize, адаптивный layout и точный input/composition.
Поддерживается одна native-группа с модальным документом.
**G4b.2i cold/warm и covered journal retirement реализованы; G4b.2k реализует adaptive exact damage; G4b.2l добавляет bounded capacity replay; G4b.2m сокращает DIB upload bytes655360→66112 при тех же CPU/read boundaries; G4b.2n сводит software acceptance и CPU/native cycles; G4b.2o реализует explicit CPU routing; D1 реализует11 A/W registry API; далее keyboard names/VK-scancode translation. Default CPU.**
