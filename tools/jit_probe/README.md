# Independent ARM64 JIT permission probe

This is an **experimental standalone homebrew application**, not part of the Windows XP runtime.

## Purpose
Check whether the actual Switch launch environment permits executable memory through libnx's official `jitCreate` API, and whether a minimal generated AArch64 function can execute. The function is `movz w0, #42; ret`.

## Build
The CI `switch-build` job compiles this independently and publishes `jit-permission-probe-nro`. Alternatively, on a machine with devkitA64 and libnx installed:

```sh
make -C tools/jit_probe
```

Put **jit_probe.nro** on SD and launch it separately from the normal `switch-recompiler.nro`. Retain the known-good runtime NRO. Do not flash system software or alter system permissions for this experiment.

## Results to record on physical hardware
- Firmware/Horizon version; Atmosphere; hbmenu/app launch mode (applet vs application/full-memory)
- `jitCreate` Result hex code
- `jitTransitionToWritable` Result hex code
- `jitTransitionToExecutable` Result hex code
- Whether generated ARM64 returns 42
- Whether `jitClose` succeeds, and whether any crash occurs

**Success on GitHub CI only establishes toolchain compilation.** Real NRO execution and JIT permission feasibility must be verified physically. A failure with one launch mode does not establish impossibility in another.

This experiment is deliberately not wired into the x86 JIT/dynarec, and the interpreter fallback remains authoritative.


## Recorded user result (2026-10-22)
The user reports that this probe was already run on physical Switch in an
older project conversation and the generated ARM64 function returned **42**.
The exact tested revision, launch mode, individual Result codes and cleanup
result were not recovered. This confirms basic execution as reported by the
user; it is not a dynarec/code-cache lifecycle test or a guarantee for every
launch mode. Do not request repetition of the minimal 42 probe as a current
prerequisite. Record environment and cache lifecycle results when testing the
actual integrated code cache. Historical pending-validation notes are
superseded only for this basic function execution result.
