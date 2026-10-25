# Switch Recompiler — глобальная дорожная карта A–I

[Визуальная карта проекта](https://omeruno.github.io/switch-recompiler/) · [Автономный HTML](roadmap/index.html) · [Как открыть и публиковать](roadmap/README.md)

Дата обновления: 2026-10-25, после G4b.1.
Проверенный код `2c4f8497926ec638fdf94441b77961769618b624`:
[CI40196352908, 6/6](https://github.com/Omeruno/switch-recompiler/actions/runs/40196352908).
[Сводка для пользователя](PROJECT_STATUS_RU.md), [активная очередь](ACTIVE_ROADMAP.md),
[технический handoff](AI_HANDOFF.md). История расследований — в handoff и
[аудите 2026-10-23](PROJECT_AUDIT_2026_10_23.md); старые очереди не являются текущим планом.

## Цель и границы

Среда запуска поддержанных 32-битных x86 Windows XP/NT5.1 user-mode приложений
на Nintendo Switch/Horizon. XP-подобная оболочка — собственный native host-shell.
Полное ядро Windows, запуск настоящего explorer.exe и x64 не являются уже
реализованными возможностями. Совместимость конкретных программ, скорость и
визуальная полнота — отдельные критерии; красивый desktop не закрывает Win32 API.

**Текущая точка: ограниченная программная F0–F5 выполнена; G4b.1 реализован
в изолированном GPU-прототипе. Следующий рубеж — G4b.2, подключение окон,
памяти и основного вывода. D, физическая E/F, H/I и расширения среды открыты.**

Рабочий путь живых приложений: x86-интерпретатор → CPU GDI → GPU-композиция
или software fallback → экран. G2 CPU fast path включён. G3/G4 GPU-GDI ещё
не активирован для Paint. Процент общей готовности и срок здесь не назначаются.

## Этап A — корректность и регрессионная база [постоянная работа]

Есть host/MinGW, CPU oracle, ARM64/QEMU, actual-EGL и Switch CI, диагностика,
known-good сборки и публичные EXE из исходников. Proprietary Windows DLL/EXE и
частные пользовательские отчёты не распространяются через публичный git.

Критерий каждого расширения: воспроизводящий публичный тест, регрессии и
exact-SHA CI. Дальнейшая цель — матрица приложений: запуск/GUI/ввод/файлы/
потоки/стабильность/производительность. A не закрывается навсегда.

## Этап B — исполнение x86 и потоки [база реализована]

Интерпретатор, GuestScheduler/кванты, guest threads, TEB/TLS/FS, lastError,
SEH-стек, callbacks/native continuations и корректный thread return существуют.
UI ownership, фоновые workers и выход проверяются публичными гостями.

Открыто: пограничные CPU/ABI случаи, совместимость и дальнейшее ускорение.
Нет готового полноценного x86→ARM64 dynarec; не заменять CPU correctness быстрыми
неверными ответами. Сохранять oracle и interpreter fallback.

## Этап C — объекты, ожидания и lifecycle [ограниченная приёмка пройдена]

Принята модель одного Process: типизированные HANDLE/права/aliases, sync objects,
wait/MsgWait, APC, timers, per-thread queues, startup/exit/cancellation и GUI
ownership. [Stage C acceptance](STAGE_C_ACCEPTANCE.md): пользовательский Switch
отчёт v2 содержит PASS: ALL 8 SUITES; базовый SHA6aacc4e, CI39810630356.

Это не вся NT-модель: cross-process/security/DACL, полная изоляция нескольких
Process, AttachThreadInput и другие расширения остаются отдельной работой.
Прежняя приёмка не подтверждает каждый будущий SHA.

## Этап D — loader и системные Win32 API [частично реализован]

Есть PE32 EXE/DLL, ABI/import/export/delay-load, refcount/unload/TLS/rollback,
ограниченный SEH/unwind и существенный Win32/CRT. Сохранять loader-lock,
отказы загрузки/attach и корректную очистку на requester CPU/TEB/TLS.

Открыто: enforcement прав страниц, полный unwind/SEH, COM, другие API и
зависимости реальных программ. Работать по воспроизводимым несовместимостям.
Pinball x86 на host сначала останавливается на отсутствующем RegCreateKeyExA;
его запуск не объявлен поддержанным. GPU-работы эту ошибку не закрывают.
Критерий: реальное поведение API/ошибок, не successful stub или имя EXE.

## Этап E — графический вывод и композиция [программная реализация, физические gates открыты]

Есть CPU GDI/ROP/readback как эталон, software backend, production Switch GL,
retained window/native planes, ownership/revisions, damage/cache/lifetime и
метрики. Public actual-x86 композиция сравнивается с CPU и настоящим GL через
EGL; [контракт](STAGE_E_COMPOSITION.md), [метрики и физическая очередь](STAGE_E_PERFORMANCE.md).
[Combined E9](STAGE_E_ACCEPTANCE.md) содержит шесть программных групп.

Текущие Paint/GPU/fallback/Lite/input/relaunch, реальные memory/latency/throughput
и аппаратная приёмка новой сборки отложены. Старые Paint open/save подтверждения
относятся к прежним baseline SHA, включая8b0ad70, а не ко всем новым версиям.
Новый GPU-GDI разрабатывается в G3/G4 и должен состыковаться с E без потери CPU
эталона/fallback; DirectDraw/D3D — отдельный этап H.

## Этап F — оболочка, файлы и сеансы [ограниченная программная F0–F5 готова]

Постоянные C/S/R/profile/CWD/Unicode, native desktop/Start/taskbar/explorer/tree,
реальные Desktop/Start `.lnk`, sort/associations, create/rename/delete/properties,
Copy/Paste/контексты, bounded TXT/BMP, checked sync file saving и сквозное
save/find/fresh-session reopen реализованы. Один активный гостевой Process
может иметь несколько окон; последовательные сеансы изолированы. Native-ввод
и frontend/output продолжают жить независимо от guest USER pump.

Статус [F acceptance](F_ACCEPTANCE.md): PASS_SOFTWARE_PENDING_HARDWARE,
22 программные группы. Все пять Luna-обновлений Claude сохранены;
[PanelArea/integration review](LUNA_INTEGRATION_REVIEW.md) закрывает обрезание
новой панели и click-through при активном EXE. Внешний вид не объявлен полным XP.

Физическая приёмка F остаётся открытой. У окон native-оболочки min/max пока декоративны;
их free drag/resize, несколько одновременных EXE, широкая поддержка изображений,
shell COM и полная среда XP не входят в закрытый baseline. [Подробный scope](F_SHELL_PLAN.md).

## Запланированное расширение F — установщики и архивы [не реализовано]

[План](F_INSTALL_AND_ARCHIVES.md): поддержанные installer EXE должны реально
писать payload в постоянную папку, создавать рабочие Desktop/Start ссылки,
правильно сообщать ошибки/отмену и иметь определённый update/remove scope.
Файловая база и ссылки уже есть; совместимость произвольных installers, MSI,
COM, child-process/service зависимостей этим не обеспечивается.

Затем реальная распаковка ZIP; позднее RAR/другие форматы с валидацией путей,
сохранностью данных и ошибками. Установка игры и совместимость её запуска —
разные проверки. Этот блок остаётся видимым после программной F5.

## Этап G — измерения и ускорение CPU/GDI [сейчас G4b.2]

GPU-GDI — текущий экспериментальный подблок на стыке E/G. Он не заменяет
исходную задачу CPU-ускорения и не означает готовый dynarec.

| Пакет | Реализовано | Состояние |
| --- | --- | --- |
| G0/G1 | Opcode/API census, [публичный PE phases/overhead тест](G1_MEASUREMENTS.md) | Инструменты измерений |
| G2 | [CPU SRCCOPY и optional executor phases](G2_BLIT_AND_PHASES.md) | CPU fast path включён |
| G3 | [GL fill/copy/readback/recovery](G3_GPU_GDI_PROTOTYPE.md) | Изолированный bounded prototype |
| G4a | [Targets/generations/direct texture composition](G4A_GPU_TARGETS.md) | Изолированный prototype, без readback ради каждой композиции |
| G4b.1 | [Ordered instanced fills](G4B_GPU_BATCHES.md), cached uniforms, pixel/count/timing comparison | 64 disjoint fills →1 draw; копии/пересечения в порядке; prototype |
| **G4b.2** | HWND/DC lifecycle, raw-DIB/ROP/read barriers, GL-state и production texture bridge | **Следующий незавершённый рубеж** |

До активации нужны реальные x86 GDI/window/lifecycle/pixel tests, EGL oracle и
измерение transfer/checkpoint/submission overhead. GPU-прототипы сейчас имеют
собственные маленькие лимиты и dedicated-context контракт; простое включение
для Paint некорректно. Host результаты не являются консольным FPS.

CPU backend отдельно: выбрать fast paths по измерениям; dynarec требует code
cache/rewrite/invalidation/icache/lifetime/fault fallback и differential oracle.
Минимальный ARM64 JIT-probe ранее дал42 на Switch по подтверждению пользователя;
не повторять его как prerequisite и не выдавать за приёмку полного dynarec.

## Этап H — DirectDraw и Direct3D [не реализован]

Отдельный переводчик graphics API: сначала определённый DirectDraw2D/palette/
surface subset, затем выбранное поколение D3D по целевым приложениям. Нужны
реальные surfaces/textures/ROP-or-API semantics/lifetime и проверяемые игры.
Наличие GL compositor или GPU-GDI этого не даёт.

## Этап I — готовый продукт [открыт]

CI/NRO/логи/публичные fixtures уже есть. Остаются матрица совместимости,
понятная конфигурация/доставка, повторяемая стабильность/сохранения/ошибки,
документация и итоговая приёмка целевого набора приложений на Switch.
Проект не объявлен заменой Windows или поддержкой всех EXE.

## Рабочая очередь и правила

1. G4b.2 — окно/память/presenter bridge с CPU fallback и публичными проверками.
2. D — конкретные compatibility gaps, отдельно memory/SEH/COM и installers.
3. Дальнейшая F/Luna/input/window ergonomics и planned installer/archive scope.
4. Физические E/F и замеры реальных нагрузок — позже, когда пользователь сможет;
   сейчас не блокируют независимую host-разработку. SD восстановлена, диагностика закрыта.
5. CPU dynarec, H graphics API и I product gates — самостоятельные крупные работы.

Сохранять корректность CPU GDI/readback, native1280x720, L legacy/ZL software,
ввод, Claude Luna и fresh guest/persistent frontend lifetime. Локального
Switch SDK нет; сборка NRO через CI. Host PASS не заменяет физическую приёмку.
Изменения только в claude/wonderful-hawking-10mpr0 без force push. Перед работой
проверять HEAD/tree/handoff; после пакета обновлять карту, выполнять положенные
проверки и проверять все шесть CI jobs точного опубликованного SHA.
