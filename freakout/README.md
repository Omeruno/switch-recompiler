# FreakOut Extreme Freeride на Nintendo Switch

Последняя сборка 0.9.0 исполняет перекомпилированный x86-код Windows-демо, доходит до шести Present и загрузки трёх DXT1 mip-уровней. Меню и заезд пока проверялись в Linux. Исходники Switch-рантайма находятся в `freakout-demo-runtime/native`.

- `freakout-demo-runtime/README.md` описывает воспроизведение исходных тестов и генерацию ARM64.
- `freakout-demo-runtime/SINGLE-RACE.md` описывает запуск Frozen Creek, управление и профиль.
- `freakout-demo-runtime/3D-MENU.md` описывает воспроизведение захватов на GPU Switch.
- `freakout-demo-port`, `freakout-static-lab`, `freakout-switch-probe` и `freakout-switch-nro` содержат ранние этапы перевода и запуска.

Для сборки 0.9.0 используй devkitA64 и libnx. Сначала запусти из корня `freakout-demo-runtime`:

```sh
python -m pip install -r requirements.txt
python native/generate.py /path/to/demo/FreakOut.exe /path/to/pcrecomp /path/to/demo
make -C native
```

На SD-карте сборка ожидает `switch/FreakOutNative/demo` с оригинальной Windows-демкой. В папке должны лежать `FreakOut.exe`, `msvcp71.dll`, `msvcr71.dll`, `Data2.bnd`, `filemap.txt` и `Data/Common/Shaders`. Результат проверки появится в `switch/FreakOutNative/boot.log`.
