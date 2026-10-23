# switch-recompiler

> **Статус: активная разработка (v2).** После итога v1.0 (см.
> [`docs/POSTMORTEM.md`](docs/POSTMORTEM.md)) проект продолжен: новое ядро
> x86, сверенное с настоящим процессором, загрузчик DLL и рантайм Win32.
> Цель — игры и приложения Windows XP на Switch.

Интерпретатор x86 (32-bit) с HLE-слоем Win32/MFC, который запускает
приложения Windows XP (`.exe`) на Nintendo Switch (homebrew, libnx).
Без внешних библиотек эмуляции: всё написано с нуля.

## Что есть

- **Ядро IA-32** (`source/cpu/`): целочисленные инструкции, x87 FPU,
  MMX/SSE/SSE2. Правильность проверяется сверкой с настоящим процессором
  (`tests/cpu_oracle/`): случайные инструкции исполняются нативно и в
  эмуляторе, сравниваются регистры, флаги и память. В CI — 150 тыс.
  тестов на каждый push.
- **Страничная память** как в Windows: регионы, защита, ошибки доступа
  становятся исключениями гостя.
- **Процесс и загрузчик** (`source/runtime/process.*`): EXE и настоящие
  DLL гостя (релокации, экспорт по имени и ординалу, перенаправления,
  TLS, `DllMain`), TEB/PEB Windows XP. Системные DLL эмулируются; DLL
  «наборов API» Windows 7+ (`api-ms-win-*`) отображаются на KERNEL32.
- **KERNEL32 и msvcrt.dll**: несколько сотен функций — куча, файлы
  (Switch: постоянные C/S/R; standalone host default — каталог EXE), синхронизация, время, `printf`/`scanf` по
  правилам msvcrt, математика.
- **USER32/GDI32** (новый слой, `source/ui/`, `win32/user32_*.cpp`,
  `win32/gdi32_module.cpp`): окна и классы, порядок окон и отсечение,
  рамки в классической теме XP, очередь сообщений, ввод, таймеры,
  области обновления; GDI — перья, кисти, шрифты TrueType, битмапы и
  DIB-секции, регионы, палитры, ROP2/ROP3, растяжение, текст и DrawText.
  Сапёр и Paint пока работают на прежнем слое v1 (с заглушками MFC).

| Программа | Состояние |
|---|---|
| `winmine.exe` (Сапёр) | Работает: геймплей, звук, диалоги, меню, реестр |
| `paint.exe` (MFC) | Окно, меню, тулбар, палитра; Карандаш, Кисть, Ластик, Прямоугольник, Эллипс, Пипетка |
| Программы на C/C++ (mingw) | Работают консольные: CRT, файлы, C++-исключения, iostream, STL, свои DLL |

## Дорожная карта

Актуальная очередь и границы готовности — в [активной карте](docs/ACTIVE_ROADMAP.md)
и [аудите 2026-10-23](docs/PROJECT_AUDIT_2026_10_23.md). Базовые runtime, GPU-output
и изоляция повторных гостевых сеансов реализованы; F2 idle desktop уже добавлен,
F3a добавил реальный проводник и path-only Run. F3b добавил выбор приложения для документа и аргументы Run.
Постоянные настройки и панель при работающем приложении ещё впереди.

1. F1 реализован: постоянные C/S/R на Switch, profile/system/temp tree,
   корректные пути и реальные данные о свободном месте.
   [Контракт, проверки и standalone default](docs/F1_FILESYSTEM.md).
2. [F2 реализован программно](docs/F2_DESKTOP.md): native idle desktop/taskbar,
   «Пуск» и выбор EXE. [F3a реализован](docs/F3_EXPLORER.md): C/S/R, реальные папки,
   back/up/адрес/обновление/сортировка и запуск EXE. [F3b реализован](docs/F3_DOCUMENTS.md): приложения документов и аргументы.
   Далее F3c–F5: постоянные настройки,
   сохранение и повторное открытие документа. [План и visual sources](docs/F_SHELL_PLAN.md).
3. Совместимость Win32/SEH/памяти расширять по воспроизводимым случаям.
4. G0 census сохранить; дальнейшая оптимизация/dynarec и DirectDraw/D3D —
   отдельные последующие программы. Аппаратные GPU/Paint/input/relaunch проверки
   остаются отложенными; host-тесты их не заменяют.

## Документация

| Файл | О чём |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Как устроен рантайм: от PE-файла до пикселя на экране |
| [`docs/AI_HANDOFF.md`](docs/AI_HANDOFF.md) | История изменений, проверенные SHA/CI и оставшиеся ограничения |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Текущие приоритеты и критерии готовности |
| [`docs/WIN32_FOUNDATION.md`](docs/WIN32_FOUNDATION.md) | Исходные контракты Win32/SEH (план с историческим исходным состоянием) |
| [`docs/POSTMORTEM.md`](docs/POSTMORTEM.md) | Итоги v1.0 и уроки (исторический) |
| [`docs/archive/`](docs/archive/) | Рабочий журнал расследования Paint (исторический, местами устарел) |

