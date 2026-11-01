# Graphics foundation and deferred questions

Owner direction, 2026-10-10. Blinx's corrected HLE attract scenes pass the
owner's appearance review. Prioritize a functioning, extensible LLE foundation
and a performant HLE with compatible caller behavior. Further exhaustive draw
and pixel qualification can follow without gating every development step.
The bounded graphics family is qualified as of2026-10-17; exhaustive graphics
coverage remains separate. Tracked in `beads-xbox.1.116`,
with producer evidence in `beads-xbox.1.117`.

Append new safely deferred questions here using stable IDs. Preserve each
question's evidence, reason for deferral and reopening trigger. Add subsequent
findings or resolution beneath the original entry; do not erase the question
or discard its reproducible inputs. Representative behavior-family checks,
existing independent tests and one regression for a material runtime change
are the default validation unit. Repeat human appearance review when a change
affects visible behavior. Reuse the accepted baseline for qualification-only
or documentation changes.

## Current disposition - 2026-10-17

The [bounded closing checklist](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/BLINX-QUALIFICATION-CLOSURE.md)
supersedes the original deferral status where a reopening trigger has fired:

- **GFX-D06's promoted required work is complete** for the bounded profile.
  Original resource lifetime G01, startup/state/noise G02-G04 and connected
  moving output, genuine exit, content retirement and neutral reentry G05 pass.
- **GFX-D04 retains precision scope.** Required enabled-noise G04 passed on
  2026-10-16 with maintained reconstruction, independent physical observations
  and a bounded native GPU comparison. Further sampling/rounding precision
  remains deferred; acceptance did not disable or omit noise.
- The other twenty-three graphics entries retain their deferred scopes and reopening
  triggers. Preserve the original entries and evidence below.

Together with the twenty-three entries in [the LLE queue](LLE-DEFERRED-QUESTIONS.md),
there are **48 retained question IDs:47 with deferred scope, and one resolved
promoted-work scope**. GFX-D04's required portion is complete. LLE-D07 retains
the historical host exception's unidentified cause; current error containment
and actual G05 execution are accepted separately, without a causal-fix claim.
LLE-D06/AUDIO-D13/AUDIO-D14 record the owner's bounded-goal clarification on
2026-10-15; AUDIO-D15 makes nominal interpolation precision explicit after the
connected output check. AUDIO-D16 records exact stopped-voice cursor reset timing;
GFX-D18/D19 retain PRAMDAC interpretation and analog WSS questions. Earlier
entries and their evidence remain unchanged. Earlier reports
calling all 27 then-recorded questions "precision deferrals" were imprecise;
they are historical counts, not 27 currently waived closing requirements.
The current bounded family count is **9/9**, based on actual accepted evidence;
deferring a question does not qualify a family.

