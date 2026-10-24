# Stage F: постоянная файловая система и XP-подобная оболочка

Статус: **F1 реализован программно** ([контракт и проверки](F1_FILESYSTEM.md));
**F2 idle desktop/theme реализован программно** ([поведение и проверки](F2_DESKTOP.md));
**F3a real directory/path launch реализован программно** ([контракт](F3_EXPLORER.md));
**F3b document applications/arguments реализован программно** ([контракт](F3_DOCUMENTS.md));
**F4a общий рабочий стол при активном госте реализован программно** ([границы](F4_WORKSPACE.md)); F3c/F4b/F5 ещё открыты. Физическая приёмка остаётся отложенной.
База: 03b9fd4. [Аудит всех этапов](PROJECT_AUDIT_2026_10_23.md) объясняет,
почему F0 не означает готовый рабочий стол. По последнему запросу пользователя приоритет — реальные функции системы,
визуальную полировку отложить. F4b.1 независимого ввода реализован программно
([контракт](F4B_INPUT_SERVICE.md)). F4b.2 постоянного frontend/output реализован программно ([контракт](F4B_OUTPUT_LIFETIME.md)).
Общий слой реальных файловых операций/свойств и browser model реализованы
([границы](F_FILE_OPERATIONS.md)); [UI-команды, ввод имени, подтверждение удаления
и свойства](F_FILE_ACTIONS.md) теперь подключены.
Проверяемый [слой копирования](F_FILE_COPY.md) добавлен; UI Copy/Paste ещё открыт.
Очередь: Copy/Paste → контекстные меню/ввод → настройки,
TXT/изображения → сценарий F5. F3c/F4/F5 целиком не закрыты.

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

Эти mounts подключены F1, desktop — F2, реальный directory explorer/path-only Run — F3a. Корень SD не является
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
| **F2: native idle desktop реализован программно** | Постоянное host-состояние shell, фон, папки/ярлыки и selection, синяя taskbar, зелёный Start, часы реального времени; asset/theme manifest; 1280x720 и общий viewport | Host screenshot/pixel/layout и controller/touch tests; вход/выход shell не создаёт guest Process и не заимствует guest-owned pointers; изменение viewport не рассогласует ввод |
| **F3: F3a/F3b реализованы; F3c остаётся** | Программы из реальных EXE-путей, «Все программы», «Мой компьютер», «Мои документы», «Выполнить», доступные настройки, About и выход из homebrew; дерево/список, адрес, back/up, сортировка/страницы, выбор и открытие EXE/документа | Каждая видимая активная команда выполняет действие; browser читает directory-backed/SD provider с лимитами и честно сообщает truncation/read errors; Start/Explorer/Run приводят к одному launch request; app association проверяет существующую поддержку |
| **F4: shell ↔ guest** | Повторное использование GuestSession/RunLauncher teardown; ownership shell/taskbar/output при активном приложении, per-launch UI/GPU mode, карантин ввода, активная task button, один guest одновременно | Launch/normal exit/PLUS abort/load error/display error возвращают тот же desktop/selection/path; несколько окон одного приложения корректны; реализация minimize/restore и меню во время работы должна иметь explicit compositor/input route, а не ложную кнопку второго процесса |
| **F5: пользовательский сценарий** | Согласованные документы/иконки/ярлыки/настройки, app associations, bounded persistence; операции создания/копирования/rename по реальным поддерживаемым API | Из «Пуска» запустить Paint, сохранить в My Documents, вернуться, найти файл в explorer и открыть снова; повторить с публичным guest в host CI, реальный Paint/SD/ввод/GPU проверить в отложенном Switch smoke |

F4a теперь сохраняет desktop/Start/taskbar при активном new-UI госте; консервативная flat-композиция и ограничения описаны в [F4_WORKSPACE.md](F4_WORKSPACE.md). F4b и F3c/F5 не закрыты. Ниже зафиксирована исходная граница F2.

F2 может сначала показывать desktop только между запусками. Это промежуточный
результат, не завершённый F4: текущий main блокируется на threadWaitForExit и
пересоздаёт output. Полноценную панель при работающем госте нельзя засчитать
скриншотом idle-shell. Нельзя изображать несколько работающих приложений при
одном Process. Несколько окон текущего приложения остаются допустимы.
Настройки и команды показывают только реализованные возможности; выход —
возврат из homebrew, не выдуманная эмуляция выключения Windows/Horizon.

## Visual sources: закреплённые источники (Tango импортирован в F2)

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

F2 импортировал три PNG Tango и их public-domain declaration; manifest/hash и
генератор native RGBA описаны в [F2_DESKTOP.md](F2_DESKTOP.md). XP.css остаётся
проверенным источником для будущей адаптации, его код/artwork пока не импортированы.

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

F1/F2 и F3a browser/path-only launch реализованы программно; далее F3c/F4/F5 небольшими
когерентными пакетами. Файловые недостачи D исправлять внутри соответствующего
пакета F1; отдельную memory-protection/SEH работу вести без заявления о полном NT.
G0 не удалять. Продолжение G/H не подменяет недостающий shell.

