#!/bin/sh
# Сборка тестовых программ Windows (mingw-w64, i686) в build-host/guest.
set -eu
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
out="${1:-$here/../../build-host/guest}"
cc="${MINGW_CC:-i686-w64-mingw32-gcc}"
mkdir -p "$out"
$cc -O2 -Wall -Wextra -Werror -nostdlib -e _start@0 "$here/registry_keys.c" \
    -o "$out/registry_keys.exe" -lkernel32 -ladvapi32
$cc -O2 -Wall -Wextra -Werror -nostdlib -e _start@0 "$here/gpu_damage_mask.c" \
    -o "$out/gpu_damage_mask.exe" -lkernel32 -luser32 -lgdi32
$cc -O1 -nostdlib -e _start@0 "$here/window_gestures.c" \
    -o "$out/window_gestures.exe" -lkernel32 -luser32
for variant in PARTIAL COPY REFUSAL; do
    $cc -O2 -Wall -Wextra -Werror -DWORKSPACE_$variant -nostdlib -e _start@0 "$here/gpu_gdi_warm.c" \
        -o "$out/gpu_workspace_$variant.exe" -lkernel32 -luser32 -lgdi32
done
$cc -O2 -Wall -Wextra -Werror -nostdlib -e _start@0 "$here/gpu_gdi_warm.c" \
    -o "$out/gpu_gdi_warm.exe" -lkernel32 -luser32 -lgdi32
$cc -O2 -Wall -Wextra -Werror -nostdlib -e _start@0 "$here/gpu_gdi_dispatch.c" \
    -o "$out/gpu_gdi_dispatch.exe" -lkernel32 -luser32 -lgdi32
$cc -O1 -nostdlib -e _start@0 "$here/gpu_window_coherence.c" \
    -o "$out/gpu_window_coherence.exe" -lkernel32 -luser32 -lgdi32
$cc -O1 -nostdlib -e _start@0 "$here/gpu_window_bindings.c" \
    -o "$out/gpu_window_bindings.exe" -lkernel32 -luser32 -lgdi32
$cc -O1 -nostdlib -e _start@0 "$here/graphics_workload.c" \
    -o "$out/graphics_workload.exe" -lkernel32 -luser32 -lgdi32
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
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/unload_module.c" -o "$out/unload_dep.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DUNLOAD_ID=2 "$here/unload_module.c" "$out/unload_dep.dll" -o "$out/unload_top.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DUNLOAD_ID=3 "$here/unload_module.c" "$out/unload_dep.dll" -o "$out/unload_side.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/rollback_control.c" -o "$out/rollback_control.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/rollback_module.c" "$out/rollback_control.dll" -o "$out/rollback_dep.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=2 "$here/rollback_module.c" "$out/rollback_dep.dll" "$out/rollback_control.dll" -o "$out/rollback_top.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=8 "$here/rollback_module.c" "$out/rollback_top.dll" "$out/rollback_control.dll" -o "$out/rollback_cycle.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=3 "$here/rollback_module.c" "$out/rollback_control.dll" -o "$out/rollback_baddep.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=4 "$here/rollback_module.c" "$out/rollback_baddep.dll" "$out/rollback_control.dll" -o "$out/rollback_parent.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=5 "$here/rollback_module.c" "$here/rollback_forward.def" "$out/rollback_control.dll" -o "$out/rollback_forward.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=6 "$here/rollback_module.c" "$out/rollback_dep.dll" "$out/rollback_control.dll" -o "$out/rollback_keeper.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/rollback_invalid_child.c" -o "$out/rollback_invalid_child.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/rollback_invalid_parent.c" "$out/rollback_invalid_child.dll" -o "$out/rollback_invalid_parent.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DROLLBACK_ID=7 -DROLLBACK_TLS_OVERFLOW "$here/rollback_module.c" "$out/rollback_control.dll" -o "$out/rollback_invalid_tls.dll" -lkernel32
# Intentionally malformed dependency: parent mapping must be rolled back too.
printf 'this is deliberately not a PE image' > "$out/rollback_invalid_child.dll"
$cc -O1 "$here/gui_loader_rollback.c" "$out/rollback_control.dll" -o "$out/gui_loader_rollback.exe" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/initfault_control.c" -o "$out/initfault_control.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 "$here/initfault_module.c" "$out/initfault_control.dll" -o "$out/initfault_module.dll" -lkernel32
$cc -O1 -shared -nostdlib -e _DllMainCRTStartup@12 -DINITFAULT_ID=2 "$here/initfault_module.c" "$out/initfault_control.dll" -o "$out/initfault_child.dll" -lkernel32
$cc -O1 "$here/gui_initfault_dynamic.c" "$out/initfault_control.dll" -o "$out/gui_initfault_dynamic.exe" -lkernel32
$cc -O1 -nostdlib -e _start@0 "$here/gui_initfault_startup.c" "$out/initfault_module.dll" "$out/initfault_control.dll" -o "$out/gui_initfault_startup.exe" -lkernel32
$cc -O1 "$here/shell_busy.c" -o "$out/shell_busy.exe" -luser32 -lgdi32
$cc -O1 "$here/shell_workspace.c" -o "$out/shell_workspace.exe" -luser32 -lgdi32
$cc -O1 "$here/shell_bitmap.c" -o "$out/shell_bitmap.exe" -luser32 -lgdi32 -lshell32
$cc -O1 "$here/shell_document.c" -o "$out/shell_document.exe" -lshell32
$cc -O1 "$here/filesystem_mounts.c" -o "$out/filesystem_mounts.exe" -lshell32 -lcomdlg32
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
    [ "$name" = "gui_loader_rollback" ] && continue
    [ "$name" = "gui_initfault_dynamic" ] && continue
    [ "$name" = "gui_initfault_startup" ] && continue
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