GFX-D15, added 2026-10-15, isolates AV request service-edge precision from the
still-required FP_DEBUG, active timing and original initialization behavior.
GFX-D16 retains isolated DEBUG-bit interpretation and exact field-publication
questions without adding a separate research gate to that functional path.
GFX-D17 retains unidentified encoder command effects separately from documented
serial acceptance and clock/reset/sync contracts; connected output remains required.

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
| GFX-D19 | What is the exact analog WSS waveform and downstream television aspect-ratio response? | Conexant100381B1.3.35/table2-5 establishes60bits6/7 field enables, data60/62/64 and increment66..6A. Original AV3's final helper writes these controls. Register/readback and independent digital clock/FIELD continuity are implemented; analog VBI insertion and a television's interpretation are outside the current digital attract presentation. This does not waive required visible output, geometry or timing. | Adding analog/VBI output or a WSS-decoding consumer, or a supported visible aspect-ratio regression attributable to WSS. Preserve the commanded payload and enables rather than treating them as inert. |
| GFX-D18 | What are the individual bit meanings, alternate profiles and readbacks of original PRAMDAC setup630,680..690,84C,898,89C,8C0/8C4? | The complete26-word AV3 table is source-bound in display-clock-20261016/vga-batch.aot-private.json. Its11 additional controls retain exact command receipt separately from hardware latches/effective output. Only the reached profile and original color-selector zero writes are admitted. No inspected source establishes an additional dependency of documented CLKO/FP counters on these words. Receipt creates no clock, ACK, active-bank copy or pixels. Connected output processing remains required in G05 and guarded; this defers exhaustive interpretation, not required supported output. | Before admitting changed profiles or register readback, or when independent evidence identifies a required clock, scaling or pixel consequence. Do not call the words inert or analog-only; implement required effects before accepting G05. |
| GFX-D17 | What functions and readbacks belong to the reached Conexant DA..FC commands? | The exact18 writes are retained in the source-bound85-entry table and a separate command receipt. Manufacturer100381B3.8.1 establishes serial ACK; its public register map endsD8. No inspected primary source attributes these bytes to Macrovision or demonstrates an effect on the documented1.3.6 clock equation or digital FIELD. Interpretations can wait while the independently specified clock/reset/sync contracts operate. No opaque hardware latch, ready event or analog-only/inertness claim is supplied. | A supported extension read, changed command/output profile, or independent clock/scaling/pixel evidence distinguishes required behavior. Required connected clock, reset/sync and representative output checks remain G02/G05; any demonstrated consequence becomes implementation work. The receipt survives timing reset/autoconfiguration and ends only at documented full serial reset. |
| GFX-D16 | What are the isolated meanings of DEBUG bits17/29 and exact FP field-publication edges beyond the required supported startup behavior? | Both original whole DEBUG words have verified physical readback. The differential HSYNC sequence and matched control publish the sampled marker before a new AV request; both restore their state. The control runs in an already exercised mode and cannot exclude persistent effects. Original AV3 neither reads the alternate bank nor polls a post-request acknowledgment; its three retrace pairs precede clearing bit17. Preserve raw words and distinct bank observations. Individual bit names, exhaustive combinations and exact publication-edge research can wait; no inertness or whole-bank-copy behavior is inferred. | A supported caller/output depends on an isolated bit or bank observation, a changed startup profile consumes it, or an observed functional failure implicates it. Real scaling, configured clocks, retrace, encoder startup and connected output remain required G02/G05; this entry waives none of them. |
| GFX-D15 | What exact clock edge/latency services FD6806A0, and which raw quiescent phase occurs on physical reset? | Physical observations establish both idle phases, both pending directions and eventual acknowledgment; the first direction completes within a 7.111us instrumented upper bracket. Original AV3 observes phase equality and then requests its complement, so exchanging both phase bits preserves its work. The explicit initialized-kernel profile selects a fresh quiescent owner with canonical phase0; actual NV2A source-clock advancement services its pending request at a declared nominal next edge. Reads, PTIMER configuration and frame callbacks do not supply completion. The isolated protocol neither copies an FP bank nor activates display timing. This is not a physical reset default or literal cold-boot claim. | A supported original consumer distinguishes the nominal service ordering or raw phase, another boot profile is admitted, or an independent edge measurement changes the contract. Replay both phase directions, frozen-clock/read controls and actual original AV calls. Required FP_DEBUG behavior, active timing/retrace, encoder programming and connected title rendering remain open; none is waived here. |
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

## Additional inactive patch-state boundary - 2026-10-15

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D20 | What nonzero FD patch/curve/transition state, reset contents and processing are required beyond the reached inactive clear? | Pinned envytools f102b823 hwtest/pgraph_state.cc maps RDI3D to fifteen FD words. Actual original13FC4C writes fifteen zeros while idle before context loading. The maintained model supports these explicit clears and their readback, with initialization knowledge invalidated by full/subengine reset. Nonzero programming, unknown reads and NV0971DE0..1E1C processing remain guarded. No reached attract consumer currently requires active patch processing. | An actual supported command or context image requires nonzero FD state, patch/curve/transition output, or reading reset contents before explicit initialization. Saved-context load/save and ordinary draw output remain required G03/G05, not waived by this deferral. |

