#!/bin/sh
# Сборка тестовых программ Windows (mingw-w64, i686) в build-host/guest.
set -eu
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
out="${1:-$here/../../build-host/guest}"
cc="${MINGW_CC:-i686-w64-mingw32-gcc}"
mkdir -p "$out"
# DLL без CRT, намеренно с той же базой, что и EXE: загрузчик обязан её сдвинуть
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -Wl,--image-base,0x400000 \
    "$here/testdll.c" "$here/testdll.def" -o "$out/testdll.dll" -lkernel32
$cc -O1 -nostdlib -e _start@0 -Wl,--image-base,0x400000 "$here/dlltest.c" "$out/testdll.dll" \
    -o "$out/dlltest.exe" -lkernel32
for t in "$here"/crt_*.c; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .c)
    $cc -O1 "$t" -o "$out/$name.exe" -lm
done
for t in "$here"/gui_*.c; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .c)
    res=""
    if [ -e "$here/$name.rc" ]; then
        ${WINDRES:-i686-w64-mingw32-windres} -I "$here" "$here/$name.rc" -O coff -o "$out/$name.res.o"
        res="$out/$name.res.o"
    fi
    $cc -O1 "$t" $res -o "$out/$name.exe" -luser32 -lgdi32 -lcomdlg32
done
cxx="${MINGW_CXX:-i686-w64-mingw32-g++}"
for t in "$here"/crt_*.cpp; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .cpp)
    $cxx -O1 -static-libgcc -static-libstdc++ "$t" -o "$out/$name.exe"
done
