# Bounded Blinx qualification closing checklist

Owner-approved finishing plan, 2026-10-11. Parent `beads-xbox.1.102`.
This checklist governs the current NTSC attract path. It replaces open-ended
component validation with explicit family closing conditions. Four of nine
families are qualified: input, display, memory and files. Calendar belongs to
scheduling and is not a tenth family.

LLE may be slow. HLE remains the performant default. Each closed family needs
a documented shared caller contract, maintained build-time selection, actual
working LLE title execution, independent behavior evidence and matching HLE
effects within declared tolerances. A fixture or hybrid run cannot substitute
for a missing original device/processing route. No all-subsystems-LLE binary is
required: fixed subsystem selections can independently establish the families.

The owner selected bounded attract qualification with representative independent
checks. Exhaustive scene-pixel proof and accepted precision questions remain in
the append-only queues. Required routing, startup, ownership, clocks and processing
cannot be deferred when their absence prevents this supported route from working.
New Game, dashboard, other titles and LLE performance work are outside this phase.

## Remaining work and closing evidence

| ID | Owner | Required work / remaining evidence | Passing condition |
| --- | --- | --- | --- |
| Q01 | `.123` | Finish reference timestamp ownership; input replay fix is committed at `784c1fd` | Original RDTSC and compiled helper observe one stopped common timeline; repeated reads/host stalls do not advance it; failure stops before fabricated GPR output |
| Q02 | `.123` | Validate relative original execution and device cadence | Source consumption, movie PTS, PIT/audio/controller clocks remain coherent in a short checkpoint; explicit input schedule is not asserted to be hardware CPU cycles |
| Q03 | `.73`, `.112` | Complete reference movie and integrated scheduling | Two natural opening lifetimes reach final source picture2263, actual Stop/Destroy and title transition; a subsequent real Start/release interrupts a movie and reaches fresh demo gameplay; original thread/IRQ/FP ownership and captured A/V timing pass |
| G01 | `.116.5` | Original CPU Reset/Release with live original physical resource and queued work | Three real lifetimes retain backing through reset/binding use, wait through original scheduling for actual completion, release then allow original free, with exact bytes and cleanup |
| G02 | `.109.6`, `.111` | Actual original AV/encoder startup and activation | Execute reached initialization on shared NV2A/SMBus/encoder owners; no guessed warm registers or fabricated handshake/ready state; actual launch crosses retained startup guards |
| G03 | `.116`, `.116.2.1` | Required original instance/PFIFO/startup state | RAMFC/instance admission and required read/write context behavior have independent semantics; maintained title renderer reaches real draws and completion |
| G04 | `.116.2` | Required enabled-noise draw behavior | A reached complete primitive has an executable supported sampling behavior; independent source/paired observation establishes it; disabling or ignoring noise is not acceptance |
| G05 | `.109`, `.116` | Connected graphics-LLE attract and representative comparisons | Actual title draws/visibility/callbacks/reentry/teardown work; retained and affected state/shader/texture/fetch/alias cases qualify supported behavior and HLE tolerances; exhaustive4331-draw numerical composition stays deferred |
| A01 | `.113` | Census original voice modes actually used by attract | Each reached format/filter/headroom/routing mode maps to an implemented stage or a specifically named remaining required task; imported/unreached extensions stay out of scope |
| A02 | `.113` | Required voice filtering/scaling and lifetime stages | Independent impulse/tone/transition evidence qualifies reached stages through the actual source/envelope/mix owner; required processing is not bypassed |
| A03 | `.113.9`, `.114` | GP/EP configuration, wake/reset and bit9 DMA input | Original B0..B3 and coherent C5/CA/D5 context drive the existing sole DSP CPUs; independently supported descriptors copy exact bytes and publish actual completion |
| A04 | `.115` | FE fence/control ordering | Required work precedes real RAM notification; actual original DPC/consumer returns, including pending/failure cases, with no substituted completion |
| A05 | `.113` | Build-selected original audio device integration and output | Unchanged original DirectSound routes to maintained APU/GP/EP/AC97 owners on the shared bus/clock; actual title output, stop/reuse and movie-relative timing match declared independent contracts |

