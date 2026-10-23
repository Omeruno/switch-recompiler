# Stage F: постоянная файловая система и XP-подобная оболочка

Статус: **F1 реализован программно** ([контракт и проверки](F1_FILESYSTEM.md));
F2–F5 ещё не реализованы. Физическая приёмка остаётся отложенной.
База: 03b9fd4. [Аудит всех этапов](PROJECT_AUDIT_2026_10_23.md) объясняет,
почему F0 не означает готовый рабочий стол. Следующий кодовый пакет — F2, native desktop/theme.

Пользовательская цель: реальная файловая структура Switch/SD, синяя панель
задач и зелёный «Пуск», рабочий стол, программы и проводник в пределах уже
поддерживаемой one-Process модели. Это native host-shell на существующих
Surface/GDI/font/presentation/input primitives, не запуск explorer.exe и не
добавление браузера/HTML/CSS runtime на Switch.

## Постоянное хранилище

Layout F1 в production Switch launch (host winrun сохраняет standalone default):

| Видимый том/путь | Реальное место | Назначение |
| --- | --- | --- |
| `C:\` | `sdmc:/switch-recompiler/drive_c/` | Постоянный guest system/user root, общий для всех shell-запусков |
| `C:\Programs` | `.../drive_c/Programs/` | Приложения с соседними DLL/данными |
| `C:\Documents and Settings\Player` | `.../drive_c/Documents and Settings/Player/` | Профиль; Desktop, My Documents, My Pictures, Application Data |
| `C:\WINDOWS\system32`, `C:\TEMP` | соответствующие реальные подпапки `drive_c/` | Согласованные env/system/temp paths; HLE не требует копирования Windows DLL |
| `S:\` | `sdmc:/` | Явное отображение реальных папок SD в браузере; режим доступа учитывается mount provider |
| `R:\` | `romfs:/` | Ресурсы/встроенные EXE, только чтение |

Эти mounts подключены F1; desktop/Start/explorer ещё не подключены. Корень SD не является
системным разделом Horizon. NAND/системные файлы консоли не нужны для этой вехи.
Существующие `sdmc:/switch-recompiler/Game/game.exe`, документы, registry.dat и
стабильные NRO сохраняются. Ничего автоматически не переносить/перезаписывать.
Новый layout создаёт только отсутствующие каталоги; старые EXE доступны через S.
Относительные пути остаются в каталоге EXE; старые сохранённые абсолютные C-пути
из per-EXE режима могут потребовать выбора через S — автоматической миграции нет.
Host-тесты используют временные directory-backed mounts, не Switch device paths.

F1 использует единую модель drive/path/root/CWD/readonly и ошибок; нельзя вводить
несколько разных правил у VFS, loader, CRT, dialogs и shell. Неизвестный диск
должен давать ошибку, а не alias C. Учитывать separators, `.`/`..`, case,
per-drive relative paths, root boundary, отсутствующий путь, Unicode и read-only.
UNC/unsupported устройства должны иметь явный неподдерживаемый результат.

## Пакеты и критерии

| Пакет | Реализация | Проверяемое завершение |
| --- | --- | --- |
| **F1: реализован программно** ([F1_FILESYSTEM.md](F1_FILESYSTEM.md)) | Mount/root configuration отдельно от EXE directory/CWD; согласованные guest module/command-line пути, env/special folders, реальные volume/free-space provider и A/W APIs; сохранение старого standalone workflow | Две публичные EXE в разных каталогах видят одни документы; сохранённый файл читается после двух fresh sessions; C/S/R round-trip, неизвестный диск, read-only, traversal, case/Unicode, соседние DLL и failed load покрыты native+real-x86 tests |
| **F2: native desktop** | Постоянное host-состояние shell, фон, папки/ярлыки и selection, синяя taskbar, зелёный Start, часы реального времени; asset/theme manifest; 1280x720 и общий viewport | Host screenshot/pixel/layout и controller/touch tests; вход/выход shell не создаёт guest Process и не заимствует guest-owned pointers; изменение viewport не рассогласует ввод |
| **F3: функциональный Start и explorer** | Программы из реальных EXE-путей, «Все программы», «Мой компьютер», «Мои документы», «Выполнить», доступные настройки, About и выход из homebrew; дерево/список, адрес, back/up, сортировка/страницы, выбор и открытие EXE/документа | Каждая видимая активная команда выполняет действие; browser читает directory-backed/SD provider с лимитами и честно сообщает truncation/read errors; Start/Explorer/Run приводят к одному launch request; app association проверяет существующую поддержку |
| **F4: shell ↔ guest** | Повторное использование GuestSession/RunLauncher teardown; ownership shell/taskbar/output при активном приложении, per-launch UI/GPU mode, карантин ввода, активная task button, один guest одновременно | Launch/normal exit/PLUS abort/load error/display error возвращают тот же desktop/selection/path; несколько окон одного приложения корректны; реализация minimize/restore и меню во время работы должна иметь explicit compositor/input route, а не ложную кнопку второго процесса |
| **F5: пользовательский сценарий** | Согласованные документы/иконки/ярлыки/настройки, app associations, bounded persistence; операции создания/копирования/rename по реальным поддерживаемым API | Из «Пуска» запустить Paint, сохранить в My Documents, вернуться, найти файл в explorer и открыть снова; повторить с публичным guest в host CI, реальный Paint/SD/ввод/GPU проверить в отложенном Switch smoke |

F2 может сначала показывать desktop только между запусками. Это промежуточный
результат, не завершённый F4: текущий main блокируется на threadWaitForExit и
пересоздаёт output. Полноценную панель при работающем госте нельзя засчитать
скриншотом idle-shell. Нельзя изображать несколько работающих приложений при
одном Process. Несколько окон текущего приложения остаются допустимы.
Настройки и команды показывают только реализованные возможности; выход —
возврат из homebrew, не выдуманная эмуляция выключения Windows/Horizon.

## Visual sources: проверено, пока не импортировано

1. **[XP.css](https://github.com/botoxparty/XP.css)**, проверенный commit
   [`376f42eb274361939eaad3b0943204c053ad26c4`](https://github.com/botoxparty/XP.css/tree/376f42eb274361939eaad3b0943204c053ad26c4).
   [LICENSE MIT](https://github.com/botoxparty/XP.css/blob/376f42eb274361939eaad3b0943204c053ad26c4/LICENSE),
   Copyright 2020 Adam Hammad, Jordan Scales. `themes/XP/_window.scss` содержит
   синие градиенты/рамки; `themes/XP/icon/` — SVG состояний close/minimize/
   maximize/restore/scroll и других controls. Подходит как лицензированный
   источник native theme geometry/colors/control artwork. CSS не исполняется
   нынешним renderer; нужен перенос в native raster/theme и офлайн rasterization
   выбранных SVG. Это не готовая реализация taskbar/Start/проводника.
2. **[Tango Icon Theme](https://github.com/stephenc/tango-icon-theme)**, mirror,
   проверенный commit [`41b8f6abd7ebb9d5c35e563c8cd8c410c88f114a`](https://github.com/stephenc/tango-icon-theme/tree/41b8f6abd7ebb9d5c35e563c8cd8c410c88f114a).
   [COPYING](https://github.com/stephenc/tango-icon-theme/blob/41b8f6abd7ebb9d5c35e563c8cd8c410c88f114a/COPYING)
   прямо объявляет icons public domain. Есть готовые цветные PNG
   `32x32/places/folder.png`, `user-home.png`, `devices/computer.png`,
   `apps/system-file-manager.png`, `actions/document-open.png`.
   Это свободные desktop icons, не оригинальные XP icons Microsoft.
3. Уже упакованные Liberation Sans/Serif/Mono и OFL в `romfs/fonts/` — текущая
   лицензированная font основа. Не добавлять зависимость от Arial/Tahoma/
   Trebuchet только потому, что исходный CSS перечисляет эти шрифты.

При импорте каждого файла сохранять pinned source/path/hash/license/attribution
в asset manifest и notices. При производной native теме сохранять MIT notice
для адаптированного XP.css artwork/code. Проверять выбранные файлы отдельно от
общей лицензии репозитория. Синюю taskbar/зелёный Start можно дорисовать native
градиентами и своим знаком проекта. Не считать изображения из случайного
«XP asset pack» автоматически свободными; proprietary binaries, Windows logo,
Bliss wallpaper и выгрузки shell32.dll в этот набор не входят.

Первый native F2 рендер должен иметь реальные artwork/font assets и проверяемую
геометрию/состояния hover/pressed/disabled/focus. Макет без файлового provider и
действий меню не закрывает пользовательскую F-веху.

## Очередь и проверки

F1 и его API/loader contract реализованы; далее F2/F3/F4/F5 небольшими
когерентными пакетами. Файловые недостачи D исправлять внутри соответствующего
пакета F1; отдельную memory-protection/SEH работу вести без заявления о полном NT.
G0 не удалять. Продолжение G/H не подменяет недостающий shell.

Для каждого кодового пакета: полный `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh`, целевые regressions, Switch build и все 6 jobs точного
опубликованного SHA. Для документов — сверка с кодом/ссылками и diff checks;
повторная компиляция неизменённого runtime не является новой приёмкой.
Физические GPU/fallback/Lite/input/relaunch/реальный Paint остаются одним списком
в [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md), без повторной диагностики SD.
