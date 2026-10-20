#!/bin/sh
# Сборка и прогон хост-тестов (Linux/WSL, g++).
#
#   sh tools/run_host_tests.sh                  5 регрессионных тестов Сапёра
#   sh tools/run_host_tests.sh path/to/paint.exe  плюс прогон Paint (trace_paint)
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

if [ $# -ge 1 ]; then
    printf '== trace_paint %s\n' "$1"
    # shellcheck disable=SC2086
    $cxx $flags "$root/tests/trace_paint.cpp" $src -o "$out/trace_paint"
    "$out/trace_paint" "$1" "${2:-2000000}" || failed=1
fi

[ "$failed" -eq 0 ] && echo "Все тесты пройдены." || echo "Есть ошибки."
exit "$failed"
