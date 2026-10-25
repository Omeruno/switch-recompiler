# G4b.2a: bounded shared-context handoff

Follow-on: [G4b.2b binds native targets to actual HWND/DC identities](G4B2B_WINDOW_BINDINGS.md).
Remaining production gates are CPU/GPU coherence and owning texture frames.

Incoming `f4a5ecfe330c2a5ff9ea08065cd8403bad19045a` passed all six core CI jobs
in [run 40200041032](https://github.com/Omeruno/switch-recompiler/actions/runs/40200041032).
This closes the GL-state slice of G4b.2. **HWND/DC identity, raw-DIB/CPU-ROP
coherence and the production texture bridge remain open.** Paint still draws
ordinary GDI on CPU. The production renderer is unchanged; the GPU-GDI side now
preserves its caller's state instead of requiring an exclusively dedicated context.

## Handoff contract

The caller supplies a loaded, current desktop GL 3.3 core context on its owner
native thread. Context ownership, device loading, buffer size and swap remain
external. No context/thread switch, active transform feedback, conditional render
or query scope may straddle a handoff. The caller must not bind the modules'
private object names or retain their CPU views after the documented lifetime.
Extension state outside GL 3.3 is outside this contract; implementation-specific
clip distances beyond the guaranteed eight must be disabled by the caller.
This is a bounded compositor handoff, not arbitrary middleware GL virtualization.

`detail::GdiGlScope` snapshots before GPU work and restores on every normal/error
exit, including initialization failure, capacity/read barriers, manager-wide
fallback, target destruction and shutdown. Saved state:

- current program/VAO, independent read/draw FBOs, viewport/scissor rectangle;
- polygon mode, color mask/clear color and affected raster enable bits;
- texture unit 0's 2D binding/sampler, previous active unit (other units untouched);
- pack/unpack buffer bindings and alignment/row length/skips/byte/bit-order stores;
- blend/depth/stencil/cull/scissor/dither/sRGB, raster discard, color logic,
  sample coverage/mask/alpha enables and the eight core clip-distance enables.

The scope establishes fill rasterization, disables inherited effects and unbinds
sampler/PBO state for owned CPU uploads/readbacks. Creation selects texture unit 0
**before** its first bind. In particular, a caller's nonzero unpack PBO cannot
reinterpret an owned CPU pointer as a GPU-buffer offset, and a caller's sampler
cannot make the module's textures incomplete. Other sampler/texture units retain
their original objects and parameters; caller images are never modified.

Nested native calls share one thread-local scope depth and one outer snapshot.
Present can flush several targets and draw without querying/restoring between
commands; CpuRead and capacity checkpoints retain the same nesting contract.
Ordinary queued Fill/Copy does not query GL unless it reaches a real checkpoint.
No allocation, glFinish, synchronous timer read or CPU image readback is added
by the state guard. State queries/restores still cost CPU/driver work: this is
correctness infrastructure, not an acceleration or zero-overhead claim.

As before, preexisting GL errors are consumed by existing fail-closed GPU checks;
the GL error queue is **not** virtualized/restored. Invalid frames reject without
output changes. A failed GPU frame must not be swapped. Target replay preserves
logical CPU pixels and manager-wide fallback remains explicit.

After actual context destruction, notify ContextLost before any GPU operation.
CPU-only continuation, reads and cleanup then skip the GL scope entirely and make
no driver calls. Device loss is still notified by the owner, not autodetected.
Default-framebuffer draw-buffer selection is owned by the platform; GDI changes
its contents on successful Present, but never swaps or claims to preserve them.

## Reproducible evidence

```sh
COMPOSITION_GL_TEST=1 sh tools/run_host_tests.sh
```

`tests/test_gpu_gdi_shared_context.cpp` creates real EGL/GL objects, distinct read
and draw FBOs, PBOs, sampler/texture units and a caller shader/VAO. It deliberately
sets incompatible raster/pixel-transfer state before initialization, copies,
instanced fills, flush/read/capacity barriers, target creation/copy/destruction,
invalid frames, injected GL errors, fallback and teardown. Independent GL queries
compare caller state; independent pixel formulas compare the resulting images.

Twelve rounds interleave actual GPU-GDI presentation and the unchanged production
OpenGlPresentation in one context. Both outputs must match complete expected
images; GDI presentation must add no target readbacks. This is genuine renderer
interoperation, not live Win32/DC integration or a physical FPS measurement.
Existing G3/G4a tests additionally retain actual context-destruction recovery and
allocator accounting. The new test fails against incoming unguarded GDI sources
at initialization under hostile PBO state; no EXE-specific behavior is involved.

The host suite records `build-host/gpu_gdi_shared_context.log`; CI publishes it
with this contract as `stage-gpu-gdi-shared-context`. The existing gpu-reference
job separately compiles/runs the oracle at O2 with strict warnings, no exceptions
and no RTTI. Core CI remains six jobs. G4b.1's reference/batch measurements still
run, including the new state-handoff costs, with unchanged exact pixel/work gates
and no timing pass threshold. Host software Mesa timings are not Switch timings.

## Remaining G4b.2 work

Next bind bounded GPU resource identity/lifetime to actual HWND/DC operations,
then implement real CPU/DIB/ROP barriers and a texture-aware production presenter
which avoids unconditional ShellSession canonical memcpy/duplicate rasterization.
Public actual-x86 window/session/pixel fixtures must precede selective activation.
Existing Luna, CPU SRCCOPY and software fallback remain intact. Local Switch SDK
is absent; CI provides the NRO build. Hardware E/F/Paint/input/relaunch stays deferred.
