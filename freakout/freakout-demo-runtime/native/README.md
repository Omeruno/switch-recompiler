# FreakOut Native Demo 0.9.1

На седьмом `Present` оригинальный код Windows-демо рисует заставку Loading. Рантайм Switch выполняет заранее перекомпилированные блоки ARM64, обрабатывает события Win32, планирует гостевой поток загрузки, читает `Data2.bnd` и принимает команды D3D9. Вступительные ролики пропускаются.

Заставка выводится после загрузки автоматически. B или плюс возвращают к сводке. A повторно показывает кадр, плюс закрывает приложение из сводки.

## Установка на SD-карту

Распакуй `switch/` в корень карты. Скопируй из установленной Windows-демки оригинальные `FreakOut.exe`, `msvcp71.dll`, `msvcr71.dll`, `filemap.txt`, `Data2.bnd` и `Data/Common/Shaders` в `switch/FreakOutNative/demo/`. Запусти FreakOut Native Demo в Homebrew Menu. В `switch/FreakOutNative/boot.log` будет результат с адресом остановки или отчётом об отрисованном кадре.

## Воспроизведение

Из каталога `freakout-demo-runtime`:

```sh
python -m pip install -r requirements.txt
python native/generate.py /path/to/demo/FreakOut.exe /path/to/pcrecomp /path/to/demo
make -C native
```

Используются devkitA64 и libnx. Ревизия PCRecomp указана в `freakout-demo-runtime/README.md`. Генератор сохраняет эталонный x86-трейс в `native/generated/reference.json`.

На Linux (с заголовками OpenSSL и libcrypto):

```sh
cc -O1 -I native/generated -c native/generated/blocks.c -o /tmp/freakout-blocks.o
cc -O2 -I native/generated -I switch native/source/*.c switch/freakout_messages.c \
  /tmp/freakout-blocks.o -lcrypto -lm -o /tmp/freakout-first-frame
/tmp/freakout-first-frame /path/to/demo /tmp/freakout-first-frame.ppm > /tmp/freakout-boot.log
python native/verify_frame.py native/generated/reference.json /tmp/freakout-boot.log /tmp/freakout-first-frame.ppm
```

Проверка сравнивает хеши 143 чтений архива, 39 загрузок сжатых текстур и вершин и индексов шести команд DrawIndexedPrimitive. Отрисовка заставки использует CPU, фиксированный конвейер и билинейную выборку из верхнего mip. До интерактивного меню и заезда нативная сборка ещё не доходит.
