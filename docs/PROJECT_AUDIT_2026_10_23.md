# Аудит карты проекта — 2026-10-23

Проверенная база: `03b9fd4d78ab2a5b31c4141f3b2c5b438e88b851`, ветка
`claude/wonderful-hawking-10mpr0`. Fetch не обнаружил более новых коммитов;
дерево было чистым, AGENTS.md не найден. Проверены AI_HANDOFF, ACTIVE_ROADMAP,
ROADMAP, документы C/E/F/G, точки интеграции Switch и соответствующие тесты.
Это аудит реализации и доказательств, а не новая аппаратная приёмка.

## Главный вывод

F0 подготовил жизненный цикл приложений, но пользовательская веха F осталась
не реализована: постоянный файловый корень, рабочий стол, панель задач, «Пуск»
и проводник. Перенос следующего приоритета на G после F0 отложил эту веху.
G0 сохраняем как полезную диагностику; дальнейшее ускорение CPU уступает F1–F5.
Не нужно повторно строить уже реализованные C, E1–E7 или изоляцию F0.

## Все этапы: факты и оставшаяся работа

| Этап | Подтверждено кодом/проверками | Что не закончено | Очередь |
| --- | --- | --- | --- |
| A: регрессионная база | Host/MinGW, CPU oracle, отдельный GL oracle, ARM64/QEMU, Switch и JIT-probe CI | Покрытие не доказывает весь x86/Win32; матрица приложений и версия артефактов развиваются постоянно | Сохранять при каждом кодовом пакете |
| B: потоки | Кванты, GuestThread/TEB/TLS/стек, продолжения и вложенные callbacks; тесты scheduler/execution/thread_resources | Нативный HLE/syscall без yield может задержать frontend; нет универсального безопасного принудительного обрыва | Точечные расширения по воспроизводимому случаю |
| C: NT-подмножество | Ограниченная аппаратная приёмка 8 suites записана в STAGE_C_ACCEPTANCE; HANDLE/waits/APC/timers/shutdown | Не полные NT: cross-process namespace/security, I/O APC, TerminateThread, AttachThreadInput и вся foreground policy | Не переоткрывать принятый bounded scope; расширять отдельно |
| D: loader/API | PE/DLL/TLS, unload/rollback/startup faults, ограниченный SEH, Win32/CRT file I/O | Права mapped-страниц не enforced; весь NT SEH/stack unwind, LoadLibraryEx datafile/image, полная COM/MFC и ряд Shell API не закончены | Файловые контракты нужны F1; остальные отдельные compatibility-пакеты |
| E0–E4 | Контракт, software reference, dirty history, bounded fallible ресурсы/lifetime; принятая отметка E — 60% | Старые описания «pending E4» уже superseded последующими пакетами | Сохранять, не реализовывать заново |
| E5–E7 | Switch GL output/fallback, независимые оконные слои, CPU ROP/readback, ревизии тайлов и host benchmark | Актуальная физическая проверка GPU/Paint/fallback/ввода и console workload evidence | Один отложенный аппаратный список |
| E8 | CPU raster/snapshot/upload/draw/swap, async GPU intervals, bytes/memory, bounded observed-input correlation | Консольные замеры; total process/driver memory; физическая input-to-display задержка | Телеметрия не закрывает аппаратные критерии |
| E9 | Публичный EXE с 6 software-группами и fail-closed отчётом, CI artifact | Реальный Switch output/fallback и Paint smoke | Не объявлять E=100% по host PASS |
| F0 | Bounded EXE picker; owned core/USER/legacy/MFC sessions; launch/exit/error/abort/return loop | Физическая приёмка picker/повторных запусков; **это не desktop-shell** | Фундамент F1–F5 |
| F1–F5 | Базовые VFS/USER/GDI/font/input/presentation primitives есть | Общий SD-backed корень, native XP-like shell, Start, explorer, интеграция taskbar/guest | **Следующая программная веха** — F1 |
| G | G0 — opt-in bounded census, public native x86 fixture, runner smoke | Репрезентативные фазы/время/overhead, выбранная оптимизация, полноценный dynarec/cache/icache/fault fallback | Сохраняем G0; продолжение после рабочей F-вехи или измеренного блокера |
| H | Законченный DirectDraw/D3D backend в этой базе не обнаружен | DirectDraw 2D/palette/flipping, затем выбранный D3D | Отдельный последующий этап |
| I | Execution/crash/renderer logs, CI NRO/EXE artifacts и build identities частично есть | Установка/обновление/config, сохранность настроек/документов, compatibility matrix и выпуск | Файловые/настроечные части поддерживают F; полный выпуск позже |

## Незакрытые файловые контракты перед проводником

