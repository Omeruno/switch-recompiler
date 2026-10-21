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

# Сборка под Switch кладёт объектные файлы в один каталог по имени файла:
# одинаковые имена .cpp в разных каталогах там ломают линковку.
dups=$(ls "$root"/source/*.cpp "$root"/source/*/*.cpp | xargs -n1 basename | sort | uniq -d)
if [ -n "$dups" ]; then
    echo "== ОДИНАКОВЫЕ ИМЕНА ИСХОДНИКОВ (сломают сборку под Switch): $dups"
    failed=1
fi

for t in test_registry_x86 test_window_x86 test_wndproc test_wndproc2 test_gdi_x86; do
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
    # shellcheck disable=SC2086
    $cxx $flags "$root/tests/winrun.cpp" $src -o "$out/winrun"
    sh "$root/tests/guest/build.sh" "$out/guest"
    for exe in "$out"/guest/*.exe; do
        [ "$(basename "$exe")" = "gui_comdlg_interactive.exe" ] && continue
        # программы работают в своём каталоге: C:\ гостя = build-host/guest
        if (cd "$out/guest" && "$out/winrun" "$exe" > "$out/$(basename "$exe").log" 2>&1); then
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
    printf 'BMtest' > "$out/guest/sample.bmp"
    rm -f "$out/guest/output.bmp"
    if (cd "$out/guest" &&
        "$out/winrun" --new-ui -f 60 -c 8:575:420 gui_comdlg_interactive.exe open > "$out/comdlg_open.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_open.log" &&
        "$out/winrun" --new-ui -f 60 -c 8:175:173 -c 16:575:420 gui_comdlg_interactive.exe list > "$out/comdlg_list.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_list.log" &&
        "$out/winrun" --new-ui -f 60 -c 8:575:420 gui_comdlg_interactive.exe save > "$out/comdlg_save.log" 2>&1 &&
        grep -q "PASSED" "$out/comdlg_save.log" &&
        [ -f "$out/guest/output.bmp" ]); then
        echo "comdlg32 end-to-end open/save: PASSED"
    else
        echo "   comdlg32 end-to-end: FAILED"
        tail -n 30 "$out/comdlg_open.log" 2>/dev/null || true
        tail -n 30 "$out/comdlg_list.log" 2>/dev/null || true
        tail -n 30 "$out/comdlg_save.log" 2>/dev/null || true
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
