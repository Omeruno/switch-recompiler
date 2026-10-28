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
rm -f "$out/stage-f-acceptance.json" "$out/stage-gpu-acceptance.json"
rm -f "$out/gpu_workspace_cycles.log" "$out/gpu_workspace_cycles.json"
rm -f "$out/stage-g-measurement.json"
rm -f "$out/stage-g-blit.json"
rm -f "$out/gpu_gdi_prototype.log"
rm -f "$out/window_gestures.log"
rm -f "$out/gpu_capacity_replay.log" "$out/gpu_differential_import.log"
rm -f "$out/gpu_damage_mask.log"
rm -f "$out/gpu_workspace_reuse.log" "$out/gpu_workspace_reuse.json"
rm -f "$out/gpu_gdi_warm.log" "$out/gpu_gdi_warm.json" "$out/gpu_gdi_retirement.log"
rm -f "$out/gpu_gdi_dispatch.log" "$out/gpu_gdi_dispatch.json"
rm -f "$out/gpu_window_bindings.log" "$out/gpu_window_coherence.log"
rm -f "$out/gpu_gdi_targets.log" "$out/gpu_gdi_shared_context.log"
rm -f "$out/gpu_gdi_batches.json" "$out/gpu_gdi_batches.log"
rm -f "$out/audio_api.log" "$out/audio_audout.log"
rm -f "$out/registry_keys.log" "$out/keyboard_api.log" "$out/profile_api.log"
failed=0

# Verify pinned artwork and committed native RGBA conversion.
python3 "$root/tools/generate_shell_icons.py" --check

# Native continuation ABI (also tested on AArch64/QEMU by CI).
if $cxx $flags "$root/tests/test_host_fiber.cpp" "$root/source/runtime/host_fiber.cpp" -o "$out/test_host_fiber" &&
   "$out/test_host_fiber"; then
    :
else
    echo "   HOST FIBER FAILED"
    failed=1
fi

# Production audout adapter with an explicit host transport model (not libnx).
if $cxx $flags -D__SWITCH__ -I"$root/tests/audio_audout_shim" \
   "$root/tests/test_audio_audout.cpp" "$root/source/runtime/audio_output.cpp" \
   "$root/source/runtime/audio_audout.cpp" -o "$out/test_audio_audout" &&
   "$out/test_audio_audout" > "$out/audio_audout.log" 2>&1; then
    tail -n 1 "$out/audio_audout.log"
else
    echo "   AUDOUT ADAPTER MODEL FAILED"
    tail -n 25 "$out/audio_audout.log" 2>/dev/null || true
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