1. `Process::LoadMainExe` в `source/runtime/process.cpp` всегда вызывает
   `FileSystem().SetRoot(exeDir_)`. Разные EXE-каталоги дают разные `C:\`.
   Файлы действительно хранятся на хосте/SD; недостаёт общего постоянного тома.
2. `Vfs::ToHost` в `source/runtime/files.cpp` отбрасывает букву диска при
   переводе в host path: `C:\saved.txt` и `D:\saved.txt` ведут в один файл.
   При этом `GetLogicalDrives` объявляет только C, а GetDriveType отвергает D.
   До нескольких томов нужны единая таблица mounts и согласованные API.
3. `GetDiskFreeSpaceA/W/ExA` и `GetVolumeInformationA/W` возвращают постоянные
   величины, в том числе `NTFS`, а не свойства SD/ROMFS. Нельзя показывать эти
   числа в «Моём компьютере» как измеренные свободное место/тип файловой системы.
4. `GuestModulePath` и построение guest command line сводят EXE к `C:\basename`.
   При общем корне и EXE в подпапках нужно сохранить реальный guest path,
   отдельные EXE directory/CWD и поиск соседних DLL.
5. `SHGetSpecialFolderPath` умеет создавать часть каталогов внутри текущего
   EXE-корня; `SYSTEMROOT/WINDIR/TEMP` объявлены строками. Это не установленное
   общее дерево Windows/профиля. Startup должен согласовать реальные каталоги,
   пользовательские документы, env, shell lookup и Open/Save.
6. `shell32_module.cpp` — частичные APIs, не shell. SHGetFileInfo не предоставляет
   готовые icon/image-list handles; ExtractIcon возвращает 0. UxTheme намеренно
   выключен (`uxtheme_module.cpp`). Ни один из этих экспортов не создаёт «Пуск».
7. Текущий `PickExecutableFile` в main.cpp — libnx console/printf, 18 строк на
   страницу. Каталог ограничен 256 EXE/4096 entries/depth; это не explorer.
   Для браузера нужна отдельная ограниченная модель текущего каталога.
8. `RunGuest` ждёт worker через `threadWaitForExit`, а output между picker/guest
   пересоздаётся. Постоянная taskbar и переключение shell/активного гостя требуют
   явного host ownership/output/input lifecycle, а не только замены картинок.

Native-аудит во временном каталоге подтвердил реальную запись/чтение байтов,
case-insensitive lookup, разные корни двух приложений и alias C/D. Switch SD
не использовалась и повторно не диагностировалась. Symlink containment, UNC,
per-drive cwd и read-only mounts ещё не являются завершённым контрактом.

## Дополнительный пробел раннего runtime

`PagedGuestMemory::Protect` сохраняет metadata, но `Read8/16/32`, `Write8/16/32`
и instruction fetch проверяют отображение, а не все права. Отдельный native
probe подтвердил успешную запись после kProtRead и чтение после kProtNone без
fault. Это установленное ограничение корректности D/A; не называть нынешний
VirtualProtect полноценной защитой страниц. Исправление требует отдельного
пакета: loader/HLE writes и guest accesses сейчас используют общие helpers,
поэтому простая проверка во всех Write* может сломать загрузку образов. Нужны
явные privileged host initialization и guest permission checks, включая границы
страниц, precise exceptions и oracle/SEH. Native shell можно разрабатывать
независимо; это не разрешает заявлять полную Windows-совместимость.

## Расхождения документации, исправляемые этим пакетом

- F0_LAUNCHER/SESSION_CORE/GUI_SESSIONS/LEGACY_SESSIONS описывали отсутствие
  production return-to-picker. Это состояние ранних пакетов; интеграция уже есть.
- ARCHITECTURE содержит исторические утверждения «GPU backend нет» и старую
  очередь до compositor. Они не отражают E5–E9/F0.
- ROADMAP всё ещё выделял следующий E1 и следующий raster/input block, хотя
  они уже реализованы. Общие критерии сохраняются; текущая очередь исправлена.
- G0 — census, не оптимизация и не обязательное условие рабочего «Пуска».

## Проверки и ограничения аудита

На входящем SHA полный локальный native/MinGW/Mesa suite ранее прошёл;
локальный AArch64/QEMU также прошёл 20,000 stack switches. При перепроверке
GitHub run [39955956133](https://github.com/Omeruno/switch-recompiler/actions/runs/39955956133)
пять jobs уже успешны, ARM64 job ещё устанавливает инструменты и не завершена.
Это не 6/6 и не новый runtime failure. Аудит добавляет два точечных native
probe и проверку ссылок/документов; source/tests/dependencies/assets не меняются.
CI нового документального SHA проверяется отдельно после публикации.

Физические E8/E9/Paint/GPU/fallback/input/relaunch остаются отложенными по
STAGE_E_PERFORMANCE.md. Новых аппаратных результатов, speedup и процента всего
проекта нет. План реализации и лицензируемые visual sources: [F_SHELL_PLAN.md](F_SHELL_PLAN.md).