# One reproducible executable for physical Switch acceptance. Each component
# keeps its own translation-unit static state and the exact normal test body.
acceptance_objects=""
for part in sync wait_multiple named signalwait timer_contention mixed_apc input timer_exit; do
    $cc -O1 -DACCEPTANCE_AGGREGATE -Dmain=StageC_$part -c "$here/gui_thread_$part.c" -o "$out/acceptance_$part.o"
    acceptance_objects="$acceptance_objects $out/acceptance_$part.o"
done
# shellcheck disable=SC2086
$cc -O1 "$here/stage_c_acceptance.c" $acceptance_objects -o "$out/stage_c_acceptance.exe" -luser32 -lgdi32 -lkernel32

# E9 combines the same unmodified test bodies, in separate translation units,
# so static globals and CHECK macros cannot collide. All groups run in one Process.
for part in stage_c_acceptance gui_presentation gui_composition gui_dialog_subclass gui_choosecolor gui_comdlg; do
    case "$part" in
        stage_c_acceptance) entry=stage_c ;;
        gui_presentation) entry=presentation ;;
        gui_composition) entry=composition ;;
        gui_dialog_subclass) entry=dialog ;;
        gui_choosecolor) entry=color ;;
        gui_comdlg) entry=comdlg ;;
    esac
    $cc -O1 -Dmain=StageE_$entry -c "$here/$part.c" -o "$out/e9_$entry.o"
done
revision="${GITHUB_SHA:-$(git -C "$here" rev-parse HEAD 2>/dev/null || echo unknown)}"
# The macro is a literal identity, never arbitrary shell/compiler input.
if ! printf '%s\n' "$revision" | grep -Eq '^[0-9a-f]{40}$'; then revision=unknown; fi
# shellcheck disable=SC2086
$cc -O1 "-DSR_ACCEPTANCE_BUILD=\"$revision\"" "$here/stage_e_acceptance.c" \
    "$out/e9_stage_c.o" "$out/e9_presentation.o" "$out/e9_composition.o" \
    "$out/e9_dialog.o" "$out/e9_color.o" "$out/e9_comdlg.o" "$out/gui_dialog_subclass.res.o" \
    $acceptance_objects -o "$out/stage_e_acceptance.exe" -luser32 -lgdi32 -lkernel32 -lcomdlg32
