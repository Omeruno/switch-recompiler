# Horizon WASM compatibility candidate

This is an interpreter-only WAMR port and executable feature probe, independent
of the page reader. It is not a game-capable browser or a selected production
engine. The pinned runtime accepts the supplied Keldurn WASM in a **host load-only
check**; its web imports are not bound and its game startup is not called.

## What executes

The same C harness on host and Switch embeds twelve small, owned WASM modules and
checks actual results: arithmetic, multivalue, bulk memory, extended constants,
externref tables, legacy exceptions, shared atomics, zero-timeout atomic.wait,
shared memory.grow and a required out-of-bounds trap. Two native threads then
execute the same shared-memory WASM counter and must produce exactly 20,000.
A native condition-variable timeout checks lock reacquisition. Fourteen passes
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

- The 0.2.0 probe opens a menu without executing tests. Up/Down select a case;
  A runs only that case and selects the next. Y runs the full suite; + quits.
  The initially selected case is `client_memory_growth`: press A to run the new
  memory test. The prior device-only TLS and scalar cases remain available.
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
- Linear memory reserves up to 4 GiB of virtual address space and commits only
  the current prefix in 64 KiB units. The aggregate backing quota is 256 MiB,
  a conservative policy limit rather than a measured game requirement or RSS.
  libnx may already have allocated the process heap. Switch uses heap pages
  aliased into a reserved stack-region address window via svcMapMemory and
  svcUnmapMemory, following libnx's thread-stack mechanism. Mapping permissions
  and sufficient stack VA must be verified on the device; no firmware update is
  involved. Host uses PROT_NONE reservations and mprotect for the same policy.
- Failed commits preserve previous memory. Partial unmap failures quarantine the
  reservation and retain still-mapped backing for a safe retry. Executable,
  fixed-address and file-backed mappings are rejected. Small mappings reserve
  64 MiB of VA slack for unshared growth, which can fail beyond that capacity.
- `client_memory_growth` uses the client's exact memory limits (266 initial and
  65,536 maximum pages), rejects an over-quota grow, then grows by 16 pages and
  checks old data, zero initialization, stable native address, exact accounting
  and cleanup. It uses our own micro-module, not game code. `unshared_growth`
  verifies failed growth also preserves an unshared instance. Failure-injection
  host tests cover quota across ranges, commit failure, partial unmap, quarantine,
  retry, virtual-release failure, overflow, invalid pointers and slot exhaustion.
- A passing host run does not verify this libnx platform layer. ARM64 compilation
  is a separate CI gate; execution, repeated-run cleanup and timings require the
  target console. No physical-page mapping privileges are assumed.
- No JS/WASM object bridge, module Workers, WebGPU, Web Audio/worklets, game
  networking, asset loading or playable world is supplied by this probe.

## Next implementation gates

1. Baseline device features passed in 0.1.1 (see evidence below).
2. Verify the new 0.2.0 memory backend on Horizon with `client_memory_growth`;
   implementation and host failure tests are complete. Repeat the new case to
   check release/reuse. Full game memory consumption remains unknown.
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

## Memory milestone — 0.2.0

The existing device report proves the old baseline, not this new mapping backend.
The new NRO selects the client-sized memory test at startup, with pre-operation
checkpoints and native Result diagnostics saved in the same report. Host CI runs
14 feature cases plus allocator failure tests. Switch now has 15 feature cases.
A passing new device case would establish reservation/commit/growth/cleanup for
this allocation shape; it would not establish Keldurn startup, performance or
rendering. The browser remains a separate reader NRO at version 0.3.2.
