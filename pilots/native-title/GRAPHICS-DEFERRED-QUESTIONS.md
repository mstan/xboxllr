# Graphics foundation and deferred questions

Owner direction, 2026-10-10. Blinx's corrected HLE attract scenes pass the
owner's appearance review. Prioritize a functioning, extensible LLE foundation
and a performant HLE with compatible caller behavior. Further exhaustive draw
and pixel qualification can follow without gating every development step.
Full graphics qualification remains open; this direction changes the order of
work, not the meaning of a qualified result. Tracked in `beads-xbox.1.116`,
with producer evidence in `beads-xbox.1.117`.

Append new safely deferred questions here using stable IDs. Preserve each
question's evidence, reason for deferral and reopening trigger. Add subsequent
findings or resolution beneath the original entry; do not erase the question
or discard its reproducible inputs. Representative behavior-family checks,
existing independent tests and one regression for a material runtime change
are the default validation unit. Repeat human appearance review when a change
affects visible behavior. Reuse the accepted baseline for qualification-only
or documentation changes.

## Foundation work before broader correctness claims

The initial read-only audit found reusable boundaries: owned draw state and
vertices (`src/machine/nv2a.hpp`), explicit ordering fences, owned renderer
packets (`src/machine/nv2a_renderer.hpp`), separate HLE inputs
(`src/machine/nv2a_hle_draw.hpp`), and build-time renderer selection
(`CMakeLists.txt` and `gpu_lle.cmake` in this directory). Capture hashes and
per-draw admission limits belong to offline qualification tools. No architecture
overhaul is indicated by this bounded audit. Whole-title original startup and
full graphics correctness are not established by that finding.

| ID | Question / finding | Next focused work |
| --- | --- | --- |
| GFX-F01 | Does completion ownership survive sampling and retirement of aliases? `graphics_backend.c` currently aggregates shared-image usage for texture queries, but surface queries use only their own serial. `graphics_completion_backend.inc` records sampled aliases; destroying the alias clears its serial. | Preserve the latest use on the surviving allocation owner. Cover source-surface lookup before and after alias retirement, with unrelated allocation work excluded. Verify selected serials deterministically; GPU timing is not the oracle. Keep this off the per-pixel path. |
| GFX-F02 | Does generic surface validity follow DMA binding/descriptor/range semantics? `src/machine/nv2a.cpp` rejects physical address zero using witnessed-profile history and can silently disable non-stencil depth when resolution fails. The existing guard prevents a reproduced boot-memory corruption. | Retain that protection while replacing the capture-derived assumption with an explicit bounded contract. Distinguish absent depth from an invalid enabled depth target. Test authored valid/invalid descriptors and observable depth behavior; do not simply remove the zero-address guard. |

These are the next foundation batch. They are not treated as qualified or
hidden among safely deferred precision questions.

## Safely deferred questions

All entries below are **open and deferred**, not qualified.

