# Horizon WASM compatibility candidate

This is an interpreter-only WAMR port and executable feature probe, independent
of the page reader. It is not a game-capable browser or a selected production
engine. The pinned runtime accepts the supplied Keldurn WASM in a **host load-only
check**; its web imports are not bound and its game startup is not called.

## What executes

The same C harness on host and Switch embeds ten small, owned WASM modules and
checks actual results: arithmetic, multivalue, bulk memory, extended constants,
externref tables, legacy exceptions, shared atomics, zero-timeout atomic.wait,
shared memory.grow and a required out-of-bounds trap. Two native threads then
execute the same shared-memory WASM counter and must produce exactly 20,000.
A native condition-variable timeout checks lock reacquisition. Twelve passes
are needed for a successful host run; Switch adds a compiler-TLS case. No positive game-readiness status is
inferred from these results.

The runtime is pinned by commit in wamr.lock.json; fetch_wamr.py rejects local
changes, and CMake rejects another revision. Runtime code is fetched into the
ignored build directory rather than committing a mutable external checkout.
The license is included with the diagnostic NRO. Fixtures have WAT source,
committed WASM bytes and SHA-256 metadata; embed_fixtures.py verifies their hashes.

## Host build

Requires CMake 3.19+, a C compiler, Python 3 and Git:

```sh
python3 browser/engine/tools/fetch_wamr.py browser/engine/build/wamr
python3 browser/engine/tools/embed_fixtures.py
cmake -S browser/engine -B browser/engine/build/host \
  -DWAMR_ROOT_DIR="$PWD/browser/engine/build/wamr" -DCMAKE_BUILD_TYPE=Release
cmake --build browser/engine/build/host -j4
ctest --test-dir browser/engine/build/host --output-on-failure
# Optional external, user-supplied game binary: parse/load only, never instantiate.
browser/engine/build/host/engine-host /path/to/keldurn_bg.wasm
```

Reassemble the adjacent .wat sources with WABT's wat2wasm --enable-all when
changing fixtures, update the checksums/expected values, then regenerate the C
table. CI does not download or distribute the game binary or account data.

## Switch build and use

Requires devkitA64/libnx and CMake/Python/Git. `make -C browser/engine` creates
switch-engine-probe.nro. Copy it into the SD `/switch/` folder and launch from
Homebrew Menu using title override (R while launching a game).

- The 0.1.1 probe opens a menu without executing tests. Up/Down select a case;
  A runs only that case and selects the next. Y runs the full suite; + quits.
  The first device-only case checks compiler TLS, followed by scalar arithmetic.
  Each selected case gets a fresh runtime, so failures are easier to localize.
- A plain-text report is appended to `/switch/switch-browser/engine-report.txt`.
- Press − for a separate **virtual address reservation only** check that asks libnx for a 4 GiB
  address range and immediately releases it. No 4 GiB physical allocation or
  growth capability is inferred from a pass.
- Optionally put the supplied client at
  `/switch/switch-browser/engine/client.wasm` and press X. This loads/validates
  its code with WAMR but **does not instantiate it, invoke start, authenticate,
  create game threads, or request graphics**. Inputs are capped at 128 MiB;
  WAMR's metadata pool is 64 MiB. Failure is reported, not treated as gameplay.

Only ASCII diagnostic text is used; the page reader's font/RomFS is not needed.
The report contains fixture/runtime errors, not cookies, tokens or game data.
The browser/profile NRO continues to be built separately. If a native thread
join fails, the harness retains its runtime instead of freeing memory beneath
an unknown live thread; exit the probe before retrying.

## Boundaries of this port

- Classic interpreter; no JIT, AOT, executable mappings or native performance
  claim. WAMR currently restricts its legacy exception handling to this mode.
- Software WASM bounds checks are enabled. libnx mutexes, conditions, joinable
  threads, TLS and stack boundaries replace Linux's platform layer. Detached
  workers are explicitly unsupported, and read/write locks serialize readers.
- The memory-map shim eagerly backs small read/write reservations and caps their
  aggregate at 64 MiB. It rejects executable, fixed-address, file-backed and
  larger mappings. Its mprotect operation checks range/permissions for this
  software-bounds mode; it does not install OS page protection. It cannot back
  the client's 4 GiB shared-memory maximum, even if VA reservation succeeds.
- A passing host run does not verify this libnx platform layer. ARM64 compilation
  is a separate CI gate; execution, repeated-run cleanup and timings require the
  target console. No physical-page mapping privileges are assumed.
- No JS/WASM object bridge, module Workers, WebGPU, Web Audio/worklets, game
  networking, asset loading or playable world is supplied by this probe.

## Next implementation gates

1. Run the NRO on firmware 21.1.0 and inspect the saved feature/memory report.
2. Implement/test a stable shared-linear-memory reservation/commit/growth strategy
   under real Horizon permissions, with quota/accounting and failure tests.
3. Establish an acceleration strategy compatible with legacy exceptions. The
   interpreter is the correctness baseline, not a promised performance solution.
4. Develop genuine WebGPU and JS/WASM host bridges, with GPU limit/shader probes;
   the supplied normal Keldurn launcher requires WebGPU. A fake adapter or skipped
   check does not supply rendering.
5. Integrate the viable runtime/graphics path with resource loading, authentication,
   input and audio, then test character creation and in-world gameplay separately.

## Device crash investigation (0.1.1)

The user reported the original probe terminated with a system error. The stage
and cause are not established. Version 0.1.1 removes automatic suite/VA execution
at startup, presents the console before running WAMR, flushes pre-operation
checkpoints to SD, and keeps earlier crash evidence instead of truncating it.
The Switch toolchain now explicitly uses the SDK's local-exec TLS model. These
changes improve diagnosis and remove an unsafe startup sequence; they do not
establish that the device crash is resolved. The saved report's final BEGIN
line identifies the operation to investigate if the process still terminates.

## Observed device result — 2026-11-05

The user's 0.1.1 report contains PASS records for all 13 distinct device cases,
including native TLS, shared-memory growth, legacy exceptions, externref tables,
the bounds trap and the two-thread WASM atomic counter. The two-thread case
passed twice. There is no FAIL or suite SUMMARY in that session; these are
individual case records. See device-evidence-2026-11-05.json for the summarized
evidence and source digest. The original crash was not reproduced in this
report; its exact cause remains unproven.

The displayed tens-of-milliseconds durations include synchronous SD checkpoints
and console frame updates. They must not be interpreted as raw WASM execution
times. Neither the 4 GiB VA reservation nor real client load-only appears in
this device report, and no game execution or GPU result can be inferred.
