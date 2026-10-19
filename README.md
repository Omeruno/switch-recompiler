# switch-recompiler

Интерпретатор x86 (32-bit) с HLE-слоем Win32/MFC, который запускает
приложения Windows XP (`.exe`) на Nintendo Switch (homebrew, libnx).
Без внешних библиотек эмуляции: PE-загрузчик, декодер x86, интерпретатор,
GDI, оконный менеджер, очередь сообщений и заглушки MFC написаны с нуля.

## Статус

| Приложение | Состояние |
|---|---|
| `winmine.exe` (Сапёр) | Полностью работает на железе: геймплей, звук, диалоги, реестр |
| `paint.exe` (MFC) | Окно, меню, тулбар, палитра, рисование пальцем и стиком. Карандаш, Кисть, Ластик, Прямоугольник, Эллипс, Пипетка работают. Линия, Текст, Лупа пока падают; Заливка, Распылитель, Кривая, Многоугольник, Скруглённый прямоугольник и выделения ничего не рисуют |

Подробная карта находок и следующий шаг: [`docs/HANDOFF_paint_investigation.md`](docs/HANDOFF_paint_investigation.md).

## Структура

```
source/
  main.cpp            точка входа Switch: ввод, кадр, UI-оверлей (меню, тулбар, палитра)
  loader/             PE-парсер и разбор ресурсов
  decoder/            декодер инструкций x86
  cpu/                состояние CPU, гостевая память, интерпретатор
  win32/              HLE Win32/MFC: host_functions, GDI, окна, сообщения,
                      реестр, IAT-патчер, таблицы ABI (mfc42u_abi_table.h,
                      win32_api_abi_table.h)
tests/                хост-тесты (регрессия Сапёра) и харнесс trace_paint
tests/data/           машинный код для тестов (test*.bin)
tools/                скрипты: генерация таблицы ABI MFC42u, аудит заглушек
docs/                 сводка расследования
romfs/                сюда кладутся .exe (в репозиторий не входят)
```

## Сборка для Switch

Нужен devkitPro (devkitA64 + libnx).

1. Положить `winmine.exe` и `paint.exe` из своей копии Windows XP в `romfs/`.
   Сами программы в репозиторий не включены: это файлы Microsoft.
2. `make`
3. Скопировать `switch-recompiler.nro` на карту в `/switch/`.

## Хост-тесты (Linux/WSL, g++)

Регрессия Сапёра (все 5 обязаны быть ПРОЙДЕНЫ после любого изменения):

```bash
SRC="source/loader/*.cpp source/decoder/*.cpp source/win32/*.cpp"
for t in test_registry_x86 test_window_x86 test_wndproc test_wndproc2 test_gdi_x86; do
  g++ -std=c++17 -O1 -I. tests/$t.cpp $SRC -o build-host/$t && (cd tests/data && ../../build-host/$t)
done
```

Прогон Paint на хосте (дамп экрана в `paint_output.ppm`):

```bash
g++ -std=c++17 -O1 -I. tests/trace_paint.cpp $SRC -o build-host/trace_paint
./build-host/trace_paint path/to/paint.exe 2000000 [toolIdHex]
```

## Методология

Каждый баг подтверждается дизассемблированием или трассировкой на реальном
прогоне, без догадок. После фикса: компиляция, 5 тестов Сапёра, затем Paint.

## Сторонний код

ASCII-глифы в `source/win32/bitmap_font.h` взяты из Adafruit-GFX-Library
(`glcdfont.c`), Copyright (c) 2012 Adafruit Industries, лицензия BSD.
Уведомление сохранено в заголовке файла.
