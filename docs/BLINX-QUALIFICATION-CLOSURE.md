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
Q02 now passes the retained short title checkpoint `c32000-tsc-v3`: Microsoft
source PTS span9.910811s versus model9.917s, zero mismatched or repeated source
bytes, and456000 actual GP frames exactly equal caller PCM after one32-frame
pipeline alignment. Independent media RMSE0.00731/0.00702 and correlation
0.99662/0.99639 pass declared limits; A/V p95 is58.22ms. The effective playback
rate is48000, separate from creation44100. Full Q03/family acceptance stays open.

G01 passes in the maintained selection:15 original graphics checks and13
native regressions. Three real Reset/Release lives wait on queued GPU completion
before replacing presentation allocations or collecting backing. Source-bound
proof is `graphics-reset-lifetime-20261011/maintained-v3/qualified-root.aot-private.json`,
SHA256 `82d2802691744814c71a41073a99a38e65fb0e13a5a86c1888a394154e490192`.
The private paced-Swap variant did not observe pending-present ownership; that
specific witness remains unclaimed. Graphics-family startup/output remains open.

A01 retained inventory establishes ordinary original constructor defaults and
actual PCM/ADPCM objects plus44100/48000 playback. Quiet captures omit custom
route/gain/pitch/loop inputs. The already planned final title batch will record
those configuration events. Generic unobserved filter extensions do not expand
the closing scope; A02 is one composed defaults/settings-to-processing contract.

A02's ordinary constructor/bypass/phase0 composition passes12 actual original
PCM16-stereo/ADPCM-mono processing lives at44100/48000,8 persistent guards and
12 wrong-gain controls. Original headroom600->0, live frequency override,
Stop/cancel/unlink, detach/rebind and finite completion pass. Root verifies103
artifact identities and51 original function bodies and replays the portable
tool once. Proof `audio-default-composition-20261012/root-portable/qualified-root.aot-private.json`,
SHA256 `8f5053884eb59c6d931f0f9014375248418bb2768c16e9d5669169653c412a48`.
No production fix is needed in this scope. A01's final census can require another
stage only if it identifies an additional reached mode. Actual original audio
allocation/configuration/ISR/DPC/output remains required in A03/A04/A05.

The Q03 batch `reference-movie-batch-20261011/q03-c32000-v5` uses frozen sources
and an isolated8GiB runtime disk copy. Its preceding v4 generation passed three
affected checks and the actual original-thread configuration fixture. One accepted-settings
JSONL census records stable object generations, effective rates, ordered routes,
gain/headroom and lifetime operations alongside the existing source/output
capture; it never drives a clock or device. Its32768-event and600-second capture
bounds cover this planned observation. The still-image diagnostic now retains
up to600 snapshots, enough for the six-hour host batch; v4 was stopped after
639.781 wall seconds when the previous180-image limit was identified. Its
short Microsoft checkpoint passes unchanged waveform/video/A-V limits with
zero source mismatches/rereads. The stopped v4 is not full closing evidence.
Final movie/scheduling acceptance awaits
the actual two natural openings and subsequent Start/release/reentry evidence.

Already proven, do not rerun for bookkeeping: actual blocked BlockOnFence and
full64-slot capacity, original KWAIT/compute-peer progress, real callback FIFO,
GPU bytes and cleanup across three CPU/GPU lives. Retained66-check proof and
root38-pin review are under `build/blinx/graphics-reset-lifetime-20261011`.
The complete retained fence/capacity task `.116.4` is closed. Reuse the default
HLE two-opening/interruption/reentry proof and the owner's
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
