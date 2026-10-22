#!/bin/sh
# Сборка и прогон хост-тестов (Linux/WSL, g++).
#
#   sh tools/run_host_tests.sh            регрессионные тесты + программы mingw
#   sh tools/run_host_tests.sh каталог    плюс winmine.exe / paint.exe из каталога
#                                         (по умолчанию local/, если он есть)
#
# Программы Microsoft в репозиторий не кладём: положите свои копии в local/.
#
# Код возврата ненулевой, если хоть один тест не собрался или не прошёл.
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="$root/build-host"
cxx="${CXX:-g++}"
flags="-std=c++17 -O1 -Wall -I$root"
src="$root/source/cpu/*.cpp $root/source/ui/*.cpp $root/source/runtime/*.cpp $root/source/loader/*.cpp $root/source/decoder/*.cpp $root/source/win32/*.cpp"

mkdir -p "$out"
failed=0

# Native continuation ABI (also tested on AArch64/QEMU by CI).
if $cxx $flags "$root/tests/test_host_fiber.cpp" "$root/source/runtime/host_fiber.cpp" -o "$out/test_host_fiber" &&
   "$out/test_host_fiber"; then
    :
else
    echo "   HOST FIBER FAILED"
    failed=1
fi

# Fast, standalone guest scheduler regression (no PE emulator link step).
printf '== guest scheduler\\n'
if $cxx $flags "$root/tests/test_guest_scheduler.cpp" -o "$out/test_guest_scheduler" &&
   "$out/test_guest_scheduler"; then
    :
else
    echo "   GUEST SCHEDULER FAILED"
    failed=1
fi

# Сборка под Switch кладёт объектные файлы в один каталог по имени файла:
# одинаковые имена .cpp в разных каталогах там ломают линковку.
dups=$(ls "$root"/source/*.cpp "$root"/source/*/*.cpp | xargs -n1 basename | sort | uniq -d)
if [ -n "$dups" ]; then
    echo "== ОДИНАКОВЫЕ ИМЕНА ИСХОДНИКОВ (сломают сборку под Switch): $dups"
    failed=1
fi

# Compile the common runtime once per suite, then link each harness against
# exactly the same objects. Rebuild on every invocation so header changes
# cannot leave stale objects; do not archive away static HLE registrations.
mkdir -p "$out/objects"
objects=""
for cpp in $src; do
    obj="$out/objects/$(basename "$cpp" .cpp).o"
    if ! $cxx $flags -c "$cpp" -o "$obj"; then
        echo "   RUNTIME COMPILE FAILED: $cpp"
        exit 1
    fi
    objects="$objects $obj"
done
src="$objects"

for t in test_registry_x86 test_window_x86 test_wndproc test_wndproc2 test_gdi_x86 test_handle_objects test_guest_execution test_signal_wait test_waitable_timer test_wait_retry_state test_alertable_revalidation test_execution_stop test_guest_execution_log test_presentation test_presentation_dirty test_present_progress test_menu_exposure; do
    printf '== %s\n' "$t"
    # shellcheck disable=SC2086
    if ! $cxx $flags "$root/tests/$t.cpp" $src -o "$out/$t"; then
        echo "   СБОРКА НЕ УДАЛАСЬ"
        failed=1
        continue
    fi
    # Тесты читают свои test*.bin из текущей директории.
    if (cd "$root/tests/data" && "$out/$t" > "$out/$t.log" 2>&1); then
        tail -n 1 "$out/$t.log"
    else
        echo "   ТЕСТ НЕ ПРОЙДЕН (лог: build-host/$t.log)"
        tail -n 20 "$out/$t.log"
        failed=1
    fi
done