## Структура

```
source/
  main.cpp            точка входа Switch: ввод, кадр, UI-оверлей (меню, тулбар, палитра)
  loader/             PE-парсер и разбор ресурсов
  decoder/            старый декодер x86 (только листинг в main.cpp)
  cpu/                ядро IA-32: интерпретатор, x87, SSE, страничная память
  runtime/            процесс, загрузчик модулей, куча, файловая система
  win32/              HLE Win32/MFC: host_functions, GDI, окна, сообщения,
                      реестр, IAT-патчер, таблицы ABI (mfc42u_abi_table.h,
                      win32_api_abi_table.h)
tests/                хост-тесты, winrun (запуск .exe на ПК), trace_paint
tests/cpu_oracle/     сверка ядра с настоящим процессором
tests/guest/          тестовые программы Windows (собираются mingw-w64)
tests/data/           машинный код для тестов (test*.bin)
tools/                run_host_tests.sh, генерация таблицы ABI MFC42u, аудит заглушек
docs/                 архитектура, итоги, архив журнала
romfs/                сюда кладутся .exe (в репозиторий не входят)
```

## Сборка для Switch

Нужен devkitPro (devkitA64 + libnx).

1. Положить `winmine.exe` и `paint.exe` из своей копии Windows XP в `romfs/`
   (или вообще не пересобирать: см. «Готовая сборка» ниже).
   Сами программы в репозиторий не включены: это файлы Microsoft.
2. `make`
3. Скопировать `switch-recompiler.nro` на карту в `/switch/`.

CI (`.github/workflows/ci.yml`) собирает `.nro` в образе
`devkitpro/devkita64` и выкладывает его артефактом (без `.exe` внутри,
их можно положить на SD как описано ниже; пересборка нужна только для встраивания в `romfs/`).

## Готовая сборка

CI выкладывает `switch-recompiler.nro` артефактом каждого прогона
(вкладка Actions → прогон → Artifacts). Программы класть на карту:

```
sdmc:/switch/switch-recompiler.nro
sdmc:/switch-recompiler/winmine.exe
sdmc:/switch-recompiler/paint.exe
sdmc:/switch-recompiler/Game/game.exe     программа с DLL — в своей подпапке
sdmc:/switch-recompiler/drive_c/Programs/App/app.exe   новый общий C:
```

При запуске появится список программ (крестовина и A). На Switch C: теперь
постоянный `sdmc:/switch-recompiler/drive_c/`, S: — SD-карта, R: — ROMFS
только для чтения. Старые папки не переносятся; относительные пути остаются
возле EXE. Старые сохранённые абсолютные C-пути могут потребовать выбора через S.

## Хост-тесты (Linux/WSL, g++)

```bash
sh tools/run_host_tests.sh            # регрессия + программы mingw (нужен gcc-mingw-w64-i686)
sh tools/run_cpu_oracle.sh 50000 all  # сверка ядра с процессором (нужен gcc-multilib)
```

Если положить свои `winmine.exe` и `paint.exe` в `local/` (каталог не
попадает в git), `run_host_tests.sh` прогонит и их: клик в Сапёре,
рисование и пипетку в Paint.

Запуск программы Windows на ПК:

```bash
build-host/winrun program.exe                    # консольная
build-host/winrun -f 20 -c 5:20:62 -d out.ppm winmine.exe   # оконная: 20 кадров, клик, снимок
```

CI гоняет хост-тесты, сверку ядра и сборку `.nro` на каждый push.

## Методология

Каждый баг подтверждается дизассемблированием или трассировкой на реальном
прогоне, без догадок; поведение процессора — сверкой с железом. После
фикса: компиляция, все хост-тесты, затем Сапёр и Paint.

## Сторонний код

ASCII-глифы в `source/win32/bitmap_font.h` взяты из Adafruit-GFX-Library
(`glcdfont.c`), Copyright (c) 2012 Adafruit Industries, лицензия BSD.
Уведомление сохранено в заголовке файла.

`source/third_party/stb_truetype.h` — растеризатор TrueType Шона Барретта
(stb), общественное достояние / MIT.

`romfs/fonts/Liberation*.ttf` — шрифты Liberation 2.1 (метрически
совместимы с Arial, Times New Roman, Courier New), Copyright (c) 2012
Red Hat, Inc., лицензия SIL Open Font License 1.1 (`romfs/fonts/OFL.txt`).