for t in test_audio_output test_ini_profile test_keyboard_layout test_host_input test_xp_icons test_command_line test_shell_browser test_desktop_shell test_native_windows test_vfs_mounts test_file_operations test_file_save_api test_file_copy test_shell_file_actions test_shell_context test_shell_preferences test_text_document test_bitmap_document test_shell_links_tree test_gpu_gdi_unavailable test_cpu_access test_window_damage_mask test_blit_fast_path test_workload_profile test_registry_x86 test_window_x86 test_wndproc test_wndproc2 test_gdi_x86 test_handle_objects test_guest_execution test_signal_wait test_waitable_timer test_wait_retry_state test_alertable_revalidation test_execution_stop test_guest_execution_log test_presentation test_presentation_dirty test_presentation_lifecycle test_window_layers test_present_progress test_menu_exposure test_pointer_input test_performance_metrics test_executable_catalog test_launcher_loop; do
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
    if $cxx $flags "$root/tests/benchmark_guest.cpp" $src -o "$out/benchmark_guest" &&
       python3 "$root/tools/benchmark_guest.py" "$out/benchmark_guest" "$guestRoot/graphics_workload.exe" \
           "$out/stage-g-measurement.json" > "$out/stage-g-measurement.log" 2>&1; then
        tail -n 1 "$out/stage-g-measurement.log"
    else
        echo "   G1 PUBLIC PE MEASUREMENT FAILED"
        tail -n 25 "$out/stage-g-measurement.log" 2>/dev/null || true
        failed=1
    fi
    if python3 "$root/tools/benchmark_blit.py" "$out/benchmark_guest" "$guestRoot/graphics_workload.exe" \
       "$out/stage-g-blit.json" > "$out/stage-g-blit.log" 2>&1; then
        tail -n 1 "$out/stage-g-blit.log"
    else
        echo "   G2 GDI COMPARISON FAILED"
        tail -n 25 "$out/stage-g-blit.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_audio_api.cpp" $src -o "$out/test_audio_api" &&
       "$out/test_audio_api" "$guestRoot/audio_output.exe" > "$out/audio_api.log" 2>&1; then
        tail -n 1 "$out/audio_api.log"
    else
        echo "   AUDIO API FAILED"
        tail -n 25 "$out/audio_api.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_profile_api.cpp" $src -o "$out/test_profile_api" &&
       "$out/test_profile_api" "$guestRoot/ini_profile.exe" > "$out/profile_api.log" 2>&1; then
        tail -n 1 "$out/profile_api.log"
    else
        echo "   PROFILE API FAILED"
        tail -n 25 "$out/profile_api.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_keyboard_api.cpp" $src -o "$out/test_keyboard_api" &&
       "$out/test_keyboard_api" "$guestRoot/keyboard_layout.exe" > "$out/keyboard_api.log" 2>&1; then
        tail -n 1 "$out/keyboard_api.log"
    else
        echo "   KEYBOARD API FAILED"
        tail -n 25 "$out/keyboard_api.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_registry_keys.cpp" $src -o "$out/test_registry_keys" &&
       "$out/test_registry_keys" "$guestRoot/registry_keys.exe" > "$out/registry_keys.log" 2>&1; then
        tail -n 1 "$out/registry_keys.log"
    else
        echo "   REGISTRY KEYS FAILED"
        tail -n 25 "$out/registry_keys.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_workspace_output.cpp" $src -o "$out/test_workspace_output" &&
       (cd "$root" && "$out/test_workspace_output" "$guestRoot/shell_busy.exe") > "$out/workspace_output.log" 2>&1; then
        tail -n 1 "$out/workspace_output.log"
    else
        echo "   PERSISTENT WORKSPACE OUTPUT FAILED"
        tail -n 35 "$out/workspace_output.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_shell_busy.cpp" $src -o "$out/test_shell_busy" &&
       (cd "$root" && "$out/test_shell_busy" "$guestRoot/shell_busy.exe") > "$out/shell_busy.log" 2>&1; then
        tail -n 1 "$out/shell_busy.log"
    else
        echo "   INDEPENDENT SHELL INPUT FAILED"
        tail -n 35 "$out/shell_busy.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_window_gestures.cpp" $src -o "$out/test_window_gestures" &&
       "$out/test_window_gestures" "$guestRoot/window_gestures.exe" > "$out/window_gestures.log" 2>&1; then
        tail -n 1 "$out/window_gestures.log"
    else
        echo "   REAL X86 WINDOW GESTURES FAILED"
        tail -n 35 "$out/window_gestures.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_shell_workspace.cpp" $src -o "$out/test_shell_workspace" &&
       (cd "$root" && "$out/test_shell_workspace" "$guestRoot/shell_workspace.exe") > "$out/shell_workspace.log" 2>&1; then
        tail -n 1 "$out/shell_workspace.log"
    else
        echo "   SHELL WORKSPACE FAILED"
        tail -n 35 "$out/shell_workspace.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_shell_bitmaps.cpp" $src -o "$out/test_shell_bitmaps" &&
       "$out/test_shell_bitmaps" "$guestRoot/shell_bitmap.exe" > "$out/shell_bitmaps.log" 2>&1; then
        tail -n 1 "$out/shell_bitmaps.log"
    else
        echo "   SHELL BITMAP WORKFLOW FAILED"
        tail -n 35 "$out/shell_bitmaps.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_shell_documents.cpp" $src -o "$out/test_shell_documents" &&
       "$out/test_shell_documents" "$guestRoot/shell_document.exe" > "$out/shell_documents.log" 2>&1; then
        tail -n 1 "$out/shell_documents.log"
    else
        echo "   SHELL DOCUMENT ARGUMENTS FAILED"
        tail -n 35 "$out/shell_documents.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_filesystem_sessions.cpp" $src -o "$out/test_filesystem_sessions" &&
       "$out/test_filesystem_sessions" "$guestRoot/filesystem_mounts.exe" "$guestRoot/testdll.dll" > "$out/filesystem_sessions.log" 2>&1; then
        tail -n 1 "$out/filesystem_sessions.log"
    else
        echo "   PERSISTENT FILESYSTEM SESSIONS FAILED"
        tail -n 30 "$out/filesystem_sessions.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_choose_color.cpp" $src -o "$out/test_choose_color" &&
       "$out/test_choose_color" "$guestRoot/gui_choosecolor.exe" > "$out/choose_color_abort.log" 2>&1 &&
       "$out/test_choose_color" "$guestRoot/gui_choosecolor.exe" --exit > "$out/choose_color_exit.log" 2>&1; then
        tail -n 1 "$out/choose_color_abort.log"
    else
        echo "   COLOR DIALOG LIFETIME FAILED"
        tail -n 20 "$out/choose_color_abort.log" 2>/dev/null || true
        tail -n 20 "$out/choose_color_exit.log" 2>/dev/null || true
        failed=1
    fi
    if $cxx $flags "$root/tests/test_dialog_lifecycle.cpp" $src -o "$out/test_dialog_lifecycle" &&
       "$out/test_dialog_lifecycle" "$guestRoot/gui_dialog_subclass.exe" > "$out/dialog_abort.log" 2>&1 &&
       "$out/test_dialog_lifecycle" "$guestRoot/gui_dialog_subclass.exe" --exit > "$out/dialog_exit.log" 2>&1; then
        tail -n 1 "$out/dialog_abort.log"
    else
        echo "   STANDARD DIALOG LIFETIME FAILED"
        tail -n 20 "$out/dialog_abort.log" 2>/dev/null || true
        tail -n 20 "$out/dialog_exit.log" 2>/dev/null || true
        failed=1
    fi
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
    if $cxx $flags "$root/tests/test_composition_guest.cpp" $src -o "$out/test_composition_guest" &&
       "$out/test_composition_guest" "$guestRoot/gui_composition.exe" > "$out/composition_native.log" 2>&1 &&
       "$out/test_composition_guest" "$guestRoot/gui_composition.exe" --classic > "$out/composition_classic.log" 2>&1; then
        tail -n 1 "$out/composition_native.log"
        tail -n 1 "$out/composition_classic.log"
    else
        echo "   WINDOW COMPOSITION REGRESSION FAILED"
        tail -n 20 "$out/composition_native.log" 2>/dev/null || true
        tail -n 20 "$out/composition_classic.log" 2>/dev/null || true
        failed=1
    fi
    # Optional locally, mandatory in CI: replay the same live x86 window scenes
    # through the production GL renderer, not a second synthetic implementation.
    if [ "${COMPOSITION_GL_TEST:-0}" = 1 ]; then
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/benchmark_gpu_gdi.cpp" \
           "$root/source/ui/gpu_gdi_prototype.cpp" -lEGL -lGL -o "$out/benchmark_gpu_gdi" &&
           python3 "$root/tools/benchmark_gpu_batches.py" "$out/benchmark_gpu_gdi" "$out/gpu_gdi_batches.json" \
               > "$out/gpu_gdi_batches.log" 2>&1; then
            tail -n 1 "$out/gpu_gdi_batches.log"
        else
            echo "   GPU GDI BATCH COMPARISON FAILED"
            tail -n 25 "$out/gpu_gdi_batches.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_shared_context.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" "$root/source/ui/presentation.cpp" \
           "$root/source/ui/presentation_layers.cpp" "$root/source/ui/window_layers.cpp" \
           "$root/source/ui/mixed_presentation.cpp" \
           -lEGL -lGL -o "$out/test_gpu_gdi_shared_context" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_gdi_shared_context" > "$out/gpu_gdi_shared_context.log" 2>&1; then
            tail -n 1 "$out/gpu_gdi_shared_context.log"
        else
            echo "   GPU GDI SHARED CONTEXT FAILED"
            tail -n 25 "$out/gpu_gdi_shared_context.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_targets.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           -lEGL -lGL -o "$out/test_gpu_gdi_targets" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_gdi_targets" > "$out/gpu_gdi_targets.log" 2>&1; then
            tail -n 1 "$out/gpu_gdi_targets.log"
        else
            echo "   GPU GDI TARGETS/PRESENTATION FAILED"
            tail -n 25 "$out/gpu_gdi_targets.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_prototype.cpp" \
           "$root/source/ui/gpu_gdi_prototype.cpp" "$root/source/ui/ui_gdi.cpp" "$root/source/ui/font.cpp" \
           -lEGL -lGL -o "$out/test_gpu_gdi_prototype" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_gdi_prototype" > "$out/gpu_gdi_prototype.log" 2>&1; then
            tail -n 1 "$out/gpu_gdi_prototype.log"
        else
            echo "   ACTUAL GPU GDI PROTOTYPE FAILED"
            tail -n 25 "$out/gpu_gdi_prototype.log" 2>/dev/null || true
            failed=1
        fi
        bindingsrc=""
        for obj in $src; do
            case "$obj" in */gpu_gdi_prototype.o|*/gpu_gdi_targets.o) ;; *) bindingsrc="$bindingsrc $obj" ;; esac
        done
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_window_gpu_bindings.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           $bindingsrc -lEGL -lGL -o "$out/test_window_gpu_bindings" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_window_gpu_bindings" "$guestRoot/gpu_window_bindings.exe" > "$out/gpu_window_bindings.log" 2>&1; then
            tail -n 1 "$out/gpu_window_bindings.log"
        else
            echo "   LIVE X86 GPU WINDOW BINDINGS FAILED"
            tail -n 25 "$out/gpu_window_bindings.log" 2>/dev/null || true
            failed=1
        fi
        coherencesrc=""
        for obj in $src; do
            case "$obj" in */gpu_gdi_prototype.o|*/gpu_gdi_targets.o|*/opengl_presentation.o) ;; *) coherencesrc="$coherencesrc $obj" ;; esac
        done
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_window_gpu_coherence.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_window_gpu_coherence" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_window_gpu_coherence" "$guestRoot/gpu_window_coherence.exe" > "$out/gpu_window_coherence.log" 2>&1; then
            tail -n 2 "$out/gpu_window_coherence.log"
        else
            echo "   LIVE X86 GPU/CPU COHERENCE FAILED"
            tail -n 25 "$out/gpu_window_coherence.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_dispatch.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_gpu_gdi_dispatch" &&
           python3 "$root/tools/benchmark_gdi_dispatch.py" "$out/test_gpu_gdi_dispatch" "$guestRoot/gpu_gdi_dispatch.exe" \
               "$out/gpu_gdi_dispatch.json" > "$out/gpu_gdi_dispatch.log" 2>&1; then
            tail -n 4 "$out/gpu_gdi_dispatch.log"
        else
            echo "   ORDINARY X86 GPU GDI DISPATCH FAILED"
            tail -n 25 "$out/gpu_gdi_dispatch.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_retirement.cpp" \
           "$root/source/ui/gpu_gdi_prototype.cpp" -lEGL -lGL -o "$out/test_gpu_gdi_retirement" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_gdi_retirement" > "$out/gpu_gdi_retirement.log" 2>&1; then
            tail -n 1 "$out/gpu_gdi_retirement.log"
        else
            echo "   COVERED GPU JOURNAL RETIREMENT FAILED"
            tail -n 25 "$out/gpu_gdi_retirement.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_gdi_warm.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_gpu_gdi_warm" &&
           python3 "$root/tools/benchmark_gdi_warm.py" "$out/test_gpu_gdi_warm" "$guestRoot/gpu_gdi_warm.exe" \
               "$out/gpu_gdi_warm.json" > "$out/gpu_gdi_warm.log" 2>&1; then
            tail -n 2 "$out/gpu_gdi_warm.log"
        else
            echo "   WARM ORDINARY X86 GPU GDI FAILED"
            tail -n 25 "$out/gpu_gdi_warm.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_workspace_reuse.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_gpu_workspace_reuse" &&
           python3 "$root/tools/benchmark_workspace_reuse.py" "$out/test_gpu_workspace_reuse" "$guestRoot/gpu_gdi_warm.exe" \
               "$guestRoot/gpu_workspace_PARTIAL.exe" "$guestRoot/gpu_workspace_COPY.exe" "$guestRoot/gpu_window_coherence.exe" \
               "$out/gpu_workspace_reuse.json" > "$out/gpu_workspace_reuse.log" 2>&1; then
            tail -n 3 "$out/gpu_workspace_reuse.log"
        else
            echo "   RETAINED WORKSPACE X86 GPU OUTPUT FAILED"
            tail -n 25 "$out/gpu_workspace_reuse.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_workspace_cycles.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_gpu_workspace_cycles" &&
           python3 "$root/tools/benchmark_workspace_cycles.py" "$out/test_gpu_workspace_cycles" "$guestRoot/gpu_gdi_warm.exe" \
               "$guestRoot/gpu_workspace_PARTIAL.exe" "$guestRoot/gpu_workspace_COPY.exe" "$guestRoot/gpu_workspace_REFUSAL.exe" "$guestRoot/gpu_window_coherence.exe" \
               "$out/gpu_workspace_cycles.json" > "$out/gpu_workspace_cycles.log" 2>&1; then
            tail -n 2 "$out/gpu_workspace_cycles.log"
        else
            echo "   CPU/NATIVE WORKSPACE CYCLES FAILED"
            tail -n 25 "$out/gpu_workspace_cycles.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_damage_mask.cpp" \
           "$root/source/ui/gpu_gdi_targets.cpp" "$root/source/ui/gpu_gdi_prototype.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $coherencesrc -lEGL -lGL -o "$out/test_gpu_damage_mask" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_damage_mask" "$guestRoot/gpu_damage_mask.exe" \
               > "$out/gpu_damage_mask.log" 2>&1; then
            tail -n 1 "$out/gpu_damage_mask.log"
        else
            echo "   EXACT DIRTY MASK X86/GL FALLBACK FAILED"
            tail -n 25 "$out/gpu_damage_mask.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_capacity_replay.cpp" \
           "$root/source/ui/gpu_gdi_prototype.cpp" -lEGL -lGL -o "$out/test_gpu_capacity_replay" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_capacity_replay" > "$out/gpu_capacity_replay.log" 2>&1; then
            tail -n 1 "$out/gpu_capacity_replay.log"
        else
            echo "   GPU CAPACITY CHECKPOINT REPLAY FAILED"
            tail -n 25 "$out/gpu_capacity_replay.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_gpu_differential_import.cpp" \
           "$root/source/ui/gpu_gdi_prototype.cpp" -lEGL -lGL -o "$out/test_gpu_differential_import" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_gpu_differential_import" > "$out/gpu_differential_import.log" 2>&1; then
            tail -n 1 "$out/gpu_differential_import.log"
        else
            echo "   DIFFERENTIAL CPU IMPORT FAILED"
            tail -n 25 "$out/gpu_differential_import.log" 2>/dev/null || true
            failed=1
        fi
        glsrc=""
        for obj in $src; do
            case "$obj" in */opengl_presentation.o) ;; *) glsrc="$glsrc $obj" ;; esac
        done
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_shell_workspace.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $glsrc -lEGL -lGL -o "$out/test_shell_workspace_gl" &&
           (cd "$root" && EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_shell_workspace_gl" "$guestRoot/shell_workspace.exe") > "$out/shell_workspace_gl.log" 2>&1; then
            tail -n 1 "$out/shell_workspace_gl.log"
        else
            echo "   WORKSPACE NATIVE/GUEST GL LAYERS FAILED"
            tail -n 25 "$out/shell_workspace_gl.log" 2>/dev/null || true
            failed=1
        fi
        if $cxx $flags -DSR_OPENGL_TEST "$root/tests/test_composition_guest.cpp" \
           "$root/source/ui/opengl_presentation.cpp" $glsrc -lEGL -lGL -o "$out/test_composition_gl" &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_composition_gl" "$guestRoot/gui_composition.exe" > "$out/composition_gl_native.log" 2>&1 &&
           EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test_composition_gl" "$guestRoot/gui_composition.exe" --classic > "$out/composition_gl_classic.log" 2>&1; then
            tail -n 2 "$out/composition_gl_native.log"
            tail -n 2 "$out/composition_gl_classic.log"
        else
            echo "   OPENGL WINDOW COMPOSITION REGRESSION FAILED"
            tail -n 20 "$out/composition_gl_native.log" 2>/dev/null || true
            tail -n 20 "$out/composition_gl_classic.log" 2>/dev/null || true
            failed=1
        fi
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
        [ "$(basename "$exe")" = "filesystem_mounts.exe" ] && continue
        [ "$(basename "$exe")" = "shell_busy.exe" ] && continue
        [ "$(basename "$exe")" = "shell_workspace.exe" ] && continue
        [ "$(basename "$exe")" = "shell_document.exe" ] && continue
        [ "$(basename "$exe")" = "shell_bitmap.exe" ] && continue
        # Requires an explicit workload; all modes/pixels are checked above.
        [ "$(basename "$exe")" = "graphics_workload.exe" ] && continue
        # Actual EGL/window lifetime inspection uses the dedicated harness.
        [ "$(basename "$exe")" = "gpu_window_bindings.exe" ] && continue
        [ "$(basename "$exe")" = "gpu_window_coherence.exe" ] && continue
        [ "$(basename "$exe")" = "gpu_gdi_dispatch.exe" ] && continue
        [ "$(basename "$exe")" = "gpu_gdi_warm.exe" ] && continue
        [ "$(basename "$exe")" = "registry_keys.exe" ] && continue
        [ "$(basename "$exe")" = "keyboard_layout.exe" ] && continue
        [ "$(basename "$exe")" = "ini_profile.exe" ] && continue
        [ "$(basename "$exe")" = "audio_output.exe" ] && continue
        [ "$(basename "$exe")" = "gpu_damage_mask.exe" ] && continue
        case "$(basename "$exe")" in gpu_workspace_PARTIAL.exe|gpu_workspace_COPY.exe|gpu_workspace_REFUSAL.exe) continue ;; esac
        [ "$(basename "$exe")" = "window_gestures.exe" ] && continue
        [ "$(basename "$exe")" = "gui_comdlg_interactive.exe" ] && continue
        [ "$(basename "$exe")" = "gui_shell32.exe" ] && continue
        # The legacy/MFC fixture is executed repeatedly by its dedicated harness.
        [ "$(basename "$exe")" = "gui_session_legacy.exe" ] && continue
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
    # Same host process, reused object storage and guest addresses: no fork-based
    # reset may hide stale CRT/heap/TLS/FD/environment state between these runs.
    if $cxx $flags "$root/tests/test_guest_sessions.cpp" $src -o "$out/test_guest_sessions" &&
       "$out/test_guest_sessions" "$guestRoot/gui_session_core.exe" > "$out/guest_sessions.log" 2>&1; then
        tail -n 1 "$out/guest_sessions.log"
    else
        echo "   sequential guest sessions: FAILED"
        tail -n 30 "$out/guest_sessions.log"
        failed=1
    fi
    if $cxx $flags "$root/tests/test_gui_sessions.cpp" $src -o "$out/test_gui_sessions" &&
       "$out/test_gui_sessions" "$guestRoot/gui_session_ui.exe" "$guestRoot/gui_dialog_subclass.exe" "$guestRoot/gui_choosecolor.exe" > "$out/gui_sessions.log" 2>&1; then
        tail -n 1 "$out/gui_sessions.log"
    else
        echo "   SEQUENTIAL GUI SESSIONS FAILED"
        tail -n 30 "$out/gui_sessions.log" 2>/dev/null || true
        failed=1
    fi

    if $cxx $flags "$root/tests/test_legacy_sessions.cpp" $src -o "$out/test_legacy_sessions" &&
       "$out/test_legacy_sessions" "$guestRoot/gui_session_legacy.exe" > "$out/legacy_sessions.log" 2>&1; then
        tail -n 1 "$out/legacy_sessions.log"
    else
        echo "   SEQUENTIAL LEGACY SESSIONS FAILED"
        tail -n 30 "$out/legacy_sessions.log" 2>/dev/null || true
        failed=1
    fi

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
    # ChooseColor without a guest hook: actual palette and OK mouse input.
    if (cd "$guestRoot" && "$out/winrun" --new-ui -f 60 -c 8:138:192 -c 16:485:477 gui_choosecolor.exe interactive > "$out/choose_color_interactive.log" 2>&1) &&
       grep -q "PASSED" "$out/choose_color_interactive.log"; then
        echo "ChooseColor mouse palette/accept: PASSED"
    else
        echo "   COLOR DIALOG INPUT FAILED"
        tail -n 20 "$out/choose_color_interactive.log"
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

if [ "$failed" -eq 0 ] && [ "${COMPOSITION_GL_TEST:-0}" = 1 ] && command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    python3 "$root/tools/record_stage_f_acceptance.py" --root "$root" --guests "$guestRoot"
    python3 "$root/tools/record_gpu_acceptance.py" --root "$root" --guests "$guestRoot"
fi

[ "$failed" -eq 0 ] && echo "Все тесты пройдены." || echo "Есть ошибки."
exit "$failed"
