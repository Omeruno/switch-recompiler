# FreakOut Demo — обработка сообщений, архив и первая DXT1-текстура

Текущая контрольная точка описана в [3D-MENU.md](3D-MENU.md).

В тестовом окружении игра проходит шесть вызовов `Present`, обработку завершения трёх вступительных роликов, чтение `Data2.bnd` и загрузку трёх mip-уровней первой DXT1-текстуры в гостевую память. Два варианта игрового кода, оригинальный x86 и автоматически перекомпилированный ARM64, проверены в Unicorn.

## Проверенные этапы

| Контрольная точка | Результат |
|---|---|
| Первые 6 чтений архива | Совпали 90 900 импортов, позиции, размеры и SHA-256 прочитанных блоков |
| Первые 3 mip-уровня DXT1 | Совпали 97 909 импортов и 19 чтений архива. Размеры уровней 4×4, 2×2, 1×1. В каждом уровне 8 байт. SHA-256 всех трёх уровней совпали |
| Обработка сообщений | Наблюдались три уведомления DirectShow, операция Peek с удалением и выполнение оригинального WndProc для каждого сообщения |
| Код для Switch | Очередь сообщений и описание размещения DXT1…DXT5 прошли хост-тесты и собраны devkitA64 |

В диагностике ролики завершаются сообщениями без декодирования, звук работает через беззвучную модель, ввод нейтральный. Модели D3D9 и DirectShow выполняются на хосте. Совпадение текстур подтверждает передачу данных в гостевую память через LockRect. Для диагностического вывода текстуры на GPU Switch см. корневой README комплекта 0.3.0.

## Воспроизведение

Нужны Windows-демоверсия, [PCRecomp](https://github.com/sp00nznet/pcrecomp) на ревизии `7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3`, Python и Zig 0.15.2. В каталоге демо должны быть `FreakOut.exe`, `msvcr71.dll`, `msvcp71.dll`, `filemap.txt`, `Data2.bnd` и `Data/Common/Shaders`. Для теста использованы оригинальные файлы из `setup1.cab`.

```sh
python -m pip install -r requirements.txt
python lift_boot.py /path/to/demo/FreakOut.exe /path/to/pcrecomp /path/to/zig \
  --probe-audio --present-limit 12 --compressed-upload-limit 3 \
  --instruction-limit 75000000 --assets /path/to/demo
```

Для остановки после шести чтений `Data2.bnd` замени `--compressed-upload-limit 3` на `--archive-read-limit 6`. Для отдельного отчёта x86 укажи те же флаги в `trace_boot.py` и передай `--out trace.json`.

```sh
cc -std=c11 -Wall -Wextra -Werror -I switch \
  tests/message_queue_test.c switch/freakout_messages.c -o message-test
./message-test
cc -std=c11 -Wall -Wextra -Werror -I switch -I /path/to/libnx/include \
  tests/texture_layout_test.c switch/freakout_texture.c -o texture-test
./texture-test
```

[Публичная Windows-демоверсия](https://www.gamepressure.com/download/freak-out-extreme-freeride-demo/z33880)
