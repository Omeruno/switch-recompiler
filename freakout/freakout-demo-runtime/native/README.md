# FreakOut Native Demo 0.9.6

Нативный рантайм выполняет заранее перекомпилированный код Windows-демо. В тесте на Linux оригинальная игра прошла Loading и отрисовала экран выбора языка на кадре №142. После подтверждения оригинальная игра перешла на экран «Please Press Enter to Continue», его кадр снят на №166. Второе подтверждение запустило загрузку 3D-ресурсов и открыло экран Create New Profile на кадре №286. Из рабочего набора D3D9 используются игровые текстуры, вершины, индексы и команды отрисовки. Вступительные ролики пропускаются.

## Установка на SD-карту

Распакуй `switch/` в корень карты. Положи файлы установленной **Windows-демки** в `switch/FreakOutNative/demo/`. Для запуска нужны `FreakOut.exe`, `msvcp71.dll`, `msvcr71.dll`, `Data2.bnd`, `filemap.txt` и каталог `Data` вместе с `Common/Shaders` и `Common/Sound`.

[Windows-демоверсия Freak Out: Extreme Freeride](https://www.gamepressure.com/download/freak-out-extreme-freeride-demo/z33880)

Запусти FreakOut Native Demo в Homebrew Menu. Крестовина и левый стик отправляют стрелки, A подтверждает, B отправляет Escape, X и ZR отправляют пробел для прыжка. Одновременное нажатие минуса и плюса завершает сессию. Отчёт сохраняется в `switch/FreakOutNative/boot.log`.

В тестовом рантайме проверены выбор языка, переход через титульный экран, создание профиля и живое главное меню. После выбора Done оригинальная игра записала 31 238 байт в `Data/User/profile.pro`. При повторном запуске выбор сохранённого USER 1 снова открывает главное меню. Кадр №420 содержит 102 оригинальных вызова отрисовки. В нём видны комната, камин и анимированная модель персонажа в кресле.

Вершинные и пиксельные DX9-программы исполняются на CPU. Для модели персонажа используются уже деформированные гостевым движком вершины и актуальные матрицы камеры и мира. Память гостя ограничена 192 МиБ. Гостевые и графические ресурсы используют отдельные области, текстуры освобождаются после последней ссылки. Аппаратная отрисовка, вывод звука и проверка на физической Switch ещё впереди.

Маршрут Single Race → Go Ski → Broken Back Mountain открывает выбор события Frozen Creek. После подтверждения рантайм загружает текстуры и исходный Lua-сценарий трассы из Data2.bnd. На кадре №572 в Linux видны рельеф склона, скалы и маркеры маршрута. `race-input.txt` в архиве содержит сценарий для проверки этой последовательности.

Проверка 0.9.6 доходит до кадра №572 с 175 вызовами отрисовки. Дальше рантайм останавливается на ещё не перекомпилированной ветке `0x0043adfd`. Управляемый спуск и поведение снега пока не проверены.

## Воспроизведение

Из каталога `freakout-demo-runtime`:

```sh
python -m pip install -r requirements.txt
python native/generate.py /path/to/demo/FreakOut.exe /path/to/pcrecomp /path/to/demo --checkpoint race
make -C native
```

Генератор использует исходные файлы демки, пишет в `native/generated/` гостевые блоки, таблицу импорта и шейдерный байткод. Сборка Switch использует devkitA64 и libnx. Ревизия PCRecomp указана в `freakout-demo-runtime/README.md`.

На Linux с заголовками OpenSSL и libcrypto:

```sh
cc -O1 -I native/generated -c native/generated/blocks.c -o /tmp/freakout-blocks.o
cc -O2 -I native/generated -I switch native/source/*.c switch/freakout_messages.c \
  /tmp/freakout-blocks.o -lcrypto -lm -o /tmp/freakout-native
/tmp/freakout-native /path/to/demo /tmp/freakout-menu.ppm 420 /path/to/menu-input.txt > /tmp/freakout-native.log
```

Для повторения теста используй чистую папку демки без сохранённого профиля. Сценарий ввода читает номер кадра, шестнадцатеричный DirectInput scan code и значение 0 или 1 в каждой строке. `menu-input.txt` в архиве содержит сценарий подтверждения языка, титульного экрана и создания профиля.

Проверка относительной адресации, масок каналов и параллельной инструкции пиксельного шейдера:

```sh
cc -O2 -Wall -Wextra -Werror tests/shader_test.c native/source/shader.c -lm -o /tmp/freakout-shader-test
/tmp/freakout-shader-test
```

`--checkpoint draw --out /tmp/freakout-loading` создаёт эталон для отдельной проверки кадра Loading. Передай файл `/tmp/freakout-loading/reference.json` вместе с журналом прогона на семь кадров и его PPM в `native/verify_frame.py`. Проверка сверяет хеши чтений архива, загруженных текстур и геометрии шести команд отрисовки.

`--checkpoint race` собирает расширенное покрытие x86-трассы вплоть до спуска на Frozen Creek. Оно предназначено для разработки нативного рантайма.
