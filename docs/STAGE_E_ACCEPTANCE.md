# E9 combined unattended software gate

CI artifact: `stage-e-acceptance-exe`. Launch `stage_e_acceptance.exe` with the
ordinary exact-SHA NRO, using the new UI. No button presses or external DLLs are
required. The fixture is redistributable and contains no Paint/Microsoft binary.
It takes several seconds; intermediate windows and the Stage C PASS banner are
normal. A component banner is not the overall result.

The same existing C test bodies are compiled into separate translation units,
renaming only their entry points. All groups run sequentially in **one Process**:

1. Stage C's eight runtime/HANDLE/wait/APC/input suites.
2. Common file dialog API/error validation and GetFileTitle A/W.
3. Standard dialog subclassing and callback replacement, including nested/modal
   and modeless dispatch. The original dialog resource is linked unchanged.
4. ChooseColor A/W, RGB/custom colors, hooks/veto/nesting, cancellation, destroyed
   dialog/WM_QUIT and owner restoration.
5. Desktop/work-area geometry and four CPU GDI corner pixels.
6. All 28 window composition, overlap/exposure, child and screen-ROP/DIB scenes.

`stage-e-acceptance.txt` is overwritten at start with RUNNING and the fixture's
build identity. Each completed group gets a numbered PASS; the only complete
software success marker is **PASS: ALL 6 SOFTWARE GROUPS**. A nonzero component
return writes FAIL, stops the remaining groups and propagates its code. An abort
or crash leaves an incomplete report and must never be counted as a pass.
Component reports (`stage-c-acceptance.txt`, `stage-c-details.txt`,
`stage-e-presentation.txt`, `stage-e-composition.txt`) retain diagnostic detail.

CI embeds GITHUB_SHA into Fixture build. Local builds use git HEAD, which does
not certify uncommitted changes. Match fixture identity with the NRO's renderer
`Build` before recording physical evidence; preserve both reports immediately
since the next run overwrites them. An unknown identity is not exact-SHA evidence.

This aggregate checks CPU-visible Win32/GDI behavior. Hardware GL texture output
is still independently covered by host GL reference tests and requires actual
Switch checks; CPU GetPixel cannot prove scanout correctness. The report always
lists GPU output/fallback, Paint dialogs/Open/Save, performance and physical input
latency as PENDING. The existing individual fixtures/GL inspectors remain in CI;
this aggregate adds cross-group cleanup/continued execution coverage rather than
replacing those pixel oracles. Paint's real file selection and I/O remain a
separate smoke, not implied by API validation alone.

Physical checklist and measurement limitations: [STAGE_E_PERFORMANCE.md](STAGE_E_PERFORMANCE.md).
Hardware E9 and the final Stage E percentage remain open until that evidence exists.