## Unconsumed inherited fog constant - 2026-10-16

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D21 | What exact inherited bits and arithmetic edge behavior belong to vertex constant c137 in the captured first draw batch? | The actual pending queue uploads the complete first program. Its sole c137 use is final MAD o5.x = v0.z*c137.x+c137.y; no relative reads, later instruction or paired output consumes that result. All eight captured draw states disable fog, and none of their active alpha/color/final combiner inputs selects FOG. Other read constants are explicitly uploaded; the final draw uses uploaded c0/c1. Independent review agrees that this output has no demonstrated consumer in this prefix. Retain transform-inputs-review.aot-private.json and draw-0/7-program-corrected.aot-private.txt under gpu-lle-first-transfer-20261016. | A supported draw consumes fog/c137, changes the program or combiner dependency, or an observable context save/reload requires these bits. This defers only this unconsumed value's exact semantics. It does not initialize c137, remove unknown-state guards, supply a context map, waive load/save, or qualify the still-pending draws. |

## Per-invocation shader initialization mechanism - 2026-10-16

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D22 | How does physical NV2A establish the initial temporary/result lanes for each vertex invocation? | NVIDIA's [NV_vertex_program specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program.txt), sections 2.14.1.4/.5, defines temporary zero and result XYZ zero/W one per invocation. This grounds the existing interpreter/compiled functional contract. The original first-seven programs write only T0.XY, while the reached PROJECT2D sampler consumes XYW. Dependency analysis keeps W visible and assigns its producer to invocation initialization; no guest constant, current attribute or serialized context byte is inferred. Applying this programming-model contract to the bounded Xbox path is an inference supported by the original program and the independent xemu implementation, not a silicon measurement. Exact TBUF clearing/slot/driver mechanism can wait; representative original LLE output and lifecycle still must pass. | A supported shader/output differs from the declared initialization contract, or raw output-buffer readback/save/reuse is introduced. Compare explicit-Q=1/2 controls with partial XY writes after varied prior Q values on actual hardware if needed. Do not use input VAB readback as proof of shader results or waive persistent-input provenance. |

## GFX-D01 addendum: early depth-culling mechanism - 2026-10-16

Exact opaque1D84 early-culling/cache behavior stays within the existing culling
question. The first seven retained draws disable depth; the eighth enables LESS
with depth writes disabled. It must execute the existing mathematical comparison
against owned zeta storage; opaque culling bits alone do not establish a different
pixel equation. Reopen this mechanism when a supported comparison/output differs,
or an observable cache/readback consumer requires it. This does not defer required
depth values, ordering or compression tags. Loaded compressed depth/stencil writes
remain guarded until a ZROP writer owns their selector/encoding; if reached by the
supported attract route, that writer is required implementation before closure.

## DAC extensions beyond the complete original RGB upload - 2026-10-16

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D23 | What are nonzero-index update behavior, DAC readback/reset values, other color widths/modes and the raster-visible timing of incomplete gamma uploads? | Both retained original upload routines start at index zero and emit all256 RGB triples. The original32-bit AV producer selects00100030. The maintained owner implements the full table and separate-channel digital lookup, with identity direct output preserved. nxdk XVideoSetGammaRamp explicitly avoids nonzero-index partial updates because of an NV2A caveat. Unreached modes/readback remain guarded, and output during an incomplete upload is rejected rather than assigned an invented bank-swap/partial-raster policy. | A supported original consumer requires a guarded operation, scanout observes an incomplete upload, or independent output differs from the complete-table lookup. Such a reached consumer promotes the needed behavior to required work. This does not defer the complete RGB upload, its actual pixel effect, connected output or normal lifecycle. |

## GFX-D16 addendum: explicit mode reentry phase - 2026-10-16

The original AV producer disconnects CRTC28.SLAVE before its full mode setup
and reconnects after programming. The existing nominal FP origin policy now
applies to each such explicit software mode boundary. Source time is retained,
detached time produces no invented display events, and real encoder reset
remains necessary to establish encoder phase. A clock interruption without
software detach remains guarded. The exact silicon counter origin at this
mode boundary remains a precision question under GFX-D16; actual original
reentry, continued clocks/completion and connected pixels remain required.

