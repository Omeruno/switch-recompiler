# Keldurn client evidence — 2026-11-05

This report covers the user-supplied `keldurn_bg.wasm` and `/mobile/app.js`.
It is static research, not a successful game run in the Switch browser.
The binary and site source are external inputs and are not stored in this repo.
The machine-readable interface summary is [keldurn-2026-11-05.json](keldurn-2026-11-05.json).

> Update: the loader follow-up at the end of this report resolves the normal
> launch gate as WebGPU-required, confirms the initial shared-memory allocation,
> identifies the optional compute pool and verifies the supplied WASI stubs.

## Verified module facts

| Field | Observed value |
| --- | --- |
| Binary bytes | 84,164,297 (80.265 MiB) |
| SHA-256 | `c8cf2c4b1ec64bc1cbf243b2d91de923cfccf8fb2c582eb70ef57fdeafd86407` |
| Version | WASM v1 |
| Defined functions | 166,879 |
| Imports | 1,041: 1,040 functions and one memory |
| Exports | 68 |
| Code section | 67,124,121 bytes |
| Data section | 16,158,048 bytes; four segments |
| Linear memory | imported, shared, 32-bit |
| Module minimum memory | 266 × 65,536 = 17,432,576 bytes (16.625 MiB) |
| Module maximum memory | 65,536 pages = 4 GiB |
| Tables | funcref: 218,327 initial/maximum; externref: 1,024 initial |
| Exception tags | one defined tag |

The maximum is an interface limit, not measured RAM consumption or an instruction
to allocate 4 GiB immediately. The JS loader may choose an initial allocation
above the module minimum. Actual game memory, GPU assets, compiled-code size,
worker stacks and total working set are still unknown.

The native WABT 1.0.42 validator accepted this exact binary with `--enable-all`.
It also accepted it with `--enable-all --disable-simd`. Thus this supplied
version does not require SIMD to validate. Neither command instantiates the
module or executes the game. The npm/WASM build of the validator aborted on
this large input; the successful results above came from the native tool.

The `target_features` custom section declares: atomics, mutable-globals,
nontrapping-fptoint, bulk-memory, sign-ext, exception-handling, reference-types,
multivalue, extended-const, bulk-memory-opt and call-indirect-overlong. Compiler
encoding hints in this list should not each be mistaken for a separate runtime
API. Shared memory, reference tables and the exception-tag section are direct
structural evidence, independently of feature declarations.

## Host interfaces

Import namespaces:

- `./keldurn_bg.js`: 1,020 entries, including the shared memory.
- `./snippets/keldurn-app-1826881576016968/inline0.js`: `mousefix_begin`,
  `mousefix_state`, `mousefix_finish`.
- `wasi_snapshot_preview1`: 18 functions. They include environment/clock access,
  fd read/write/seek, path operations, proc_exit and sched_yield. Each import
  needs a binding; the import list alone does not prove which paths gameplay uses.

The wasm-bindgen-style names and exports contain Rust type/crate names and Rayon
pool interfaces: `initThreadPool`, `wbg_rayon_start_worker`, `initComputePool`,
`setEngineThreads`, `engineThreadStats` and TLS/thread-destruction exports.
These establish a substantial Rust/wasm-bindgen host interface, rather than a
standalone WASI command that can be run by supplying only basic filesystem calls.

The module imports both GPU families:

- WebGPU bindings include GpuAdapter/GpuCanvasContext, requestAdapter,
  requestDevice, createRenderPipeline, createComputePipeline, shader modules,
  buffers, textures and GPU limits/error handling.
- WebGL bindings include WebGl2RenderingContext, shader/program creation,
  framebuffer operations, buffer uploads, multisampling and draw calls.

This proves both binding families exist in the build. It does **not** prove
that a WebGL2 fallback is enabled, selectable, adequate for this game, or active
in the user's browser. Selection and configuration needed the JS loader and a runtime trace. The
loader follow-up below confirms a WebGPU gate in the normal entry. Dependencies
can still include unused alternative backends.

Audio bindings include decodeAudioData, AudioBuffer/BufferSource and
AudioWorklet. Exported WASM audio processors suggest worklet integration.
DOM/canvas/navigator bindings and worker construction are present. The user's
DevTools screenshots independently show successful WebSocket connections; the
opaque constructor import names alone do not identify their full protocol.

## What mobile/app.js establishes