# Программы Windows из tests/guest через winrun (нужен mingw-w64)
if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    printf '== guest programs\n'
    # Isolate generated files/directories so repeated suites cannot change
    # which directory-list row the scripted file picker selects.
    guestRoot=$(mktemp -d "$out/guest-run.XXXXXX")
    # shellcheck disable=SC2086
    $cxx $flags "$root/tests/winrun.cpp" $src -o "$out/winrun"
    sh "$root/tests/guest/build.sh" "$guestRoot"
    # Real Process allocation/reclamation, including static TLS from MinGW PE.
    if $cxx $flags "$root/tests/test_thread_resources.cpp" $src -o "$out/test_thread_resources" &&
       "$out/test_thread_resources" "$guestRoot/gui_seh.exe" > "$out/test_thread_resources.log" 2>&1; then
        tail -n 1 "$out/test_thread_resources.log"
    else
        echo "   THREAD RESOURCE RECLAMATION FAILED"
        tail -n 20 "$out/test_thread_resources.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_process_detach.cpp" $src -o "$out/test_process_detach" &&
       "$out/test_process_detach" "$guestRoot/gui_process_detach.exe" "$guestRoot/gui_last_thread_exit.exe" > "$out/test_process_detach.log" 2>&1; then
        tail -n 1 "$out/test_process_detach.log"
    else
        echo "   PROCESS DETACH REGRESSION FAILED"
        tail -n 30 "$out/test_process_detach.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_startup_lifecycle.cpp" $src -o "$out/test_startup_lifecycle" &&
       "$out/test_startup_lifecycle" "$guestRoot/gui_startup_lifecycle.exe" > "$out/test_startup_lifecycle.log" 2>&1; then
        tail -n 1 "$out/test_startup_lifecycle.log"
    else
        echo "   STARTUP LIFECYCLE REGRESSION FAILED"
        tail -n 35 "$out/test_startup_lifecycle.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_dll_unload.cpp" $src -o "$out/test_dll_unload" &&
       "$out/test_dll_unload" "$guestRoot/gui_dll_unload.exe" > "$out/test_dll_unload.log" 2>&1; then
        tail -n 1 "$out/test_dll_unload.log"
    else
        echo "   DLL UNLOAD REGRESSION FAILED"
        tail -n 30 "$out/test_dll_unload.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_loader_rollback.cpp" $src -o "$out/test_loader_rollback" &&
       "$out/test_loader_rollback" "$guestRoot/gui_loader_rollback.exe" > "$out/test_loader_rollback.log" 2>&1; then
        tail -n 1 "$out/test_loader_rollback.log"
    else
        echo "   LOADER ROLLBACK REGRESSION FAILED"
        tail -n 30 "$out/test_loader_rollback.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_initfault.cpp" $src -o "$out/test_initfault" &&
       "$out/test_initfault" "$guestRoot/gui_initfault_startup.exe" "$guestRoot/gui_initfault_dynamic.exe" > "$out/test_initfault.log" 2>&1; then
        tail -n 1 "$out/test_initfault.log"
    else
        echo "   INITIALIZER EXCEPTION REGRESSION FAILED"
        tail -n 35 "$out/test_initfault.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_gui_input.cpp" $src -o "$out/test_gui_input" &&
       "$out/test_gui_input" "$guestRoot/gui_thread_input.exe" > "$out/test_gui_input.log" 2>&1 &&
       "$out/test_gui_input" "$guestRoot/gui_thread_input.exe" --paced > "$out/test_gui_input_paced.log" 2>&1; then
        tail -n 1 "$out/test_gui_input.log"
        tail -n 2 "$out/test_gui_input_paced.log"
    else
        echo "   GUI INPUT OWNERSHIP REGRESSION FAILED"
        tail -n 35 "$out/test_gui_input.log" 2>/dev/null || true
        tail -n 35 "$out/test_gui_input_paced.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_presentation_guest.cpp" $src -o "$out/test_presentation_guest" &&
       "$out/test_presentation_guest" "$guestRoot/gui_presentation.exe" > "$out/test_presentation_guest.log" 2>&1 &&
       "$out/test_presentation_guest" "$guestRoot/gui_presentation.exe" --classic > "$out/test_presentation_classic.log" 2>&1; then
        tail -n 1 "$out/test_presentation_guest.log"
        tail -n 1 "$out/test_presentation_classic.log"
    else
        echo "   PRESENTATION GUEST REGRESSION FAILED"
        tail -n 20 "$out/test_presentation_guest.log" 2>/dev/null || true
        tail -n 20 "$out/test_presentation_classic.log" 2>/dev/null || true
        failed=1
    fi
    # Execute IA-32 instructions in alternating worker quanta; FS must
    # resolve the currently selected worker TEB, not the main TEB.
    if (cd "$guestRoot" && "$out/winrun" --probe-x86-threads "$guestRoot/gui_seh.exe" > "$out/x86_threads.log" 2>&1); then
        tail -n 1 "$out/x86_threads.log"
    else
        echo "   x86 worker execution: FAILED"
        tail -n 20 "$out/x86_threads.log"
        failed=1
    fi
    # Verify two separately allocated guest TEB/TLS/stacks before execution.
    if (cd "$guestRoot" && "$out/winrun" --probe-threads "$guestRoot/gui_seh.exe" > "$out/thread_probe.log" 2>&1); then
        tail -n 1 "$out/thread_probe.log"
    else
        echo "   guest worker isolation: FAILED"
        tail -n 20 "$out/thread_probe.log"
        failed=1
    fi
    for exe in "$guestRoot"/*.exe; do
        [ "$(basename "$exe")" = "gui_comdlg_interactive.exe" ] && continue
        [ "$(basename "$exe")" = "gui_shell32.exe" ] && continue
        # Intentionally exits with 55; all six modes are checked by the
        # post-exit inspector above, including retained DLL state and code.
        [ "$(basename "$exe")" = "gui_last_thread_exit.exe" ] && continue
        # программы работают в своём каталоге: C:\ гостя = свежий guest-run каталог
        if (cd "$guestRoot" && "$out/winrun" "$exe" > "$out/$(basename "$exe").log" 2>&1); then
            tail -n 1 "$out/$(basename "$exe").log"
        else
            echo "   $(basename "$exe"): НЕ ПРОЙДЕН"
            tail -n 20 "$out/$(basename "$exe").log"
            failed=1
        fi
    done
    # End-to-end dialog test: open an existing file, then save a new file.
    # The virtual 800x600 desktop centers a 530x390 dialog. Script a click
    # on the dialog Open/Save button at approximately (575,420).
    printf 'BMtest' > "$guestRoot/sample.bmp"
    if (cd "$guestRoot" &&
        "$out/winrun" --new-ui -f 60 -c 8:575:420 gui_comdlg_interactive.exe open > "$out/comdlg_open.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_open.log" &&
        SR_COMDLG_TRACE=1 "$out/winrun" --new-ui -f 60 -c 8:-1003:0 -c 16:575:420 gui_comdlg_interactive.exe list > "$out/comdlg_list.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_list.log" &&
        "$out/winrun" --new-ui -f 60 -c 8:575:420 gui_comdlg_interactive.exe save > "$out/comdlg_save.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_save.log" &&
        [ -f "$guestRoot/output.bmp" ]); then
        echo "comdlg32 end-to-end open/save: PASSED"
    else
        echo "   comdlg32 end-to-end: FAILED"
        tail -n 30 "$out/comdlg_open.log" 2>/dev/null || true
        tail -n 30 "$out/comdlg_list.log" 2>/dev/null || true
        tail -n 30 "$out/comdlg_save.log" 2>/dev/null || true
        failed=1
    fi
    # Run the special-folder test AFTER file-dialog scenarios: it creates
    # My Documents, otherwise the list's first item becomes a directory.
    if (cd "$guestRoot" && "$out/winrun" gui_shell32.exe > "$out/gui_shell32.exe.log" 2>&1); then
        tail -n 1 "$out/gui_shell32.exe.log"
    else
        echo "   gui_shell32.exe: НЕ ПРОЙДЕН"
        tail -n 20 "$out/gui_shell32.exe.log"
        failed=1
    fi
else
    echo "(mingw-w64 не найден: гостевые программы пропущены)"
fi

bins="${1:-$root/local}"
if [ -f "$bins/winmine.exe" ]; then
    printf '== winmine.exe\n'
    # кадр 5: клик в левую верхнюю клетку — поле должно измениться
    if (cd "$bins" && "$out/winrun" -f 12 -d "$out/winmine_before.ppm" winmine.exe &&
        "$out/winrun" -f 12 -c 5:20:62 -d "$out/winmine_after.ppm" winmine.exe) > "$out/winmine.log" 2>&1 &&
       [ -s "$out/winmine_before.ppm" ] && ! cmp -s "$out/winmine_before.ppm" "$out/winmine_after.ppm"; then
        echo "winmine (прежний слой окон): PASSED"
    else
        echo "   winmine: НЕ ПРОЙДЕН (лог: build-host/winmine.log)"
        failed=1
    fi
fi
if [ -f "$bins/winmine.exe" ]; then
    printf '== winmine.exe, новый USER32/GDI32\n'
    # кадр 5: клетка; кадры 8 и 14: меню «Игра» -> третий уровень сложности
    if (cd "$bins" && "$out/winrun" --new-ui -f 30 -s 6 "$out/wm_new_cell.ppm" -c 5:100:185 winmine.exe &&
        "$out/winrun" --new-ui -f 30 -s 4 "$out/wm_new_start.ppm" winmine.exe &&
        "$out/winrun" --new-ui -f 30 -s 20 "$out/wm_new_expert.ppm" -c 8:100:115 -c 14:140:191 winmine.exe) > "$out/winmine_new.log" 2>&1 &&
       ! cmp -s "$out/wm_new_start.ppm" "$out/wm_new_cell.ppm" && ! cmp -s "$out/wm_new_start.ppm" "$out/wm_new_expert.ppm"; then
        echo "winmine (новый слой окон): PASSED"
    else
        echo "   winmine, новый слой: НЕ ПРОЙДЕН (лог: build-host/winmine_new.log)"
        failed=1
    fi
fi
if [ -f "$bins/paint.exe" ]; then
    printf '== paint.exe\n'
    # shellcheck disable=SC2086
    $cxx $flags "$root/tests/trace_paint.cpp" $src -o "$out/trace_paint"
    # Карандаш рисует и рисунок переживает перерисовку; пипетка берёт цвет
    if (cd "$out" && ./trace_paint "$bins/paint.exe" 3000000 0 0x27c) > "$out/paint_pencil.log" 2>&1 &&
       grep -q "рисунок сохранился" "$out/paint_pencil.log" &&
       (cd "$out" && ./trace_paint "$bins/paint.exe" 3000000 0 0x27f) > "$out/paint_picker.log" 2>&1 &&
       grep -q "ЦВЕТ ПОДОБРАН ВЕРНО" "$out/paint_picker.log"; then
        echo "paint: PASSED"
    else
        echo "   paint: НЕ ПРОЙДЕН (логи: build-host/paint_*.log)"
        failed=1
    fi
fi

[ "$failed" -eq 0 ] && echo "Все тесты пройдены." || echo "Есть ошибки."
exit "$failed"