The original vblank handler also reads FIELD while AV3 is still programming
the encoder, before the next input VSYNC and final timing reset. Manufacturer
100381B1.3.4/6/22/31 separates the running output counter from input timing
acquisition; continuity and the documented two-clock FIELD publication delay
are required. Exact silicon phase during partial live H_CLKO/PLL updates,
including the detached input-sync history, remains under this precision scope.
The declared functional policy preserves raw output counters and applies a
changed comparator at the next encoding edge; this is not a measured latch edge.
The identified framebuffer-address consumer does not branch on FIELD for the
retained progressive mode/flags. A supported consumer that distinguishes the
transient parity, a changed interface, or an observed output/timing failure
reopens exact phase work. Do not substitute a fixed FIELD value or promote
unknown full-raster/scaler state merely to satisfy this interrupt read.
Private attribution: `build/blinx/gpu-lle-diagnostic-boundary-20261016/`;
the short original-mode trace is in `build/noise-irq-lle/`.

## Unreached configuration and submission readbacks - 2026-10-16

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D24 | What are the general CFG0 mirror readback bits and behavior of reads from the old write-only USER PUT aperture? | The physical SDK startup writes PGRAPH CFG0 with PFB's03070003 and reads00000003. The probe now checks only that exact input/result pair, without inferring a general mask. The next physical attempt reads zero from USER PUT; pinned envytools nv4_pfifo.xml marks that older aperture write-only, while active CACHE1 PUT is readable. Probe5302c64 uses the owned readable cursor and retains real submission/GET checks. Existing emulated configuration words and generic USER storage are not evidence for physical readback semantics. No reached original-title consumer requiring these readbacks has been identified; full bit scans and additional invalid-access observations add no present closing requirement. | A supported original caller reads these locations, an accepted output depends on unresolved configuration bits, or a new profile needs a different input/result. Correct the required read semantics before accepting that consumer. Physical command publication, actual GET progress, enabled noise pixels and connected rendering remain required; this entry waives none of them. |

Private evidence remains in probe `results/noise-physical-runout-20261016/`,
`results/noise-physical-cfg0-20261016/`, `results/noise-cfg0-profile-20261016/`
and `results/noise-put-owner-20261016/`. The physical zero USER read does not
establish that all reads of the write-only aperture always return zero.

## GFX-D05/D18 addendum: nominal SD digital picture - 2026-10-16

The reached original640x480 NTSC tuple now has a bounded functional digital
contract, documented in [NV2A-DIGITAL-INPUT](../../docs/reference/graphics/NV2A-DIGITAL-INPUT.md).
Independent X-HD bus-receiver code identifies SD601 limited-range DDR444;
ITU601/Khronos define the nominal conversion, and Conexant100381B supplies
actual input packing and code remapping. The maintained GPU emits those digital
samples, and the encoder input owner receives them. It does not treat the
PRAMDAC words as inert or substitute raw RGB storage for a converted result.

Exact silicon coefficient widths, rounding and LUT/conversion ordering stay
in GFX-D05/D18. The supported identity DAC and disabled scaling avoid claiming
unresolved order or interpolation. Arithmetic is explicitly nearest-rounded
standard601; the two-level RGB roundtrip bound describes that mathematical
model, not measured silicon agreement. Different command profiles, nonidentity
TV gamma, and enabled input-edge corrections stay guarded. Reopen when a
supported original consumer reaches them or independent representative output
distinguishes the nominal policy. Further coefficient measurements are not a
prerequisite for implementing this declared functional profile.

The endpoint is active digital input before encoder decimation/flicker/analog
processing, with a host RGB preview. It neither describes a complete timed bus
waveform nor establishes analog output. Enabled noise, connected original
attract output and normal lifecycle remain required. No deferral ID is added
or removed by this clarification.

## GFX-D04 addendum: observed noise and filtering precision - 2026-10-16

Physical original-profile off/on/on/off pixels and three authored batches now
separate texture-relative phase, palette roles and lower/upper RGB ranges. The
shared coordinate/channel-step reconstruction is implemented in maintained
commit c49789b:8192 actual PFIFO texture draws pass against authored physical
expectations. All832 logical physical enabled samples fit within one byte unit,
all208 conservative centers and all64 original samples are exact. Low/high range
behavior is implemented; it is no longer the unresolved part of this question.
Native comparison and connected original title/lifetime acceptance remain open.