The 20,584-byte uploaded `app.js` is a mobile/PWA comfort and installation module.
It imports `./platform.js`, manages wake lock/fullscreen/preferences and builds
an installation dialog. It is not the generated WASM import implementation.
It cannot establish GPU selection, WASM initialization parameters, game resource
URLs or WASI behavior. Do not implement mobile onboarding as a substitute for
those required game interfaces.

## Consequences for this branch

The current reader cannot instantiate this client:

1. It has no WASM runtime or wasm-bindgen bridge, no worker/shared-memory host
   integration, and no WASM exception/reference support supplied by the host.
2. Its page body cap is 2 MiB; this module is 80.265 MiB. A future binary-resource
   path needs separate streaming, cancellation, cache and memory limits.
3. Its classic script cap is 16 KiB and it has no ES module loader. Even the
   optional uploaded mobile module exceeds that script cap.
4. It has no WebGL/WebGPU canvas, Web Audio/worklets, or game transport bridge.
5. The reader's 16 MiB QuickJS heap and millisecond callback budgets are not
   game execution/performance settings. Increasing them does not supply APIs.

Do not select a small WASM interpreter or call the game supported solely because
it runs an arithmetic sample. A candidate must first validate a matching feature
probe (shared memory, atomics, exception tags, externref/funcref, bulk memory),
then demonstrate ARM64/Horizon execution, worker synchronization and a viable
GPU bridge. Executable-memory/JIT/AOT constraints and performance need device
measurements; no compatible engine has been selected yet.

## Initial investigation gates (superseded by the loader follow-up below)

1. Obtain the actual generated binding/loader JS. `./keldurn_bg.js` is an explicit
   import namespace, but is not proof that an HTTP resource with that name is
   served unchanged. Locate it in Network/Sources or the entry module's imports.
   Also obtain the mousefix snippet/worker bootstrap if referenced by that loader.
2. Establish the selected GPU backend and whether WebGL2 is a working fallback.
3. Inspect initial memory, worker setup, WASI shims and main/worker module wiring.
4. Compare candidate engines against these requirements, build one feature probe
   for Horizon, and measure memory/execution before attempting the full module.
5. Implement the selected graphics/resource/input/audio bridges as an integrated
   engine milestone, with the reader/profile UI retained around it.

This closes the uncertainty about the binary's shape and exposes the principal
compatibility gates. It does not close the full game-client investigation or
provide a credible release date.

## Reproduce

```sh
python3 browser/tools/inspect_wasm.py /path/to/keldurn_bg.wasm --output /tmp/client.json
# Optional: list every import for binding analysis.
python3 browser/tools/inspect_wasm.py /path/to/keldurn_bg.wasm --full-imports
# Native WABT 1.0.42, without running the game:
wasm-validate --enable-all /path/to/keldurn_bg.wasm
wasm-validate --enable-all --disable-simd /path/to/keldurn_bg.wasm
```

The Python tool inspects bounded section metadata and rejects truncated fields;
it explicitly does not validate function bodies. CI tests it with small synthetic
modules, without including any proprietary game payload.

## Loader follow-up — supplied JS and saved entry page

Capture scope: the user reached character creation, not the in-world gameplay
stage. These files establish core loader/binary requirements, but are not a
complete in-world network/resource trace. World entry may load more modules,
zone assets, shaders, audio and transport traffic. Peak memory, shader coverage,
frame rate and session stability cannot be inferred from this capture. Those
measurements belong to a later representative in-world trace; they are not
needed to recognize the current WebGPU/shared-memory engine feasibility gate.

The five requested files were supplied: `keldurn.js`, `startup_cache.js`,
`wasi_stubs.js`, `compute_worker.js`, and `client_capabilities.js`.
All four uploaded copies of keldurn.js are byte-identical. The repeated Games.zip
is also byte-identical to the previous archive. The loader's namespace
`./keldurn_bg.js` is implemented inside keldurn.js's import object; a separate
HTTP file with that name is not needed to explain these imports.

The [JS inventory and binding check](keldurn-js-2026-11-05.json) records file
sizes/checksums without storing site source. The saved HTML was reviewed locally
for its boot sequence; no page/account content or credential values are copied
into the repository. Its `/api/play` response is fetched dynamically and was
not supplied or requested for this analysis.

### Interface coverage

