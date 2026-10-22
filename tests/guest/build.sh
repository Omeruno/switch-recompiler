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
# Redistributable, CRT-free lifecycle DLLs; real PE TLS directories/callbacks.
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/lifecycle_dep.c" \
    -o "$out/lifecycle_dep.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DLIFE_ID=2 "$here/lifecycle_module.c" \
    "$out/lifecycle_dep.dll" -o "$out/lifecycle_top.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DLIFE_ID=3 -DLIFE_DISABLE "$here/lifecycle_module.c" \
    "$out/lifecycle_dep.dll" -o "$out/lifecycle_disabled.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DLIFE_ID=4 -DLIFE_TLS "$here/lifecycle_module.c" \
    "$out/lifecycle_dep.dll" -o "$out/lifecycle_late.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DLIFE_ID=6 -DLIFE_FAIL "$here/lifecycle_module.c" \
    "$out/lifecycle_dep.dll" -o "$out/lifecycle_failed.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DLIFE_ID=7 -DLIFE_PARENT "$here/lifecycle_module.c" \
    "$out/lifecycle_failed.dll" "$out/lifecycle_dep.dll" -o "$out/lifecycle_parent.dll" -lkernel32
$cc -O1 "$here/gui_thread_dll_lifecycle.c" "$out/lifecycle_top.dll" "$out/lifecycle_dep.dll" \
    -o "$out/gui_thread_dll_lifecycle.exe" -luser32 -lkernel32
$cc -O1 "$here/gui_process_detach.c" "$out/lifecycle_top.dll" "$out/lifecycle_dep.dll" \
    -o "$out/gui_process_detach.exe" -luser32 -lkernel32
$cc -O1 "$here/gui_last_thread_exit.c" "$out/lifecycle_top.dll" "$out/lifecycle_dep.dll" \
    -o "$out/gui_last_thread_exit.exe" -luser32 -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/lifecycle_startup.c" \
    "$out/lifecycle_dep.dll" -o "$out/lifecycle_startup.dll" -lkernel32
$cc -O1 -nostdlib -e _start@0 "$here/gui_startup_lifecycle.c" "$out/lifecycle_startup.dll" \
    "$out/lifecycle_dep.dll" -o "$out/gui_startup_lifecycle.exe" -lkernel32
for t in "$here"/crt_*.c; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .c)
    $cc -O1 "$t" -o "$out/$name.exe" -lm
done
for t in "$here"/gui_*.c; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .c)
    [ "$name" = "gui_thread_dll_lifecycle" ] && continue
    [ "$name" = "gui_process_detach" ] && continue
    [ "$name" = "gui_last_thread_exit" ] && continue
    [ "$name" = "gui_startup_lifecycle" ] && continue
    res=""
    if [ -e "$here/$name.rc" ]; then
        ${WINDRES:-i686-w64-mingw32-windres} -I "$here" "$here/$name.rc" -O coff -o "$out/$name.res.o"
        res="$out/$name.res.o"
    fi
    extra=""
    [ "$name" = "gui_seh_context" ] && extra="-msse2"
    $cc -O1 $extra "$t" $res -o "$out/$name.exe" -luser32 -lgdi32 -lcomdlg32 -lshell32
done
cxx="${MINGW_CXX:-i686-w64-mingw32-g++}"
for t in "$here"/crt_*.cpp; do
    [ -e "$t" ] || continue
    name=$(basename "$t" .cpp)
    $cxx -O1 -static-libgcc -static-libstdc++ "$t" -o "$out/$name.exe"
done