The combined data retains37 one-unit RGB residuals at left block boundaries.
Nominal left-texel weights1/255 and1/256 each explain all832 OFF and ON values.
Their nonuniqueness leaves exact filter weight/internal order unestablished. Exact subtexel phase, fixed-point filter weights and rounding
remain in GFX-D04's existing precision scope after supported decoding exists.
Reopen if a supported representative output exceeds its declared error bound,
the residue changes coverage/ordering or a required consumer distinguishes it.
Do not replace actual sampling with copied observed pixels, or use this scope
to waive color-range behavior or the connected title run. The private
evidence is the probe's `build/noise-range-pixels-review-20261016/` and
`results/noise-physical-{fifo,controlled,low-red,range}-20261016/`.

The implementation boundary remains replaceable: decoding applies to owned
texture bytes before sampling; DrawJob latches the selected noise bit and the
decoded-image cache includes the conversion profile in its identity. The existing LLE draw packets own
their texture snapshots; no worker should consult mutable live noise state.
Native HLE currently preserves state159's boolean caller contract while using
native compressed sampling. Its accepted appearance and fast path remain the
baseline; the eventual representative comparison must state that approximation's
numeric limits. No per-frame random effect or forced CPU texture upload is
introduced by this evidence. This addendum adds no question ID or family gate.

## GFX-D01 addendum: inactive texture clip comparisons - 2026-10-16

The reached17F8 control stores sixteen comparator-polarity bits, independently
specified as GEQUAL/LESS. It does not enable clipping; CLIPPLANE texture-stage
mode5 consumes those bits. Maintained storage now preserves the actual setter
and DrawJob snapshot, while all-stage mode5 processing remains guarded before
pixels/completion. Captured4331 API inputs and five custom shader definitions
contain comparator zero; the retained replay has no active mode5. That corpus
is separate from the in-progress connected acceptance run.

Active texture clip processing and its zero/negative/NaN interpolation edges
remain an extension within this clipping scope. Reopen immediately if a required
original draw selects mode5, a visible clipping regression appears, or raw
readback/SAVE begins consuming this control. Then establish its actual fragment
and state-transfer behavior; neither zero comparator bits nor an unreferenced
combiner input justifies skipping its discard effect. The source references,
corpus-presence limitations and focused queued-draw tests are recorded under
`build/blinx/shader-clip-storage-20261016/` and the governing closure checklist.
No new question ID or closing requirement is introduced.

## Constant SAVE after direct RDI writes - 2026-10-17

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| GFX-D25 | Why do eight saved constant words retain original bits after verified direct RDI tagging? | The SAVE-only physical batch writes/reads all768 distinct words exactly, then serializes760 exact markers and eight values equal to original OR marker at the same candidate offsets. The mechanism is unknown; no OR rule is implemented. A separate direct-source experiment verifies all768 original constants after LOAD, then all768 distinct replacements after source-only edits and another LOAD, with no DATA writes or SAVE. The required initial LOAD mapping therefore has independent evidence. Production SAVE/replacement-context guards remain; no reached attract consumer currently requires SAVE after direct RDI constant edits. Private receipts: probe results/context-constant-snapshot-observed-20261017 and context-constant-load-observed-20261017. | The supported title reaches SAVE/replacement-context preservation, direct constant RDI edits become observable through SAVE, or initial LOAD/rendering disagrees with the established source mapping. Promote the needed behavior before accepting that reached route. |

GFX-D21 addendum: the original initial c137 bits, along with all192 constant
slots, now have direct physical LOAD evidence. The earlier unconsumed-fog review
remains historical; it no longer justifies treating the initial bank as unknown.
Exact fog/arithmetic edge behavior and newly reached consumers retain their
existing reopening conditions. GFX-D25 is a separate unresolved SAVE question.

## GFX-D01/D05 addendum: reached compressed draw writes - 2026-10-17

