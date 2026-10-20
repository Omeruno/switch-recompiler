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
src="$root/source/cpu/*.cpp $root/source/runtime/*.cpp $root/source/loader/*.cpp $root/source/decoder/*.cpp $root/source/win32/*.cpp"

mkdir -p "$out"
failed=0

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
        # программы работают в своём каталоге: C:\ гостя = build-host/guest
        if (cd "$out/guest" && "$out/winrun" "$exe" > "$out/$(basename "$exe").log" 2>&1); then
            tail -n 1 "$out/$(basename "$exe").log"
        else
            echo "   $(basename "$exe"): НЕ ПРОЙДЕН"
            tail -n 20 "$out/$(basename "$exe").log"
            failed=1
        fi
    done
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
        echo "winmine: PASSED"
    else
        echo "   winmine: НЕ ПРОЙДЕН (лог: build-host/winmine.log)"
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