The supplied glue defines all 1,020 names expected in the WASM's core namespace
(including memory). All 65 statically named WASM exports referenced by the glue
exist in the uploaded module. wasi_stubs.js exports all 18 required WASI names.
These are name-coverage checks, not proof of argument-signature compatibility
or a successful execution. Mousefix and other auxiliary module implementations
still have to be available when the module graph is loaded.

### WebGPU is an actual launch gate

client_capabilities.js checks navigator.gpu, crossOriginIsolated,
SharedArrayBuffer, construction of shared WebAssembly.Memory and a usable GPU
adapter. The saved normal entry module invokes that check before starting its
WASM/module/resource download sequence and fails startup on a rejected result.
There is no normal WebGL fallback in this supplied launcher. Thus a WebGL2-only
port does not satisfy the current normal boot path, despite the binary's GL
bindings. A genuine WebGPU implementation/translation layer or an independently
verified supported alternative client path is required. Faking a positive
capability check does not supply the renderer.

This supersedes the earlier uncertainty about whether the normal launcher
requires WebGPU. Backend choice inside the Rust engine and actual device
feature/limit requirements still need runtime verification.

### Memory, pools and audio

The normal entry calls init with the compiled module and no memory override.
The glue therefore creates shared memory with initial=266 and maximum=65536,
matching 16.625 MiB initially and a 4 GiB declared maximum. No larger initial
allocation is present in this call. Subsequent growth and overall process/GPU
memory remain unmeasured.

startupPolicy supports the site's own safe configuration: `startup=safe` selects
zero compute helpers, preload concurrency four and zero additional heap-headroom
hint. Normal desktop policy may request one to four helpers based on hardware
and policy overrides. startComputeForPolicy skips zero helpers and catches pool
startup failure; the entry starts the pool without awaiting it. This supports
treating the optional compute pool separately from the mandatory shared-memory
interface. It does not establish that all worker/worklet functionality can be
removed or that safe mode will meet performance needs on Switch.

When enabled, compute helpers are module Workers created from a Blob URL of the
worker source. The compiled WebAssembly.Module and shared memory are posted to
each helper; the helper imports the main JS and instantiates against that same
memory. No per-worker independent copy of the whole linear memory is specified.
Pool startup is synchronized with audio initialization because Atomics.wait is
not usable inside the AudioWorklet global scope. AudioWorklet processor
initialization and shared-memory lifetime are therefore engine integration work,
not cosmetic audio controls.

### WASI is already deliberately stubbed

The supplied WASI adapter returns EBADF/ENOSYS for file/path and clock calls,
returns empty environment data, permits sched_yield and throws on proc_exit.
The browser should reproduce this observed contract rather than planning a full
POSIX filesystem solely because those symbols appear in the binary. The game
loads its resources through web APIs; whether any exceptional WASI path matters
still needs execution testing.

### Real boot/data flow

The entry checks capabilities, starts a same-origin authenticated `/api/play`
request, compiles WASM, imports modules and preloads a startup manifest in
parallel. Credentials from the API are passed transiently to the engine and then
the temporary global is deleted. No real credentials are needed in the report,
repository or diagnostics. Never ask for the API response as an ordinary fixture.

startup_cache.js uses compileStreaming with a buffered compile fallback, a
bounded-concurrency manifest/data fetcher, retries/timeouts and an in-memory
asset catalog/cache. The ordinary scalar WASM build is the default; SIMD is an
explicit optional loader experiment, consistent with the earlier validator
result. Optional persistent caching is installed by asset_cache.js, which was
not in this upload, so its format and storage requirements are not established.

### Remaining source graph and next work

Six direct core dependencies have not been supplied:

- gpulimits_pipeline.js
- growth_realm.js
- quest_events.js
- snippets/bevy_render-0dc7d31cc6170747/inline0.js
- snippets/keldurn-app-1826881576016968/inline0.js
- snippets/keldurn-app-1826881576016968/inline1.js

The startup helpers additionally import platform.js, mobile_memory.js and
gpu_recovery.js. The entry references further resource/UI helpers. This is an
inventory of the remaining graph, not a claim every helper is essential to the
first graphics probe. No account/session dumps are needed to resolve it.

There is enough evidence now to evaluate engines against **shared-memory WASM,
exception/reference support and a real WebGPU path on Horizon**. The next gate
is an ARM64/Horizon feasibility prototype and a documented engine/backend
choice, followed by full resource/module/input/audio integration. Request further
site modules when a specific unresolved dependency blocks that work. Reader DOM
improvements alone cannot cross this engine gate.
