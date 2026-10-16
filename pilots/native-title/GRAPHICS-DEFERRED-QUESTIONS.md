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

## Current disposition - 2026-10-14

The [bounded closing checklist](../../docs/BLINX-QUALIFICATION-CLOSURE.md)
supersedes the original deferral status where a reopening trigger has fired:

- **GFX-D06 is required work**, not a current deferral. Original resource
  lifetime G01 is complete; startup/instance integration and connected title
  execution remain G02/G03/G05.
- **GFX-D04 is split.** Supporting the reached enabled-noise primitive is
  required G04 work. Further sampling/rounding precision can remain deferred
  after that behavior exists; disabling noise does not satisfy G04.
- The other twelve graphics entries retain their deferred scopes and reopening
  triggers. Preserve the original entries and evidence below.

Together with the twenty-one entries in [the LLE queue](LLE-DEFERRED-QUESTIONS.md),
there are **35 retained question IDs: 34 with deferred scope, and one fully
promoted to required work**. GFX-D04 also has the required portion above.
LLE-D06/AUDIO-D13/AUDIO-D14 record the owner's bounded-goal clarification on
2026-10-15; AUDIO-D15 makes nominal interpolation precision explicit after the
connected output check. Earlier entries and their evidence remain unchanged. Earlier reports
calling all 27 then-recorded questions "precision deferrals" were imprecise;
they are historical counts, not 27 currently waived closing requirements.
The current family count is **6/9**; this reconciliation qualifies no new work.

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

These were the initial foundation batch, with their bounded resolutions recorded
below. They were not hidden among safely deferred precision questions.

GFX-F01 resolution, 2026-10-10: image allocations now own a shared completion
record retained by presentation surfaces and texture aliases. Sampling updates
that record; alias or original-wrapper retirement cannot erase a surviving
allocation's use. Reset gives replacement images new records while aliases
retain the superseded image's record. Queries select the record directly rather
than scanning texture slots. The authored regression first failed on the old
source-surface lookup, then passed over three device lifetimes. The existing
backend, float-blend, depth/cache, guest ABI and scheduler batch also passes
(11 CTest entries total). No per-pixel work or routine framebuffer transfer was
added. This resolves completion ownership within the maintained adapter;
writable/depth alias content generations remain GFX-D07.

GFX-F02 resolution, 2026-10-10: generic surface destinations now use an explicit
local class-0x3d/target-0 contract with checked descriptor, offset, inclusive
limit and physical-aperture ranges. Actual zeta binding is independent of
layout validity. Enabled comparison/stencil and invalid bound write storage
reject before memory or renderer effects. Truly unbound write-only zeta can
ignore stale layout; optional absent storage and protected-zero boot writes
retain separately named compatibility dispositions. The existing boot canaries
pass, and the texture-source range body is preserved separately, unchanged.
Six representative CPU suites pass: 49 authored helper cases, 38 actual
direct/paged generic draws, existing NV2A/boot cases and 1,844 vertex-color draws,
plus tile, shader and scratch tests. The ALWAYS/mask0 renderer fixture now
checks that storage is unnecessary, contrasted with a LESS comparison that
must resolve the resident depth input. No pixel math or native HLE rendering
cost is added. These bounded contracts resolve the immediate finding; complete
silicon/source-role semantics remain deferred in GFX-D11 through GFX-D14.

## Safely deferred questions

These are the original recorded questions, not claims of qualification. Apply
the current dispositions above: GFX-D06 has reopened, and GFX-D04 has both
required and deferred portions.