| ID | Question | Evidence retained / why it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D01 | What additional clipping, culling, coverage and depth cases need qualification? | The generic consumed-color-before-clipping fix stays installed. Authored clipping/reference tests and original inputs/replay tooling are retained. Current-generation original state/pixels pass through draw 2162. Clipping/raster changes remain local to the reference and renderer implementation. | A visible regression or representative family failure implicates these equations, a new supported input domain requires them, or full qualification resumes. |
| GFX-D02 | How do remaining ARGB/stencil/alpha/combiner and texture-format profiles compose? | The remaining-input census covers 2,192 draws in 79 state groups /191 state-and-vertex batches. The ARGB/stencil scan covers 2163-2458; 2261 is the first surviving clip geometry, not a proved write. The 2163 empty-profile proposal passes 105 controls but has no original replay qualification. Existing component-format tests remain usable. | A new supported behavior consumes an unqualified state/format, a regression identifies that family, or full qualification resumes. |
| GFX-D03 | Does the independently expected producer chain qualify the whole frame and native composition? | Original prefix 2162, independent producer snapshots and five qualified postprocess consumers are preserved. Full-frame producer error propagation/native composition remains open. HLE appearance already passes the owner's corrected-run review. | A producer/consumer dependency changes, an appearance or state/lifetime regression occurs, or whole-frame numerical qualification is resumed. |
| GFX-D04 | What is the enabled DXT1-noise behavior and acceptable native error contract? | Noise-off scene evidence is explicit; enabled noise remains unqualified. This is a bounded sampling/precision extension, subject to the existing renderer/state contracts. | Noise becomes observable in a failing scene or required supported profile, or the exact original behavior is being qualified. |
| GFX-D05 | Which exact arithmetic/decoder/interpolation results require silicon or another host? | Source/DLL/CRT identities and host premises accompany existing proofs. Shader rounding, near-integer ARL, DXT decoder precision and exact interpolation/timing questions remain explicit. Generic acceleration diagnostics can select a different route and compare only color; that gap is distinct from Blinx's retained production HLE observer. | Porting/changing the reference arithmetic or accelerated backend, extending its advertised correctness scope, a relevant regression, or final hardware qualification. |
| GFX-D06 | When can full original graphics startup, lifetime and ring rollover be qualified? | The build-time original renderer route and captured original-code fixtures remain maintained. Current title-startup/hardware dependencies are guarded and recorded in `GRAPHICS-LLE.md` and `GPU-COMPLETION-LLE.md`. The current prefix does not establish whole-title original execution. | A safe bounded observation resolves the existing startup question, or full original-title integration is the active deliverable. |
| GFX-D07 | How should writable/depth aliases share content generations? | Current guest aliases explicitly reject these operations in `graphics_texture.inc`; the depth conversion cache has a content-generation contract. This is an extension prerequisite, not an observed current cache failure. | Before admitting writable/depth alias operations, make the generation allocation-owned and check alias mutation/reset/retirement. |
| GFX-D08 | How should automatic float blending handle overlapping geometry or broader blend profiles? | Current semantic admission proves nonoverlapping geometry and keeps its guard. A single pre-draw snapshot cannot represent ordered blending among overlapping fragments. Existing admitted HLE behavior stays unchanged. | Before broadening that admission, choose and test a GPU strategy that preserves the required order. |
| GFX-D09 | Can texture comparisons and constant uploads use dirty generations? | Current CPU comparisons and unconditional constant uploads are localized costs. GPU conversion/blend caches and their performance boundaries remain intact. No performance regression is established by this audit. | Profiling identifies material cost and guest-write tracking can supply trustworthy dirty generations. |
| GFX-D10 | How should the private reference donor be replaced for the intended product build? | The guest-independent adapter keeps native handles out of guest memory. `native_phobos_d3d.py` explicitly excludes its private GPL reference from the intended non-GPL product; this remains existing migration debt. | When selecting the product backend, preserve the adapter's semantic, ownership and completion contracts rather than copying donor internals into callers. |

## Preserved checkpoint and review

The latest continuous original checkpoint is **2162/4331 draws (49.92% of that
captured frame)**, with 361,948 independently expected writing fragments and
2,822,707,200 owned-pixel comparisons. It retains 117 actual-artifact fault
controls. The last 20 draws passed in one batch with 72 source-only batch
controls and zero new writes. This percentage is not an overall graphics
completion estimate. The service-family burndown remains **4/9**.

Private artifacts under `build/blinx/frame-inputs-firstworld-20261010` include
`producer-prefix2162-final-qualified.aot-private.json`, the original
`persistent-xy-corrected-producer-2162-checkpoint.aot-private.json`, frozen
expected/actual snapshots, source-first batch bindings, authored controls,
remaining-profile census, and the stopped ARGB/stencil scan. Capture inputs and
their independent expectations stay separate; observed pixels do not become
replacement goldens.

The owner reviewed
`build/blinx/default-blend-attract-regression-20261010/scene-review.png` and
replied **"Looks right at a glance"**. The immutable private review records
the image, run reports and all 33 presents. No new title/audio run was needed.
This establishes the requested still-image appearance baseline, not motion,
audio synchronization or complete numerical LLE qualification.