Dependencies: Q01 -> Q02 -> Q03 closes movies/scheduling. G01 and source audits
can run beside Q work; G02/G03/G04 -> G05 closes GPU/graphics. A01 -> A02 and
A03/A04 -> A05 closes audio. Only unresolved *required* hardware semantics move
to the final hardware batch; precision questions remain deferred.

Q01 passes in the maintained reference selection:18 affected checks, six native
checks and actual unbound/failure controls. Three real original CPU thread lives
hold RDTSC/compiled observations constant through120ms host stalls and advance
only on common-clock input. Source-bound proof is
`reference-tsc-20261011/v1/qualified-tsc.aot-private.json`, SHA256
`81cbe843cfe9ed4c5486d9b0213ad7b1908cb9d0f89e58d4e2119f74eb1979ac`.
Q02/Q03 and whole family acceptance remain open.

Already proven, do not rerun for bookkeeping: actual blocked BlockOnFence and
full64-slot capacity, original KWAIT/compute-peer progress, real callback FIFO,
GPU bytes and cleanup across three CPU/GPU lives. Retained66-check proof and
root38-pin review are under `build/blinx/graphics-reset-lifetime-20261011`.
Reuse the default HLE two-opening/interruption/reentry proof and the owner's
accepted appearance baseline when their runtime dependencies are unchanged.

## Profile and validation boundaries

The current `APU_REFERENCE` profile selects original scheduling/files/memory/USB
but native audio/input and D3D11 graphics adapters. Its common-clock composition
can qualify Q work; it is not full original GP/EP or graphics-family acceptance.
Use separate fixed graphics-LLE and original-audio device selections for G/A
closure. The default native profile remains the performance path.

One generation gets one source/input/executable manifest, reused by related
checks. Run focused affected tests during changes, then one coherent suite and
one necessary title regression per material integration batch. Retain meaningful
failures and reuse unchanged evidence; no automatic whole-suite/title repetition.
Declare waveform/pixel/timing tolerances before evaluating a candidate. Playback
uses one alignment per actual lifetime, not fresh alignment per capture window.

Before a long Q03 observation, validate a short content/device-clock checkpoint.
Stop on demonstrated divergence. An unchanged failure does not justify a longer
run; a new attempt needs a diagnosed code/configuration/instrumentation change.
After two unsuccessful changed-generation checkpoints, report the precise
divergence and refine the dependency rather than sweeping cadence values.
Slow wall progress alone is not an accuracy failure or an optimization request.

Human review is needed only for changed visible/audible behavior. Prepare a short
offline clip at normal playback speed; use it alongside measured timing/ownership.
New findings enter this checklist only if they prevent its frozen supported
contract; otherwise append evidence and reopening triggers to the existing queues.

## Resources, hardware and final disposition

Primary plus at most one authorized worker, two logical CPUs each, Normal
priority and F: temporary/build storage. Only one GPU-heavy run at a time.
Lower resource use when the owner plays a game; otherwise do not throttle LLE
merely because it is slow. Keep Beads updates local; no Dolt synchronization.

The Xbox remains stopped after the unexpected SG write readback until the
outstanding owner inspection/restart confirmation. Do not ping/probe while that
stop is unresolved. Afterwards use standing read/benign-write authorization,
verified owned RAM and recovery copies. Stop after any unexpected WRITE result.
Avoid dangerous boot/HDD/EEPROM/flash operations. Do not replay failed writes.

Close a family only when its complete bounded row set passes and the maintained
selection executes the service in title. Record source, selection, evidence and
landed commit in Beads and the burndown. At completion, report9/9 *bounded*
families with explicit limits. If essential semantics remain unavailable, report
their exact blocker and smallest required observation; do not relabel them as
qualified or continue unrelated validation indefinitely.