Для каждого кодового пакета: полный `COMPOSITION_GL_TEST=1 sh
tools/run_host_tests.sh`, целевые regressions, Switch build и все 6 jobs точного
опубликованного SHA. Для документов — сверка с кодом/ссылками и diff checks;
повторная компиляция неизменённого runtime не является новой приёмкой.
Физические GPU/fallback/Lite/input/relaunch/реальный Paint остаются одним списком
в [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md), без повторной диагностики SD.


## Рабочий план после F1 (2026-10-23)

Проверенная исходная точка: `d7a15b79650e337429573651c02774fca96dc787`,
[CI 39965068140](https://github.com/Omeruno/switch-recompiler/actions/runs/39965068140)
— все шесть jobs успешны. Ниже очередь реализации, а не заявление о готовности.

1. **F2 — desktop/theme: реализован программно, см. [F2_DESKTOP.md](F2_DESKTOP.md).** Сначала проследить существующие Surface/font/input/
   presentation и picker lifecycle; выбрать общий viewport и независимое от guest
   состояние shell. Затем импортировать минимальный набор закреплённых свободных
   иконок с manifest/notices, реализовать фон, ярлыки, синюю taskbar, зелёный Start,
   часы и состояния элементов. Подключить idle-shell к production frontend,
   сохранив выбор приложения и режимов запуска. Критерий: воспроизводимый host
   render/screenshot, layout и hit-test на 1280x720 и другом размере, управление
   контроллером/касанием и отсутствие зависимости от времени жизни guest Process.
   Действия Start, ещё не реализованные в F3, не показывать активными.
2. **F3 — реальные действия и проводник: F3a/F3b реализованы, F3c остаётся.** Общая модель launch request для Start,
   списка программ, Run и explorer; provider поверх F1, без второй схемы путей.
   Реальные C/S/R, My Documents, переходы back/up, адрес и ограниченные списки с
   сортировкой/страницами и явными ошибками. Затем All Programs, About, доступные
   настройки и выход из homebrew. Критерий: host-тесты на временном дереве с
   Unicode/readonly/несуществующими путями и лимитами; каждая активная команда
   выполняет своё действие, EXE запускается по одному согласованному маршруту.
3. **F4 — работа shell рядом с гостем.** До добавления активной task button и
   minimize/restore определить владение compositor/output и маршрутизацию ввода:
   нынешний блокирующий threadWaitForExit сам по себе этого не обеспечивает.
   Сохранить один guest, GuestSession cleanup и per-launch UI/GPU выбор. Критерий:
   normal exit, abort, load/display failure возвращают прежние каталог/selection;
   held input не проходит в новую сессию, taskbar и меню работают при активном
   госте через явный маршрут ввода, повторные запуски не оставляют ресурсы гостя.
4. **F5 — законченный доступный сценарий.** Добавить поддержанные ассоциации,
   ярлыки/настройки и ограниченное сохранение состояния, затем реальные операции
   создания/копирования/rename. Критерий: публичный guest в host CI проходит
   Start → приложение → сохранение в My Documents → возврат → поиск в explorer →
   повторное открытие. Реальный Paint на Switch остаётся отдельной отложенной
   аппаратной проверкой, как и GPU/fallback/ввод/повторные запуски.
5. **После программной F-вехи — измерения G.** Сохранить G0, выбрать публичную
   воспроизводимую PE-нагрузку, измерить фазы и накладные расходы профилирования;
   выбирать оптимизацию только по результату. Отдельно вести page protection/SEH,
   затем дальнейшие H/I по активной карте; F не означает готовность всего проекта.

Перед каждым пакетом: сверить HEAD/remote/рабочее дерево, прочитать актуальный
handoff, назвать фактическую базу и ближайший результат. Не перезаписывать более
новые изменения. После пакета: целевые и положенные общие проверки, обновление
handoff/карты, обычный commit/push в рабочую ветку и CI именно опубликованного SHA.
Локального Switch SDK пока нет; Switch compilation проверяется CI, без вывода об
аппаратной приёмке. Проблему SD повторно не диагностировать.

Во время активной работы ориентир контрольной точки — каждые 40 минут: сверить
цель пакета, записать сделанное/проверки/блокеры/следующий шаг и сообщить статус.
Это рабочий протокол, а не установленное расписание: инструмента будильника в
текущей среде нет, автоматическое пробуждение агента между сессиями не настроено.
Незавершённый пакет не объявлять готовым из-за наступления контрольной точки.


## Будущий блок: установка программ и архивы (запрос 2026-10-24)

После базового F5: [поддерживаемые EXE-установщики и импорт архивов](F_INSTALL_AND_ARCHIVES.md).
Нужны реальные папки установки, согласованные системные пути, рабочие ярлыки
на Desktop/в Start и проверка запуска после перезапуска среды. Зависимости
установщика (дочерние процессы, MSI/COM/Win32) реализуются по публичным тестам;
принимать произвольный installer EXE за уже поддержанный нельзя. Отдельно —
ZIP, затем RAR/другие форматы, корректные пути, ошибки, лимиты и сохранность
существующих файлов. Этот блок запланирован, сейчас не реализован.