The preserved original route reached loaded compressed depth/stencil writes at
initialized attract132. The D01 addendum's reopening trigger fired: this writer
is required implementation, tracked in `beads-xbox.1.127`. It now has queued
real-LOAD depth/stencil, compression-selector, neighbor-preservation and failed
publication checks using the existing independently checked NV20 codec. Actual
connected gameplay/exit/reentry remains required G05.

GFX-D05 retains the exact silicon choice of8/16/64-byte representation and
compression/cache timing beyond the declared logical storage contract. The
authored encoder is not a hardware compressor oracle. Reopen this precision
scope if a supported raw compressed-storage/tag consumer, aliasing transition,
ordering observation or rendered result distinguishes the representation.
Required logical depth/stencil, actual1D80 ownership, initialized tags and
success-only publication are not deferred. This clarifies existing D01/D05
scope; it adds no question ID or waived closing requirement.

## GFX-D02 addendum: inactive eye-vector state - 2026-10-17

The original normal-cadence run reached NV097181C/3E808081 during attract loading.
The required three-component SET_EYE_VECTOR setter at181C/1820/1824 is implemented
as exact raw command receipts with per-component provenance; this reached producer
is not deferred. Pinned xemu8fc320869718 definitions and handler establish the
component layout. The existing admitted texture modes NONE/PROJECT2D/CUBE_MAP do
not consume eye-vector state. Queued controls vary all components, including
signed zero/NaN encodings, without changing the entire rendered target.

Constant-eye reflection (mode12 hex), exact silicon quantization, physical
EYEVEC readback and context serialization remain outside this bounded profile.
Their guards are retained; raw command receipts do not assert those projections.
Reopen this part of GFX-D02 when a supported draw consumes that vector through
reflection, an actual register/context consumer reaches it, or output identifies
a mismatch. Implement that consumer before accepting its route. This extends
the existing state/shader-profile question; it creates no new ID or G05 waiver.


## GFX-D25 addendum: reached dashboard semantic SAVE

The stock dashboard boot now reaches SAVE during target2 graphics suspension.
The HLE build preserves a real per-instance context capsule, not stale source
bytes; strict SAVE rejection remains. Raw unknown prefix bytes lose their
initialized bits and are rejected by CPU/GPU consumers. Only established
surface projections are serialized; the eight constant anomalies remain
unresolved. See [HLE context transport](../../docs/reference/runtime/HLE-CONTEXT-TRANSPORT.md).
Reopen exact raw mapping when an actual saved-byte consumer is reached, a
validated mapping contradicts semantic state, or an unsupported context-local
field becomes necessary. This adds no silicon timing or netplay claim.

GFX-D25 semantic transport follow-up: run046 found an idle dashboard PMC bit12
reset after CPU writes invalidated its saved allocation, supporting intentional
boot-context discard. HLE admits explicit discard for an owned loaded context,
idle disabled FIFO/empty pull and draw queues/closed primitives/empty vertex
buffers and clear fault flags, followed by matching re-enable. Unchanged SAVE
is still required for the separately scoped saved subengine-reset path.
PRAMIN/valid snapshots/raw unknowns/storage/tags/global counters survive.
Exact PGRAPH reset contents and timing remain deferred; strict LLE unchanged.
Reopen when a reached consumer requires omitted reset effects or retained
state contradicts a validated reference.

## GFX-D02 addendum: opaque09F8 HLE raster omission

Dashboard boot048 reaches a loaded draw that does not satisfy the earlier
known-value4 receipt restriction. Envytools still names this method UNK09F8;
the current host renderer and inspected xemu renderer have no effect consumer.
HLE preserves absent/known raw receipts, validates setters (0..4 or15), logs
first omitted use and proceeds under an explicit raster-effect omission.
Strict LLE retains its known-value4 restriction. No reset/LOAD default or
physical pixel equivalence is inferred. See the
[consumer policy](../../docs/reference/runtime/HLE-OPAQUE-INITIALIZERS.md).

Reopen when physical evidence or visible output identifies a required effect,
or a supported renderer adds a consumer. Clipping, depth, multisample storage,
shader and texture requirements remain implemented or guarded; this defers
only the unidentified method's raster effects.
