# FreakOut Native Demo 0.9.2

Нативный рантайм выполняет заранее перекомпилированный код Windows-демо. В тесте на Linux оригинальная игра прошла Loading и отрисовала экран выбора языка на кадре №142. После подтверждения оригинальная игра перешла на экран «Please Press Enter to Continue», его кадр снят на №166. Из рабочего набора D3D9 используются игровые текстуры, вершины, индексы и команды отрисовки. Вступительные ролики пропускаются.

## Установка на SD-карту

Распакуй `switch/` в корень карты. Положи файлы установленной **Windows-демки** в `switch/FreakOutNative/demo/`. Для запуска нужны `FreakOut.exe`, `msvcp71.dll`, `msvcr71.dll`, `Data2.bnd`, `filemap.txt` и каталог `Data` вместе с `Common/Shaders` и `Common/Sound`.

Запусти FreakOut Native Demo в Homebrew Menu. Крестовина и левый стик перемещают выбор. A подтверждает, B отправляет Escape, одновременное нажатие минуса и плюса завершает сессию. Отчёт сохраняется в `switch/FreakOutNative/boot.log`.

В тестовом рантайме проверены выбор языка и переход на титульный экран. На самой консоли и в заезде проверка ещё впереди. DX9-программы собираются из исходных файлов демки и загружаются в рантайм. Рендеринг 3D-сцены этих программ и воспроизведение звука ещё требуют реализации.

## Воспроизведение

Из каталога `freakout-demo-runtime`:

```sh
python -m pip install -r requirements.txt
python native/generate.py /path/to/demo/FreakOut.exe /path/to/pcrecomp /path/to/demo
make -C native
```

Генератор использует исходные файлы демки, пишет в `native/generated/` гостевые блоки, таблицу импорта и шейдерный байткод. Сборка Switch использует devkitA64 и libnx. Ревизия PCRecomp указана в `freakout-demo-runtime/README.md`.

На Linux с заголовками OpenSSL и libcrypto:

```sh
cc -O1 -I native/generated -c native/generated/blocks.c -o /tmp/freakout-blocks.o
cc -O2 -I native/generated -I switch native/source/*.c switch/freakout_messages.c \
  /tmp/freakout-blocks.o -lcrypto -lm -o /tmp/freakout-native
/tmp/freakout-native /path/to/demo /tmp/freakout-language.ppm 142 > /tmp/freakout-native.log
```

Для повторения теста нужна чистая папка демки без сохранённого языка. Дополнительный аргумент принимает текстовый сценарий ввода с номером кадра, шестнадцатеричным DirectInput scan code и значением 0 или 1 в каждой строке.

`--checkpoint draw --out /tmp/freakout-loading` создаёт эталон для отдельной проверки кадра Loading. Передай файл `/tmp/freakout-loading/reference.json` вместе с журналом прогона на семь кадров и его PPM в `native/verify_frame.py`. Проверка сверяет хеши чтений архива, загруженных текстур и геометрии шести команд отрисовки.

`--checkpoint race` собирает расширенное покрытие x86-трассы вплоть до спуска на Frozen Creek. Оно предназначено для разработки нативного рантайма.