| ID | Question | Evidence retained / why it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D01 | What additional clipping, culling, coverage and depth cases need qualification? | The generic consumed-color-before-clipping fix stays installed. Authored clipping/reference tests and original inputs/replay tooling are retained. Preserved original state/pixels pass through draw 2162 in their pinned qualification generation. Clipping/raster changes remain local to the reference and renderer implementation. | A visible regression or representative family failure implicates these equations, a new supported input domain requires them, or full qualification resumes. |
| GFX-D02 | How do remaining ARGB/stencil/alpha/combiner and texture-format profiles compose? | The remaining-input census covers 2,192 draws in 79 state groups /191 state-and-vertex batches. The ARGB/stencil scan covers 2163-2458; 2261 is the first surviving clip geometry, not a proved write. The 2163 empty-profile proposal passes 105 controls but has no original replay qualification. Existing component-format tests remain usable. | A new supported behavior consumes an unqualified state/format, a regression identifies that family, or full qualification resumes. |
| GFX-D03 | Does the independently expected producer chain qualify the whole frame and native composition? | Original prefix 2162, independent producer snapshots and five qualified postprocess consumers are preserved. Full-frame producer error propagation/native composition remains open. HLE appearance already passes the owner's corrected-run review. | A producer/consumer dependency changes, an appearance or state/lifetime regression occurs, or whole-frame numerical qualification is resumed. |
| GFX-D04 | What is the enabled DXT1-noise behavior and acceptable native error contract? | Noise-off scene evidence is explicit; enabled noise remains unqualified. This is a bounded sampling/precision extension, subject to the existing renderer/state contracts. | Noise becomes observable in a failing scene or required supported profile, or the exact original behavior is being qualified. |
| GFX-D05 | Which exact arithmetic/decoder/interpolation results require silicon or another host? | Source/DLL/CRT identities and host premises accompany existing proofs. Shader rounding, near-integer ARL, DXT decoder precision and exact interpolation/timing questions remain explicit. Generic acceleration diagnostics can select a different route and compare only color; that gap is distinct from Blinx's retained production HLE observer. | Porting/changing the reference arithmetic or accelerated backend, extending its advertised correctness scope, a relevant regression, or final hardware qualification. |
| GFX-D06 | When can full original graphics startup, lifetime and ring rollover be qualified? | The build-time original renderer route and captured original-code fixtures remain maintained. Current title-startup/hardware dependencies are guarded and recorded in `GRAPHICS-LLE.md` and `GPU-COMPLETION-LLE.md`. The current prefix does not establish whole-title original execution. | A safe bounded observation resolves the existing startup question, or full original-title integration is the active deliverable. |
| GFX-D07 | How should writable/depth aliases share content generations? | Current guest aliases explicitly reject these operations in `graphics_texture.inc`; the depth conversion cache has a content-generation contract. This is an extension prerequisite, not an observed current cache failure. | Before admitting writable/depth alias operations, make the generation allocation-owned and check alias mutation/reset/retirement. |
| GFX-D08 | How should automatic float blending handle overlapping geometry or broader blend profiles? | Current semantic admission proves nonoverlapping geometry and keeps its guard. A single pre-draw snapshot cannot represent ordered blending among overlapping fragments. Existing admitted HLE behavior stays unchanged. | Before broadening that admission, choose and test a GPU strategy that preserves the required order. |
| GFX-D09 | Can texture comparisons and constant uploads use dirty generations? | Current CPU comparisons and unconditional constant uploads are localized costs. GPU conversion/blend caches and their performance boundaries remain intact. No performance regression is established by this audit. | Profiling identifies material cost and guest-write tracking can supply trustworthy dirty generations. |
| GFX-D10 | How should the private reference donor be replaced for the intended product build? | The guest-independent adapter keeps native handles out of guest memory. `native_phobos_d3d.py` explicitly excludes its private GPL reference from the intended non-GPL product; this remains existing migration debt. | When selecting the product backend, preserve the adapter's semantic, ownership and completion contracts rather than copying donor internals into callers. |
| GFX-D11 | What are the complete DMA target, page, access and coherency semantics for surface destinations? | Pinned xemu and nxdk sources establish the local class-0x3d construction but do not qualify all mapping/flag combinations. GFX-F02 implements and tests a bounded local flat surface contract, with distinct unsupported mappings and retained raw descriptor fields. Non-surface DMA roles stay separate. | Before extending destination admission beyond the supported mapping, or when a representative supported title requires another encoding; qualify its mapper without changing unrelated DMA roles. |
| GFX-D12 | When is an intentional physical-address-zero color/zeta destination safe and faithful? | Class-0x3d/base-zero descriptors are legitimate constructions; they do not establish destination intent. The reproduced boot regression and its low-RAM canaries remain preserved. Physical zero stays explicitly protected; nonzero offsets from base zero remain supported when their range is valid. | A bounded implementation contract and independent evidence distinguish intentional use from the reproduced boot hazard, or final hardware qualification needs that address. Do not infer a hardware valid bit from software initialization history. |
| GFX-D13 | What is the exact hardware behavior of depth writes without a usable binding when depth/stencil tests are disabled? | The existing software floor permits optional absent zeta and protects the boot's zero-destination write-only case. GFX-F02 separates those compatibility cases from malformed bound destinations and required comparison/stencil storage. This preserves existing boot behavior without claiming hardware equivalence. | Before advertising those compatibility dispositions as hardware-correct, or if a supported title depends on their exact effects. Enabled comparison/stencil correctness remains immediate foundation work. |
| GFX-D14 | Which texture-source and other DMA roles need their own descriptor and physical-zero admission contracts? | The generic texture-source range helper also retains a nonzero-address guard, while `dma_object_address` serves additional roles without complete class/target semantics. GFX-F02 separates destination validation and preserves these existing read/source behaviors. Their callers and original captured inputs remain available; no supported-source regression is established here. | A new supported source encoding or regression requires different admission, or complete DMA-role qualification resumes. Give each role its own read/write contract rather than spreading destination safety assumptions into unrelated callers. |

## Preserved checkpoint and review

The preserved continuous original checkpoint is **2162/4331 draws (49.92% of that
captured frame)**, with 361,948 independently expected writing fragments and
2,822,707,200 owned-pixel comparisons. It retains 117 actual-artifact fault
controls. The last 20 draws passed in one batch with 72 source-only batch
controls and zero new writes. This percentage is not an overall graphics
completion estimate. The service-family burndown was **4/9 at this 2026-10-10
checkpoint**; use the current closing checklist for present status.

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

Subsequent GFX-F02 core source changes do not inherit the pinned original
prefix's numerical qualification. Its preserved DLL and snapshots stay intact;
the new source has the representative CPU contract qualification above.
