# Bounded Blinx qualification closing checklist

Owner-approved finishing plan, 2026-10-11. Parent `beads-xbox.1.102`.
This checklist governs the current NTSC attract path. It replaces open-ended
component validation with explicit family closing conditions. Seven of nine
families are qualified: input, display, memory, files, scheduling, movies and audio.
Calendar belongs to scheduling and is not a tenth family.

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

## Owner goal and stopping rule - 2026-10-15

The active goal is 9/9 qualified families for this bounded Blinx attract profile.
Close the remaining audio and combined GPU/graphics tracks when actual selected
LLE execution, required output, ordering, ownership and normal lifecycle pass
representative independent checks. Preserve the performant default HLE and reuse
unchanged evidence. No all-LLE executable or exhaustive hardware model is required.

Each new blocking question must identify a supported original consumer and the
output, forward-progress or lifetime consequence that prevents acceptance. A new
raw register observation alone does not establish a new closing requirement.
Establish whether it affects a required consumer; if it does not, retain the
observation and defer its interpretation with a reopening trigger. Hardware
stop-after-unexpected-write and fresh-owner/recovery requirements remain intact.

Normal drained DSP reset/reinitialization and actual pending notification ordering
remain required. Exhaustive reset interleavings, unreached in-flight cancellation,
malformed-command combinations and broader encoder/boot coverage are extensions
in LLE-D06/AUDIO-D13/AUDIO-D14. Existing guards remain on unsupported cases.
Support one coherent startup profile; never transplant live D4 encoder values
into retained 8A state or manufacture ready/completion values to cross a guard.

Use focused affected checks while implementing, then one coherent acceptance
sequence per changed selection covering representative output and the required
normal stop/reuse/reentry/retirement transitions. Reuse qualified component and
HLE appearance evidence when dependencies are unchanged. Do not expand the run
matrix or requalify completed families for bookkeeping. Additional precision and
coverage findings go to the append-only queues unless the consumer test above
demonstrates a blocker. These rules do not close a currently incomplete route.
Current disposition: seven of nine families;46 retained deferral IDs,45 scopes.
GFX-D24 retains general CFG0/old USER readback questions beyond the required
probe publication correction; a reached original consumer reopens that scope.

## Original DAC upload and digital gamma consumer - 2026-10-16

A private diagnostic retaining original CPU/callback execution but deliberately
omitting enabled DXT1 noise stops after165.078s at the DAC write-index port.
This exposes required implementation work; it supplies no noise, pixel or
lifecycle acceptance. The maintained noise guard remains unchanged.

Retained original upload routines each write index zero and256 RGB byte triples.
The original AV producer selects GENERAL_CONTROL00100030 for the32-bit path.
The maintained DAC owner now receives that complete table and the digital
scanout applies separate red/green/blue lookups. Identity ramps retain the
direct framebuffer path; a changed ramp requires converted output. Gamma
updates never alter framebuffer storage. Unsupported reads, widths, modes and
nonzero-index updates remain guarded; incomplete uploads cannot claim output.

Four affected NV2A/display/tile checks pass in0.63s. A private component check
executes both retained upload routines against maintained DAC MMIO, verifying
769 writes apiece, the second routine's32 ordering reads, unchanged framebuffer
bytes and exact independently computed256-pixel output. The authored caller
and scanout geometry do not establish original connected title acceptance.
Evidence: `build/blinx/original-dac-upload-current-20261016.aot-private.json`
(with its preserved reference DLL) and
`build/blinx/dac-gamma-tests-20261016.aot-private.log`.

GFX-D23 records unreached DAC extensions and partial-upload timing with explicit
reopening triggers. Required full upload and its pixel consumer are implemented,
not deferred. The changed private noise-omitting diagnostic crosses the DAC
boundary and stops after182.187s at original AV mode reactivation. Physical
noise, connected original rendering
and normal lifecycle remain required. Families remain7/9.

The next clock change recognizes the original software mode boundary: AV3
explicitly clears CRTC28.SLAVE before programming and sets it afterward. This
starts a new nominal FP epoch using the already declared functional origin,
while preserving the shared time owner, source-edge accounting and independent
encoder reset. Clock loss without explicit software detach still fails. Four
affected clock/register/handshake checks pass0.17s, including no events during
detach, new-mode vblank timing, frozen time and the unexpected-clock-loss guard.
Exact activation phase remains in GFX-D16. The changed original-CPU diagnostic
stops after159.063s at FIELD continuity. Its omitted noise still prevents any
family acceptance claim.

An independent short reproduction now executes actual AV3 startup, followed by
original AV2 option14 and FIELD15. Option14 changes the documented horizontal
luma-filter bits96[5:4]; the control owner incorrectly invalidated all timing on
that write. Filter-only changes now preserve the established counter/FIELD epoch,
while input-line or active-line changes still invalidate it until a real reset.
Both original startup phases, filter on/off and same-mode AV3 pass in0.76s.
Focused clock/control checks pass, including nonzero counter/phase preservation
and timing-change negative controls. Root reviewed the manufacturer definitions,
the original producer/consumer path and17 current source/result identities in
`build/blinx/field-continuity-20261016/review.aot-private.json`.
The prior full-title log lacks the last encoder mutation, so its precise cause
is not inferred from this component reproduction. One changed private diagnostic
stops after154.063s at FIELD with encoder timing unknown. Its transition trace
records early mode reset, later invalidation and attachment without a subsequent
observed final reset. Static original-title review places FIELD after the
synchronous AV3 call returns; the exact branch/continuation is being traced.
This does not establish an asynchronous early FIELD consumer or justify a timing
fallback. No FIELD value, clock phase or readiness is substituted.

The next single private diagnostic stops after161.094s with the same guard.
Its opt-in read-only observer pairs AV invocations with actual caller returns.
At failure AV3 is still waiting for retrace; it has not reached its final reset
branch or returned, and no AV2 FIELD15 call entered. The original vblank ISR's
FIELD port read is the reached consumer. This supersedes attribution to the
title's later synchronous post-AV3 query. The fixed-size early FIELD sampling
window was exhausted, so exact final sync counters are not claimed. The
counter's live-update contract remains under source review. Terminal receipt:
`build/blinx/gpu-lle-diagnostic-boundary-20261016/run.aot-private.json`.

Commit3217c5e separates the documented running output FIELD counter from full
input/raster acquisition. Actual rational clock edges continue during explicit
FP detach; real resets establish origin, and ordinary input/picture changes
preserve the output counter. Both natural and actual-sync FIELD transitions
retain the two-clock publication delay. Unsupported live interfaces remain
guarded, including master HSYNC offsets; restoring a latch cannot seed phase.
Full raster/scaler validity is never inferred from FIELD availability. Exact
partial-write phase stays in GFX-D16 with its supported-consumer trigger.
Two affected device checks pass0.078s; four original direct/thread AV mode
checks pass1.797s. The previous board coupling fails the new early-vblank
control. Root reviewed the primary definitions, corrected two initial review
findings and verified39 source/build/result identities. The optional observer
also has a bounded overflow control. Evidence:
`build/blinx/field-boundary-observer-20261016/counter-seal.aot-private.json` and
`build/blinx/noise-original-roi-review-20261016/counter-root-review.aot-private.json`.
The one changed private original-CPU diagnostic reaches its300.157s supervisor
ceiling without a guard failure. Actual interrupt-side FIELD reads now succeed
before the new mode's first VSYNC, while full input timing remains unknown.
This crosses the earlier consumer failure. AV3 itself has not returned and is
still observed at its first retrace-poll loop while timers/interrupts advance.
The next work is a short poll/clock/IRQ liveness investigation, not another
unchanged full-title run. Receipt:
`build/blinx/gpu-lle-diagnostic-counter-20261016/final-review.aot-private.json`.
This is not connected rendering or lifecycle acceptance; the private noise
omission still precludes family qualification. Families remain7/9.

The short retrace investigation establishes a concrete CPU/device ordering
counterexample: six complete maintained FP VSYNC pulses occur while the exact
original polling instructions read only low and complete no retraces. Giving
the same CPU opportunities inside those unchanged pulses completes all three.
This authored schedule is not a replay of unrecorded title polling timestamps.

Whole GPU LLE now requires the existing coherent stopped reference composition:
native audio uses APU_REFERENCE, or original audio uses its shared device
timeline. The former QPC/XAudio2 composition cannot coherently slow only PIT
while guest audio cursors, USB frames, replay and TSC continue on host time.
Default HLE remains unchanged. Reference input proposals are now split into at
most32 PIT ticks (28.44us) per CPU opportunity for this GPU profile, below its
shortest reached95.33us VSYNC. The remaining interval is retained exactly;
audio advances to admitted time before publication. Reads do not advance time,
and oscillator rates/pulse widths are unchanged. This functional interleaving
policy does not claim physical CPU instruction/cycle timing.

Eleven focused checks pass in5.22s, including actual original direct/thread AV3
startup and mode reentry in both phases under this production clock selection,
the original launch producer, PCM/USB/input/TSC coherence, and bounded interval
publication. The portable standalone quantum fixture also passes with GCC;
the maintained CMake checks use MSVC. Attach/detach and failed-publication
guards remain intact. Evidence:
`build/blinx/reference-timeline-20261016/checks.aot-private.json` and
`build/blinx/gpu-lle-diagnostic-counter-20261016/retrace-slice-review/`.
Actual connected title passage, enabled noise and lifecycle remain open.

The changed connected diagnostic reaches300.235s without a guard, but AV3 id5
still does not return from its first retrace loop. The new clock is selected;
PIT/USB/scheduler continue and nine complete retained FIELD samples remain
valid around lines482/483 (one later log record is torn and excluded). Thus
the interleaving correction is necessary for coherent composition but is not
sufficient to resolve the live wait. No per-poll values/timestamps were retained,
so the remaining cause is not assigned to interrupt starvation or the status
read path. Source review identifies13B210 as the actual GPU ISR and13BBC0 as
its DPC; the FIELD read in13B400/13B47D belongs to the latter. Slow host callback
time alone cannot explain missed guest pulses under the stopped clock.
The next short fixture will include the actual GPU ISR/DPC registrations omitted
from the passing AV-only fixtures. No unchanged full-title rerun is planned.
Receipt: `build/blinx/gpu-lle-diagnostic-clock-20261016/final-review.aot-private.json`.

The short original-interrupt reproduction now passes both a direct AV3 caller
and an original guest-thread caller. Each executes the unchanged generated
13B210 ISR and13BBC0 DPC four times, returns from04010101 AV3, disconnects the
interrupt and retires its owned objects; the threaded case also joins and
retires its original thread and caller allocation. The production hash-checked
image loader supplies actual code pages and imports. The88-body direct-call
closure is copied unchanged by `tools/native_gpu_av_callbacks.py`; unknown
indirect targets retain the existing guards. The first mixed-harness attempt
failed its routing assertion because native scheduling bypassed the original
DPC dispatcher; the final fixture requires the original callback target at
compile time. It supplies an authored empty flip queue and NULL user callback,
so it does not establish connected title or pixel acceptance.

Both controls also pass with the new opt-in actual-read observer enabled in
about11s. That observer records the byte already returned by FD6013DA, without
an additional MMIO read, clock admission or altered CPU block. Counts stop at
16 periodic observations or128 emitted transitions per invocation; truncation
is explicit and a finite external ceiling remains necessary. Evidence:
`build/blinx/av-interrupt-20261016/poll-preparation/`.
Reproduction: prepare the private closure with `native_gpu_av_callbacks.py`
(`--generated`, `--xbe`, `--output`), provide its C path through
`PILOT_GPU_AV_TEST_CALLBACKS` and the matching image through
`PILOT_GPU_AV_TEST_XBE`, build `pilot_callback_kernel_lle_test`, then select
`pilot_gpu_av_interrupt_lle` and `pilot_gpu_av_interrupt_thread_lle` in CTest.

The retained PRE-LOAD snapshot has a concrete additional consumer: the vblank
event wait block belongs to priority9 threadD0009418, above the priority8
polling thread. Its registered A04F0 DPC callback only updates RAM counters and
reads the shared TSC; it adds no guest-time admission. Waking the separate
priority9 waiter may change polling opportunities, but the snapshot alone
does not prove its behavior during the later AV3 wait. The next bounded title
diagnostic records actual polling values, times and event-list observations.
Families remain7/9; no new precision or hardware requirement is introduced.

## Separate physical draw owner ready offline - 2026-10-16

Probe861b995 now reaches the genuine SDK initialization return, with273 queued
methods and16 actual ISR/foreground service events in the offline device model.
Twelve groups covering121 lifetimes pass in26.577s. Root reviews the source and
failure paths and verifies214 unique artifact identities; the reviewed separate
image contains the original first-draw sampling profile. These results establish
the bootstrap implementation, not physical rendering or a noise algorithm.

The existing remote dashboard restart and probe return completed. The reviewed
image was uploaded as a separate work-directory app after verifying recovery
backups and backing up the installed probe on G:. The installed app is unchanged.
Loaded entry bytes and untouched result storage match the reviewed image.
The first physical bootstrap rejects its initial engine-enable precondition at
phase1 with one read, zero hardware writes, no IRQ claim and no allocations.
Fresh boot leaves PGRAPH disabled. The previously qualified PGRAPH-only enable
is therefore required preparation before another fresh app lifetime; no failed
write or partial GPU initialization is being retried.

The next preflight distinguishes pending PCRTC input from disabled global IRQ
delivery. Probe a627afb admits only the documented globally masked vblank and
lets the actual SDK mask PCRTC before enabling delivery. Thirteen offline groups
pass. Physical initialization then claims IRQ, retains four allocations and
completes13 stores with matching readbacks. It stops before instance writes at
an inherited512-byte runout ring overlapping the new context. The new bounded
owner reserves an unused512-byte tail inside its verified existing allocation,
with independent compiled drift/readback/publication checks. Probe e9a820f passes
14 groups/132 lifetimes in31.268s; root verifies206 artifact identities and exact
bootstrap/SDK code, data and resolved relocations in the original-profile build.
The fresh physical continuation crosses runout relocation and full instance
initialization, then stops at PGRAPH CFG0: the SDK writes PFB's03070003 and
the immediate mirror readback is00000003. No following GPU operation or capture
runs. This is a configuration mirror, not the adjacent ZCOMP_OFFSET. The prior
model used PFB=3 and a generic latch, so it did not exercise this physical pair.
A bounded adapter correction preserves the full SDK write, rechecks the exact
source word and requires the exact measured full-word result; other input or
readback values remain unsupported. Fresh recovery/continuation is required;
no general register mask, physical SDK success or family acceptance is claimed.
Probe abee32b passes15groups/138lifetimes in33.370s; the retained prior executable
reproduces the failure under the corrected measured fixture. Its next fresh
physical run passes CFG0 and reaches12,388 stores, stopping on first USER PUT
readback. The specific primary NV10:G80 XML marks that aperture write-only;
the revised probe uses readable CACHE1 DMA_PUT after verifying exclusive DMA
channel0 selection.16bootstrap groups/141lifetimes pass34.650s; five capture
groups/26cases and three original-profile transport groups pass, including the
actual248-command input. Root verifies249 artifact identities and equivalent
bootstrap/SDK object code in the original-profile image. These do not establish
physical publication, GET progress or pixels. Remote DASH was acknowledged,
but subsequent FTP/probe checks time out; owner power-cycle was requested.
Private receipts: probe `results/noise-physical-runout-20261016/`,
`results/noise-physical-original-20261016/`,
`results/noise-physical-vblank-20261016/` and
`results/noise-runout-owner-20261016-final/`, with latest corrections/results in
`results/noise-cfg0-profile-20261016/`, `results/noise-physical-cfg0-20261016/`
and `results/noise-put-owner-20261016/`.

## Original sampling capture integrated; physical startup still open - 2026-10-16

The unchanged8x8 region also passes a source-bound usefulness review: the
original shader places the four vertices at the640x480 screen corners, with
one level-zero texel per screen pixel. The region is strictly interior and its
four central BC1 blocks contain distinct endpoints and all four selectors.
This rules out an empty-geometry or constant-input capture; it does not predict
physical filtering, noise or final pixels. No profile or runtime changed.
Evidence: `build/blinx/noise-original-roi-review-20261016/review.aot-private.json`.

The separate probe can now build a private-input capture from the actual first
draw's shader, four vertices, consumed constants and full eleven-level DXT1 mip
chain. It preserves geometry/viewport/filtering and crops only an8x8 output
window. Texture/vertex allocations are relocated; target and DMA objects remain
owned by the genuine SDK. Four identical off/on/on/off observations use actual
draw and M2M commands. No original handle, callback or target address is replayed.

The profile generator verifies its reviewed input and every binary hash/length,
and confines generated title bytes to ignored build storage. The real nxdk object
and diagnostic PE/XBE compile, with executable conversion identity checked.
Three focused transport groups pass both authored and actual inputs (248 actual
configuration commands). They establish byte copying, multi-submission relocation,
ordered readback and failure stops using deliberately arbitrary model pixels;
they provide no silicon noise or original-frame acceptance.

The parallel bootstrap batch completes the actual20KiB PRAMIN initialization in
the offline linked-SDK model. It uses the previously measured alias, preserves
unknown RAMFC/NVM bits, and retains resources on failure. Eleven methods covering
111 lifetimes pass. Root verifies176 retained identities and the source ownership
changes in `build/blinx/noise-instance-root-review-20261016.aot-private.json`
(SHA256 `b00b5ee1729658ed642b09c410b50a9ceed17160bf9b32234d385800a85214d2`).
This supersedes the RAMFC stop: the next unsupported effect is CRTC index31,
before successful SDK initialization, channel submission or physical drawing.

Capture review found an asynchronous error gap: uninstrumented CPU copies could
continue after an ISR's failed hardware write. A checked polling-only capture
entry now masks GPU interrupts before these copies and prohibits SDK service.
Fixed authored polling helpers retain their own immediate checks; automatic
block instrumentation is excluded only there so it cannot insert work between
a failed readback and return. The genuine SDK and pre-mask entry stay guarded.
Full initialization, the physical representative comparison, connected original
rendering and normal lifecycle remain required. No Xbox operation was performed
for this batch. Still7/9 qualified;44 deferral IDs/43 scopes are unchanged.

Probe commit `956d8dd` contains the reviewed instance/polling owner and original
capture integration. The final SDK-prefix checks pass11/11; shared transport
passes5 groups/22 cases; original and authored profile transport each pass3 groups.
Root verifies378 current-source, retained test and final original-profile image
identities in the probe's `build/noise-original-linked-20261016-final/root-review.aot-private.json`
(SHA256 `818ceaf2d5132f07dd15a33319a61080edbf998074f245ececb638d556f7eda0`).

## Captured-queue batch removes two reached command gaps - 2026-10-16

The 1B68 stop is addressed with all 18 stage1..3 bump matrix/scale/offset setters.
Exact raw command receipts are preserved; matrix encodings are bounded by the
primary Kelvin tests, while scale/offset accept their full word. Stage0, physical
readback projection and actual bump shader modes remain guarded. Authored queued
checks establish that inactive bump state cannot change ordinary textured pixels.

Auditing actual dispatch branches through the eighth END found one additional
missing method at queue index1115: 1710=0, BREAK_VERTEX_BUFFER_CACHE. The loaded
path has no persistent vertex/transform cache, so its idle-boundary admission
preserves current attributes and refetches subsequent indexed storage. It does
not invalidate shader instructions or invent a clock/global idle event. Tests
change positions/colors behind the same indices and check changed pixels, retained
current values, and active/nonzero rejection without later completion.

Root also narrowed the palette provenance gate: normalized direct/DXT textures
do not consume palette state when an unrelated RAM tile is active. The separate
pitch-linear tiled profile retains its existing layout/palette guard. Both shader
execution paths pass an active unrelated-tile regression. The four-job build and
two affected checks pass (`context-batch-build2`/`context-batch-tests2`, 0.93s).

The private `preflight-loaded-queue.aot-private.py` reuses captured physical RAM
and PRAMIN to find command guards cheaply. It first reproduced 1710 after seven
draws, then executed eight draws in 0.313s after this batch. This diagnostic uses
authored setup and acknowledges seven startup notifications without executing
their original callbacks; it stops at the next software notification. GFC1 also
lacks tile/control state and initialized-byte provenance. These omissions forbid
using its pixels, retirement or success as original-title qualification. The
actual unchanged CPU/callback route ran separately in
`gpu-lle-loaded-batch-20261016`: it exits2 after190.093s at
`NV2A DXT1 noise pixel behavior remains unqualified`, entries56382, with no
captured presentation. It passes the previous1B68 boundary but does not establish
eight original draws or qualified pixels. Root verifies its complete binary/source
identity and the worker pins in `build/blinx/loaded-batch-root-review.aot-private.json`
(SHA256 `8e17b19760dffe9c8f3172797fc6acb739b63c8358690e649b797a0f62c21a10`).
The implementation is committed as `bcc7973`.

G04 now has retained original geometry/program/texture inputs and a confirmed
connected noise consumer. Its missing independent pixel observation requires a
working physical draw owner. The existing paired module is finite, but its
separate bootstrap still stops before RAMFC and cannot produce its genuine SDK
success certificate. Finish that continuation as a coherent batch, then measure
a representative original sampling profile. The authored nearest-only microcase
alone cannot establish the reached mip/filter behavior. A read-only INFO confirms
the installed probe is responsive; the last successful surface experiment has no
failure hold, but retained resources require the already established remote
recovery route before another takeover. No live write or recovery was performed
for this checkpoint. Seven families remain qualified; GPU/graphics, reached
enabled-noise output and normal lifecycle remain open.

Independent branch/source review:
`build/blinx/remaining-first8-audit-20261016/review.aot-private.json`, SHA256
`a1ba2578b8414225ec2270cd41fba41caef7c515a772685e8a3c0eace47aba77`.
No physical console operation, new HLE performance claim or additional precision
closing requirement is introduced by this batch.

## Loaded draw implementation checkpoint; title acceptance pending - 2026-10-16

The maintained batch connects actual queued LOAD/state production to
bounded programmable CPU drawing. It admits the 32 audited startup initializer
methods, tracks current-attribute knowledge across immediate/indexed/inline input,
and checks consumed shader inputs and persistent constants. Authored tests check
actual destination pixels, completion, retained vertex attributes and refusal of
missing required state. PRAMIN readback changed by unmodeled object updates remains
guarded until explicitly replaced; invalid read widths cannot overflow the host
read buffer. Two-sided lighting remains explicitly guarded in this bounded route.

Per-invocation temporary/result initialization uses the existing functional shader
contract; its physical mechanism is recorded in GFX-D22. The texture test retains
the PROJECT2D Q dependency and distinguishes explicit Q=2 from a subsequent
partial-XY result initialized with Q=1, on both interpreter and compiled paths.
An initial test selected the legacy bilinear filter while expecting nearest texels;
explicit nearest filtering and coordinates away from texel boundaries correct the
fixture without a renderer change. The failed log is retained.

Independent original-state review found two overly broad guards: no17CC visibility
enable is written in the captured prefix, and draw8 enables depth LESS with writes
disabled. Unknown counter enable now invalidates count history without blocking
pixels; a later enable setter cannot repair it, while explicit count clear can.
Reports still require known count/history and descriptor. Depth/stencil admission
requires its actual comparison and zeta inputs. Authored LESS pass/fail cases preserve
the read-only depth buffer. Compressed ZROP writes remain guarded before mutation;
ordinary memory expansion is not silently accepted as their compression contract.

`build/blinx/context-draw-build6.aot-private.log` records a successful four-job
build. `context-draw-tests6.aot-private.log` records both affected checks passing
in0.32s; the five selected original-driver checks in `context-draw-pilot-tests6`
pass in2.48s. Dispatch structural checks pass; staged repository hygiene scans994
blobs with zero findings. Optional OpenGL coverage remains unavailable. No new
HLE performance claim or physical Xbox operation is made.

The changed original run in `gpu-lle-loaded-draw-20261016` passes actual context
LOAD and stops at class97 method1B68 after169.063s, before drawing. The earlier
handler-range audit missed18 reached stage1..3 bump setters whose existing range
handler has no implementation; the loaded guard correctly refuses them. They
remain required work. One elective pre-LOAD canonical RAM snapshot is passive,
default off and owned by the same execution borrow. Root verifies all1733 captured
packet words against its64MiB bytes, retaining actual vertex/texture inputs for
offline replay rather than substituting authored RAM into an original-title claim.

Root verifies36 merged review/input pins (one exact reconstructed predecessor for
a comment-only correction), current binary/source identity and the RAM/packet
agreement. Evidence: `build/blinx/loaded-draw-review-20261016/root-review.aot-private.json`,
SHA256 `781d8c8f0cb3886f2dc46b81dbb875fc710874dcaf769f775dac27c0afc87f68`.
This closes the implementation checkpoint, not a family. Connected original
rendering, reached enabled-noise output and normal lifecycle remain required;
the family count remains7/9.

## Loaded scalar consumers connected; geometry remains open - 2026-10-16

Loaded contexts now admit established object/state producers and supported scalar
operations through the actual queued dispatcher. Per-bit raster knowledge preserves
unwritten enable bits; program/constant uploads require explicit cursors and record
the components they establish. DMA identities require actual binding, separately
from raw loaded surface values. PRAMIN descriptor/table reads require initialized
bytes. Unknown methods and connected vertex consumers still stop before retirement.
A returning fault callback cannot allow a failed handler to continue the queue.

Focused fixtures execute a real LOAD followed by 2D copies, color clears, semaphore
publication and flip progression. They check actual destination pixels, loaded
offset/pitch use, live register updates and both parser selections. Missing clear
inputs, second-plane descriptor bytes or a consumed compression selector reject
before the valid first plane writes. Unsupported consumers preserve their packet
and GET on retry; missing or crossing semaphore inputs cannot publish completion.
Subsequent DMA bindings invalidate unqualified raw BASE/LIMIT readback rather than
return stale loaded values. The immutable original image remains retained.

Independent review found and resolved the old MMIO flip guard intercepting valid
increments, missing second-plane descriptor provenance and an unknown compression
selector. The final NV2A fixture passes in 0.28 seconds; the shader dependency test
passes in 0.01 seconds, and dispatch structural checks pass. Initial compile errors
(mixed auto deduction and the standalone test's include dependency) are corrected;
their logs remain private. Existing optional OpenGL skips are unchanged.

The portable shader helper walks existing decoded operations backward from selected
output components. It preserves parallel MAC/ILU reads, partial writes, scalar fog
and R12/oPos aliasing; unsupported state writes/addressing reject transactionally.
Five authored groups and 24 queries over all eight retained actual programs pass.
The first seven require c98.XY, c132.XYW and c136.XY for the tested visible outputs;
c137.XY appears only when fog is requested. Full texture output queries explicitly
retain unknown initial Z/W dependencies. The eighth requires c0/c1. This helper is
tested but not yet connected to draw admission; it does not supply unknown values.

Root verified 33 helper pins and the six scalar-review inputs, retaining the exact
worker-reviewed fixture before root added the final 2D-copy cases. Evidence:
build/blinx/context-method-review-20261016/review.aot-private.json, SHA256
52d55cd05a19224d51b2f6e0b300c6407d01c9a0062ae4201d5764f01a281032.

A retained-queue check identifies 32 initializer method/class pairs outside the
old dispatcher before the first draw; the first is class39 method0180 at index4.
These formerly fell through silently. A grouped source audit will distinguish
retained setters with guarded later consumers from required effects. No new long
capture is needed to rediscover this known stop. Next finish that initializer
batch and connect shader/vertex/raster provenance, then demonstrate original LLE
rendering and the required lifecycle. No title capture or hardware operation in
this milestone. Still 7/9 qualified, with 43 deferral IDs / 42 scopes unchanged.

## Owned first-context LOAD storage implemented - 2026-10-16

The maintained NV2A now copies the complete37F0 producer allocation into an
independent context owner and applies the23 measured surface words, including
the correct2D/3D offset/pitch mirrors. RawBASE/LIMIT never manufacture DMA object
identities. Unknown bytes remain opaque; the measured36A0 SAVE prefix is not
misrepresented as the complete hardware LOAD extent. Original header1 and the
measured inverse header11 admit the bounded USER11 projection. Other USER profiles,
dynamicCONTROL readback, unknown frontend/RDI reads, replacementLOAD andSAVE remain
guarded. The first original USER consumer preserves the low bits; this is not a
general bit4 law or a measured full-pipeline identity.

Every source byte must have an actual PRAMIN write, including the opaque tail.
Host-zero allocation cannot satisfy input initialization. Admission requires the
explicit original ten-mode setup, real first-context reset/FD/cache preparation,
idle engine, disabledFIFO, channel0 and an owned in-range instance. Validation
finishes before live mutation. LOAD supplies no CHID_VALID, FIFO release, method
retirement, semaphore or synthetic completion. The original followingCONTROL
write still owns channel validity. Loaded method consumers currently stop before
retirement until their field provenance is connected; this is a storage/transfer
milestone, not a successful original draw or family closure.

Independent review catches and resolves three implementation gaps: an empty-queue
reset could discard the unknown-state guard, invalid PRAMIN widths could mark
non-guest bytes initialized, and a failed drain could continue fetching unreadDMA.
Reset remains guarded afterLOAD; legal write widths and nonwrapping extents are
checked first; drain failure propagates through both parser selections and retries.
The full NV2A CPU fixture passes0.38s, including atomic rejection, missing prefix/
tail bytes, source independence, mapped mirrors and both prefetched/unread queues.
Dispatch structural guards pass. The portable decoder also accepts the retained
actual producer and hardware-saved image, preserving every input byte and matching
all23 independent hardware markers. No new game capture or hardware operation.

The full original initializer proves titleDEBUG7=0028C3FF, matching the maintained
pipeline. Earlier suspicion that boot0008CFFF would block these first draws was
based on an incomplete producer slice. Boot's differing bits remain guarded;
their supposed fragment-limit meaning was unsupported and the comment is corrected.
The successful physical transfer did not recordDEBUG7, so no matching live profile
is inferred. Superseded review reports are preserved.

Root evidence: build/blinx/owned-context-load-20261016/root-review.aot-private.json
(SHA25648ea75056c76eda140ed9c555e186fd9860d52e02061061f6f55ce2f411492fb).
Three handoffs are independently pin-checked, including the corrected complete
initializer and final adversarial source review. The first root decoder review
mistook the inverse completion count for a raw readback vector; its failed
assertion/script is retained, and the final review claims only the actual count
and marker evidence. Next connect explicit state producers and guarded consumers,
then execute the required original rendering/lifecycle. Still7/9; existing43
deferral IDs/42 scopes unchanged.

## Consumed common state bounded; stale palette guard corrected - 2026-10-16

The first-eight pending-draw common-state audit records153 setter-coverage entries
and both pre-draw clears. Required object/DMA bindings, surface scalars, active
raster/combiner state, texture0 descriptors and array formats are explicitly
produced before use. Original driver+144=149C makes the actual first-context
DEBUG0=70000/0 reset branch execute before LOAD; the maintained admitted reset
already clears primitive mode and assembly vectors. This is an actual reset
origin, not an inferred image zero or a claim that STATUS idle proves no assembly.
The eighth draw's disabled v4 consumes current secondary color produced by the
preceding seven draws; replay must preserve that history. Initial COMMON/raster/
texture/surface values are not established as visible dependencies for these
eight outputs. This is a source/queue dependency result, not executed rendering.

The audit finds one artificial dependency: the normalized sampler rejected any
nonzero inherited palette even though its admitted formats are all nonindexed.
The correction removes that check inside the existing whitelist only. Palette
storage/readback is preserved and indexed0B/unknown formats remain guarded.
The CPU fixture compares all256 target bytes with palette0 and a validly encoded
palette address outside fixture RAM, requires actual sampling, and verifies0B
still has its named rejection with unchanged pixels/executed-batch count.

The first full fixture also exposes a stale older negative: alphaRef9 misses a
dedicated A4 profile but is supported by generic GEQUAL, so it no longer implies
global rejection after stale palette state stops blocking fallback. The negative
now uses unsupported alphaFunc0208, retaining every no-output/dedicated-counter
assertion and adding its named kAlphaFunc skip. The failed result is retained.
Independent review agrees with this consumer distinction. Final nv2a CTest passes
in0.79s (1.39s total); unrelated OpenGL fixtures retain their existing SDL3-unavailable
skips. No game capture, HLE renderer change, threshold adjustment or hardware
operation is needed for this correction.

Root verifies20 source/input identities and preserves the predecessor source from
Git. Private evidence: build/blinx/common-consumer-audit-20261016/review.aot-private.json
(SHA25693422f490bcf31bdef5777dcd1139160d58fa8c24ffb79c8d6d6270e88091ddf),
and build/blinx/nonindexed-palette-20261016/root-review.aot-private.json.
Next implement actual owned context/header/control transfer, preserve unknown
bytes and field provenance, use measured mappings, and guard any observable
inherited read/partial write/readback/save until its semantics are established.
This avoids exhaustive unused-default mapping without admitting a no-op loader.
GPU/graphics remain open;7/9 and the existing deferral counts are unchanged.

## Surface context map measured; transform dependencies bounded - 2026-10-16

Probe37ec5df completes one23-field surface batch after autonomous remote recovery.
Distinct legal markers in six offsets, six bases, five pitches and six limits
map uniquely to saved bytesE0..138. After SAVE, every live field is explicitly
cleared and checked; actual LOAD restores all23 markers. Borrowed RAM/pointer/
FIFO restoration passes. Critical time76683/3375000 seconds is22.720889ms.
The3496-word prefix replaces every sentinel;84 original tail words are preserved
and checked before inverse LOAD, so no poison or reconstructed hole is loaded.
The23 measured mappings replace the prior six-limit pattern hypothesis.

The batch retains27 differences from original input, including raw328=0C plus
previous header/3024/369C differences. Extra observations do not become new gates
without an actual supported consumer. Independent root byte review verifies
mapping, raw output, canaries, input/code identity and previous-observation delta;
the successful pinned checked path establishes inverse/restoration. Eight focused
methods pass22.659s and the strengthened first-observation failure control7.436s.
No default HLE behavior changes. GPU/graphics remain open;7/9 qualified with
43 retained deferral IDs/42 scopes. No hardware recovery hold remains, although
retained VM/pins cannot be reused as fresh ownership.

Independent transform review establishes exact live RDI program selector10 and
constant selector17, including raw constant lane orderW,Z,Y,X. Original producer
regions are program[1A9C,231C), constants[231C,2F1C). The first seven pending draws
upload their complete11-step program and consumed c98/c132/c136; the eighth
uploads its complete12-step program and c0/c1. Only inherited c137 feeds unused
fog, covered by existing GFX-D21. Therefore arbitrary initial transform-bank
precision is not a new blocker for these eight outputs. Exact arbitrary saved
slot/live-bank identity remains unproven; this does not authorize omitted bank
transfer, zero substitution or ignoring a later observable inherited/save consumer.
The next step is mapping genuinely consumed common state and implementing the
guarded original context route, followed by connected rendering/lifecycle.

Evidence: probe results/context-surfaces-observed-20261016/ (saved image SHA256
511216e436dcea8d4286cf57ef14cbba84de8a08becc37607cd49e46c69ba6c4), and title
build/blinx/transform-bank-audit-20261016/review.aot-private.json (SHA256
d0765b96657936500cb5b18230b713cd6ae84e23cc68a2532006bd9791f1e102).
Root verified all16 transform-review input identities; no title recapture was needed.

## Audio family qualified; GPU transfer investigation continues - 2026-10-16

Audio A01-A05 closes for the bounded attract profile. Root reviewed54 unique
artifacts and the original producers, maintained device ordering, independent
processor models and authored failure controls. The changed muted capture exits0
after3748.5 host seconds with no guards. Original retained object2196917448
performs Stop10751, observes retired status, restarts at Play10783, and produces
96000 further frames on both analog and digital routes under the same owner.
Normal retained-buffer reuse is the actual lifecycle; observer cutoff is not
claimed as guest teardown. Previously accepted retirement/reentry covers that
separate contract.

The complete post-Stop trace contains192072 records. Independent reconstruction
matches4293 concurrent Send sums,25752 original GP publications and535 continuous
mixed-input EP blocks. Actual GP/EP programs and state are retained, with one
initial filter state and no reseeding. Original SG and codec initialization bind
both ends to the same8KiB ring. Replaying captured FIFO cursors/timestamps and
absolute codec reads yields135168 known frames exactly, including all96000
required postrestart frames. Root separately reproduced this result with an
event-merged bytewise ring implementation. Seven unknown initial codec blocks
are excluded before restart; every required scored frame is known. All three
unchanged waveform windows pass with zero error; there is no fitted lag/gain/rate.

The initial accepted trace is bit-identical. A01 census, A02 independent stages,
A03 configured startup/drained reentry, A04 actual timeout/queued-DPC completion,
and matching HLE/Q03 output/timing/ownership evidence remain reusable. The final
all-three timeout proof is audio-compute-retirement-20261015; the earlier
audio-timeout-retirement-20261015 retains its selector2 failure and the accepted
drained route. No new family requirement or precision deferral is introduced.
Default HLE remains unchanged and performant; LLE speed is not an acceptance gate.

Root proof: build/blinx/audio-family-qualified-20261016/qualified-root.aot-private.json,
SHA256 e5e7a9b380248bd3aa3e1d85e3f3126166a19755dbea3f37e3b9c0d807776e1d.
Worker handoff: audio-postrestart-window-20261016/worker-review/final-handoff.aot-private.json,
SHA256 e8cde0d441bb89a3ece88af21252824918fb4d232a33ba18f5958011df972d88.
The failed context-per-frame parser and linear-spool alignment remain preserved;
source-bound resident-context and actual ring models corrected the offline review,
without changing runtime samples, scoring windows or thresholds.

Hardware recovery used the already authorized probe DASH, dashboard SMC Restart,
fresh dashboard startup log and remote probe launch. No owner navigation was
needed. The configured fa2b513 experiment then passes fresh allocation, upload,
instance/context enable, cache invalidation and LOAD/idle checks, but returns raw
CTX_USER11 instead of the probe's exact1 witness. No SAVE, restoration, result
buffer read or retry follows; changed volatile state is retained pending recovery.
Original13FD3C and pinned pbkit preserve all non-CHID CTX_USER bits after LOAD;
the image producer sets bit0 but does not assert whole-register equality. Thus
the exact witness needs source review; neither full transfer nor the bit4 meaning
is qualified by this result. Private receipts: probe results/context-recovery-20261016/
and context-transfer-configured-20261016/. Remaining required work is G02-G05.

The next reviewed probe759d353 completes after a separate remote SMC recovery.
Actual original-image LOAD and poisoned-RAM SAVE pass, followed by verified RAM,
pointer and FIFO restoration. USER loads/saves11;3496 of3580 words replace their
sentinels,3487 equal input, and84 sentinel-equal words form tail36A0..37EC.
Nine written differences are retained, not normalized away. Critical time is
76370/3375000 seconds, about22.628ms. This establishes the bounded physical
transfer experiment, with raw save SHA256
91d08c27b21ac5ac916b8cd1f36711a8909104a85c9ae2676b8a2559505e7f24.
The consumed field-to-maintained-state map and connected draws remain required.
Internal GPU state/configuration and VM/pins remain retained; PFIFO stays disabled.
No recovery hold follows this successful experiment. Private evidence is probe
results/context-transfer-observed-20261016/; seven compiled/host methods pass
in15.475s, and independent review accepts the fixed sampling/stop boundary.

Earlier dated milestones below retain their historical counts and open scopes.

## Bounded physical context transfer stops at load witness - 2026-10-16

Probe15e74e4 supplies a separately owned64KiB VM buffer, checked upload of the
actual hash-bound37F0 producer image, and a bounded CLI/pinned-reservation
transaction. The planned successful path backs up20KiB, loads the real image,
poisons only the image RAM, saves actual PGRAPH state, checks outside canaries,
then restores borrowed RAM/pointer/FIFO. Failure restores CPU flags only and
performs no later hardware operation. Five actual compiled/host test methods
pass in9.041s, including LOAD/SAVE timeout/interrupt and failed restoration;
independent review accepts the ownership/stop boundary and primary
nxdk/Linux STATUS-zero completion ordering. This is probe validation, not
physical context-format qualification.

The single physical attempt returns prepareCA000001 and passes every input
write/readback. Execute returnsDE40001B: pointer111D and LOAD1 were issued,
idle/no-interrupt passed, but CTX_USER was not the required1. Its unexpected
raw value is not encoded in this receipt. No sentinel, SAVE, restoration,
result-buffer read or retry follows. VM/pins and potentially changed volatile
state remain; hardware is stopped pending the requested power cycle.
Evidence: probe results/context-transfer-20261016/. Nothing touches storage or
boot configuration. The alias result remains valid; this is not a successful
transfer or an emulator defect diagnosis.

Offline source audit identifies a concrete profile discrepancy: this direct
experiment required CTX_CONTROL0, while original14003D and pinned pbkit set
DEVICE_ENABLED10000000 before loading. The actual load helper additionally
performs conditional local resets/inactive FD clear, cache invalidation and
channel selection. Resolve that preparation as a batch before a changed
attempt, preserving the failed observation and all current transfer guards.
Audio's sole muted mixed-output capture continues independently. Still6/9;
43 retained deferral IDs/42 scopes.

The follow-up also finds retained DEBUG1=0, disabling instance/context processing.
Original13FE80 and pinned pbkit set118700. A changed closed probe variant now
adds the three checked preparation writes: DEBUG1=118700, context
DEVICE_ENABLED10000000, then DEBUG1 cache invalidate with stable readback.
Primary register definitions distinguish dynamic CTX_CONTROL bits and the
cache-invalidate strobe. A legal mismatching CTX_USER is preserved in RET with
no following hardware access. The host selects this variant only; old compiled
bytes remain unchanged for the failed evidence. Six compiled/host methods pass
in11.590s plus the raw-return host check in1.442s; independent delta review has
no blocker. No revised physical operation occurs while recovery is pending.
This pure context-transfer preparation does not claim geometry/tiles/rendering
initialization or prove the cause of the first failed observation.

Root separately reviews the prepared audio lifecycle/mixed-output finalizer and
verifies all nine helper/control pins. It reconstructs concurrent sends through
Mix/GP/continuous EP, with one initial state seed and a unique pre-Play transport
anchor fixed before the unchanged scored windows. Upstream missing history is
not inferred; unchanged qualified contracts are reused. Preparation review
has no blocking finding, but actual finalized output remains required. Private
receipt: audio-postrestart-window-20261016/worker-review/root-prepared-review.aot-private.json.

## Instance comparison and restarted audio observability - 2026-10-16

Follow-up hardware progress: probe18bb56b compares eleven reversed-block
layouts across the complete20KiB window. Exactly64-byte blocks match; all five
CPU pages are nonzero/nonuniform. Eight focused compiled/host methods pass in
15.559s. The fresh20KiB allocation experiment fc7d160 returns NULL before any
marker/MMIO write and is not retried. It does not invalidate the read comparison.

Probe b40ba06 then supplies a different bounded experiment: query/pin existing
pages, exclude other CPU work with CLI on the single-core console, recheck
disabled PFIFO/idle PGRAPH/global GPU IRQ masking and scanout nonoverlap, change
one CPU DWORD, check both views, and restore/check the original word only on
success. No kernel calls/yields occur during the borrowed interval; every
failure restores CPU flags and immediately stops hardware access. Seven focused
compiled/host methods pass in1.024s, and independent review accepts that CPU
exclusion boundary. The live result confirms both marker and restoration:
two CPU stores, no GPU-register write, no retry or recovery hold. Private probe
evidence: results/instance-block-orders-20261016/,
instance-owned-alias-20261016/ and instance-borrowed-word-20261016/.

This resolves the tested CPU/PRAMIN alias and establishes a narrow borrowing
technique. It does not establish the full saved-state format or a successful
context transfer. The next GPU step is a separately bounded direct load/save
experiment using this exclusion boundary and retained original data; the full
SDK bootstrap's unrelated control guards need not be bypassed. Families remain6/9.

Probe7693dbd queries the existing GPU reservation without resizing it, verifies
the five CPU pages and compares their complete20KiB contents with two fixed
PRAMIN layouts. Neither ascending nor reversed-DWORD order matches any page;
all five CPU pages are nonzero/nonuniform. A separate122-read sample confirms
varied data in both windows and unchanged PMC enable. No reservation/data/MMIO
write, allocation, selector or context command occurs. Five compiled-payload/
host test methods pass in0.960s; the shared identity gates pass in2.349s.
This rejects the tested correspondences, not every possible backing relation.
No physical alias or exclusive instance owner is inferred. Context transfer
remains required. Evidence: probe results/instance-mapping-20261016/.

Finalized audio review identifies a missing oracle precondition: another looping
Play10819 plus one-shots overlap the scored postrestart output. The accepted
initial stage trace reached its capacity before this transition, so it supplies
no postrestart source/rate/send provenance. The single-source comparison neither
qualifies the mixed output nor demonstrates an emulation defect. Its failed
reports and fixed origin remain preserved. Actual retained-object Stop/status/
restart, epochs and96000 frames on both routes pass independently.

A separate bounded observer window is committed5439711 at the actual successful
Stop, before concurrent starts. It borrows all existing stage events and adds no
guest processing or time input; the accepted initial trace stays separate.
Eight focused persistence/trigger cases pass. Replaying all12073 completed
calls from the finalized journal through the actual compiled tracker requests
exactly one trace: object2196917448, priorPlay63, Stop10751, lifetime1, with no
32-slot exhaustion. The next acceptance input must contain actual concurrent
source/rate/send and DSP state, not another assumed isolated ADX waveform.
Root reviewed the observer, runner, test coverage and381 prepared input hashes.
One changed muted capture started2026-10-16 at07:52:46UTC under four logical
CPUs/Normal priority. The worker exclusively owns its live journal/PCM/HDD and
will review the actual mixed output after automatic same-buffer lifecycle plus
96000-frame finalization. No additional unchanged run or family closure. Still6/9.

## Pending transform dependencies and hardware readiness - 2026-10-16

The first seven pending draws upload their complete eleven-step vertex program.
Its reads of c98/c132/c136 have explicit queue uploads; the only inherited read,
c137, feeds final fog output o5.x alone. The eighth draw uploads its complete
twelve-step program and both read constants c0/c1. Every captured draw disables
fog, and no active alpha/color/final combiner input selects FOG. The latter
dependency check avoids assuming that disabled fog alone makes its value
irrelevant. Independent review agrees. GFX-D21 retains exact c137 semantics
with a consumer-based reopening trigger; no value is seeded and no transfer
or draw guard is removed. Evidence: gpu-lle-first-transfer-20261016/
transform-inputs-review.aot-private.json and the corrected program disassemblies.

The physical probe responds. Twenty-nine fixed read-only control observations
complete, but PMC_ENABLE has both PGRAPH and PFIFO disabled. Values returned
from those disabled blocks cannot establish initialized context contents or
FIFO ownership. No hardware write occurred in that readiness batch. A separate
guarded PGRAPH-only enable then succeeds with exact readback; FIFO remains
disabled and global interrupts remain off. Twenty-five fixed follow-up reads
complete, showing idle PGRAPH/no pending PGRAPH interrupt and some retained
context words. These observations are not reset defaults, context ownership or
saved-image offsets. No context command, RDI_DATA access, PRAMIN store or storage
operation occurs. The probe remains available with PGRAPH enabled. Ten offline
payload/host tests pass; independent review's post-EXEC receipt-error finding
is corrected and covered. Probe evidence: results/pgraph-enable-20261016/.
The changed audio run subsequently terminates successfully after4022.640s,
with no guard or supervisor error. Its actual retained-buffer Stop/status/restart
and96000 continued frames on each output route trigger cooperative finalization.
The worker's finalized lifecycle provenance review passes, but the unchanged
three-window continued-output comparison fails (correlation0.105..0.203,
RMSE0.0311..0.0518). Its single early alignment was already weak; the result
does not yet distinguish an output defect from source/oracle provenance error.
The worker is diagnosing the finalized evidence without another capture or
lag/gain/rate retuning. Audio remains open. Still6/9;
43 retained deferral IDs/42 deferred scopes.

## Actual first selector and bounded pending draw batch - 2026-10-15

The extended observation terminates at a real device guard after290.140s,
before its480s ceiling: original context switching requests pointer111D at
FD400784. The stopped original callback scope retains the actual compiled
registers and driver1446E0 separately from its suspended raw PC. Channel0,
table110A and RAMFC711000 agree with that same producer/driver owner. Its
complete14320-byte image remains byte-identical to the actual producer result,
SHA256 c900b436eddf545a418fb349e20f4147d997beee216a8b8650f6735707896021.
The earlier180s ceiling was an incomplete observation, not a diagnosed deadlock.

Passive decoding retains256 prefetched parameters and1477 unread packet words.
All1733 captured words reconcile into1392 method parameters without a missing
branch or partial packet. Captured RAMHT/object bytes resolve the four bindings
to classes39,9F,62 and97. There are eight complete BEGIN/draw/END submissions;
their programmed state and upload history are retained for the required field
audit. These are pending commands, not executed draws or pixels. Evidence is
build/blinx/gpu-lle-first-transfer-20261016/request-review.aot-private.json,
pending-methods.aot-private.json and draw-context-dependencies.aot-private.json.

The independently documented16-bit context pointer now latches and reads back
without changing channel validity, PRAMIN or FIFO execution. High bits reject
without altering the old pointer; reset invalidates initialization knowledge.
Load1 and save2 remain explicit faults because their consumed saved-image map
is incomplete. No successful transfer or completion is inferred from pointer
acceptance. The original raw-CPU fixture now accepts the selector and reaches
the load guard while preserving its paused queue/channel/PRAMIN snapshot. The
compiled callback fixture exercises pointer readback on the same owned bus.

The NV2A suite passes in0.27s and five affected original GPU integration checks
pass in1.09s. Independent worker review found no additional issue. No new full
title run is needed merely to encounter the already source-bound next load
instruction; the next work is the consumed saved-state mapping and real transfer,
then reached output/noise behavior. The audio acceptance run remains separate.
Families remain6/9 and deferred scopes42 IDs/41 scopes; no hardware operation.

## Inactive FD preparation and compiled GPU bus ownership - 2026-10-15

The maintained RDI3D bank now supports the reached fifteen-word inactive clear.
Independent [envytools f102b823 state load/dump](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/hwtest/pgraph_state.cc#L2302)
maps these words to FD patch state; nxdk pb_load_gr_ctx independently issues the
same clear before context loading. Only explicitly initialized words can be
read, index advancement stops on rejection, and both PGRAPH/subengine resets
invalidate initialization knowledge. Nonzero state and active patch/curve/
transition methods remain guarded. Review found that those methods previously
fell through the generic unknown-method case; explicit rejection now prevents
a false claim that the cleared state remained valid. GFX-D20 retains unreached
active processing/reset contents without waiving required context transfer.

Compiled GPU ISR/DPC accesses now use the existing stopped original-CPU bus
scope and its synchronous DMA borrow. A failed scoped access never retries as
idle access. Passive capture distinguishes the suspended raw CPU from current
compiled registers, retaining the latter separately without inventing an
original instruction PC. Review also caught a diagnostic-read exclusion that
missed memory-bus owners. The corrected test reads an actual original allocation,
registers that same address as a memory-bus region, and verifies rejection with
unchanged canaries before unbinding/freeing it. The initial fixed-address test
failed because the assumed address was not mapped; its log is retained.

The existing NV2A suite passes in0.53s with actual direct-memory PFIFO batching
telemetry as well as the strict path. Seven affected original GPU suites pass
in1.26s, including genuine DPC -> compiled MOV -> registered GPU clear/readback
with unchanged PIT time. Raw capture guard also passes. The strengthened memory
owner test passes in0.31s. Evidence: display-clock-20261016/fd-context-*.

The changed connected run reaches its180.203s supervisor ceiling without a
device guard, but has only actual producer entry/return records, no selector,
load or pixels. The final samples show continued original execution, three
threads and changing PCs; this is not evidence of completion or a diagnosed
deadlock. Executable383ad9292ec26abfe1c5ea7a0745752d7bdea06f462bc4fe436e34b6cd88d988
and DLL30f3e6a6b23804f8b633e5944f3ec9e671641c4fd8af69d612bdaeb6a91f9c4d
are pinned under build/blinx/gpu-lle-fd-context-20261016/. Next is a sufficiently
bounded observation of the first actual transfer, followed by its independently
established saved-image semantics. No context-transfer success is supplied.
Audio commit38f6836 is in its separate changed acceptance run; still6/9.

## Retained audio restart and reached context-switch preparation - 2026-10-15

The changed audio capture is terminal after4259.766s, return2. The original153
synchronization callback now succeeds on the required retained object: Stop10751
returns0 at45333000PITticks and GetStatus10752 reports stopped. Public position0
and frequency44100 also return successfully; looping Play10783 then reaches the
VOICE_ON68 nonzero-cursor guard. Other-object playback is not substituted for
this required restart. No lifecycle checkpoint or successful capture footer exists.

The source-bound short reproduction now runs original Stop/status/unlink/cancel,
public position0/frequency44100 and the complete179EFD/179C4B region/start sequence
under its declared allocated-SGE precondition. It emits no explicit CBO store.
The maintained stopped, locked ordinaryPCM16 VOICE_ON resets low24 CBO before
ACTIVE, retaining the adjacent envelope byte and existing converter/envelope
owners. Source-origin and negative ownership fixtures pass; the exact same
original restart reproduces the previous guard with the old implementation.
After restart,2048 frames match the independently specified source with residual
6.21e-6 and unchanged source bytes. Root review retains the fixture's limit:
outer Play SGE acquisition and full title/DPC/output acceptance are not supplied
by this small check. AUDIO-D16 defers exact silicon reset-edge precision and
unreached start profiles, not required normal restart. Production preparation
is complete: the updated executable and four original synchronization/callback/
timer controls pass. Root verified the reviewed source, executable and build-log
pins. The changed connected title run is active under the audio worker, using the
reviewed private executable. Its automatic same-buffer restart/output checkpoint
remains the acceptance target; no new success is claimed. Evidence:
audio-synchronize-lifecycle-20261016/worker-review/ and
audio-cursor-lifecycle-20261016/PREPARED.aot-private.md.

Graphics aperture commit7765ecf passes the six affected original suites. The
40s follow-up with existing progress diagnostics shows continued initialization,
not a device fault. A justified180s ceiling then reaches a real terminal guard
after144.062s: the original13FC4C context-switch caller clears RDI bank3D with
fifteen zero words before its first400784/400788 load. That bank is unsupported.
The actual complete producer image is retained, but the selector/load request
has not yet executed. Next is this reached preparation batch and consumed context
semantics. Evidence: build/blinx/gpu-lle-context-startup-20261016/ and the private
13FC4C caller extract under gpu-lle-cpu-aperture-20261016/. Still6/9.

## CPU UMA aperture and actual context production - 2026-10-15

Original CPU texture stores now reach the owned retail UMA bytes through the
F0000000..F3FFFFFF PCI framebuffer aperture. The separate memory callback
preserves1/2/4/8-byte transactions; register callbacks retain their narrower
contract. Every access checks the actual original identity mapping and borrows
the existing allocation/protection/lifetime owner. No writable RAM alias or
generic physical-address mask is installed. PCI relocation/readback is not
inferred from this initialized profile. Pinned xemu482088490c2d5983 exposes
BAR1 as an offset-zero shared-RAM alias; the retained original PDEs establish
the reached bus mapping. Implementation is authored here.

The software GPU retires already-issued host draw jobs before the CPU transfer,
without pulling pending FIFO packets or advancing device time. Independent
review caught two boundaries and both are fixed: a delayed draw fault must
prevent the CPU transfer, and invalid active tile extents cannot be treated as
linear. All three tile banks are checked; overlapping tiled storage and host
renderer residency remain guarded. Existing texture conversion reuse compares
the newly fetched complete source bytes. No HLE rendering path changed.

The core aperture check passes in0.81s. Six affected original transport/AV/
observer suites pass in3.22s, including actual MOVUPS/MOVNTPS/SFENCE instructions,
both64-bit halves, shared ordinary RAM readback and cleanup across three real
original-thread/GPU lifetimes. Existing whole-range/protection/retired-owner
checks remain active. Evidence: build/blinx/display-clock-20261016/aperture-*.

Changed Blinx crosses the texture-write boundary and now executes the actual
13F26D producer through its real return13C08E. The passive records bind the
original source hash and driver, channel0, instance111D, table110A and RAMFC
711000. Its complete37F0-byte image has477 nonzero DWORDs and SHA256
c900b436eddf545a418fb349e20f4147d997beee216a8b8650f6735707896021.
The40s observation reached its supervisor ceiling with no device guard, no
context-selector/load request and no pixels. It is not normal title completion.
Evidence: build/blinx/gpu-lle-cpu-aperture-20261016/. The follow-up enables
existing bounded PC/PIT/thread progress diagnostics to locate the post-producer
wait; no new completion/state is supplied. G02-G05 remain open; still6/9.

## Initialized launch producer and reached CPU aperture - 2026-10-15

The maintained GPU selection now runs original AV3 once before title threads,
after selecting the shared runtime timeline. This is an explicit initialized
NTSC launch input. Mode0 invokes the original API's0F010101 default, using
format0x12/pitch2560 and a real original-kernel-owned1228800-byte framebuffer.
Original page walks establish encoder8A/current-mode0/saved-image0 in the retained
RAM. The initializer checks those actual globals, executes the original producer,
then verifies its real mode update. No encoder bytes or ready state are supplied.
The framebuffer is retained across title takeover; its eventual retirement and
any required mode transition remain part of G05. Duplicate initialization and
starting without the shared clock are rejected before another allocation.

The focused fixture passes in0.66s, including the owned DMA view, post-return
FIELD and actual early AV2 option11/parameter5 read/modify/write. Independent
review corrected ordering so any selected audio/reference timeline owns both
initialization and title execution. The current GPU-only selection is unaffected
by that ordering correction; no live audio image was accessed.

Actual Blinx now crosses the previous EN_REG_RD stop and runs original GPU
ISR/DPC callbacks. The changed title stops after6.016s at its real MOVNTPS at
0016AC1B, writing F344E000. The bridge currently rejects that physical bus address.
The retained page table's F34000EB PDE maps the region to the same bus address;
this is not an ordinary missing RAM PTE. Next: implement the supported NV2A CPU
memory-aperture route with allocation/lifetime and device ordering, then collect
the first real G03 context. No context record or connected pixels are claimed.
Evidence: build/blinx/gpu-lle-launch-profile-20261016/ and
build/blinx/display-clock-20261016/launch-profile.aot-private.log.

## Original AV3 startup returns - 2026-10-15

Direct and genuine-thread original AV3 now return success in both initial AV
phases. The calls execute all reached encoder/VGA/PRAMDAC setup, three actual
clock-driven retrace waits, encoder timing reset and final WSS programming.
Post-return checks establish the programmed clock rate, retained FIELD owner,
AV acknowledgment and GPIO levels. Both original threads terminate and their
handles are independently invalid after close. Clocked waits can also retire
the captured file worker's existing handle10; its previously qualified one-handle
baseline change is accounted for without accepting leaked authored handles.

The short checks caught a real model defect: original BA3F->2F clears DACOFF
after timing reset, but blanket invalidation discarded digital timing. The
manufacturer's power-management description and table2-5 preserve digital
operation through DAC power changes. DAC writes with unchanged SLAVER now retain
the timing epoch; an interface change still invalidates it. WSS60..6A likewise
preserves digital timing, with reserved bits guarded. Independent board checks
retain the expected later FIELD edges after these writes. Two affected device
suites pass in0.61s; all four original startup cases pass in1.52s. Evidence is
build/blinx/display-clock-20261016/dac-clock-tests.aot-private.log and
startup-final-original.aot-private.log.

The11 additional PRAMDAC commands are retained as received commands, not invented
hardware readbacks or established pixel-processing effects. Their receipt cannot
create AV acknowledgment or activate FP timing. Both scanout routes still stop
before framebuffer reads/resolution when this processing is unresolved. GFX-D18
records the remaining interpretation; required supported output remains G05.
GFX-D19 defers analog WSS/VBI waveform and downstream TV aspect response. This
milestone establishes the initialized AV3 call, not a cold boot or connected
title graphics acceptance. Next: run the changed original title selection and
capture its actual first context request. G02-G05 remain open; still6/9.

## Original VGA setup batch - 2026-10-15

The complete reached sequencer, graphics-controller and attribute tables now
have bounded register owners. Indexed accesses retain independently written
state; attribute writes alternate index/data and input-status reads restore
index phase. Fresh reads and unsupported modes remain guarded. The explicit
initialized AV3 profile supplies its required initial attribute index phase,
not arbitrary register contents. Original AV3 starts with an index write and
does not first read input status. This input is separate from clock routing
and cannot reinitialize an already owned phase. Existing SR1 blanking still
suppresses framebuffer fetches without stopping synchronization.

Only the original idempotent VGA-enable command is admitted; its receipt does
not invent VSE2 readback. Miscellaneous-output programming and the later tables
retain the reached extended-color selection. Planar memory/text rendering and
additional clock/reset modes are not enabled by register transport. Register
facts come from [IBM VGA/XGA May1992 pp2-42..54 and2-78..96](https://bitsavers.trailing-edge.com/pdf/ibm/pc/cards/IBM_VGA_XGA_Technical_Reference_Manual_May92.pdf)
and pinned envytools nv_vga.xml; implementation is authored here.

Original direct/thread calls in both idle phases now reach the remaining
PRAMDAC table, first FD680898=10000000 at80034F5D. The26-word table is retained
as a batch, with11 controls beyond the existing FP timing/control owner. This
is not a completed original mode return or output qualification. The later VGA
tables pass component checks but are not yet reached by those original calls.
GPU/register suites pass in0.82s; independent review reports no material issue
in the bounded transport. Final evidence is under
build/blinx/display-clock-20261016/vga-*.aot-private.log. Still6/9.

## Shared display-clock integration - 2026-10-15

Original direct and genuine-thread AV3 calls in both idle phases now cross
PLL_CONTROL and reach the same next unimplemented VGA write: FD0C03C3=01 at
80034F15. The selected initialized profile supplies the measured routing word
100A0000; it supplies no encoder contents or ready/retrace result. The new board
owner couples programmed CLKO to FP counters and encoder sync, retaining separate
GPU phase and real encoder timing-reset epochs. The functional counter origin
is declared nominal; exact activation/phase stays in GFX-D16. Connected original
startup and output are still required, so this does not close G02.

GPU work now observes the original device owner's cached published PIT time.
GPU MMIO and platform ports advance old configuration before mutation, without
sampling wall time or pumping original execution inside callbacks. Manual frame,
FIELD or encoder-clock injection is rejected when this board owner exists.
Explicit component fixtures retain their separate external-clock selection.

Independent review caught and corrected a vblank edge that depended on time-step
partitioning. Sparse FP traversal now visits vertical blank even when horizontal
blank already suppresses DISPEN. Split and unsplit expectations check the exact
edge, separate encoder reset, sync ordering and delayed FIELD transitions. Four
affected device suites pass in0.37s; eight original transport/AV suites pass
in1.22s; three C-ABI ownership rejection checks pass. Delta review has no remaining
findings. Evidence: build/blinx/display-clock-20261016/. No hardware work or live
audio HDD access was needed. Remaining VGA and PRAMDAC setup has been extracted
as one batch; unknown pixel-processing controls are not qualified by this clock
integration.

## Original synchronous audio callback transport - 2026-10-15

The reached KeSynchronizeExecution route now executes original kernel153,
including its actual interrupt-level raise/lower and one-argument callback.
Admission binds the original audio caller/context, connected interrupt object,
stopped same-thread CPU owner and exact original callback frame. The generated
callback preserves the actual interrupt flag, shares the existing CPU/PCR/MMIO
owner and returns through the original service. Generic or nested unsupported
synchronization calls remain guarded; no native service replacement or lifecycle
completion is supplied.

Root reviewed the source, exact original instruction extracts and four passing
focused cases: synchronization with IF0/IF1 and nested original query, rejected
disconnected owner, existing callback guards and existing timer/DPC behavior.
The prior dispatcher reproduces the original153 rejection in the same fixture.
These callbacks are authored generated-body equivalents: the tests establish
transport, ownership and ABI, not the actual title callback's driver effects.
Private source pins, red control and results are in
build/blinx/audio-synchronize-20261016/REVIEW.aot-private.md.

Next is one changed connected title run for actual retained-buffer Stop/reuse,
continued output and timing. The failed capture below remains preserved;
existing unchanged waveform/component evidence is reused. A05 remains open.

## Retained audio Stop reaches synchronous callback gap - 2026-10-15

The two-hour-ceiling capture is now terminal after4029.016 host seconds, with
returncode2. It reached the normal title transition and actual retained-buffer
Stop entry10751 at45,331,875 device ticks, then stopped at callback ordinal153.
The original audio DPC17838E ->1782F2 calls KeSynchronizeExecution through
18A000, using KINTERRUPT17DE78, callback177ECC and its actual driver context.
The retained kernel export8001E1D0 raises the interrupt's synchronization IRQL,
calls the one-argument routine, restores IRQL and returns through RET12.
The compiled callback bridge lacks this original service/callback transport;
the guard is not evidence of a waveform or DSP arithmetic defect.

One earlier Stop10712/GetStatus10713 pair succeeded for a different object.
It does not satisfy the retained playing object's stop/restart condition.
There is no lifecycle checkpoint or post-restart waveform acceptance. Both
partial output files and the final API journal were preserved. Do not restart
this unchanged capture. Implement and exercise the real synchronous callback
through short original-kernel fixtures before another connected title run.
Private terminal report: build/blinx/audio-retained-lifecycle-2h-20261015/capture/
run.aot-private.json; source audit in its sibling worker-review directory.

## Encoder serial acceptance reaches clock integration - 2026-10-15

Manufacturer 100381B sections 2.4/3.8.1 distinguish serial acknowledgment from
register effects: data bytes are acknowledged, while 00..06 remain read-only.
Attempted writes now leave their unknown ID/status values untouched. The actual
DA..FC command bytes are retained separately as received commands, not hardware
latches or readback. Independent review removed an initially proposed blanket
clock/FIELD guard: section1.3.6's documented clock equation and explicit
reset/sync contracts have their own known inputs. No inspected source ties
these extensions to a required digital clock consumer. Those contracts continue
to operate; command receipt itself generates no reset, clock or ready event.
This is not an unconditional inert/analog-only finding. GFX-D17 retains the
unknown interpretation and reopens on a supported read, changed profile or
observed required output consequence. Connected clock/scaling/output checks
remain mandatory. Full software reset clears the command epoch under section
1.3.4; timing reset/autoconfiguration do not erase the unresolved commands.

Original AV3 now executes its whole common/mode encoder table and the subsequent
GPIO22/24 writes. Direct calls in both AV phases stop at the actual PLL_CONTROL
read FD68050C, PC80034F09. Genuine thread calls reach the same instruction, but
shared device work first rejects the old nominal FIELD injection after encoder
programming. These are distinct real clock-integration boundaries; neither call
returns fabricated success. GPIO19 is high,22/24 low, as the original code
specifies. A later FIELD callback now preserves an already recorded first fault.

The affected checks cover normal SMBus status/IRQ retirement, read-only-write
preservation, all18 extension receipts and independently specified clock/reset
behavior. Eight original transport/AV/encoder cases cover both call routes.
These tests pass in0.438s for the encoder suite and1.094s for the eight original
cases. They
establish the advanced execution boundary and existing component behavior, not
completed mode initialization. Private final evidence is
build/blinx/encoder-serial-20261015/even-final/validation.aot-private.json.
No hardware operation or live audio disk access was needed.

Next: replace the selected route's nominal FIELD/frame phase with actual board
clock/sync ownership, retaining the independent encoder and GPU timing models.
Unknown extension effects have the explicit consumer-based disposition above;
they are not a new independent research gate. G02-G05 remain open.

The portable rational source-clock accumulator is also prepared: explicit owner,
absolute timestamp/frequency, exact fractional output-edge carry and old-rate
advancement before a rate change. Independent rational, chunking and rejection
checks pass under strict GCC and MSVC C++17 builds; root reviewed its arithmetic
and state-preservation contracts. It does not yet select a display source or
connect FP counters/encoder sync. That integration remains the next G02 work.

The encoder also exposes a non-mutating clock observation for that scheduler:
unknown, unsupported, configured-but-undriven or driven. Incomplete original
programming can be observed without faulting before a consumer needs the clock.
The existing consuming query still rejects unavailable clocks; neither query
creates elapsed time or output edges. The focused encoder suite passes in0.06s,
including unchanged output on unknown/unsupported observations and preserved
consumer faults. Evidence: build/blinx/encoder-clock-observation-20261015/.

## Measured DEBUG storage and original encoder continuation - 2026-10-15

The maintained FP owner now admits the two independently observed whole DEBUG
words as configuration storage, preserving opaque bits17/29. Fresh unknown
reads, other words and unsupported widths remain guarded. Writes generate no
activation, bank copy or AV completion. Independent source review found no
defect; the FP/AV device suites pass in4.438s including the existing timer oracle.

Actual original AV3 direct and thread calls in both initial AV phases cross
that write and reach encoder programming. Their first new guard was reserved
72=00. Conexant100381B table2-1 marks72 reserved, table2-5 requires zero for
reserved fields and the manufacturer's examples explicitly write72=00. The
encoder now accepts only that zero write without inventing readback, resetting
timing or changing other state. Nonzero writes and reads remain guarded. Faults
now report the offending register/value so another capture is not needed just
to identify the stopped transaction.

The final six original transport/AV checks pass in6.218s and the encoder suite
passes in1.219s. Both original calling routes and both phases stop at the same
next write, DA=0F, with caller result canaries intact. Original return and output
remain unqualified. Private final validation is
build/blinx/fp-debug-storage-20261015/reserved-zero/boundary-validation.aot-private.json.

A single source audit extracted all85 common/mode encoder writes, with actual
page walks and unchanged RAM identity. Apart from reserved72, all63 documented
entries pass existing value guards. The remaining table entries are18 writes
in DA..FC and three writes00=00. Their details are retained in
build/blinx/encoder-table-audit-20261015/tables.aot-private.json,
SHA256 dc4973631fd08fa1fdf9fa68690ee2ded360a89a9a3414cea8ab901be06e792c.
DA..FC are absent from the retained public register map; Macrovision attribution
is unproven. Register00 is read-only identity, not writable storage. The later
original return helper also enables documented WSS fields at60 and writes62/64.
Resolve these as a batch within G02; no separate family or acceptance matrix
is added. Encoder clock/sync, real retrace and connected graphics stay required.

The clock-source audit permits a declared initialized GPU routing profile as
an input before first guest access, independently of any D4 encoder contents.
It does not justify importing a warm encoder, claiming a reset default or
generating ready/retrace events from profile selection. The reached CLKO rate,
display-clock division/gating and status/sync mapping remain real implementation
work. The old boot trace's PLL_CONTROL read0 was unmodelled and is not hardware
baseline evidence. No PLL-routing profile is added by this storage change.

## Physical FP sequence observation - 2026-10-15

GFX-D16 records isolated DEBUG-bit naming/combinations and exact field-publication
edge questions. Required scaling, configured clocks, retrace, original encoder
startup and connected output remain G02/G05. The measured register-storage
contract preserves both original DEBUG words without assigning unknown bits an
activation, ready, completion or bank-copy effect. Device tests cover fresh
unknown state, both observed words, invalid writes, unchanged captured banks,
size/alignment and no AV completion from DEBUG writes.

The prepared G02 differential sequence completed on the Xbox with all 20 planned
writes and verified restoration. The sampled alternate HSYNC marker was 756
before the AV request, after acknowledgment, after three retrace pairs and after
the final DEBUG word plus three pairs. Both pending AV directions were observed.
Final programmed/alternate HSYNC are 755, DEBUG21101100, CONTROL10100111 and
AV11, matching the initial context. No unexpected readback or retry occurred.

This establishes that AV acknowledgment was not required for this sampled HSYNC
publication in the current mode. It does not establish whether DEBUG caused it,
an active whole-bank copy, exact latch edges or the complete original AV3 route.
The prior HVALID observation remains distinct. A matched HSYNC control with
DEBUG/AV unchanged subsequently completed with 10 stores and restored all
readbacks. Alternate HSYNC was 756 before and after three retrace pairs. This
shows publication without fresh DEBUG/AV writes in the already exercised mode;
it cannot exclude persistent effects from the earlier DEBUG sequence or prove
cold-start behavior. No individual unknown DEBUG-bit meaning is inferred.

Original AV3 submits its request at 80035052, writes CRTC28 at 8003506D, and
waits for three real low-to-high retrace pairs at 8003507B..89. It does not poll
the alternate timing bank or a post-submission acknowledgment. The next required
work is the actual configured clock/timing/retrace producer and encoder route.
Exact whole-bank publication timing is not a new gate without a supported
consumer and forward-progress/output/lifetime consequence.

Executed payload SHA256 is
bd1b53be486b4cfef204cdf075c35400e516e8192d4407e2ad9a91df79ae256f;
source SHA256 is
addae831bec2315432fcb263bfcba4ac549c316639250d9ed447f34f63a43c4c,
retained by companion commit c9b1947. Private replies and restoration summary
are in the companion results/fp-debug-sequence-live-20261015/observation.
The matched-control payload SHA256 is
93cff1c22c3b0110fe662a1c8dbbeb22d25d26c87510c215d728514067c69f44;
its receipt is in results/fp-sync-control-live-20261015/observation. Fourteen
affected compiled-byte/host tests pass, including exact stores, ABI, each-read
failure stops, deadlines and no retries. Independent review's prior-history
limitation is incorporated. Both physical observations completed successfully.
G02-G05 remain open; family disposition is still 6/9. The muted two-hour audio
lifecycle capture continues separately; its initial capacity-closed DSP trace
matches the already accepted trace exactly and needs no repeated analysis.

## Lifecycle capture limit and next graphics experiment - 2026-10-15

The retained-buffer audio capture finalized cleanly at its3600000ms host ceiling
(3608.312s supervisor duration), with no guards and8647 completed calls. Its
finalized journal has five Plays and no Stop:34.249 guest seconds did not reach
the normal title timeout. Both output streams retain5536768 PCM bytes and5407
stamps. This is preserved evidence, not A05 acceptance. Do not restart the same
one-hour configuration. The source timer counts1440 title updates, not a fixed
guest duration. The observer now accepts a finite ceiling up to7200000ms, with
four existing duration/cooperative-stop/error cases passing. Root reviewed the
worker's parser/test change and runner difference: only the host ceiling and
matching supervisor fallback change. Guest CPU, input, clocks and audio stages
are unchanged; the other five audio-library members and retained AOT inputs
match. The fresh muted `audio-retained-lifecycle-2h-20261015/capture` run is now
active, four logical CPUs at Normal priority, with the existing automatic
lifecycle/output stop and owner-game detector. Two hours is a maximum, not a
required capture length. It owns the same isolated persistent HDD exclusively.

The terminal capture's complete framing/cadence check passes: analog/digital
payloads and epochs match, and its initial DSP trace is bit-identical to the
already accepted trace. Reuse that evidence. Private terminal report SHA256 is
034a53871dd70049cd7c82bd16e72b45d446680472de745ccf064be1a26d2f29;
new executable SHA256 is3fe661aac1aef4ec5b5375086ce0f292ff576cb1318b9227d493741f489633a4.

One bounded read-only observation of the owned Windows title process confirmed
the normal selector50A590=0 and timeout counter9E7F9C=1090 near the ceiling.
It used only process query/read access, with pinned PID/start/executable and
guest allocation/XBE/code identity; no writes, debugger, suspension or guest
time change. Sequential samples are diagnostic, not lifecycle acceptance.
The worker traced normal phase4 through r01s01demo.dat stage0 to round1.adx and
prepared its independent decoded source. An earlier replay.adx attribution was
corrected before comparison: that load belongs to phase10. No post-restart
waveform comparison is possible without the actual transition.

G02's next offline preparation is a differential current-mode DEBUG sequence
with a measured HSYNC755->756->755 marker. The companion probe's
`docs/FP-DEBUG-SEQUENCE.md` specifies exact stores, bounded AV/retrace waits,
planned restoration and immediate stop after unexpected readback. Nine actual
compiled-payload/host tests and five unchanged bank regressions pass; companion
commit `e8d7711` retains the preparation. No console operation has occurred.
Independent review confirms the original additionally toggles CRTC28 from0 to83
for format12 and later resets the encoder. The differential experiment omits
those operations: an unchanged alternate field cannot disprove original startup
activation. The DEBUG words change scaling plus unresolved bit17 together;
neither sets clock-power-down bit28. Families remain6/9; no precision deferral
or functional guard is removed by this preparation.

## Quiescent AV protocol and original initializer - 2026-10-15

G02 now has a clock-owned FD6806A0 protocol in the maintained NV2A, selected by
the initialized-kernel graphics route before its first guest access. Physical
evidence supplies idle00/11, pending01/10 and completion in both directions.
Original AV3 tests only bit0/bit4 equality at80034CE3 and later emits the saved
phase's complement at80035052. Exchanging both phase bits preserves its work.
Canonical idle0 therefore represents this explicit quiescent startup profile;
it is not a measured reset default. Initialization is once per owner, with the
before-first-access requirement satisfied by the native creation call site.

Pending acknowledgment is serviced by actual NV2A source-clock advancement at
a declared nominal next edge. Reads, frozen time, host frame callbacks and
PTIMER ratio changes do not complete a request. This does not copy FP banks or
activate video. GFX-D15 retains the precise service edge/latency and raw reset
phase, with a supported-consumer reopening trigger. FP_DEBUG, active timing,
encoder programming and genuine retrace remain required functionality.

Both original AV3 call routes (direct service and genuine original thread) now
cross the initial handshake read in both idle phases and stop at the actual
FD680880 write of21121111. All four preserve caller result canaries and the
actual GPIO19 output. They do not reach the later original request submission.
The maintained MMIO tests separately cover both request directions, passive
pending reads, clock service, retained distinct FP banks and rejected writes;
the existing PTIMER oracle is reused. Two device suites pass in0.62s and six
original transport/AV cases pass in1.33s. Private receipts:
`build/blinx/av-quiescent-20261015/validation.aot-private.json` and
`original-validation.aot-private.json`. One Sol6.1 worker reviewed source and
ownership independently; root reviewed its findings and ran these checks.

The source audit also identifies a genuine producer for the retained8A encoder:
original AV3 writes BA80,BA3F,6C46, executes its original delay, writes6CC6 and
programs its configuration table before its first later8A read. Execute this
exported initializer on the same CPU/GPU/SMBus owners before early title AV
queries once its remaining device semantics work. The private INIT wrapper is
not an alternative: its driver-global pointer is zero in the pinned capture.
Do not fabricate that driver structure, substitute D4 state, seed the saved-mode
global or call this literal cold-boot continuation. The retained AV disassembly
SHA256 is15c06de72c227d275087fc787da724e66d95abcb58087fdacd13ec4aa834e324;
the RAM identity and original instruction/table evidence remain unchanged.

The existing muted audio capture continues in
`build/blinx/audio-retained-lifecycle-20261015/capture`; it is not replaced by
these graphics checks. Its published stage prefix is available for early review.
The worker prepared a final journal/PCM lifecycle review, explicitly retaining
the need to validate actual post-restart output. Families remain6/9.

## Corrected title ordering checkpoint - 2026-10-15

The changed muted title at `build/blinx/audio-gp-ep-order-title-20261015/`
finalized at its 12.5-guest-second observation checkpoint after 768.875 host
seconds, exit zero and no guards. The executable SHA256 is
`61699ae4d4268c6a34a0ebeca2748310e08b09bab5d098c12d82e0ad8dab6216`.
The original journal contains 1,061 completed calls, 144 successful neutral input
reads and five Play calls, with no Stop. This checkpoint does not retire the
guest's retained buffers or establish the normal Stop/reuse lifecycle.

All 4,334 comparable EP input groups now contain consecutive producer quanta;
the newest chunk is one or two quanta old. Eleven incomplete prefix reads are
excluded explicitly. Both streams contain 1,367,040 identical PCM bytes, with
1,335 contiguous codec stamps exactly on their absolute 6,000-PIT-tick edges.
The current stage trace has 3,161 exact send-to-MIX banks, 18,966 exact main GP
writes and 393 independently reproduced EP blocks and state loads. The latter
starts from one observed input-state anchor after two incomplete prefix programs;
every subsequent state is derived independently. The trace SHA256 is
`32542a56061b8f7672cc83532afee13ad691ea84cbbdfa7898ba1b90e9d8902a`.

The fixed-gain, prescribed-pitch independent title-music comparison improves
substantially after the chronology fix, but initially misses the existing
full-band gate. All three half-second windows meet RMSE <= 0.02. Two right-channel
correlations are 0.98727 and 0.98173, below 0.99. Do not convert a successful
ordering check or a small absolute error into full waveform acceptance.

Saved-data localization finds no source cursor discontinuity; the original ADX
source still agrees with the independent decoder. Over 99.99% of the source-rate
difference power lies above 16 kHz. Applying the same fixed 16 kHz diagnostic
filter to both signals, with zero relative lag and no fitted gain/rate, yields
correlations above 0.999999 and RMSE below 0.000085. The maintained converter uses
declared `SRC_SINC_FASTEST`, whereas the independent oracle uses SciPy's default
polyphase filter. This localizes a plausible filter-response difference; it does
not waive the full-band gate or prove silicon interpolation. The independent
nominal-kernel reconstruction below resolves this difference without another run.

The authorized Sol6.1 worker independently reconstructs the declared nominal
kernel as a direct symmetric convolution from the pinned float32 coefficient
table. It uses prescribed pitch -501, zero relative lag and the actual observed
source-zero prefix, with no production resampler API or padding of unavailable
positive-time input. All 101,184 captured stereo rate frames match bit-for-bit.
Changed-pitch and reordered-source controls fail. Root reviewed the implementation,
verified all 14 input pins, checked that the coefficient/source files match the
actual build dependency, and independently reproduced the exact rate-frame bytes.

The existing independent FFmpeg ADX decode through this nominal kernel, fixed
headroom/volume and independently qualified EP transform now passes all three
full-band windows under the unchanged RMSE <= 0.02 / correlation >= 0.99 gates.
Worst RMSE is 0.0001381; minimum correlation is 0.9999657. The single previously
declared alignment stays at frame 204384; gain, rate and filter response are not
fitted to the capture. This accepts representative connected output under the
existing nominal-rate contract. It establishes neither silicon-exact
interpolation nor the missing normal Stop/reuse transition. AUDIO-D15 records
the replaceable interpolation precision question and its reopening trigger.
The independently authored check completes in 1.44s. Its private report is
`kernel-review-worker/report.aot-private.json`, SHA256
`0782c49edb8eb1bc0b60e2e68065b8a18a05379bff9c46f19dec79c259144e02`.

Receipts are `capture-review.aot-private.json`,
`stage-review/stage-review.aot-private.json`,
`title-music-headroom-review.aot-private.json` and
`rate-band-diagnostic.aot-private.json` beside the capture. All three main
analyses completed together in under nine seconds. The title is terminal;
no unchanged rerun is needed to investigate this resolved residual. Audio remains
open for actual normal Stop/reuse and continued output through that transition.
Families remain 6/9; the queues now retain 35 IDs / 34 deferred scopes.

`native_audio_call_report.retained_buffer_checkpoint` now supplies the next
capture's review trigger: successful original Play/Stop/Play on the same retained
buffer followed by two seconds of both output streams. Final Release is not a
prerequisite. Failed calls, a different buffer, a released/recycled pointer,
insufficient output or another Stop cannot trigger it; regressing output counters
are rejected. Eight observer tests pass in 0.069s. Replaying the finalized 1,061-call
journal leaves its report unchanged and correctly produces no lifecycle trigger.
This is an observation stopping rule, not a substitute for reviewing the output
and lifetime evidence. The old private Release-based checkpoint is superseded.

That checkpoint's G02 startup question is refined by the quiescent-protocol
milestone above. Original initialization still requires FP_DEBUG and real
activation/retrace; existing already-active D4 observations cannot substitute
for the retained8A producer. G03-G05 remain downstream, and hardware stays in
the final phase.

## GP/EP ordering and original startup branch - 2026-10-15

The changed `audio-title-stage-transition-20261015` capture finalized cleanly at
14.518 guest seconds:1,765 completed calls,245 successful original input reads,
53 actual Start reads, six Play calls and no Stop. Both output streams contain
1,748,992 PCM bytes. It is a bounded observation, not normal guest retirement.
The stage journal stopped at its64MiB capacity, retaining complete records over
about2.1 seconds; the prepared four-second description was an upper bound, not
the duration actually retained. Its immutable trace SHA256 is
`203ee4f9a024a43e21ff0f3beb66a6464c60ab95c6b52a7405b77d4eb6077b69`.

This trace binds the current title source to88,200 independent FFmpeg ADX frames
at correlation0.999985/0.999989, with no source cursor discontinuity. All3,161
observed send-to-MIX banks and18,966 main GP writes match their preceding stage.
However, every one of4,323 comparable EP ring reads mixes producer quanta: the
first chunk is16 quanta newer than the following chunk. The earlier trace shows
the same defect in3,069 reads. Equality to current RAM hid this chronological
error. This is a required output/ownership defect, not a precision deferral.
The old zero-state EP arithmetic oracle cannot certify this later-starting
window without its missing initial context; its independent component proof
remains reusable. No program mismatch is inferred from that prefix limitation.

A short fixture running original GP/EP setup reproduces the first read as
`[1,0,0,0,0,0,0,0]` and later reads as `[57,42,43,44,45,46,47,48]`.
The device clock now waits for eight completed32-sample producer slots before
admitting the256-sample consumer, while GP fills the opposite ring half. Codec
cadence is unchanged. SE pause retains partial groups; normal paired bootstrap
starts a fresh group. A missing EP request and unpaired live ring restart retain
explicit guards. Idle-voice FE trapping preserves the completed unread half.
This is a nominal coherent phase, not a measurement of silicon sub-frame phase.
No samples, DSP completion or guest time are synthesized, and default HLE is
unchanged. Private evidence is in `audio-order-fixture-20261015/`; the original
three-configuration, two-generation timeout/reentry fixture and chronological
read checks replace full title startups during this fix's feedback loop.
All54 affected device/adapter checks pass; the adapter's old absolute-phase
expectation was updated after its first run exposed that stale assertion.
The original three-configuration fixture verifies328 complete consecutive EP
reads over two reset/reentry generations and original free. Baseline traces are
rejected by the same chronology checker. The final component receipt is
`audio-order-fixture-20261015/qualified-component.aot-private.json`, SHA256
`5593f83dce284ffd6ef03f8d80861e52d59aafb294a5783adc90a92418bec669`.

The diagnostic recorder now publishes its immutable prefix immediately on
capacity/window completion, while the same title continues running. Four storage
cases check exact records, early independent readability and idempotent later
finish. Future lifecycle runs can therefore get early stage feedback without
repeating startup. This changes host observation only; the completed ordering
checkpoint retained its already-built recorder and finalized normally.

One authorized Sol6.1 worker independently traced the startup/lifecycle source;
root checked the source hashes and relevant original instructions. Neutral
controller insertion sets the edge consumed by intro predicate18110. Dispatcher
188A0 then cleans up the intro and selects title phase1. Thus hotplug legitimately
skips the opening movies. The first Start opens the title menu and need not stop
music. The normal title-exit route reaches wrapper102890, which calls original
Stop/status polling and retains the buffer. Actual Stop followed by Play and
correct continued output is the required lifecycle; final Release is unnecessary
for this path given the accepted destruction/reset components. Within attract,
the source-proven transition uses the title timeout of0x5A0 updates; do not keep
repeating Start-only captures expecting Stop.

Initial-connected USB/audio calibration and opening-movie original-audio A/V
coverage remain excluded additional startup coverage under the one-profile rule.
The delayed-hotplug profile still needs corrected connected output, timing and
actual normal Stop/reuse evidence. These findings close no family:6/9 remains.

## Reuse saved observations and stop on events - 2026-10-15

Reviewing the saved646.203s capture requires no new title execution. Its92 video
records identify85 distinctive frames of `h_title02a`, source frames0..52. This
is the silent title-background video, not an opening movie. Relative source-PTS
error is16ms at p95 and33ms maximum, inside the existing two-frame allowance.
The110 original position queries cover four wraps of the actual stereo PCM16
65536-byte ring; every write cursor has the original128-byte lead. CBO includes
prefetch, so its residual is reported without inventing a silicon timing limit.
`build/blinx/audio-hotplug-service-stack-20261015/connected-prefix-review.aot-private.json`
SHA256 `f06270bd4aea599609bd7db8277d7e5fa91289a0260e8c92da644f1afc0372af`
binds the review and original constructor/caller evidence. It does not establish
opening-movie A/V synchronization or a normal voice lifecycle.

The remaining measurement gap is actual game input receipt. The optional journal
now observes the already-reviewed108-byte XDK4831 `XInputGetState` body at183B28,
verifies its complete source hash, and records its22-byte state only after a
successful original return. Failed calls do not read an output. Caller and entry
stack are included in the journal and paired by the report. Existing block hooks,
guest clocks, DSP execution and default HLE processing are unchanged. The retained
607-call journal still parses; authored successful input and changed-caller checks
validate the report, not live input correctness. The changed title builds with
retained AOT objects, recompiling only runtime/audio-binding/input-host sources.

One changed muted capture at `build/blinx/audio-neutral-lifecycle-20261015/`
keeps the actual controller neutral after its9s hotplug and records input, audio
and video together. Its runner checks the journal every five host seconds and
requests cooperative finalization after a successful Play/Stop/final Release,
a later successful Play, and two seconds of additional output on both streams.
This is a review checkpoint, never family acceptance or synthetic guest teardown.
Six host-observer checks take0.60s: the saved prefix, failed Stop, nonfinal Release,
missing later Play and an insufficient stream cannot trigger it. The hour ceiling
is a safety bound, not a required capture duration; inspect progress and stop on
a measurement blocker. Do not repeat this sequence unchanged or require a title
transition that the actual bounded profile does not exercise. Original retirement
component evidence remains reusable. This capture is still pending acceptance.
Its first successful original input read occurs at9.131 guest seconds, followed
by further successful neutral-state reads. Thus host delivery and original game
receipt are now distinguished in the live route. The immutable
`audio-neutral-lifecycle-20261015/input-feedback-progress.aot-private.json`
records this prefix, build/source identities and observer checks; it is not a
completed title capture or A05 closure.

A separate subsecond analysis of the retained output against a two-second FFmpeg
decode of `title.adx` does not meet the existing waveform limits. That candidate
comparison uses the observed pitch and -300 volume, but does not yet bind the
complete source-to-DSP configuration. It is diagnostic evidence requiring that
binding, not proof of a new device defect or grounds for another title run. The
result and the corrected capture-framing analysis remain private beside the
saved prefix. No new deferral or family closure;6/9 remains.

The saved title-music comparison now includes the independently qualified
selector2 PCM transform and original device-wide submix headroom. Constructor
176D72 initializes bins0..30 to headroom1 and bin31 to0; startup178646 publishes
those settings through178191. The ordinary stereo bins therefore contribute
half their input before the observed -300 volume. Omitting this original default
caused the earlier factor-of-two level discrepancy. This corrects the oracle,
not the emulator, and no fitted gain or relaxed threshold is used. With both
stages included, three half-second windows have RMSE0.0081..0.0155 but correlation
0.834..0.899, below the existing0.99 requirement. The exact current title source
and DSP-stage binding still need resolution; this is not a proven device defect.
The private `title-headroom-source` and `title-music-headroom-review` reports
retain the source hashes and comparison. Separate extraction of88,200 actual
input frames from the accepted Q03 reference route agrees with FFmpeg's ADX
decode at correlation0.999985/0.999989 and RMSE0.000459/0.000399. That source check
does not establish the current original-audio route's input or final output.

Saved-evidence analysis also exposed many small disk reads under contention.
The reference DMA reader now batches them in a1MiB buffer. On4,096 real retained
records (2,251,216 bytes), old and new readers return identical complete reports,
source bytes and rows; both reject truncated headers and payloads. The private
`buffer-reader-check.aot-private.json` records this check. Cached timings are not
a controlled speed comparison. No guest timing, processing, HLE selection or
acceptance condition changes, and these analyses require no title rerun.

## Idle interrupt feedback and service-stack ownership - 2026-10-15

The hotplug guard has a short reproducer: an actual IRQ interrupts original
KeSetTimer while an idle DPC lends the CPU a guarded service stack. Before the
fix, the owner and idle bounds still match, but the frame is `e8007ea8`, outside
the idle stack. The original guard rejects it at ordinal149. The first isolated
reproduction took11.58s under load; the corrected case takes0.58s. Future changes
to this boundary use that case before another connected title capture.

Each original service now records its actual caller for the lifetime of its
existing bridge stack. An interrupt on that stack requires the current service
depth, the same caller and an entirely in-bounds frame above its guard page.
Idle metadata checks still apply. No frame moves, fabricated guest allocation,
clock shortcut or changed HLE processing is involved. The positive case also
executes a nested original query into native stack spills; a separate negative
case changes idle metadata and still stops before the nested callback/result.

Seven affected callback/idle-stack/page-sharing checks pass in2.00s. Review also
found an overstrict fixture assertion: original KeInsertQueueDpc returns its
Boolean through SETE AL, leaving upper EAX unspecified. The fixture now compares
the defined byte. The first broad check failure, diagnostic rerun and five
bounded repeats remain retained; the first failure lacked its result diagnostic,
so it is not claimed as a proven instance of that assertion defect. Both the
original-scheduling and mixed-scheduling fixture executables build. A single
muted title check uses the same hotplug/Start/release inputs, followed by an
unchanged neutral sample one guest second later to request cooperative stop.
It finishes after646.203 host seconds with no runtime guard. All five input
samples are delivered through11.5 guest seconds; movie-buffer processing is
reached. Both outputs retain1,169,408 bytes in1142 identically timed blocks,
including316,236 nonzero bytes, with zero common-payload differences. The
finalized journal records607 completed original calls and no pending call.
It includes232 Lock/Unlock pairs and110 position queries; no successful Stop,
restart, final release or device fence appears. Input delivery is not claimed
as successful movie interruption or normal audio teardown.

`build/blinx/audio-dpc-fence-20261015/idle-service-qualified-component.aot-private.json`
binds the sources, native artifacts, before/after checks and actual title prefix,
SHA256 `8525ea34c80168a827c2d695e873092a152a8bea2e8aa5aa173df3cdf7f46d8c`.
The title is terminal, with both partial outputs finalized and device owners
retained as designed. `.113.36` closes this stack-boundary defect. Reuse the
saved movie/audio/API observations for the next A05 timing/lifecycle review;
another unchanged capture is not justified. This does not close A05 or change
the6/9 family count. No hardware operation or new deferral.

## Audio feedback loop and connected hotplug prefix - 2026-10-15

The unchanged-binary delayed-hotplug capture finished after1806.453 host seconds
without a runtime guard. Actual input records show connection at9 guest seconds,
Start at10 and release at10.5. Analog and digital each contain3,562,496 bytes in
3479 identically timed blocks, with2,520,025 nonzero bytes and zero differences
between their common payloads. This is18.555 seconds of captured stereo PCM,
not30 minutes of guest playback. Prior independent DSP arithmetic/source-prefix
evidence is reused; matching the two transports does not replace that evidence.
`build/blinx/audio-dpc-fence-20261015/title-hotplug/analysis.aot-private.json`
retains the result. Its partial host cutoff does not establish guest teardown.

The owner requested faster feedback. Existing first410s timing observations
attribute254s inclusively to device-clock work and111s to original CPU execution;
these categories overlap and are not an exclusive performance breakdown.
Device work includes the actual GP/EP instruction execution. No guest-clock
speedup, skipped DSP polling or substituted completion is admitted to shorten
the run. The55 prior focused checks took26.54s; they remain the implementation
feedback loop, with only affected checks rerun after a change.

Captures now support an external early-stop request that persists both partial
audio outputs and the optional API journal at the existing serialized work
boundary. Device owners remain live; a request is never guest shutdown or a
qualification result. The initial six affected checks passed in3.997s total,
including persistence failure and pending ownership. A real title request
ended after125.016s and preserved both176,128-byte streams. Early inspection
found that a compiled-wrapper logger observed no calls: this profile executes
audio on the original CPU. That failed measurement is retained under
`build/blinx/audio-connected-acceptance-20261015/`; it is not acceptance.

The corrected recorder uses the existing original-CPU block hook, source-bound
entry points and thread/caller/cleaned-stack return pairing. It introduces no
instruction hook, extra block splitting or clock opportunity. A short original
CPU fixture executes repeated stdcall bodies with observations disabled/enabled;
return values, original stack cleanup and guest time agree, while service reentry
and MMIO reads remain denied. The fixture initially lacked its established
original child-priority setup; restoring that setup resolves the unrelated
captured storage-thread dispatch failure. Compiled audio wrapper changes were
removed. A5s title preflight then rejected the recorder's initial20-byte argument
limit because the actual Lock API consumes32 bytes. The bounded transport now
supports eight argument words, and the short fixture exercises that full ABI.
The final affected set passes9/9 checks in24.00s. An earlier positive-case20s
wall-time limit expired during concurrent work; the same case passes in1.93s
in the final set, with no behavioral workaround or relaxed timeout.
The actual title recorder, PCM/SPDIF and movie-luma timing are collected
together; verbose diagnostic tracing is disabled outside diagnosis. Inspect
the live journal before allowing a long observation and stop once the needed
evidence is present or a measurement blocker is found. The duration limit is
a safety ceiling, not a required testing interval. No further full-title rerun
is justified without a specific changed behavior or missing acceptance event.

The corrected live title prefix already records31 completed original calls,
including Play, CreateSoundBuffer, Lock/Unlock and DownloadEffectsImage, with
both output counters advancing. Successful stop/restart/final release and a
published fence have not yet appeared in this retained prefix. The report
explicitly leaves those milestones false. A mutated return from another thread
is rejected. `.113.35` closes the diagnostic feedback controls, not A05.
`build/blinx/audio-dpc-fence-20261015/feedback-qualified-controls.aot-private.json`
binds the retained sources, native artifacts, nine checks and actual journal
prefix, SHA256
`7692358eaa658490af38f3a19e9fc9afc53c71183099bfc6fce0bd1f9cf42147`.
The corrected capture is `build/blinx/audio-connected-live32-20261015/`.
It subsequently stops after402.797 host seconds at actual controller connection
(9 guest seconds), with `device LLE idle callback stack owner or bounds changed`.
The run is terminal, not waiting; do not restart it unchanged. Its31-call journal
prefix and both partial audio captures are retained. The previously accepted
hotplug run reached further, so this guard requires diagnosis before attributing
it to a changed component or accepting the connected lifecycle. The next step
is the existing focused idle-IRQ/DPC stack fixture and original USB callback
path. Failure-only diagnostics now report the expected/actual idle owner,
stack bounds and candidate frame; admission and the failure condition are
unchanged. No extra title run is needed merely to recheck the capture controls.

Families remain6/9. A05 still needs actual connected output/timing and normal
stop/reuse/reentry acceptance; initial-connected USB calibration remains open.
GPU/graphics G02-G05 are unchanged. No new hardware operation or deferral.

## Original queued audio DPC and serialized FE fence - 2026-10-15

The original kernel now dispatches its actual queued17838E DPC through
177E53/177DF0 and the voice completion consumer. Mono/stereo natural completion,
explicit stop and a withheld-service timeout case pass. The CPU writes only its
complement sentinel; real device service writes the token to the original owned
page. The timeout case returns without publication and retains that page until
later service. All four cases restore original allocation statistics.

Admission composes the retained independent physical token observation with the
original consumer/control instructions and supported FE/VP methods that finish
synchronously on the sole device owner. Their source, MIX and notification writes
precede submission. A subsequent advancing device tick publishes the token;
repeated reads or an unchanged tick do not. Independent GP/EP computation on
separate MIX/scratch owners is neither completed nor retired by this fence.
This is a functional composition, not a new hardware measurement of arbitrary
active queues or silicon latency. Held/queued unknown methods, pending retirement
NOTIFY, unresolved traps and work reentry remain guarded. The original ISR can
read and release its own trap without losing the established device profile.

All55 affected native/device checks pass, including real preceding SGE writes,
source/publication failures, device-clock service, three native owner lifetimes
and pending-page retention on rejected close. The final original fixture passes
4/4 after correcting an assertion placed in its pre-DPC branch; the failed script
result is retained. Default HLE processing and its accepted finite-output,
completion and ownership comparisons are unchanged and reused.

This closes the bounded FE fence/control component `.115` and completes isolated
A04 acceptance alongside the prior original timeout retirement/reentry evidence.
A05 still requires connected title output/timing and normal stop/reuse/reentry;
the family count remains6/9. One changed muted title check uses real controller
Start/release at10/10.5 guest seconds since first poll. Its host observation
cutoff cannot substitute for guest teardown; no result is claimed here.

Private proof `build/blinx/audio-dpc-fence-20261015/qualified-component.aot-private.json`
binds28 source/binary/report/prior-evidence identities, SHA256
`d51c2cc5d5173de851c31708402506fb43daeb09b4035982938a59abcbb0c644`.
No Xbox operation or additional precision matrix was needed.

The first connected-controller title attempt stops after71.234 host seconds,
before playback: original USB DPC1890A2 calls188A8E and reads FE80200C before
the original audio initialization establishes XGSCNT. This is USB/audio clock
calibration: it compares48 times the USB frame number with the audio counter,
retains an offset and may adjust the OHCI frame interval. The early read cannot
be silently replaced with zero or granted the prior generic uptime behavior.
`title/run.aot-private.json` and `usb-counter.aot-private.json` retain the actual
failure and original source spans; no family acceptance follows from this run.

A separate affected check reuses the exact binary with the previously working
disconnected-controller startup, actual hotplug at9 guest seconds and Start/
release at10/10.5 seconds. It does not qualify a controller connected at entry.
The inherited counter/calibration profile remains an explicit A05 scope question
until its consumer consequences are resolved; no new deferral is silently granted.
Evidence is under `build/blinx/audio-dpc-fence-20261015/title-hotplug/`. Its
30-minute host observer is bounded and muted; no result is claimed yet.

## Analog cyclic output and original codec retirement - 2026-10-15

Independent physical one-node4096-word and two-node1024/2048-word analog
rings both repeat beyond LVI without software refill. Each bounded observation
retains64 timestamped samples, SR0C, PIV=CIV and6112 consumed words. Explicit
stop preserves the remaining count/cursor; reset retires the descriptor before
the original base is restored. Both streams are stopped. The two12KiB diagnostic
owners remain pinned until a separate recovery; no unexpected write occurred.
Probe `docs/PCM-RING.md` records the sanitized result and physical evidence hash.

The maintained analog model now wraps at LVI, matching the independently
established digital behavior. Pause preserves the prefetched descriptor; reset
is required before replacing its backing. Output rejection, source failures,
completion ordering and zero-length descriptor guards remain explicit.
All51 affected native/device checks pass. Three unchanged original codec
helper lifetimes each transport18432 analog bytes while digital pauses/restarts;
three original ISR/DPC lifetimes each transport16384 bytes on both outputs,
acknowledge real completion, reject duplicate retirement, stop/restart, and
release all original allocations through original destructors. The helper
fixture retains its explicit single-CPU kernel queue/synchronization floor;
it does not replace the separate APU DPC FEMEM requirement.

Evidence: `build/blinx/audio-pcm-cyclic-20261015/streams-bound/`, `irq-bound/`
and `ctest-final.aot-private.log`. All3 original DSP timeout/reentry cases also
pass against the rebuilt transport in `dsp-bound/`. The changed muted title
finishes its609.110s bounded observation without a runtime guard. Both outputs
contain1,389,568 bytes in1357 identical-timestamp blocks, including582,566
nonzero bytes. Analog and digital payloads match exactly;387 blocks inside
the retained stage window also match the actual EP FIFO0 writes with zero byte
differences. The initial analyzer compared the streams' intentionally different
format tags; explicit analog0/digital02000000 checks and comparison of their
common timestamp fields resolve that observer-only mismatch without another run.
This closes `.113.34`'s cyclic traversal/output/codec-lifecycle component.
The journal reached its64MiB capacity; this partial observation does not close
whole-attract output/timing, normal title teardown or APU DPC ordering in A04/A05.
The family count stays6/9. Zero-length rings, live LVI reconfiguration
and precise AC-link phase remain guarded or deferred with reopening triggers;
no HLE processing or accepted appearance changed.

## Original timeout retirement now passes all configurations - 2026-10-15

The selector2 stop is resolved for the original transfer-idle shutdown route.
The previous guard conflated stopping a DSP computation with completing it.
The pinned independent [reset implementation](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/dsp/gp_ep.c#L251)
resets the processor when reset is asserted, without requiring another ready
request. The retained physical configured-reset comparison independently
establishes stopping/rebootstrap of running processors with no DMA. Combining
those contracts supports stopping computation once memory work is idle; this
is a source-supported composition, not a new silicon measurement of every
mid-frame reset state.

Admission is restricted to the actual retirement sequence: stopped setup,
retained notification owner, cleared event/transfer work, FE control1 and halted
mode, disconnected voice lists and GP0 before EP1. Pending DMA, unacknowledged
events, an incomplete control sequence and unrelated reset remain guarded.
EP reset retains the request latch instead of fabricating a ready1, and stops
the same CPU before RAM can be released. No extra clock advance is introduced.

All three unchanged original configurations now pass two processed generations,
the actual668us timeout, GP0/EP1 holds, warm bootstrap and original cleanup.
Selector2 has request0 before its hold; new startup reaches its own real C4
request. All18 affected regressions pass, including actual authored computation
interruption and the missing-FE-halt rejection. The prior drained route retains
its unchanged acceptance evidence. No new hardware experiment or HLE change was
needed for this correction.

Evidence: `build/blinx/audio-compute-retirement-20261015/`. This completes the
isolated original retirement/reentry component of A04. DPC FEMEM completion and
connected audio output/retirement remain required. Earlier NOTIFY completion
no longer blocks this stopped-codec component; its full-profile deferral still
depends on connected output acceptance. Families remain6/9.

## Original notification-timeout retirement boundary - 2026-10-15

The maintained FE now implements the observed setup-stop method, preserves an
explicit pending notification, and exposes the observed active/stopped status
without treating a status read as completed DSP work. The unchanged original
retirement caller runs its actual 668 one-microsecond stalls against the same
GP/EP CPUs and owned RAM. There is no injected status1 or supplemental drain.

Selectors0/1 each complete two processed generations, original retirement,
warm bootstrap and final original allocation cleanup. Each retirement begins
with a real outstanding GP frame and advances751 PIT ticks. The original CPU
polls status80 throughout, then both drained CPU owners accept their holds.
Further time after original free performs no device RAM access. These runs
have stopped codec streams; they do not establish connected output acceptance.

Selector2 does not pass this fallback at the same EP event boundary. After
668us its GP frame and current peripheral transactions have drained, but its
EP program is still at PC05F4 with request0, rather than back at its request
loop. The next original1510 read reaches the existing drain guard. The pending
notification and allocations remain retained; no hold, free, completion or
successful retirement is fabricated. This distinguishes a still-executing EP
frame from an empty DMA queue. Normal selector2 retirement remains required;
the earlier-NOTIFY-completion candidate in AUDIO-D14 is not accepted for the
whole profile. This is a reached normal-lifecycle question, not a request for
an exhaustive arbitrary-reset matrix.

The next A04 work is to establish this reached selector2 stop/hold behavior
and the separate original DPC FEMEM completion, using existing program and
hardware evidence first. Do not lengthen the original timeout or repeat the
held-DSP NOTIFY experiment. Evidence and the stopped selector2 report are
retained under `build/blinx/audio-timeout-retirement-20261015/`. HLE and accepted
visuals are unchanged; the family count remains6/9.

All18 focused clock/reset/control/fence/native-owner regressions pass. The
prior drained original retirement/reentry proof still passes all three
configurations on the rebuilt implementation. The active-timeout investigation
retains its expected failing selector2 exit explicitly; it is not relabeled a
three-configuration pass. Native close now retains an outstanding notification
even when both CPUs are held, until an accepted retirement transition retires
the request. Private checkpoint `reviewed-checkpoint.aot-private.json` binds40
source, executable, output and prior physical-evidence identities, SHA256
`9daaf81ed0d268244bcfaeb047ff49a5468fc76791ad34bb6360e1c70424b3a8`.

## FE active-context comparison - 2026-10-15

The fresh physical comparison now reaches original control0/SE15. Setup clocks
write only the second global16-byte notifier slot; the first remains pending.
The actual2010C=0 method changes SE15 to7 and FE7900F to7100F, with PIO80 and
decode10C/0. After20104=2, PIO is80 and raw decodeD0104/2, but the actual first
status byte remains80 through6729 console polls over10000.296us. The later
whole-owner capture still shows an unchanged first16 bytes. Complete64KiB
guards pass with only the observed second slot and capture window excluded.

This resolves the halt's setup transition and distinguishes queue progress
from completion. It does not qualify status1 publication or its ordering.
Neither arbitrary delays nor presumed DSP acknowledgements follow from these
observations. Audit the original NOTIFY-specific initialization and actual
consumer/owner requirements next; do not repeat this held-DSP experiment or
expand a raw-register precision matrix. The DPC FEMEM consumer still needs
actual completion; the family count remains6/9.

The original17C2AD retirement callsite does not test the return of17C1B1;
it proceeds to IRQ/control/SE stop and GP0/EP1 holds. The already retained
nine-case execution proves that17C1B1's actual pending path polls/stalls668
times and returns the same active-branch result as successful notification.
Therefore test that original timeout path with actual clocked work, ownership,
reset/reentry and cleanup before treating its earlier status1 exit as mandatory.
If that supported fallback preserves output and lifetime, retain the faster
notification path as a deferral with a reopening trigger. This is a candidate,
not a waiver of safe retirement or the separate DPC memory fence.

Probe commit `12eb899`,11 actual-x86 checks and raw observations are documented in
`docs/FE-NOTIFICATION.md` in the probe worktree. Private final capture:
`results/fe-notification-active-v4-20261015/execute.aot-private.json`.
The two earlier active generations stopped on observer assumptions before the
final NOTIFY and were separately recovered. None is relabeled as a completion.
Final dashboard/SMC recovery and a fresh probe PING/INFO are confirmed; no owner
action is pending. The reviewed private proof pins118 source, payload, capture,
caller and recovery artifacts, SHA256
`2ddcf3acb5cfbc54e721f807a1699fc903f9762275f307218a5588c44af7f6f6`.

## Earlier FE method quiet-context observation - 2026-10-15

The original2010C=0 /20104=2 pair now has a physical observation in a fresh
owned notification allocation, with DSPs held, SE7, codec streams stopped and
interrupts masked. After2010C the decoded method is10C/0 and PIO capacity80;
after20104 capacity is7C, decode remains10C/0, and the actual original status
byte remains80. The complete64KiB isolation check passes. This establishes a
pending queue entry under the quiet context, not active completion or its
progress trigger. An immediate emulated status1 is not supported by this result.

Diagnostic writes stopped at that unexpected capacity result. Remote DASH,
dashboard SMC restart and fresh probe PING/INFO have since completed; no owner
action is needed. Probe commit `d55c1c1` retains the observer and seven actual-x86
checks. Private receipt `results/fe-notification-20261015/reviewed-observation.aot-private.json`
pins the source, payloads, raw observations and recovery.

The original live startup trace already supplies the next reached context:
FE control1510=0 and SECTL15 before retirement, unlike the quiet comparison's
1/7. Test that context on a fresh owner next. Required pending-work ordering,
actual original consumer completion and DPC/FEMEM composition remain A04/A05;
no repeat of the stopped-context experiment or new precision matrix is needed.

## Original drained DSP retirement and reentry - 2026-10-15

The configured-reset observations are now adopted by the existing sole GP/EP
CPU owners. Original GP0/EP1 holds admit only a complete configured profile,
stopped admission and drained peripheral/frame work. EP retains its request
latch; a warm B3 read before original reconfiguration still rejects because
hardware did not establish a zero counter. No owner, DMA engine or clock is
replaced to discard history.

The unchanged original driver now executes two processed generations on the
same owners for each of its three configurations. Each generation runs eight
real EP events and57 GP quanta, then the original SE stop helper, actual drain
and whole17C2AD retirement. Warm setup reloads all2048 P words on each CPU,
executes original configuration and processes another generation. After final
retirement, original destructors free all allocations while held devices stay
alive; further guest time performs no RAM access. Codec streams remain stopped.

FE control reads/programming now distinguish actual outstanding work from past
processing. The original warm C5=FFF clear applies the independently sourced
write-one-to-clear policy only in a drained bootstrap; pending frame/DMA/EOL
cannot be discarded. Exact unconsumed warm background-bit behavior remains a
precision detail under AUDIO-D13. Actual active2010C/20104 notification ordering
and connected original output still belong to A04/A05.

Private source/executable identities and receipts are retained under
`build/blinx/audio-configured-reset-20261015/`. This completes the drained DSP
reset/reentry component of A03; it is not full audio-family acceptance or proof
of the active FE branch. The earlier configured hardware experiment is reused;
no additional hardware operations or audible playback were needed.

## Configured reset hardware question resolved - 2026-10-15

After confirmed remote dashboard/SMC restart and fresh probe startup, the bounded
GP and EP experiments each complete cold configuration, hold, a changed source
marker, fresh bootstrap/reexecution, and final hold. Each uses six single-store
phases and its own pinned owner. Full2048-word P comparisons and distinct warm
markers establish real reload, with no DMA, setup event or audio output.

Both processors reproduce B3=0 immediately after B1 and20 after the fixed gap
on both generations. GP host request/status become0/0 while held; EP retains
1/100. EP B3 before reconfiguration remains nonzero and differs across the two
generations; do not infer that EP reset clears its counter from the GP result.
Held bank reads remain raw evidence, not a new memory-retention requirement.

This resolves the isolated configured-reset/reexecution observation. The runtime
adoption and original drained stop/reuse check above now follow it; pending-work
cancellation remains guarded under AUDIO-D13. No further hardware measurement
of this same no-DMA reset case is required for bookkeeping. A04 and analog A05
remain distinct required questions. Physical receipts are in probe
`results/qualification-final-audio-20261015/gp-reset-bounded/` and
`ep-reset-bounded/`; eight focused observer methods pass (one checker-path error
was corrected by rerunning only its failed method). No HLE runtime changed.

## Remaining work and closing evidence

| ID | Current status | Owner | Required work / evidence | Passing condition |
| --- | --- | --- | --- | --- |
| Q01 | Complete | `.123` | Reference timestamp ownership; input replay fix at `784c1fd` | Original RDTSC and compiled helper observe one stopped common timeline; repeated reads/host stalls do not advance it; failure stops before fabricated GPR output |
| Q02 | Complete: short checkpoint | `.123` | Relative original execution and device cadence | Source consumption, movie PTS, PIT/audio/controller clocks remain coherent in a short checkpoint; explicit input schedule is not asserted to be hardware CPU cycles |
| Q03 | Complete: full 600 GP seconds, numeric/scene/ownership accepted | `.73`, `.112` | Reference movie and integrated scheduling | Two natural opening lifetimes reach final source picture2263, actual Stop/Destroy and title transition; a subsequent real Start/release interrupts a movie and reaches fresh demo gameplay; original thread/IRQ/FP ownership and captured A/V timing pass |
| G01 | Complete | `.116.5` | Original CPU Reset/Release with live original physical resource and queued work | Three real lifetimes retain backing through reset/binding use, wait through original scheduling for actual completion, release then allow original free, with exact bytes and cleanup |
| G02 | Open: original pre-title AV producer passes; connected title crosses encoder/aperture setup and produces its real context; actual title mode/takeover still required | `.109.6`, `.111` | Actual original AV/encoder startup and activation | Execute reached initialization on shared NV2A/SMBus/encoder owners; no guessed warm registers or fabricated handshake/ready state; actual launch crosses retained startup guards |
| G03 | Open: actual image/table/RAMFC/queued draws captured; physical transfer passes; maintained owned LOAD and23 surface projections pass; field-provenance consumers and connected rendering remain | `.116`, `.116.2.1` | Required original instance/PFIFO/startup state | RAMFC/instance admission and required read/write context behavior have independent semantics; maintained title renderer reaches real draws and completion |
| G04 | Open: needs reached primitive | `.116.2` | Required enabled-noise draw behavior | A reached complete primitive has an executable supported sampling behavior; independent source/paired observation establishes it; disabling or ignoring noise is not acceptance |
| G05 | Open: needs G02-G04 | `.109`, `.116` | Connected graphics-LLE attract and representative comparisons | Actual title draws/visibility/callbacks/reentry/teardown work; retained and affected state/shader/texture/fetch/alias cases qualify supported behavior and HLE tolerances; exhaustive4331-draw numerical composition stays deferred |
| A01 | Complete: final 10,348-record / 1,094-generation census | `.113` | Census original voice modes actually used by attract | Each reached format/filter/headroom/routing mode maps to an implemented stage or a specifically named remaining required task; imported/unreached extensions stay out of scope |
| A02 | Complete: required ordinary stage domains | `.113` | Voice filtering/scaling and lifetime stages | Independent impulse/tone/transition evidence qualifies reached stages through the actual source/envelope/mix owner; required processing is not bypassed |
| A03 | Complete: configured reset and three original drained retirement/reentry lifetimes compose with accepted A04/A05 | `.113.9`; GP `.114` complete | GP/EP configuration and wake/reset | Original B0..B3 and coherent C5/CA/D5 context drive the existing sole DSP CPUs; independently supported descriptors copy exact bytes and publish actual completion |
| A04 | Complete: all3 original timeout retirement/reentry configurations, actual queued DPC/FEMEM completion and connected A05 acceptance | `.115` complete | FE fence/control ordering | Required work precedes real FEMEM publication and the original DPC completes; retirement preserves output/ownership through supported completion or its actual bounded timeout. Representative pending/failure checks prevent false success, without an exhaustive fault matrix |
| A05 | Complete: actual original retained Stop/status/Play plus96000 continued frames; independent mixed GP/EP and causal codec-ring output exact, root accepted | `.113`, `.113.9`, `.113.34`; codec group `.113.33` complete | Build-selected original audio device integration and output | Unchanged original DirectSound routes to maintained APU/GP/EP/AC97 owners on the shared bus/clock; actual title output, stop/reuse and movie-relative timing match declared independent contracts |

Dependencies: Q01 -> Q02 -> Q03 closes movies/scheduling. G01 and source audits
can run beside Q work; G02/G03/G04 -> G05 closes GPU/graphics. A01 -> A02 and
A03/A04 -> A05 closes audio. Only unresolved *required* hardware semantics move
to the final hardware batch; precision questions remain deferred.

Latest audio checkpoint: native word MMIO and the complete original codec
IRQ/refill group pass independent patterned-output, acknowledgement, duplicate
ISR, queued DPC, restart and destruction checks across three lifetimes. Actual
kernel callback tests also pass. The first changed title exposed an ISR FS:[24]
fault; all original continuations now borrow the same captured PCR with one
canonical session publication, including callbacks entered without host TLS.
The120.125s muted title passes the codec completion path and reaches frame
swap, but its external cutoff left capture buffers unflushed. A validated
serialized observation stop now persists both incomplete streams. The changed
604.0s host run has no runtime guard and retains1,301,504 digital bytes over
1,271 blocks (6.779s), including503,769 nonzero bytes. Analog PCM stops after
the initial8,192 silent bytes/eight blocks. Original17BB1C configures both for
cyclic use without software refill; this makes analog ring traversal a required
final hardware question. The physical SPDIF observation cannot settle it.
The subsequent passive stage journal now captures those missing boundaries.
The first title journal filled on silent startup voices; a validated nonzero
source trigger then retains the audible prefix without truncation. At its actual
cursor96, the independent movie source comparison meets0.02/0.99 tolerances:
RMSE0.008986/0.008479, correlation0.998368/0.998091 across72,032 frames. All2,251
mix banks match reconstructed sends,13,500 GP main transfers match their input
banks,3,069 comparable EP reads match GP content and274 comparable digital
chunks match the actual EP ring. These are bounded content checks, not full
ownership/timing or output-transform acceptance.

All281 live PCM program loads and47 control contexts identify original EP
selector2. Its754-word PCM filter program differs from the selector0/1 matrix
used in the failed diagnostic. The new independent integer oracle now matches
all three retained fixtures (49,152 samples) and all281 live blocks (143,872
samples) exactly, including all78-word state loads derived continuously from
prior input. No fitted parameters or emulator behavior changes were needed.
This resolves the captured waveform discrepancy. Next address original active
FE ordering and codec/GP/EP stop/reset ownership, then remaining connected
output/HLE-effect/timing/attract acceptance. No new hardware or title run was
needed for this numerical proof; the analog question stays in the final
hardware batch.

The retirement source audit identifies the original active2010C/20104 method
and consumer group before control1510, SE stop and GP0/EP1 reset. Earlier
already-stopped fixture disposal does not cover it. Those active ordering and
configured warm-reset questions remain required final hardware work. The analog
probe is now prepared under `.113.34`: original single-node and distinguishing
two-node silent rings, one-write phases and64 timestamped samples. All12 focused
offline methods pass; no analog hardware result or emulator correction is yet
claimed. See the [retirement contract and preparation](../pilots/native-title/AUDIO-LLE.md#remaining-retirement-contract-and-analog-probe-preparation---2026-10-15).

The GP/EP configured-reset observer is now prepared as well: fresh pinned owner,
finite counter/control program with no DMA/output, separate GP0/EP1 hold, changed
source marker, and verified fresh bootstrap. Six offline methods pass, including
actual emitted x86 and DSP instructions and held-bank preservation/clearing
alternatives. This is instrumentation evidence only; configured reset and active
FE ordering still require the final physical batch. Probe plan:
`docs/DSP-CONFIGURED-RESET.md`; receipt:
`results/dsp-reset-20261015/qualified-offline.aot-private.json`.
AUDIO-D12 defers only raw nontrapped FE reason values/timing; required active
IRQ/DPC/queue behavior remains A03-A05. Families6/9; deferred31 IDs /30 scopes.
See the [selector2 PCM proof](../pilots/native-title/AUDIO-LLE.md#selector2-stereo-pcm-contract---2026-10-15),
[audible stage and mode diagnosis](../pilots/native-title/AUDIO-LLE.md#audible-stage-diagnosis-and-ep-mode-identity---2026-10-15),
[passive stage journal](../pilots/native-title/AUDIO-LLE.md#passive-audio-stage-observation---2026-10-15),
[earlier digital prefix diagnosis](../pilots/native-title/AUDIO-LLE.md#digital-prefix-diagnosis---2026-10-15),
[bounded output evidence](../pilots/native-title/AUDIO-LLE.md#bounded-output-observation---2026-10-15),
[codec interrupt evidence](../pilots/native-title/AUDIO-LLE.md#codec-interrupts-and-original-callback-pcr---2026-10-15)
and [runtime publication evidence](../pilots/native-title/AUDIO-LLE.md#runtime-input-sge-publication---2026-10-15).

The next GP/EP host-control batch has a
[source-bound register inventory](../pilots/native-title/AUDIO-LLE.md#dsp-host-control-source-audit---2026-10-14).
GP FF10 receives an owner-derived value, not a fixed1; generic emulator
readback storage does not establish these register effects. This inventory is
preparation only and leaves A03-A05 open.

The subsequent stopped hardware batches observed original zero writes,
GP FF10 input30000001 reading1 and EP FF10 retaining1 after a0 write.
An isolated one-store check also established CPU EP status100 clearing to0
with unchanged surrounding controls. Both unexpected-write stops were followed
by confirmed remote recovery; the final isolated check passed and leaves no
manual recovery pending. These are physical inputs to the remaining shared
control/clock implementation, not a connected-startup or family pass.

The held CPU host controls are now implemented through the existing APU/GP/EP
owners, with nine focused current-source checks passing. A further GP input
mask/zero/reset batch passed. A later read also found EP status100 again, so
elapsed-time status reads remain guarded rather than exporting a permanent0.
See the [implementation and remaining limits](../pilots/native-title/AUDIO-LLE.md#cpu-dsp-host-controls-connected---2026-10-14).
No changed title was launched; complete startup/shared scheduling remains next.

The subsequent live-memory source check now completes both unchanged GP/EP
setup entrypoints in three lifetimes. Each performs102 actual device
transactions and eleven accesses to currently owned original RAM; each sole
DSP CPU loads one exact8192-byte bootstrap and remains ready, without executing
instructions or advancing guest time. Original destructors free every allocation
after the fixture retires its device owners. This is not an active reset proof.

Physical observations corrected the reference emulator's ignored1000/1004
output methods: base programming also resets the end/cursor, and the original
length2000 command restores end2000. The model updates its existing FIFO owner.
An extra length2100 experiment trapped and remains rejected. All unexpected
write stops were followed by confirmed remote recovery. Ten focused current-
source checks pass, including real FIFO transfer regressions; the connected
title checkpoint above predates this change. See the
[startup proof and limits](../pilots/native-title/AUDIO-LLE.md#original-gpep-setup-connected---2026-10-14).
Shared DSP execution/cadence, then active FE/output/reset, is the next group.
The family total remains6/9. New AUDIO-D11 retains unreached output encodings
and trap behavior; the queues now hold30 IDs /29 deferred scopes. Required
shared execution and output are not deferred.

The enclosing original17BE82 call now also returns through its actual10000us
stall service. Shared guest time supplies instruction budgets to the retained
sole CPUs: GP executes6538 cycles and yields at PC2B; EP executes1333125 cycles
and polls at PC32. All three lifetimes retain the exact bootstrap and original
RAM ownership, with zero frame events. Thirty focused checks pass in11.36s,
including repeated-time/partition checks and native codec/teardown regressions.
The rate is a nominal budget calibrated to retained EP polling intervals,
not a newly measured oscillator or an EP708000-threshold qualification.
[Stopped-clock evidence and the remaining active group](../pilots/native-title/AUDIO-LLE.md#stopped-dsp-execution-from-shared-time---2026-10-14).
Active32-sample GP /256-sample EP events, shared codec ordering and original
FE/output/reset remain required. No changed title or new hardware batch ran.

The subsequent active-clock/source batch now passes34 focused checks and three
original lifetimes with eight EP events and57 GP quanta each. Actual GP/EP DMA
uses current original allocations, and admitted work finishes after the original
SE stop helper before fixture disposal. The first active prefix exposed missing
warm voice-list initialization; original kernel/title retirement supplies all
nine FFFF pointers, now reflected in the handoff. Four kernel source cases and
three title control lifetimes also pass. Builds/runs began after the game exited.
Native clock/lookup adoption and original active reset remain required. Source
also establishes that startup starts SPDIF before PCM; the retained active-SPDIF
guard is required A05 work, not a precision deferral. Family status remains6/9.
See the [active clock evidence and remaining integration](../pilots/native-title/AUDIO-LLE.md#active-gpep-clock-and-original-source-execution---2026-10-14).

The selected native owner now installs the verified immutable EP lookup and
uses the shared active clock. All41 focused checks pass, including actual EP
reads/DMA of the complete lookup,24 native GP quanta/three EP events, three
timestamped PCM catch-up frames and compiled MMIO/ownership regressions. One
muted56.98s title checkpoint crosses the previous GP FF00 guard and stops at
GP FF10 with low nibble5. The original owner is only DWORD-aligned; the earlier
fixture's16-byte alignment concealed this bit2 input. Its behavior remains
guarded pending independent evidence. This title has not reached active output.
See [native integration and its limits](../pilots/native-title/AUDIO-LLE.md#native-shared-clock-and-immutable-ep-image---2026-10-14).
The current family total remains6/9; no required work or new precision question
was deferred.

A held-GP hardware batch subsequently observes the reached bit2 input and
permits its narrow admission correction. Six affected checks and three original
active lifetimes with varied DWORD alignment pass. The next title run crosses
that guard and reaches original stall ordinal151, but its120s observation
cutoff occurs without a runtime fault or startup return. Progress records and
source identify a host-time input dependency: this selection still uses QPC,
so expensive DSP service contributes to the next elapsed interval. The existing
qualified stopped reference timeline is currently bound only by the other audio
profile. Connect original audio to that shared input/TSC policy before another
long title attempt; do not make LLE speed an acceptance condition. Required
SPDIF/FE/output/reset remain open. No hardware recovery is pending.

Original audio now binds the existing stopped input/common TSC policy, including
USB/controller replay, at the retained32000-block cadence. Thirty-eight affected
checks pass. The real kernel's10000us requested stall returns after112ms of this
explicit simulation input; hardware CPU-cycle timing is not claimed. Active
GP/EP events and original RDTSC pass, host pauses add no work, and rejected input
publishes neither PIT/device time nor a fabricated counter. A changed muted
111.38s title crosses the previous DSP-startup wait and reaches the original
SPDIF start helper's byte status read at FEC00176. That reached width, subsequent
CR1D and active stream behavior form the next required batch; no field is skipped
to force startup. No speaker output or hardware operation ran. Families remain
6/9 and the deferred queues remain30 IDs /29 scopes. See the
[current input-timeline evidence](../pilots/native-title/AUDIO-LLE.md#original-audio-input-timeline---2026-10-14).

## Original AC97 startup crossed - 2026-10-14

The subsequent SPDIF group now has candidate digital consumption through the
existing ACI ring/RAM/clock/IRQ owner. Four focused executable checks pass,
including unchanged PCM regressions. Three original-driver lifetimes each
produce8192 PCM bytes and9216 digital bytes through live owned RAM, then run
their original destructors and free every allocation. The extra1024 digital
bytes are the deliberately stopped prefix: original start(-1) resets and
restarts that ring rather than continuing its partial descriptor. A read-only
physical batch confirms byte/word status agreement for both stopped streams;
no hardware writes ran. Native title admission remains guarded pending its
digital consumer/teardown connection and active physical transport evidence.
No title retry or family closure is claimed. Families6/9 and deferred30 IDs /
29 scopes are unchanged. See the [candidate SPDIF evidence](../pilots/native-title/AUDIO-LLE.md#candidate-spdif-transport-and-original-stream-lifetimes---2026-10-15).

The subsequent silent hardware group contradicts the candidate's end-of-ring
halt: after completion, SR is0C, CR remains1, PICB continues changing and both
CIV/PIV stay0. After a separately confirmed recovery, a fresh observation
reproduced this state. Stop gives SR0D with stable partial PICB; byte W1C gives
SR1; reset clears BDBAR/indices/count, and restoring the original stopped base
succeeds. No manual recovery is pending. The native guard remains necessary
until traversal/continuation and digital capture are corrected together. A
multi-node ring will distinguish wrap from last-node repetition. Six payload
test methods pass; no title run or family closure is claimed. Evidence and
the successful retirement are in probe `docs/SPDIF-STREAM.md` and the private
`results/spdif-completion-context-20261015/` receipts.

The subsequent fresh two-node observation resolves that traversal question:
64 coherent samples over63.678ms show1->0->1->0->1, SR0C/CR1 throughout, and
PIV=CIV at every sample. Different descriptor lengths1024/2048 words and the
observed countdowns establish repeated traversal of both nodes. A separate
read-only retrieval worked around the bulk command's high-alias rejection;
no capture or start was repeated. Guarded retirement then gave stable partial
PICB1312 with CR0/SR0D, reset cleared pending completion/base/indices/count,
and the original stopped base was restored. No hardware recovery is pending.
Eleven payload test methods pass. The candidate must now replace its digital
last-buffer halt and always-next PIV behavior; native capture/teardown and
one changed connected title remain next. Families6/9 and deferred30 IDs /
29 scopes are unchanged. Probe evidence: `results/spdif-ring-20261015/` and
`docs/SPDIF-STREAM.md`; no full build, title launch or speaker output ran.

The bounded SPDIF group is now implemented and validated. Digital traversal
repeats the ring and reports stable PIV=CIV; zero-length cyclic descriptors
fail before unbounded work. Native output has separate format-tagged digital
capture and both-stream stop/reset/close ownership, including rejected
cross-kind bindings. Forty-five distinct native checks, four focused MinGW
checks and three unchanged original-driver lifetimes pass. Each original
lifetime emits8192 PCM bytes and17408 raw digital bytes, including the stopped
prefix and two complete digital traversals, then releases its allocations.

One changed muted/headless title checkpoint crossed the old FEC00176 boundary
and stopped at `FE820280`, DWORD write0, after62.766s. Both actual title
captures remain partial with zero accepted bytes: the downstream method
blocks startup before output, so this does not close audio. Evidence and
the next method-group handoff are in
`build/blinx/audio-spdif-cyclic-20261015/`. Families6/9 and deferred30 IDs /
29 scopes are unchanged; no new hardware operations or HLE changes occurred.

The physical codec identifies as WM9709, matching the manufacturer's fixed48kHz
DAC with zero ordinary mixer/rate/power readbacks. The maintained ACI replaces
the generic SigmaTel mixer defaults, admits the explicit ready post-kernel
profile and implements the original stopped PCM/SPDIF ring setup. Both physical
stream resets clear BDBAR/indices/count and return CR0/SR1. The original SPDIF
format values02000000/02000002 read back correctly while stopped. Active SPDIF
and link reset transitions remain guarded; a reset write is no longer silently
ignored. The default HLE audio implementation is unchanged.

The initial12-store batch passed. Its NAM readbacks concealed whether writes
themselves release CAS. A separate one-store experiment exposed the incorrect
immediate-clear assumption: direct CAS returned1. No further experiment or
restoration followed; dashboard SMC restart and remote probe launch recovered
without controller input. A newly prepared bounded payload then observed busy
immediately and completion after one original20us wait, followed by correct
acquisition/read-release behavior. Its final three-store batch also compared
both SPDIF formats and restored the original. All prior allocations were retired
by restart. No manual recovery remains pending.

Pending codec commands now belong to the existing ACI and shared guest clock.
CAS polls never advance work. A nominal next48kHz link boundary supplies their
completion, with exact phase and synchronous NAM-read bus latency retained as
new AUDIO-D10. Three unchanged original helper groups return, produce six real
owned descriptor rings and release all original allocations. Each also executes
the real1000-wait failure branch while the semaphore remains acquired. The
prepared helper proof does not replace enclosing IRQ-registration returns.

All42 targeted checks pass in28.91s. One changed, muted105.5s connected title
run crosses the enclosing original codec initialization/IRQ setup and reaches
GP DWORD WRITE FE83FF00=0 (original PC17B883). This is the next required DSP
host-control group, not a codec-output pass. Capture contains zero PCM/stamps.
The title uses only its memory/file PCM consumer, with no speaker transport.
Current evidence is `build/blinx/audio-ac97-startup-20261014/`; physical details
are probe `docs/AC97-STARTUP.md` and its private results. Families remain6/9.
The queues retain29 IDs,28 with deferred scope; no required work was waived.

## Complete stopped VP initializer and AC97 boundary - 2026-10-14

The immediate-zero experiment omitted an original startup precondition:
FE1510 must be cleared before these methods execute. A bounded comparison
with inherited control1 retained decoder804/free7C and a zero destination
through100,000 polls. Matching the original control0 allowed the actual
input pointer to publish with SE stopped. A separate causal experiment held
804/808 for1,000 consistent observations, then released the pending pair by
clearing only that control. This resolves the earlier probe setup question;
it does not close active notification-fence ordering.

Physical differential writes identify the nonsequential method-to-register
mapping:2A0->2078,2A4->207C,2B0->2080,2AC->2084,2A8->2088. Each0 write andFFF
restoration affected exactly its observed field. Method2C0 updates20AC with
the original packed submix encoding. Separate owned tables distinguish input
804/808 publication to VPSGE from output1800/1808 publication to GPF. A full
60KiB owned-area check after the two input cases found only their expected
pointers changed. The physical field/gate batch and submix/output follow-up
completed24 and16 planned stores and restored controls/mappings.

The maintained APU now implements reachedFFF field initialization, submix
configuration and actual pointer writes through its existing RAM owner.
Native/source admission is shared. Non-null field consumers, halted/held
methods, active contexts, unsupported widths/encodings and unowned writes
reject explicitly. The physical held-pair observation is retained for A04;
this change does not pretend to implement its missing runtime command queue.
No additional DSP, register or RAM owner is introduced. HLE processing is unchanged.

Three complete unchanged original VP initializers return through maintained
MMIO:42 writes,three exact owned input-table publications and768 original CPU
voice self-links. Original destructors release all prepared allocations. The
device's owned physical RAM view and original CPU self-link writes are checked
separately; this is a prepared caller proof, not a complete boot. All38 targeted
checks pass, including three native allocation/write/free lifetimes and explicit
held-context, width, range, missing-selection and failed-publication cases. The
first native fixture incorrectly counted DMA bytes rather than transactions;
its corrected assertion expects two real four-byte writes, and the failed log
is retained.

One changed connected title run crosses the original method stop and reaches
DWORD READ FEC0012C in AC97 initialization after64.625s, with zero PCM bytes or
stamps. The runtime's0000009F diagnostic is an ordinal, not an original PC.
Current evidence is `build/blinx/audio-vp-method-order-20261014/`, authoritative
source `source-final/`, tests `tests-final.aot-private.log` and connected `title/`.

A separate clock comparison stopped before any methods when FE status bit15
changed. Both unexpected experiments were recovered by the existing dashboard
SMC restart/remote launch sequence, without manual input. The later successful
batches leave the probe responsive, with restored stopped controls/mappings;
its64KiB allocation remains retained until restart. Probe details are
`docs/VP-STARTUP-METHODS.md` and its private result directories.

Next is the original AC97 initialization group, followed by DSP host controls,
shared cadence and required active FE work. Families stay6/9;28 question IDs
retain27 deferred scopes. No required task is reclassified as a deferral.

## VP mapping transport and unresolved method ordering - 2026-10-14

A fresh owned64KiB allocation passed physical/page/protection/extent checks and
zero-initialization. Four mapping writes (VPSGE, GPF and unnamed2038/203C)
returned their exact owned addresses while SE was stopped and DSPs held.
The two unnamed slots each receive an original-owned8KiB,16KiB-aligned region.
The maintained APU now retains those addresses in its existing register owner,
with stopped-context, width, alignment/range and known-read guards. Active table
consumers are still unqualified. The original prefix has FE free before its
first method; the physical mapping writes were made with FE halted. This is
address-transport evidence, not an active-table or timing proof.

Native and original-source transport now share one admission function. The
already-implemented SSL table mapping is connected through that same contract.
Three original VP setup prefixes cross all six mappings, initialize768 actual
voice self-links and stop at the first unknown method2A4=FFF. All allocations
are released by original destructors. The first source checker incorrectly
expectedFFFF links; unchanged original instructions establish self-links0..255,
and the corrected checker retains that evidence. The new native lifetime test
also moved its deliberate terminal rejection to the last lifetime, respecting
the shared engine's existing permanent-fault contract.

All37 targeted checks, including three native allocation lifetimes, pass.
Three full original stopped initializer executions also pass. One changed
connected title run crosses FE802038 and reaches FE8202A4=00000FFF after45.828s,
with zero PCM bytes/stamps. Current private evidence is
`build/blinx/audio-vp-mapping-20261014/`, final source `source-final/`,
initializer `initializer/` and connected run `title/`.

The physical input-SGE method pair804/808 returned immediate zero instead of
the expected owned pointer. Exactly eight device stores had been sent: four
maps, one output-handle limit, one FE control, and two method writes. No later
hardware operation, output-table test, restoration or cleanup write followed.
This does not distinguish latency from different method semantics; synchronous
method completion remains unproved and required A03/A04 work. The initial RB
request had separately been rejected by the protocol before RAM access; a
bounded read-only EXEC check established the allocation's zero table contents.
Neither stopped observation is a passing method result.

On the owner's recovery instruction, probe DASH, dashboard SMC restart and
remote probe launch restored service without controller input. Fresh read-only
checks observed GP0/EP1,SE7,FECTL7138F,ISTS0,IEND8 and replacement mappings;
the old allocation is retired and must never be reused. No restart or launch
command was retried; transient startup liveness failures were observed again
read-only. Receipts are probe `results/vp-startup-mapping-20261014/`.

Next work is the original VP method/control batch, including its execution and
publication ordering, then remaining DSP host controls/shared cadence. Families
stay6/9;28 retained question IDs contain27 deferred scopes. No new deferral or
HLE processing change is introduced.

## Stopped initialization batch and connected VP mapping boundary - 2026-10-14

All23 fixed configuration writes match physical readback with SE stopped,
GP/EP held, FE halted and global APU interrupts masked. Four additional FE
control writes observe88 ->70088,8 ->70008,100F ->7100F and restoration7138F.
All other endpoint controls and the stopped sample counter remain unchanged.
These27 volatile writes perform no storage operation, DSP release,
playback or automatic retry. They establish stopped transport, not the active
effects of arbitrary configuration encodings.

The maintained device retains those values in its existing canonical register
file. A separate admission bitmap requires original-value writes during stopped,
halted startup; unsupported values/widths/overlaps and later active access reject.
FE control reads expose observed status bits only in the qualified startup
scope. Active status/ordering remains A04; configuration storage does not
acknowledge unimplemented work or replace a DSP clock/event producer.

The first changed title run exposed missing warm FE control state before the new
block. The already-retained pre-video/network capture supplies FECTL71F8F.
Original kernel initialization/retirement supplies108F outside the captured
status/trap fields; the actual original mode0 helper preserves the captured
value. The explicit initialized-kernel handoff now includes that state without
granting FE notification completion or claiming a cold-reset default.

Three unchanged original mode-helper/initializer executions return through live
maintained MMIO. The source checker
binds physical payloads/readbacks and the pre-video capture to original code.
All36 targeted component/native tests pass. The corrected handoff required one
negative clock fixture to explicitly free the FE before testing an unsupported
active mode; its prior implicit free-running startup premise was wrong.

The final changed title run crosses the old FETFORCE0 stop and reaches
DWORD WRITE FE802038=02F80000 after57.453s, with zero captured PCM bytes/stamps.
The two attempts have distinct retained executables/logs; the earlier diagnostic
is not the final implementation. Current evidence:
`build/blinx/audio-initialization-batch-20261014/`, final `source-handoff-v2/`
and `title-handoff/`; matching physical observations remain in probe
`results/audio-init-controls-20261014/`.

The next source inventory adds original VP setup17B79F, omitted by the earlier
GP/EP-only fixture. Its write at17B7DF supplies the reached2038 boundary. The
combined prepared setup records65 writes, including15 mapping writes and13 DSP
host-control writes, with actual owned allocations and natural cleanup. This
groups the next work; it does not admit the unknown mappings or methods.
Families remain6/9;28 question IDs retain27 deferred scopes. HLE processing is
unchanged, and required active field effects, shared cadence and output remain open.

## Physical sample counter gate/reset/resume - 2026-10-14

Nine bounded volatile writes all returned their expected values: one original
XGSCNT zero write while stopped, then SECTL7/15 transitions and halted mode2/3
observations. GP/EP remain held, FE remains halted and the global APU interrupt
stays masked. No storage operation, retry, DSP release or audio playback occurs.
The zero write changes the retained442758 to0. Mode1 advances to49796; mode0
holds it. A later mode1 resumes from49796 and stops at105627. Modes2/3 remain
at105627 in the halted context. Two host time intervals are consistent with48k
samples/second, but do not measure exact rate or sample-edge phase.

`ApuSampleCounter` replaces unconditional uptime readback with a retained,
gated count on the existing shared clock. Mode1 advances independently of FE
halt; mode0 holds, and reads do not advance it. Original stopped DWORD zero is
admitted; nonzero, running, partial and overlapping writes remain guarded.
An initialized-kernel handoff does not invent a warm counter value: the original
zero write establishes it. Backward time and changing/invalid frequency reject.
Active mode2/3 processing remains guarded, separately from observed halted
retention. AUDIO-D09 records only the replaceable sub-sample phase policy.

All34 targeted component and native integration checks pass. They include three
real shared-clock counter lifetimes, compiled ISR/DPC reads of the actual retained
count, explicit unknown-warm rejection, rollover and existing reset/GP/EP/FE
regressions. `native_audio_counter_reference.py` rechecks every physical payload,
write readback, stopped endpoint and timing interval, plus all recorded original
instruction identities and the three original initializer/control lifetimes.
Private evidence: `build/blinx/audio-init-controls-20261014/` and matching probe
results. No new title run is claimed: the earlier FETFORCE0 guard still precedes
this counter initialization. Remaining work is the rest of the startup controls,
shared DSP cadence, active FE ordering and actual output. Families remain6/9.
There are now28 retained questions,27 with deferred scope. Default HLE processing
and its existing running-clock conversion are unchanged.

## Deferral reconciliation and idle counter finding - 2026-10-14

At this earlier audit, the append-only queues retained27 question IDs, not27
deferred closing requirements. GFX-D06 has reopened as G01-G03/G05 (G01 is
complete). GFX-D04 splits required enabled-noise behavior G04 from additional
sampling/rounding precision. There are26 entries with deferred scope, including
that split entry. See the [current dispositions](../pilots/native-title/GRAPHICS-DEFERRED-QUESTIONS.md#current-disposition---2026-10-14).
The cross-family queue also now marks callback/scheduling integration completed
under Q01-Q03 and distinguishes reached A02 stages from guarded extensions.
This is a status correction, not new runtime qualification; families remain6/9.

A fixed physical read-only startup-control observation completed50 DWORD reads,
followed by12 reads over three temporal rounds. The latter spans2.328 host
seconds: SECTL remains7 and offsets2008/200C/2020 remain6C9A0/6C186/6C300.
The first snapshot's GP/EP reset and FE/SE state match before and after.
There were zero MMIO writes, zero storage operations and no retries.
This establishes an idle observation, not active counter rate, writable-counter
semantics, or causation by SECTL alone. The current unconditional XGSCNT update
in `McpxApu::set_time` does not represent this frozen state. Investigating the
required counter start/stop/reset contract belongs to A03-A05; it is not silently
added to the precision queue. No runtime behavior or HLE processing changed.
Private observations and exact payload scripts:
probe `results/audio-init-controls-20261014/`.

## Reset controls connected to sole DSP bootstrap owners - 2026-10-14

Original driver execution records GP/EP reset1 then3 over three allocation and
destruction lifetimes. Each release captures the actual two-page scratch image;
the maintained MMIO route loads all12,288 original program words exactly across
those six images. Original kernel retirement and retained physical entry supply
GP0/EP1. Separate retained physical GP-marker and EP-startup runs verify release3
and all2,048 loaded P words per DSP. The original programming and pinned xemu
reset-bit/reference bootstrap model support the bounded hold/release bridge;
this does not establish arbitrary warm-reset effects or held-bank readback.

Reset callbacks belong to the existing sole program/CPU owners. Holds retain
bank storage and perform no DMA; release uses the existing complete SG fetch,
24-bit P publication and executable-cache invalidation. Repeated release/read
does not refetch or run instructions. Unsupported control bits, partial or
overlapping accesses, unavailable owners, failed second-page reads and existing
counter/control/pending-work reset guards fail explicitly. No alternate DSP,
fake ready bit, completed frame or generated output is introduced. Released
native DSPs remain guarded at shared-clock service and cannot silently disappear
through teardown; connecting their admitted execution to that clock is next work.

Six component checks pass, including the new reset test with six authored CPU
lifetimes/twelve boots and actual instruction-produced markers. All25 native
device/compiled-MMIO checks pass. Three legacy failure stimuli used now-supported
reset3; they now exercise unsupported abort bit3 and preserve their original
failure/ownership assertions. One changed title run crosses the initial reset
and stops at DWORD WRITE FE801500=0 (FETFORCE0), after52.313s, with zero captured
PCM bytes/stamps. It does not reach connected release/bootstrap yet.

Next batch: remaining original FE initialization controls, GP/EP host controls
FF00/FF04/FF10/FF14/FF5C and FIFO/SG method setup, then shared DSP cadence, active
FE ordering and actual title output. The source trace records later controls
without admitting their unknown effects. Evidence:
`build/blinx/audio-reset-startup-20261014/`. No new hardware operation, playback
or HLE processing change;6/9 families and27 precision deferrals remain.

## Startup FE control separated from notification completion - 2026-10-14

The initialized-kernel handoff now supplies FE control1 from the retained
physical idle observation. Original kernel init/retirement controls bit0;
an additional source case proves it preserves unrelated high bits, so their
observed zero value is not attributed to kernel clearing. Original Blinx
17BC0C performs nine explicit control RMW calls over three driver lifetimes,
and the complete17BF66 initializer leaves control0. All original calls and
destructors return naturally with instruction/source identities retained.

The sole APU owner admits these startup control reads and0/1 writes separately
from the existing captured idle-fence contract. Unused held DSP owners may be
attached. Processing enable, FE traps, VP/host-bank writes and DSP transport
revoke startup admission; restoring registers cannot restore it. Frame/work
history and partial/overlapping transactions are guarded. Startup control
never admits FEMEMADDR/FEMEMDATA or publishes a notification. Existing96 idle
fence publications still pass alongside120 overlap guards and new history/
notification-separation checks. The23 native integration checks also pass.

Underlying device read failures now preserve the caller's output value. A
private candidate restoring the previous assignment fails the new sentinel
check, while maintained code passes. This closes a transport failure detail
without fabricating a read value or original CPU return.

One changed connected title run passes FE control and reaches DWORD WRITE
FE83FFFC=0, exiting2 after65.5s. Its partial capture contains zero PCM bytes
and zero stamps. Next is original GP/EP reset/bootstrap, then active FE ordering
and connected output/lifetime timing. The full FECTL/device startup and active
notification contracts remain open. No physical operation or HLE processing
change occurred;6/9 and27 precision deferrals remain. Evidence:
`build/blinx/audio-fe-startup-20261014/`. Do not rerun this unchanged reset guard.

## Initialized kernel audio handoff - 2026-10-14

The pinned4983 kernel supplies a source-derived initial control contract.
Actual original800654DC clears SECTL and sets its low three bits; original
80067442 idle retirement clears processing bits0x18 and leaves SECTL7.
The same calls initialize IEN=AFA and clear its global interrupt enable.
Four different incoming SECTL values all reach those postconditions through
natural original returns, with353 distinct executed instruction identities.
The original idle retirement also writes GP reset0 and EP reset1, matching
the retained physical pre-video/network probe-entry capture on this kernel.
DSP reset transitions and active completion are still separate requirements.

`native_audio_handoff_reference.py` retains exact kernel/title source identities,
actual accesses and ABI checks. It executes original Blinx SECTL and IEN RMW
helpers across three real driver allocation/destruction lifetimes, then the
complete original17BF66 initializer. That initializer replaces IEN with D8;
the kernel AFA value is an initialized-kernel contract, not a universal warm
application value. The kernel fixture explicitly supplies an empty software
owner, DISPATCH_LEVEL and passive register storage. It never substitutes
active FE completion, a callee return or a DSP ready event.

The maintained native post-kernel audio owner now starts SECTL7/IEN=AFA and
admits their aligned DWORD reads. General board cold-reset defaults remain
unchanged. Repeated reads, three owner lifetimes, original-derived control
values and unsupported-width checks preserve held DSPs and zero DMA/PCM work.
All22 focused device/compiled-MMIO checks pass. No HLE processing or physical
Xbox operation is introduced. Evidence: `build/blinx/audio-startup-context-20261014/`.

Connected title checks cross the former SECTL and IEN read guards. The final
changed binary reaches DWORD READ FE801510 after53.047s and exits2 with the
explicit FE-control admission guard. No draw or PCM output is established.
One preceding launch failed before title entry because the new private IO
library retained its configure-only empty HDD path; correcting that compiled
path resolved the preparation error. Runs use the retained isolated post-run
disk as persistent input, with separate EEPROM/config/storage. The next required
step at that checkpoint was initial FE control/RMW admission; the section
above now records its implementation and the next GP-reset boundary.
Families6/9 and27 precision deferrals remain fixed.

## Sustained original EP and independent output - 2026-10-14

All three unchanged original configurations sustain32 consecutive events for
both silence and six distinct tones:192 accepted event returns in total.
Selectors0/1 perform230 transfers each; selector2 performs1198, including555
output nodes. Actual FIFO rings wrap, acknowledgements retire, each event
returns to the original poll, and repeated reads/same-clock calls/idle execution
preserve banks, RAM, transfers and event count. Original destructors release
all allocations. No runtime production or HLE behavior changes were needed.

The first-event check's immutable-directory assumption was too broad for
successive events. Original P:01C3..01C7 explicitly toggles bit8 of P:0116,
alternating the shared input window. The maintained check now requires that
exact instruction/update once per event and preserves all other directory
fields. Tones431/653/887/1103/1327/83Hz differ across256-sample windows so a
stale or swapped window cannot pass by replaying identical signal data.

Independent arithmetic transcribed from the original coefficient stores and
multiply/accumulate sequence matches all65536 compared PCM16 samples across
the two PCM settings and both signals. The encoder emits IEC61937 AC-3 bursts
at6144-byte spacing. Ten complete frames (five per signal) pass independent
FFmpeg decoding with explicit CRC checking and explode-on-error. Each decode
contains7680 six-channel48kHz frames. Full-band tone gain is within1% of the
authored input; LFE uses a10% representative gain bound, with other authored
tone leakage below2%. Silence stays below2^-13 full scale. Exact LFE phase and
hardware latency are outside this representative check. The final partial
burst is explicitly excluded from decoding, rather than accepted as complete.

Five negative controls reject swapped PCM channels beyond hash validation,
swapped decoded channels, substituted silence, nonzero burst padding and
corrupt AC-3 payload CRC. Eighteen retained startup/first-event/revocation
checks pass, including the missing-image Y:0FC9 guard. The private signal loan
does not provide original GP production or AC97/title consumption. Initial
SECTL, real startup/reset/shared-clock timing, active FE ordering and connected
title output remain required. Families6/9 and27 precision deferrals stay fixed.
Evidence: `build/blinx/audio-ep-sustained-20261014/`; maintained commands are
`native_ep_startup_reference.py --ep-sustained-input ...` and
`qualify_native_ep_output.py`. No Xbox or playback operation occurred.

## EP FIFO1 source and all original first-event returns - 2026-10-14

Four confined physical cases resolve the next reached FIFO1 route. Ordinary
control4622/source001F copies24 distinct PCM16 samples. Source401F produces
24 zeros despite the same nonzero X:001F data. Source441F also produces zeros,
rejecting the proposed literal-value interpretation. Finally, the exact
original source401F/count1498 produces all1498 zero samples and advances CUR
by2996 bytes. The source/canaries, untouched alternate destination, FIFO0,
34 fields, immutable allocation and2048 bootstrap words are preserved. Each
fresh allocation is retired through separate remote recovery. There is no
playback or storage/boot modification, and no DSP device write after START.

The existing DMA owner admits control4622 ordinary X output and the reached
source401F zero route. It reuses the shared PCM16 packing, FIFO1 cursor/SG,
failure and completion owners. It does not mask the source into writable RAM
or infer a general flagged address map. Source441F and adjacent encodings,
input/other formats, GP, missing banks and invalid counts remain guarded.
Frozen ordinary, flagged24 and flagged1498 programs reproduce full captured
destinations, source/canaries, other banks and32 non-timing fields under work
policies1/2/4. Deliberately wrong ordinary and zero-output goldens reject.
Four-word bank-edge, ring-wrap and partial-write-failure checks retain exact
published bytes without fabricated CUR/node/EOL on failure.

With the private Y image, unchanged original selector2 now completes36 DMA
transfers,18 output nodes and22 RAM write calls, acknowledges the frame and
returns naturally to PC0031/0032 with SP0 and no pending work. Its final three
nodes are FIFO1 zero output1498, ordinary FIFO1 output220 and128-word linear
output. Further polling preserves RAM/banks. Selectors0/1 retain their eight
transfers and natural return. Without the image, selector2 retains the exact
Y:0FC9 guard; no replacement table or missing-data fallback is introduced.

This closes the first-event transfer blocker, not the audio family. Sustained
events, independent numerical codec output, shared clock/reset/initial SECTL,
active FE ordering and connected title output remain required. HLE is unchanged;
count6/9 and27 accepted precision deferrals remain. Evidence:
`build/blinx/audio-ep-fifo-source-20261014/` and probe
`results/ep-fifo-source-20261014-{ordinary,flagged,flagged-high,flagged-long}/`.

## EP high-Y lookup and original codec continuation - 2026-10-14

Two bounded physical observations resolve the former Y:0FC9 stop. The first
writes and immediately verifies sixteen known low-Y words, then reads
Y:0FC0..0FCF twice and Y:0FC9 with the original indexed addressing form. The
high values are stable, differ from the low-Y pattern, and agree with the
indexed read. A second fresh owner repeats that check and captures all
Y:0100..0FFF:0100..07FF reads zero, while0800..0FFF contains lookup data.
These are read observations, not proof of physical write protection or a
universal image across consoles. The captured bytes remain private.

Each observation uses three consumed CPU register stores, no high-Y store or
DSP peripheral command, exact9216-word allocation and2048-word bootstrap
preservation, and separate remote recovery that retires the allocation. The
full3905-read batch takes5.922 seconds. No playback, storage or boot changes.

The maintained APU accepts one caller-supplied2048-word/24-bit image before EP
ownership or execution, copies it into a separate immutable owner, and exposes
it only to CPU Y:0800..0FFF reads. Writable RAM, DMA and host maps stay the
same. Missing images, high-Y writes and Y:0100..07FF remain guarded; the
observed zero interval is not silently added as RAM. No board data or generated
replacement table is compiled into the repository. HLE remains unchanged.

With this image, fresh unchanged selector2 now completes33 verified DMA
transfers,15 output nodes and18 RAM write calls before the next guard. Its
shared input is zero, but processing now produces nonzero intermediate state.
The next four-word descriptor is `41A2,4622,5DA,401F`; the adjacent85E8 word is
stale metadata. A private instruction trace confirms original helper029A
constructs it from R1=1, N0=5DA, R0=1F and N4=1, including source
`(N4 << 14) + R0`. The meaning of that source encoding is unresolved; no
masking, malformed-descriptor diagnosis or new DMA admission is justified yet.
Selectors0/1 still return naturally after eight transfers. Without an image,
selector2 retains its exact ten-transfer Y:0FC9 stop.

Seventeen focused commands pass, including the full physical lookup-program
comparison, wrong-image rejection, image/write/bank guards, retained stereo
physical comparisons and18 fresh original setup/event/revocation outcomes
with/without the image. Four offline probe methods cover six actual-DSP
synthetic scenarios and emitted-x86 ownership. Numerical codec correctness,
full selector2 event completion, clock/reset/SECTL/active FE and connected
title output remain required. Count6/9 and27 accepted precision deferrals stay
unchanged. Evidence: `build/blinx/audio-ep-y-lookup-20261014/`, diagnostic
`build/blinx/audio-ep-fifo1-trace-20261014/`, and probe
`results/ep-y-{lookup,image}-20261014/`.

## EP stereo FIFO and original first-event return - 2026-10-14

The earlier malformed-descriptor diagnosis was incorrect. Original helper2B1
intentionally constructs control400603/count1001:256 stereo frames, two planar
channels separated by256 X words, PCM16 FIFO0 output. Its descriptor owns four
words; the apparent fifth word is stale scratch metadata. The trace executes
the unchanged original builder and preserves its inputs and resulting fields.

Two confined physical cases,400403/400603, now establish this form with bit9
clear/set. Both wrote512 exact PCM16 samples to the SG destination, advanced
CUR by1024 bytes, and preserved the alternate destination,514 source/canary
words,8704 immutable owner DWORDs and2048 P words. Immediate D6 was0; EOL80
appeared after35 polls; terminal D6 was0; immediate ACK80 became0 after64 NOPs.
Both fresh allocations were retired through separate DASH/SMC/single-launch
recovery with fresh PING/INFO, without user action or storage/boot changes.

The existing DMA owner admits only this EP stereo FIFO0 shape, reorders its
word references, and retains the established PCM16 packing and FIFO SG/cursor
owner. Frozen hardware programs match512 samples and32 non-timing fields under
policies1/2/4. A wrong-channel output golden rejects. Ring wrapping, partial
write failure, four-word bank-edge ownership and13 adjacent unsupported modes
retain their guards. No HLE implementation or performance policy changes.

Fresh unchanged original setup, first event, ownership revocation and actual
destructors pass their expected outcomes across all three settings. Selectors0/1
complete seven input nodes and one stereo FIFO output, return naturally to
PC0031/0032 with SP0, no pending work, acknowledged frame and C4=1, then keep
polling without further RAM/bank mutation. Selector2 completes ten nodes:
seven inputs,78-word linear output, stereo FIFO output and2146-word P reload.
It then stops at original PC031E (`MOVE Y:(R0+N0),A`), reading Y:0FC9 with
R0=0FC2/N0=7. This high Y-memory route is unresolved; no invented mapping or
zero read is supplied. Diagnostic faults now retain the offending address.

These original inputs and outputs are zero. Nonzero transfer correctness is
established by the separate physical patterns; nonzero original processing,
connected clocks/reset/initial SECTL/active FE/title output remain required.
Count6/9 and27 precision deferrals are unchanged. Evidence:
`build/blinx/audio-ep-fifo-20261014/`, diagnostic trace
`build/blinx/audio-ep-fifo-trace-20261014/`, and probe
`results/ep-interleave-20261014-{stereo-clear,stereo-set}/`.

Probe readback now uses one existing armed TCP connection for sequential fixed
load/RET payloads, with no retry or protocol/XBE change. The second case's1029
reads took0.906 seconds. Four loopback tests cover exact order, denied ARM,
invalid addresses, malformed replies/disconnect and caller cancellation.

## EP linear output and common original boundary - 2026-10-14

Two physical cases establish count78 linear format6 output from X063D with
control59E2/5BE2. Both copied all78 exact zero-extended words to the SG-mapped
destination; distinct high-byte destination signatures were overwritten in
full. The alternate destination,80 source/canary words,34 control/witness
fields,9060 immutable owner words and2048 P words passed. Both allocations
were retired through separate remote dashboard/SMC recovery and a single probe
launch, with fresh PING/INFO. No user action or storage/boot change was needed.

The shared DMA owner now admits EP5BE2 only for X linear output. Y/P output,
circular and adjacent bit9 profiles retain their guards. The frozen physical
programs reproduce78 exact output words, the untouched alternate destination,
source/canary words and32 non-timing fields through the maintained CPU under
DMA policies1/2/4; a deliberately wrong output golden rejects. Immediate
busy visibility and polling count are excluded explicitly and remain required
timing questions. The physical cases read initial/terminal D6 zero, EOL80
after14 polls, immediate ACK80 and delayed ACK0.

The unchanged original selector2 now completes its eighth transfer, output
`41A2,5BE2,4E,63D,7458`. This zero-input driver setup produces78 zero words,
so it is transfer/ownership evidence, not completed signal processing. Its next
descriptor is `41A2,400603,1001,400,7458`. Selectors0/1 still stop after seven
inputs at `41A2,400603,1001,500,12000`. The initial interpretation of this as
malformed control/count4097 was superseded by the original-builder trace and
physical stereo FIFO observations above. Count1001 is a packed field and the
FIFO descriptor has four words.

Fresh original setup/event/revocation and destructor checks pass for all three
settings. Output, DMA, lifecycle, processor and counter checks retain their
existing guards; HLE processing is unchanged. Evidence:
`build/blinx/audio-ep-output-20261014/`, probe
`results/ep-output-20261014-{x-clear,x-set}/`. Count6/9, precision27 unchanged.

## EP delayed acknowledgement ownership - 2026-10-14

Two physical frame events isolate C5 acknowledgement from subsequent C4 writes.
After the first C5=2, immediate C5 was102 and the read after64 actual NOPs
was100. After the second C5=2, there was no immediate read: the first read,
after64 NOPs, was100. No C4 occurred between acknowledgement and either delayed
sample, and there was no final C4. Thus acknowledgement clears bit1 without
another ready request or an immediate read; the former C4-clear interpretation
is superseded. This does not establish C4's behavior on an unacknowledged event.

The DSP completed both events, but the CPU's final control-context guard stopped
before the SE-stop write. A later fixed read found the expected five controls;
the exact transient member that failed was not retained, so it remains unknown.
Only read-only follow-up occurred:34 fields, three ACK samples,2048 P words,
witnesses/canaries and the owned timing capture passed. Raw FF10 was0 in that
later context; it is not assigned a universal register meaning. Separate
DASH/SMC/single remote launch and fresh PING/INFO retired the owner without
human intervention. Future probes retain the five raw final guard inputs.

The existing APU now owns one queued EP acknowledgement mask for frame and DMA
flags. Reads cannot drain it. The sole DSP CPU advances acknowledgement work
at each retired instruction, independently of its selectable DMA dispatch rate.
C4 requests readiness and cannot silently consume an unacknowledged event.
Pending ACK work prevents reset/rebind, silent detach, idle FE acceptance and
new event publication; explicit DMA work cannot overtake an old queued ACK and
let it erase a newer completion. Empty startup acknowledgements remain no-ops.

Two nominal work quanta preserve the observed immediate/late ordering. They
are not a measurement of silicon bus latency or halted-CPU progress. Exact
latency, intermediate busy visibility and common-clock integration remain
required until their effect on the supported original processing is resolved.
There is no additional precision deferral at this checkpoint.

The frozen two-event program matches34 non-timer control/canary/ACK fields and
2048 P words through the actual maintained CPU; a wrong late-ACK golden rejects.
The older eight-event sequence still matches its25 non-timer fields. Actual
packet, DMA policy1/2/4, pending-ACK lifecycle and original seven-input-transfer
checks pass. No output profile or reset guard was relaxed. Evidence:
`build/blinx/audio-ep-ack-delay-20261014/`, probe
`results/ep-ack-delay-20261014/`. HLE is unchanged; count6/9, precision27.

## EP input transport and real program reload - 2026-10-14

Four physical EP cases independently establish linear format6 input with bit9
clear/set into X0BC0 and P0400. Each fresh owner completed two24-word copies.
The DSP captured each destination immediately after EOL, before acknowledgement;
the second source differed from the first. Exact SG bytes, final destination,
fresh witnesses, canaries and2048 immutable P words (2024 plus24 destination
words for P cases) passed. All owners were retired by separate dashboard/SMC
recovery and one remote launch, followed by fresh PING/INFO. No user action
or storage/boot modification was required.

The common DMA owner now admits only EP5BE0 linear input to X/P, preserving
EP Y/circular/output and adjacent-flag guards. The maintained sole CPU receives
P publication at its existing serialized work boundary and invalidates its
existing executable cache. No second interpreter, program bank or HLE route
was introduced. Count24 physical evidence does not establish large-count timing.

The unchanged original driver's actual directory, scratch and mappings now
drive seven verified input transfers for each of three settings configurations:
24 control words, its selected P0300 block (201/188/754 words), then five256-word
shared input blocks. Every destination word matches the original owned source;
source RAM, dynamic directory and GP banks remain intact. Original setup and
destructors are executed afresh. An independent authored call of unchanged
builder276/helper2E1 also copies32 words and returns normally; count32 is an
offline extent check. Neither check establishes completed EP processing.

The next required boundaries are specific: selector2 reaches linear output
`41A2,5BE2,4E,63D,7458`; selectors0/1 reach unresolved descriptor
`41A2,400603,1001,500,12000`. Investigate those original execution paths before
proposing a hardware experiment for the unusual descriptor. EP output bit9
remains guarded. Reset, initial SECTL, active FE, B3/708000 and connected title
output remain required; the family count remains6/9 with27 precision deferrals.

Hardware also exposes a timing qualification gap: C5=80 acknowledgement reads
80 immediately and0 after64 bounded read-loop iterations, without a later
D4/START or C4 causing that clearance. Immediate START D6 read0; terminal D6
read0. The existing nominal DMA service/ACK floor is not physically exact for
these intermediate observations. This change admits data transport only;
ACK/busy timing remains required. This checkpoint also identified that the
earlier frame-event sequence did not establish C4 causality. The later isolated
ACK observation and correction above resolve that causal question while
preserving the raw earlier observations.

Evidence: `build/blinx/audio-ep-dma-admission-20261014/`; physical probe:
`results/ep-dma-qualified-20261014/`. Probe tests include56 actual-DSP synthetic
control-flow scenarios and emitted-x86 ownership/stop checks; those tests do
not substitute for the four physical observations. HLE processing is unchanged.

## EP clock-owned events and original first-event boundary - 2026-10-14

Eight physical EP events completed with GP held and all nine voice-list words
empty. Successive QPC intervals were17996..18007 ticks at3375000 Hz, about
5.333 ms; successive B3 deltas were710998..711002. The initial event's phase
was measured separately and is not treated as a complete period. DSP C5 read102
at the event and still read102 immediately after C5=2. A read following C4=1
observed100 before the next actual polling loop. This sequence alone did not
isolate delayed C5 acknowledgement; the later ACK section supersedes its former
C4-clear interpretation. The meaning of background
bit8 and general CPU/DSP register aliases remain unassigned.

The bounded batch stopped SE successfully with exact readback, then verified
all34 fields, fresh ownership, canaries and2048 loaded P words. Two preceding
batches stopped immediately at newly observed event/acknowledgement values,
retained read-only evidence and used separate dashboard/SMC recovery. The
successful batch also completed remote recovery and fresh probe PING/INFO;
all prior allocation addresses are retired. No user intervention was needed.

The maintained APU now publishes EP events through its existing absolute
setup-frame clock, gated by SE/FE and the retained, running EP CPU. Executed
C4 requests the next event. The later ACK correction above replaces the original
implementation's C4-based clear with queued C5 acknowledgement. Register reads
cannot advance this clock. CPU teardown clears the callback while retaining
device control/event history; warm rebind remains guarded. Pending DMA cannot
be silently completed or bypassed by frame publication. The same real CPU
executes the frozen observer: eight events,25 exact control/canary fields,
2048 P words and unchanged other EP/GP banks and RAM. Nine B3 samples are
explicitly excluded from this control comparison, not normalized to pass.

Three clock/owner lifetimes, disabled-SE and halted-FE checks, busy-DMA rejection,
30 EP packet/30 GP frame cases and retained startup guards pass. The original
SEN test now follows actual startup through its configuration return and
P7 increment to PC0031/0032, SP0; its old C4-halt expectation was stale. Giving
that unchanged original program its first clock-owned event reaches the real
EP DMA descriptor-format guard before any transfer or output. This identifies
the next required processing boundary without inventing a PC or completion.
This retained image contains original bootstrap/helper code but STOP fill in
the dynamic table at P0100..0119. The next step is the original driver's real
table/scratch publication; this boundary is not evidence that an arbitrary
descriptor from that unpopulated table needs a new hardware experiment.

The physical cadence observation does not qualify exact instruction timing,
the known configured-prefix B3 discrepancy, the original708000 threshold,
late/missed-frame behavior, active FE ordering, EP bit9 transport or reset.
Connected title output still requires those owners and the initial SECTL
context. GP's separate C4 behavior is not inferred from EP. HLE processing is
unchanged. Evidence: `build/blinx/audio-ep-event-20261014/`; physical probe:
`results/ep-event-20261014-v3/`. Count remains6/9 and27 precision deferrals.

## EP C4 continuation correction - 2026-10-14

Physical EP execution disproved the unconditional C4=1 halt inherited from the
reference emulator. The bounded observer continued after that write, read C5=0
and entered its no-further-writes error3 branch. Fresh ownership, all34 fields
and2048 loaded P words were verified by a read-only follow-up. CPU FF10 changed
to1; the context guard stopped before any IEN/FE/SE control action. Raw FF14
had separately changed from the older100 capture to180 before the batch and
read100 afterward; no general register identity is inferred.

The sole maintained EP CPU now retains C4 control publication without halting.
Unchanged original startup returns through CA0/D5/D4=4000/D7=0, acknowledges
the empty event under its explicit startup owner, and reaches its actual C5
poll at0031/0032 with SP0. Further execution keeps polling without fabricating
an event or changing RAM/banks. Control survives owner teardown; unqualified
warm reset/rebind, unowned/busy acknowledgements and a late initial clear reject.
The historical first-yield entries below are superseded by this hardware finding.

The frozen observer matches32 control/canary fields and2048 P words. Its two B3
samples remain outside this control-flow comparison: pre-configuration B3 is
inherited context, and the configured prefix measured32 on hardware versus31
in the current model. That timing discrepancy stays in required A03 work;
the original708000 threshold is not declared qualified or deferred. The30 EP
packet/30 GP frame checks, actual original configuration/bit9 paths, counter
lifetimes and focused negative guards pass. HLE processing is unchanged.

The next event probe must execute an actual C5 polling loop instead of assuming
C4 halts. GP's separate C4/frame behavior, event production, reset/cadence,
active FE ordering and connected output remain open. Remote DASH, dashboard
SMC restart and probe return succeeded without user input, retiring all prior
allocation addresses. Evidence: `build/blinx/audio-ep-c4-continuation-20261014/`;
probe: `results/ep-wake-20261014-v2/`. Count remains6/9 and27 precision deferrals.

## EP configuration batch and original tail - 2026-10-14

A fresh hardware batch established the reached EP C5=FFF, CA=0, D7=0 and
D5/D4=4000 configuration readbacks with no DMA or output. All34 fields and2048
loaded P words passed. Initial D6 was0, correcting the model's unsupported
EP STOPPED default. CA supports only the observed zero profile; D5 admits
the exact4000 empty-list seed while other unobserved EOL seeds stay guarded.
The maintained CPU matches the frozen observer's full result, with observed
initial D5 supplied as captured context, and rejects a wrong CA golden.

An authored caller now executes the unchanged original post-yield setup tail
at02F3 through its actual RET, preserving RAM and DSP banks and producing no
DMA. The full original entry still ends at its genuine first yield: a real
wake has not been supplied. First-yield,30 EP packets,30 GP frames, retained
bit9/counter/owner guards and the busy-configuration rejection pass. HLE
processing is unchanged. Evidence: `build/blinx/audio-ep-config-20261014/`;
probe evidence: `results/ep-startup-registers-20261014-v2/`.

The dashboard SMC restart and remote probe return then completed without user
input; previous hardware allocation ownership is retired. Required EP wake,
CPU control/reset, cadence/708000 threshold, active FE and connected output
remain open. The count remains6/9, with27 accepted precision deferrals.

## Resumed EP startup: first yield - 2026-10-14

The original EP entry now executes its initial C5=FFF clear and reaches the
actual C4=1 yield at PC02F3/SP2, through the existing sole EP CPU. Admission is
limited to the executing bootstrap owner with no EP DMA history, pending DMA
or event bits. The shared C5 write-one-to-clear contract uses the existing
acknowledgement path; it neither produces an event nor invents a wake. External
FFF writes and a clear after actual DMA completion remain rejected. A second
run attempt cannot resume the yielded CPU or change its banks.

Original P workspace/directory effects, source RAM and all GP banks pass; the
ordinary30 EP packet and30 GP frame cases and the retained counter/descriptor
guards also pass. The hardware event source, CA/D5 context, reset/cadence and
EP708000 threshold remain open. This is a source-supported empty-state
continuation, not new silicon event qualification or a family closure. Evidence:
`build/blinx/audio-ep-first-yield-20261014/`.

Hardware access resumed after the dashboard migration and observed SMC restart.
The probe responds remotely and16 fixed read-only EP context loads completed.
The new dashboard leaves nonzero EP scratch/FIFO tables and different IEN,
with GP held0, EP held1 and raw FF14=100. Those inherited pointers are not owned
test buffers and were not dereferenced. The previous zero-table hardware
stimulus cannot be reused unchanged; no qualification device write followed.
The count remains6/9 and the27 precision deferrals are unchanged.

## GP configuration continuation and remaining startup gates - 2026-10-14

The unchanged original GP entry now executes its exact B2/B0/B1 configuration
and reaches the first C4 yield at PC002B. A passive counter producer belongs
to the existing serialized GP CPU. Its epoch begins after successful B1
instruction retirement, and callback reads use a cached pre-instruction
boundary without advancing time or reentering the DSP core. The frozen hardware
observer predicts all seven raw samples exactly; eleven focused tests pass.
Unsupported profiles and unmeasured warm reset/rebind remain guarded. EP's
matching passive profile has since passed the bounded checks below.

Original GP profiling branches reconverge after the same DMA/output operations.
Their clock precision is explicitly deferred as AUDIO-D08; EP's threshold can
enter a permanent error spin and is not covered by that deferral. There are now
27 accepted precision questions (14 graphics,13 cross-family). Required wake,
EP configuration, active FE ordering, initial title SECTL context and connected
output remain open. This first-yield result does not manufacture a frame or
qualify the audio family. Private evidence is
`build/blinx/audio-gp-counter-profile-20261014/`.

One actual default-context GP frame also passes through that production owner:
the real VP frame publishes MIX/event2, unchanged original firmware executes
eight DMA nodes, and832 output DWORDs match independently ordered MIX bytes.
The CPU returns to C4 yield PC002B, releases its MIX lease and preserves the
program and all RAM outside the known voice/output extents. This uses the
retained original driver's allocations, scratch and shared GP/EP aliases;
the voice/PCM loan remains an explicitly authored input. Zero context skips
the program and parameter reload branches. The next GP gate is the actual
original nonzero effects/control publication and its acknowledgement, not
additional default-frame repetitions. EP has not started. Evidence:
`build/blinx/audio-gp-continuation-20261014/handoff-v3.aot-private.json`.

The subsequent unchanged original effects upload now also returns success.
Original177AB4/1778D3 allocates, decodes and publishes its nonzero packet, then
pauses at the real177A3A acknowledgement wait. One production VP/GP frame runs
60 DMA nodes, reloads the exact decoder-produced program and clears the flags
through actual GP DMA. Only591 changed bytes from recorded device writes into
still-live original allocations are returned to the paused CPU. The original
wait returns EAX0 with stack, nonvolatiles and borrowed asset preserved. Two
malformed flags/count cases stop before frame output. The fixture's large-pool
alignment was corrected using a separate actual original-kernel observation;
the production SG alignment guard was retained. This closes the first upload/
acknowledgement gate. At the actual pre-effects PC0037, all1712 initial parameter
words also match the original decoded source after four reload/ack DMA nodes.
The reached flags3 upload therefore covers both program and parameter reload.
The retained caller/binding audit establishes no separate later effect-data/
commit service; an unreached flags2 transaction is not an added family gate.
EP and the connected shell remain open.
Evidence: `build/blinx/audio-gp-publication-20261014/handoff.aot-private.json`.
The stronger initial-state proof is `handoff-initial-state-v2.aot-private.json`
in that directory; its new checker identity supersedes the earlier binary.

The finite EP observer also completed on hardware: seven B3 samples match the
GP observation0/0/0/0/0/20/280, with34 exact fields and2048 loaded P words verified.
Its raw FF14 word changed0to100 after reset release; the first context guard
stopped, and only a separately tested read-only capture followed. No later
hold/restoration write occurred; the owned allocation remains retained. The
register's role, original EP708000 elapsed threshold, C5 event semantics,
wake/cadence and full output remain open. Probe evidence:
`results/ep-b3-physical-20261014-v2/summary.aot-private.json` at9ea85fb.

The existing EP CPU now owns that exact passive B2/B0/B1 profile, with a
separate retained lifetime marker from GP. Nine focused checks pass, including
both frozen observers, independent reset/rebind guards and GP regression. The
unchanged original EP entry initially advanced to its first unsupported mode
at P0017: `BSET #20,OMR` enables DSP stack extension. That retained failure
motivated the bounded resident-stack implementation below.
This closes the bounded EP profile producer, not EP startup, cadence or its
708000 threshold contract. Evidence and 53-pin root review are in
`build/blinx/audio-ep-counter-profile-20261014/`.

The maintained donor preparation now supports SEN only while the hardware
stack holds all entries: SP=SC, depths0..14, within SZ, with no extension history
or interrupt. The unchanged original EP prefix reaches its actual C5=FFF write
at P02EF, with maximum depth3 and SP=SC2. Its30 P-context words are cleared,
saved directory fields and startup P1..5 are exact, and X/Y extension memory,
GP banks and source RAM remain unchanged. The real production EP owner stops
at that C5 operation; no acknowledgement or yield is supplied. Spill/refill,
task switching, explicit stack ports, early loop abort and interrupts stay
guarded. This removes the reached stack blocker without claiming full SEN.

Six resident-stack tests,30 single/block negative executions,24 existing
non-SEN regressions and the maintained EP packet/guard checks pass. The actual
original GP effects frame still performs60 DMA nodes, loads1712 exact parameter
words and leaves RAM byte-identical to its accepted capture. Root verified
72 handoff pins and independently regenerated all8 prepared Rust files.
Evidence: `build/blinx/audio-ep-stack-extension-20261014-worker/handoff.aot-private.json`
and `build/blinx/audio-ep-stack-extension-20261014-root/`.
Hardware remains deferred at the owner's request. EP C5/CA/D5, wake/reset,
cadence and the708000 threshold remain required; families stay6/9, with27
accepted precision deferrals and no HLE processing change.

## Hardware-session tooling and GP observations - 2026-10-14

Current result: the circular output clear/set pair also passed physically,
establishing exact format2 SG bytes, preserved alternative destination, cursor
28-to-12 wrap and EOL followed by idle. Both fresh owners passed separate
hold/SG restoration. The maintained GP implementation now admits exactly
5BE0,4BD0,4BD2 while retaining all other bit9 profile and EP guards. Seven
focused tests pass; count32 coverage is offline, not an additional physical
measurement. Probe compatibility checks also pass against this admission.

The unchanged preserved original GP graph now passes all 1,501 blocks and
73,549 real DMA nodes, with 192,078 nonzero output words and 14,931 retained
scratch bytes matching native output and physical SGE transport. This completes
the bounded bit9 issue `.114`. The fixture uses the same DSP instruction core
on both sides and an explicit graph shell; it does not qualify original
boot/framework scheduling, EP/FIFO/AC97 output or the full audio family.
B0-B3, reset/wake/control context and connected title output remain in A03-A05.
Private evidence: probe `results/gp-output-physical-20261014/`, main
`build/blinx/audio-gp-output-admission-20261014/` and
`build/blinx/audio-gp-original-output-20261014/`. The chronological observations
below retain earlier guards and failures without changing their meaning.

The next finite GP configuration observation also completed. With exactly the
original B2=FFFFFF/B0=1/B1=1 stores, raw B3 samples were0,0,0,0,0,20,280.
The preconfiguration16-NOP interval stayed zero; subsequent16/256-NOP intervals
plus sample/store instructions advanced20/260. All38 fields, witnesses,
canaries and immutable P passed; separate hold/SG restoration matched.
No DMA START, frame event, EP, storage or boot operation occurred. This supplies
a concrete configuration/counter observation for the next producer decision;
it does not yet determine clock source, rate, epoch, halt/wake or EP behavior.
Private evidence: probe `results/gp-b3-physical-20261014/summary.aot-private.json`.

The original-sequence input-only baseline subsequently copied all four expected
scatter/gather source words into X0400-X0403. Its descriptor and eight new run
witnesses matched, and all 2,044 immutable program words plus the four excluded
P target words verified. STOP/FREEZE/UNFREEZE/START samples were 0/8/0/16, so
RUNNING acquisition was observed. However, all six bounded EOL samples were zero;
terminal D6 was 16, error was 10, and the success token remained zero. The later
CPU capture proves the bytes arrived, not when they arrived relative to the last
DSP sample. This is **input-byte evidence, not completed-list qualification**.
No GP cleanup or retry followed that failed capture. After a normal firmware
reboot, a new probe launch, sixteen matching quiet/mapping reads and a fresh
allocation, the longer observation at probe `9548cf2` passed physically. C5 EOL
was 80 on sample 10 of the 1,024-sample bound; terminal D6 was 0, error was 0,
and token/phase/witnesses matched. All four X words again matched the SG source,
the descriptor was unchanged, and the 2,044 immutable P words plus four excluded
P words verified. This establishes the bounded input baseline's bytes and
completion. The six-sample window was shorter than this observed completion;
no universal latency or cycle-accurate timing follows from two separate runs.
The same seven peripheral writes were used. Three owner-bound checks passed,
covering 19 maintained DSP scenarios and four x86 transport checks, without a
production library or XBE rebuild.

A separately reviewed retirement verified the exact completed program and all
38 fields before holding GP and restoring its original SG pointer; both immediate
readbacks matched. Seventeen actual-x86 retirement scenarios passed. A new
allocation and independently observed quiet floor are ready for the original
5BE0 input comparison. No allocation was freed or reused, and no dashboard,
storage or boot changes were needed. Bit9 admission and production LLE/HLE
remain unchanged. Baseline and retirement evidence are probe
`results/original-input-eol-physical-20261014/` and
`results/input-eol-retire-20261014/`.

The subsequent original5BE0 comparator also passed physically (probe
`c35688c`). In its distinct allocation, initial C5/D6=0/0 skipped the optional
acknowledgement. All four X words matched the59E0 SG signature, all five
descriptor words were retained, and EOL80 appeared on poll10 with terminalD6=0,
error0, the comparator token, phase2 and all eight witnesses exact. Full P
verification passed. This supplies independent clear/set evidence for the
four-word linear format6 GP input shape; it does not establish all bit9 cases.
The next representative batch covers Y/program destinations and circular input.
The four owner-bound checks passed, including explicit synthetic CPU control
flow and strict production-bit9 rejection. Production LLE/HLE are unchanged.
Private evidence: probe `results/input-bit9-physical-20261014/`.

The subsequent six-case hardware batch also passed:59E0/5BE0 to Y and P,
and49D0/4BD0 circular Y. All four destination words, preserved-bank canaries,
descriptor fields, eight witnesses and2,044 immutable P words matched.
The32-byte circular ring wrapped from28 to12 with exact high24 data.
Linear EOL appeared on poll10, circular on15; every terminalD6 was0 and every
error was0. These are observed completion samples, not a silicon timing law.
Every case used a fresh owner and passed separate completed-program retirement
with exact immediate hold/SG-restore readbacks. No storage or boot writes occurred.

This batch also corrected the probe's Y capture aperture. The first Y-clear
case passed after four read-only loads from the correct address; the program
was not rerun. Original and corrected captures remain separate. Earlier samples
labeled Y actually read an aperture gap and cannot establish Y contents; the
earlier X/P/completion conclusions are unaffected. Both samplers now have
independent address-sentinel tests, with21 focused tests passing and zero skips.
Private evidence: probe `results/gp-shape-batch-physical-20261014/` and
`docs/GP-PAIRS.md` (probe commit `f461234`). Narrow GP-input admission for5BE0/4BD0
is now implemented, preserving all existing extent, ownership, MIX and executable
publication checks. EP/output/FIFO and adjacent bit9 profiles remain rejected.
Seven focused tests pass, including paired exact bytes/cursor/EOL at counts4/32
and retained rejection/publication failures. Count32 is an offline existing-
transport regression, not a larger-count physical measurement.

The preserved original graph was rebuilt against that change and advanced
past its first4BD0/count32 input node. It then stopped after one completed node
at circular output4BD2/count32. Its authored contract still passes. No original
descriptor or byte was changed, and output remains guarded. This identifies
the next required physical pair: circular GP output with bit9 clear/set into
owned temporary RAM. Private result:
`build/blinx/audio-gp-original-admission-20261014/report.aot-private.json`.
Whole graph execution, EP/output and broader A03-A05 remain open.

The same physical observations also corrected GP initial/idle-STOP D6 from the
model's invented STOPPED32 to the observed0. FREEZE/UNFREEZE remain8/0;
busy STOP and EP semantics are unchanged and remain separate. Seven focused
regressions pass, including absence of RAM/node/EOL effects on the idle path.
This uses the existing hardware captures, not another console test cycle.

Private evidence and immutable source snapshots are probe
`results/original-input-baseline-20261014/`, including `physical-assessment.aot-private.json`.

The original STOP/FREEZE candidate is prepared at probe commit `2be3ab8`.
It records initial C5/D6, issues at most two D6 control commands, and checks
RUNNING-clear then FROZEN-set with sixteen samples per stage. No descriptors,
START or transfer are issued. Thirteen actual maintained-DSP scenarios and
four actual-x86 transport checks pass; terminal witnesses alone cannot report
success. Root acceptance is probe
`results/gp-initialization-offline/accepted-root.aot-private.json`, SHA256
`10c5840817ddd1b0fd69bf2b8f6f0a513dc0ccafcabca48e97abb0fdbe4ebfcb`.
The candidate subsequently passed physically: initial C5/D6 were 0/0, STOP 2
returned D6=0 and FREEZE 3 returned D6=8, both accepted on their first sample.
All eight new witnesses, success token, error=0 and phase=2 matched; all 38
fields and 2,044 immutable program words were retained. No descriptor, START,
transfer or output was issued. This resolves the idle initialization prerequisite,
not busy STOP, bit 5, universal reset defaults or DMA semantics. Production
LLE/HLE behavior is unchanged. Physical evidence is probe
`results/initialization-physical-20261014/physical-assessment.aot-private.json`.

The prior peripheral observation's ownership context was retired by an ordinary
dashboard launch for probe maintenance. Subsequent software launches are not
cold boots; the next device experiment requires fresh context, ownership and
witnesses. The probe now provides physically checked concurrent FTP and TCP:
an exact file roundtrip retained TCP liveness during upload and download.
Startup failures and drive availability are readable through TCP INFO, and an
updated executable was staged, verified and launched remotely without owner
screen-reading or a dashboard round trip. Normal boot paths are unchanged.
The reached circular bit9 output pair subsequently passed as recorded above.
Next evidence concerns the remaining original startup and wake/control context.
Remote access removes a session-maintenance obstacle;
it does not close A03 or change
the **6/9** family count or 26 accepted precision deferrals.

## Corrected GP diagnostic on the restarted console - 2026-10-13

The subsequent finite peripheral observation now completes on physical hardware.
After an owner cold boot and standalone v0.5 launch, sixteen fixed control/mapping
reads matched the quiet floor. A new pinned nine-page allocation and unique
execution signature passed three current-owner checks and all 58 artifact pins.
SG installation and RESET 3 read back exactly. All eight C5/D6 samples were zero,
with agreement between absolute and MOVEP addressing on both passes; sample count
8, phase 2, the completion token and all eight witnesses matched. The independent
immutable-P comparison passed all 2,044 words.

This resolves the peripheral-read prerequisite and contradicts initial D6=32 for
the observed startup context. It does not establish general reset defaults,
STOP/FREEZE transitions, DMA or bit9. Incidental X0400-X0403 data are retained as
observations, not transfer evidence. The next device experiment must follow the
original bounded STOP/FREEZE initialization, with separately reviewed fresh-owner
provenance. No production LLE default or HLE behavior changed. Private evidence:
probe `results/peripheral-physical-20261014/`; families remain **6/9** and the 26
accepted precision deferrals are unchanged.

The earlier corrected diagnostic and the source audit that motivated this new
observation are retained below.

The owner confirmed a restart and the final hardware phase resumed using the
installed v0.4.0 probe. Identity and sixteen fixed control/mapping reads matched
the required quiet floor. A fresh nine-page allocation and a new durable-ledger
signature bound the corrected 734-word diagnostic to this session. Three focused
owner-specific checks passed, including thirteen DSP instruction paths; unchanged
broader preparation proofs were reused. All 55 source/context/payload identities
were checked before the separately reviewed hardware phases.

Preparation, full preflight, SG installation and RESET 3 returned exactly the
requested results. The physical capture retained all 38 raw fields and verified
all 2,044 immutable program words. Entry witnesses matched and phase was 1;
completion witnesses, metadata and descriptors were zero. X/Y targets remained
zero and P targets remained STOP. The acceptance helper rejects finalization.
The corrected run no longer reports the earlier false completion, but does not
establish DMA, bit9, the hardware wake cause or an exact stopped instruction.

This narrows the next investigation to original startup/peripheral read context.
The authored D6-equals-32 guard was explicitly an offline starting assumption,
not independently established silicon state. Preserve the failed result and
resolve its provenance before any further device write. No automatic retry,
reset, restoration, allocation free or storage operation followed the capture.
Private evidence is under probe `results/qualification-20261013-resume2/`.
Families remain **6/9**, with 26 accepted precision deferrals; no HLE behavior
changed.

The subsequent original bootstrap audit found explicit STOP/FREEZE initialization
before descriptor publication, followed by UNFREEZE/START in its transfer helper.
It tests RUNNING bit 4 and FROZEN bit 3, with no initial whole-register-equals-32
requirement. The independently prepared peripheral-read program compares C5/D6
absolute loads with MOVEP reads, recording every value without DMA or peripheral
writes by the DSP. Eleven offline tests pass, including unchanged fresh-diagnostic
contracts, actual x86 transport and three maintained-DSP input cases with stable
terminal resumes. The later physical result is recorded above; no production DMA reset default,
descriptor guard or HLE behavior changed. See probe `docs/GP-PERIPHERAL-READ.md`.

## Offline frontier and final hardware dependencies - 2026-10-13

The owner renewed the full qualification goal with real-console work last.
This is the end of the presently identified *independent* offline tasks, not
completion of the remaining implementations. Once observations establish the
missing contracts, their implementation and connected title checks still follow.
Do not count a probe improvement, source audit, or prepared capture as another
qualified family. The count remains **6/9**, with the same 26 precision deferrals.

| Gate | Offline work already available | Required next evidence / implementation |
| --- | --- | --- |
| G02 | Original AV producer chronology, encoder controls/timing and disabled-caption C4 contract | Establish the supported startup context and display activation. The live D4 encoder selection cannot supply the retained 8A Conexant state; preserve that profile distinction. Then execute the original route without warm seeds. |
| G03 | Passive original producer/first-transfer capture is integrated at `24468bc`; independently sourced internal/RDI packing is now recorded below | Bind saved-instance byte offsets and save/restore semantics to actual consumer state, including required RAMFC/ownership fields. Internal lane packing alone does not establish that map. Capture the real first request after G02; never replace it with an invented image or successful no-op. |
| G04 | Guarded diagnostic GPU bootstrap and paired noise capture preparation | Obtain the required reached enabled-noise primitive and independently establish its supported sampling behavior. Preserve the existing enable guard until then. |
| G05 | Existing representative state, lifetime, scene and producer evidence | Run the connected original graphics route after G02-G04, retaining real completion/reentry/teardown and affected representative comparisons. Reuse unchanged evidence; exhaustive scene-pixel composition remains deferred. |
| A03 | Original directory/storage/composed startup audits; sole GP/EP processors; physical GP input/output pairs; exact5BE0/4BD0/4BD2 admission; preserved original graph passes1,501 blocks/73,549 DMA nodes | Complete original shell/owner execution: B0-B3, remaining reset/wake/control transitions and EP remain required. The graph fixture does not supply original scheduling or output ownership. |
| A04 | Idle FE publication and guarded original-DPC reproducer | Establish active-work ordering and initial control provenance, then connect publication to genuine completed work. Restoring idle register values cannot discard prior work history. |
| A05 | Shared original-device adapter, compiled/raw MMIO route and real PCM capture consumer | Connect admitted GP/EP/VP work to the shared-clock owner after A03/A04. The current work callback advances the codec while DSPs remain held; wiring complete does not mean original DSP scheduling/output is complete. Then run actual output/ISR/DPC/stop/reuse/timing acceptance. |

The old G03 preparation handoff is superseded by its accepted maintained
implementation. This sweep reverified 103 unique retained artifact identities
(146,443,908 bytes), with no mismatch, and reused the existing 34 snapshot ABI,
two raw-hook and three disabled-observer controls. No replacement capture API,
unchanged build or title run is needed merely to re-establish that milestone.

Pinned envytools `f102b82381f3f11cee113d16374c87091db039d9` supplies more
specific evidence than the earlier context-documentation review:

- [`nvhw/pgraph_kelvin.c`](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/nvhw/pgraph_kelvin.c#L601)
  maps Kelvin upload words to internal XFPR storage as
  `[u3, u2, u1 & 0x0fffffff, 0]`; its constant-load helper retains component order.
- [`hwtest/pgraph_state.cc`](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/hwtest/pgraph_state.cc#L2214)
  gives matching RDI load/dump order: XFPR lanes retain order; XFCTX and VAB
  reverse four-lane order. Lighting storage has explicit 22-bit packing and inverse.

These are internal/RDI representation contracts, not proof of saved-PRAMIN
offsets, NV2A context-transfer effects, or new physical test results. The bounded
`hwtest/` and `nvhw/` search found no Kelvin/NV2A saved-image serializer; the
`400784` match belongs to G80. Do not substitute another GPU generation's map.
The private source-pinned supplement is
`build/blinx/graphics-startup-consumer-20261012/envytools-packing-supplement-20261013.aot-private.json`,
SHA256 `8ce4d5000b5d04edbfbc9f185bd33ae78998387e67dd91118531bce61a699aa5`.

The owner restart prerequisite was satisfied before the corrected diagnostic and
again before the peripheral observation above. The standalone v0.5 probe is now
installed; its software self-restart command remains physically unqualified and
does not itself establish recovery or a cold boot. New hardware requests must
use fresh owner/context provenance. Do not rerun the completed marker prelude,
rewrite the currently started source, or retry an earlier failed write automatically.

## Fresh GP execution exposes a diagnostic mismatch - 2026-10-13

Independent G02 work now admits documented C4 readback selectors and the caption
gate with both caption fields disabled, matching the fields preserved by the
original AV disable operation. Focused positive/negative and affected SMBus checks
pass; startup state, enabled captions and legacy transport remain guarded. See
the [bounded C4 contract](../pilots/native-title/GPU-COMPLETION-LLE.md#c4-selectors-with-caption-encoding-disabled---2026-10-13).
This removes a control-register limitation without closing G02 or changing HLE.

A separately allocated, never-installed nine-page source passed preparation and
full preflight. Its SG installation and RESET 3 start returned the exact requested
values. This candidate reused the earlier bounded marker/STOP and full-P ownership
evidence while preserving the old allocation; it did not rely on RESET 0 bank
contents. Eight focused offline tests and three checks using the actual reserved
witness passed, including six maintained DSP instruction paths.

The physical capture contains all 38 raw fields and an independent comparison of
all 2,044 immutable P words. All eight unique witness words and phase 2 appeared,
but the original status, success/error and descriptor fields were all zero, and
none of the target banks matched either source signature. The acceptance helper
correctly rejects this result despite the matching witnesses. Loaded code and a
final witness do not establish correct guards, DMA completion or bit9 semantics.

The offline audit found a diagnostic flaw: after a guard failure, a synthetic
resume from STOP enters the adjacent final-witness block without DMA activity.
The probe now gives each terminal STOP a branch back to itself. The frozen old
image fails the resume regression; the corrected image passes. Eight focused
tests cover thirteen DSP paths, including repeated resumes after success and
failure. This fixes the diagnostic, without establishing the physical wake cause
or changing the production DSP model. A new hardware run is still required.

No automatic retry, reset, restoration
or free followed the capture; both owners remain pinned. HLE behavior is unchanged
and the family count remains six of nine. Private evidence is under probe
`results/qualification-20261013-fresh/physical-assessment.aot-private.json`.

## Hardware resumed: owned GP bootstrap passes - 2026-10-13

The owner restored the probe and the final hardware batch resumed. Thirty-nine
fixed display/APU reads completed. A fresh nine-page, 16KiB-aligned owned low-RAM
allocation passed physical/protection/extent/pinning checks, independent original
SG inspection and exact SG calibration. The authored six-word GP program produced
its marker; an independent observer compared all 2,048 loaded P words. This closes
the owned-mapping/marker prerequisites, not A03 or the audio family.

The prospective hold requested RESET 0 and read back exactly 0. Subsequent control
reads retained the quiet floor, but P0000 returned the previous marker instead of
the verified instruction. Five further distinct X/Y/P addresses returned that same
marker while RESET remained 0. These observations suggest an inaccessible or stale
host aperture; they do not establish actual bank contents, erasure or aliasing.
The held success/error reads also both equal the success token, so they cannot
supply the clear-start binding. No source replacement, clear-bit start, recovery
write, restoration or free/unlock followed. The allocation remains pinned until
ordinary owner restart. Do not guess RESET 1/2 or reuse these held-marker reads.

A bounded original DirectSound audit shows source preparation before its RESET1,
control setup and RESET3 sequence, with no X/Y/P host reads between those stores.
Readable held banks are therefore a requirement of this diagnostic, not an
established requirement of the title route. The next preparation should use a
separate fresh owned-source lifetime: verify source/SG/guards before pointer
installation and first start, let the DSP initialize its own results, and bind a
nonrecycled execution witness to actual loaded P, descriptor/status and target
observations in the supported running/STOP context. Sole ownership/quiescence and
freshness still need explicit proof; a token alone or a guessed reset sequence
cannot supply them. The successful marker/bootstrap evidence is reusable and
need not become a repeated prelude. This is the next design, not DMA acceptance
or a recovery write in the current session.

The first prepared encoder 8A/C4 HAL read returned C0000185
(STATUS_IO_DEVICE_ERROR); 6C was not sent and the failed read was not repeated.
The live kernel version is 1.0.4983.6. A bounded export lookup, mapped-window checks
and 26 exact selected AV code DWORDs establish the live cached encoder-slave
address. Its BYTE is D4; the source-bound retained snapshot's BYTE is 8A.
This establishes different cached encoder selection, not a chip identification.
Neither the HAL status alone nor this console's current encoder state supplies
the retained Conexant EN_REG_RD/6C/C4 startup state. Resolve the original producer
or a matching independently supported context; do not transplant live D4 values.

Private physical acceptance in the probe worktree is
`results/qualification-20261013-resume1/accepted-hardware-milestone.aot-private.json`,
SHA256 `b51c314777dc714d5611d7ed8bf7bbbcc7c9f089e00ba5e1f0be8965467da16c`.
The adjacent source review preserves live/retained identity and reset-access limits.
No unexpected write readback, storage/boot/EEPROM/flash operation, new title run,
unchanged test rerun or production/HLE change occurred. The earlier unrelated
unexpected SG write was not retried. **6/9 families remain qualified**, with 26
precision deferrals unchanged; the remaining work now has these narrower physical
boundaries rather than a console-availability blocker.

## Movies, scheduling and final census closed - 2026-10-13

Q03 passes all seven frozen numeric gates after the full 600 GP seconds
(28,800,000 frames), with zero capture errors or runtime stop/fault lines.
Both natural op01 lifetimes reach source picture 2263, actual Stop/Destroy and
title return. Their real Play durations are 75.566/75.567 seconds. Independent
media waveform RMSE is at most 0.004625, correlation at least 0.999828,
and A/V absolute p95 is 52.296/52.269 ms against the declared 66.800 ms bound.
Each comparison uses one fixed alignment per actual Play, includes the quiet
tail, and derives pitch from the source rather than fitting its rate.

The later Start at 445 seconds interrupts the actual Artoon movie (voice 731),
with Stop/Destroy at 445.014 and release at 446. Root reviewed all 12 retained
scene witnesses: title 339, loading 349 and fresh 3D Blinx demo gameplay 393
establish the required return/reentry. The final diagnostic opening remains
playing at cutoff and is not substituted for a required completed lifetime.

Retained original thread/IRQ/KDPC/FP/clock and matching default-HLE proofs are
reconciled with this changed integrated title result. The full run also passes
the corrected second-demo physical-publication boundary. Movies `.73` and
scheduling `.112`, including the coherent-clock and display-ownership integration
tasks, are qualified for this bounded attract contract. The independent static
import audit closes only its historical inventory, not every imported branch.

A01 closes with 10,348 complete accepted records, 1,094 generations, 1,077 played
ADPCM generations and 1,090 actual terminal releases. Four objects legitimately
remain at cutoff. New send vectors/loop extents reuse qualified processing
stages; downstream original DSP graph/output remains required under A03-A05.
Root reviewed the Sol 6.1 census/static proofs and their independent parsers.

Durable root acceptance is
`build/blinx/q03-final-20261013/qualified-root.aot-private.json`, SHA256
`aee59a1cab5ee2e8e26d6d818f5ddad010b87150cf2e98c0b4bdfc0d2d301ea7`.
The unchanged bulk retention index binds 552 byte-verified files / 2,274,098,392
bytes on F:; the root proof separately binds the retained selected configuration.
Original Z: paths resolve through that index. Raw reports retain their historical
pending-review flags; the root proof supplies the final acceptance.

The title took 27,843.141 host seconds. Return code 1 is the independently
observed host termination after successful bounded capture, not guest teardown.
The deadline guardian never intervened. No test, title or hardware rerun was
needed for closure. The executed source is frozen `a3f0388`, with selected-path
compatibility to maintained runtime `3e43614` reviewed; current HEAD execution
is not asserted. Cadence 32,000 is an explicit execution schedule, not a physical
CPU-cycle claim. No HLE runtime/performance change is introduced here.

**6/9 families are qualified; audio, GPU and graphics remain open.** G02-G05 and
A03-A05 are the remaining required dependency chain. The 26 accepted precision
deferrals are unchanged. Hardware resumed after owner-confirmed probe readiness;
the current physical findings and remaining prerequisites are recorded above.
Earlier progress entries preserve their then-current counts and acceptance limits.

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

The loop/control readiness audit reuses the accepted A01 prefix: all194 region
setters precede their generation's first Play, including20 nonzero-loop Plays.
Original stopped SetLoopRegion stores settings and returns without region PIO;
later Play owns actual region/start writes and the derived control queue. Root
verifies16 original function bodies, two vtables and the existing source/stop
proof limits. No additional required offline loop implementation was identified.
Do not repeat arithmetic fixtures or substitute an authored allocated/active
object for the required original title route. After A03/A04, observe original
allocation, pre-Play region setup, actual Play/loop/Stop/DPC/output together.
Proof `audio-loop-control-readiness-20261012/accepted-root.aot-private.json`,
SHA256 `95e5957b43c4fcc8244f23726173e5cf708308480cea821d98abdafe8355abd4`.
This is readiness evidence; full A01/A05 and audio remain open.

The bounded A03 preparation `.113.26` is implemented in probe commit `bb208df`.
Seven separate offline payload variants connect the existing owned low-RAM and
marker prerequisites to the unchanged539-word clear-bit DSP diagnostic. Hold and
start each end at one reset store's immediate raw readback. RAM preparation changes
only the8192-byte source and two16-byte signatures; all9216 owned DWORDs and actual
page ownership are checked. Held samples bind the initial success/error words;
start rejects stale success or changed markers without clearing them. Running
samples retain29 raw metadata/descriptor/target fields. Independent P verification
compares2044 immutable words and preserves the four possible P target results.

Seven focused checks pass across79 extracted-x86 contexts, with zero skips;
the affected builder follow-up and optional missing-source discovery also pass.
Root reviews463 artifact/source identities, exact allowed memory changes, all29
raw fields and13 store/readback cases without repeating functional or DSP tests.
Root proof `audio-gp-clear-payload-readiness-20261012/root-review/accepted.aot-private.json`,
SHA256 `bff2759b544ff0215c39afe5aa1e897e73571783988d12f51b224d65aa6cd019`.
The integration receipt records only line-ending/trailing-whitespace normalization
of the four probe files after that review. Existing marker, low-RAM, original
descriptor-builder and DSP proofs are reused. Actual calibration/bootstrap,
held/quiescent bank context, DMA interpretation, GP/EP startup, active FE ordering
and title output remain required. This adds no hardware runner or production
semantics and performs no console operation; audio and the4/9 family count remain
unchanged.

A05's offline readiness audit found a concrete integration gap: `ORIGINAL`
already retained original DirectSound execution, but the native-title runtime
did not own or map the complete APU/AC97 devices. The shared engine already
provides synchronous DMA, device work, PIT time and PIC interrupts. Task
`.113.23` connects these interfaces with one APU/AC97 pair and held GP/EP
owners, guarded access admission and exclusive build selection. Unknown
reset/wake, bit9 input and active FE ordering remain A03/A04 dependencies;
wiring alone cannot qualify original audio startup or output. The older
original-title stop at ordinal160 predates the current IRQL transport and is
not a demonstrated current failure. Root reviewed the source boundaries and
35 frozen audit inputs; no new title or hardware run was needed. Readiness
handoff `audio-original-integration-readiness-20261012/handoff.json`, SHA256
`e40e157d158fb84e787f7ba168351ae76f460ef0290f27778748ebde3b44ff48`.

The bounded `.113.23` wiring implementation now passes nine focused controls.
An authored raw CPU thread executes registered ACI MMIO, descriptor DMA on
original allocations, shared clock work and PIC IRQ6 acknowledge/EOI. Its
consumer receives exactly1024 bytes from one512-word descriptor; GP/EP stay
held. Unknown reset/FIFO gaps, absent/rejecting consumers, invalid DMA extents,
partial registration rollback and retained active/faulted owners pass. Root
review corrected the initial clock origin and overly broad FIFO admission.
Six actual CMake selection cases and the changed runtime translation unit pass;
29 frozen source files and50 artifact identities are verified without repeating
functional tests. Root proof `audio-original-device-20261012/accepted-root.aot-private.json`,
SHA256 `2ed6a4a206695499d15d4da5e20ba00a1b62323f54ebff20b45a2808c702e7aa`.
The compatible ORIGINAL profile selects the adapter automatically. This wiring
checkpoint left the real output consumer for `.113.24`; compiled MMIO without
shared-engine serialization remains guarded. Actual title startup/processing,
original audio ISR/DPC, output and stop/reuse remain A05 work.

The bounded `.113.24` consumer now accepts actual AC97-fetched PCM into owned
memory with passive device-lifetime, PIT epoch, observed PIT and codec-frame
stamps. Full, failed or finalized consumers reject whole chunks before device
completion without altering accepted bytes or stamps. ORIGINAL runtime can
select a new private destination through `XLLE_PILOT_ORIGINAL_PCM_CAPTURE`.
Absent selection retains the before-DMA RUN guard. No native decoded source,
audio worker or second clock is introduced. Complete publication requires
retired device ownership and successful write/flush/close before a non-replacing
move from the exclusive `.partial` file. Active or faulted owners retain their
consumer; a readable footer in a failed partial is not successful persistence.

Fifteen focused controls pass, including actual raw CPU RUN/DMA/clock/PIC and
exact1024-byte output readback, nonempty-prefix rejection, retained ownership,
rollback and write/flush/close/publication failures. Root verifies31 frozen and
current sources and71 artifact identities, independently reads the output, and
builds the production library and changed runtime translation unit. No title
or unchanged functional test is repeated. Root proof
`audio-original-pcm-root-20261012/accepted-root.aot-private.json`, SHA256
`109bbd2b68bea463dfe5fb1a20fda428031d64cd9ee05fc25fbafca6a986f1ba`.
This closes the consumer connection, not A03/A04 or actual title output;4/9
families remain qualified.

The bounded `.113.25` compiled-MMIO transport now passes four focused processes.
Only a real saved-context original ISR/DPC continuation receives a same-thread
scope; nested original execution suspends/restores that scope. Dispatch uses the
existing engine lock, complete registered owner range and checked DMA capability.
It adds no device, clock or interpreter invocation. Failed reads publish no value,
and the first terminal failure survives later callback/context errors. Successful
runtime fault decoding resumes the compiled instruction.

Actual original IRQ3, nested higher-priority IRQ1 and a queued DPC exercise seven
native MOV fault resumptions. Two fixture register reads borrow16-byte DMA from
an actual original contiguous allocation. Wrong-thread, outside-scope, nested
bus and raw nested-work access reject; compiled reads add no work pump or PIT
advance. Unknown GP reset3, callback failure and complete-extent DMA failure
preserve host context and retained owner lifetime. The fixture proves transport,
not actual title callbacks, active audio or DSP/AC97 output.

The compatible ORIGINAL configuration and changed runtime translation unit build
pass. Root verifies46 current/frozen source pairs and127 identities, confirms
native audio backend exclusion and nine unchanged raw-MMIO/DMA/clock/mapping
functions, and corrects read publication to require no retained failure. No
unchanged functional/title checks repeat. Root proof
`audio-compiled-mmio-20261012/root-review/accepted.aot-private.json`, SHA256
`89b73bec8bb939dcf440f1de8154c0df24bd1f12d28bde656852b619f5afe2ed`.
Ordinary original title DirectSound runs on the raw CPU; no previously reached
compiled-title guard is claimed. A03/A04 and actual A05 startup/ISR/DPC/loop/
output/reuse/timing remain open. Existing held-device and unknown-semantics
guards remain in force; the independent v9 capture generation is unchanged.

The current integrated ORIGINAL startup boundary is now established by `.113.27`.
One hidden, silent Z: attempt on CPUs12/13 returned naturally with code2 after
55.375s. Actual original USB initialization precedes the retained audio-device
admission guard at log873; there are no actual draw submissions. The112-byte
AC97 partial contains a failed header/footer with zero PCM bytes and zero stamps.
The generic error does not identify the guarded transaction, so register-specific
attribution requires a diagnostic improvement before another bounded attempt.
This is a current startup-boundary result, not A03/A04/A05 or audio acceptance.
Root verifies the execution, manifest, log and empty failed capture and preserves
the small evidence set on F: under
`audio-original-title-startup-20261012/z-startup-root/accepted-root.aot-private.json`,
SHA256 `2ce0592389a90b90dc9f1f4cc19420529006852e670e652456e6e8666b007e5b`.
Temporary Z: staging changes only generated runtime paths and relinks the accepted
objects/libraries. The fresh disk matches the original8GiB seed before launch;
post-title disk contents are unmeasured and cannot be reused as a fresh seed.

Follow-up `.113.28` adds address/width/direction and write-value diagnostics
before the unchanged admission guard. Four focused controls pass, including
failed-read value preservation and raw-engine first-failure transport. One
changed diagnostic title naturally returns2 after57.890s at a DWORD READ of
SECTL `FE802000`; no read value, actual instruction PC, draw or PCM is claimed.
The independently hashed post-prior-run disk is a distinct diagnostic input,
not a fresh seed. Root verifies the small retained result and build provenance,
including a corrected private-project CMake regeneration mistake, without
repeating functional tests. Durable proof
`audio-original-title-startup-20261012/guard-attribution-root/accepted-root.aot-private.json`,
SHA256 `7753d1811dde6a0fb8457850d690cbe0c75f7ce55e5133e08f20304efd1cce78`.

SECTL's generic zero/last-written model fallback is not an independently
established startup state. The accepted SECTL7 observation belongs to an
explicit captured idle probe context. Original helper17BC9E preserves bits
from a SECTL read; original code17C1EF tests bits0x18. Neither establishes the
initial value of this title's context. At this checkpoint the read remained
guarded and initial control context joined A03/A04's final hardware batch.
The2026-10-14 initialized-kernel handoff section above supersedes this initial
SECTL/IEN dependency with original kernel init/retirement evidence and retained
physical corroboration. This diagnostic attribution alone did not close audio.

The Q03 batch `reference-movie-batch-20261011/q03-c32000-v6` used frozen sources
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
Generation v5 was stopped after841.406 wall seconds: the original scheduling
launch never bound the host input provider to OHCI, so the planned Start could
not reach the original controller code. The maintained launch now binds that
provider without starting the native host-clock worker. Five affected checks
pass; actual OHCI enumeration and endpoint2 DMA deliver all18 Start/release
bytes into original-owned RAM, preserve time through host stalls and reject a
backwards publication. The unbound control fails on the expected Start bytes.
Proof `reference-input-ohci-20261012/qualified.aot-private.json`, SHA256
`a3d83229b2a9a3f15e2edb27b34c938f5838697d24ca94dc73973639b7556f94`.
The restarted generation checks for actual replay initialization immediately.
Final movie/scheduling acceptance awaits
the actual two natural openings and subsequent Start/release/reentry evidence.

`tools/native_reference_movie_closure.py` reviews the finalized batch without
another title run. It identifies sources from independent luma references,
uses one fixed audio alignment per real Play, compares the complete opening
media span and requires actual Stop/destroy, final picture2263, Start/release
and subsequent title. Whole-run artifact identities and zero-error capture
finalization must pass first. A separate waiting process executes frozen analysis
once after a successful batch finishes. It starts no title or hardware operation and
does not close a family automatically; final scene/census/ownership review
remains required. Five analyzer controls reuse998 actual complete records and
the exact456000-frame Microsoft comparison, while rejecting partial records,
backwards model time and unfinished batches. Proof
`reference-input-ohci-20261012/analysis-controls-v2/qualified-controls.aot-private.json`,
SHA256 `f5bbb6d0ddd194e50915816359933cb26aa7aeed0958d9659bd84ecbfa250dbd`.

The maintained analyzer now classifies interruption from actual Start, ordered
Stop/Destroy, independently identified source truncation and the immediate title
successor. The subsequent movie may be a known logo; duration alone cannot turn
a late opening skip into natural completion. Both natural openings require
their own title transitions. Successful repeated Stop calls retain the first
playback retirement. No additional title-loading or picture-recency deadline
is introduced. Root review also found that the real op01/artoon001 references
omit their final-frame timestamps: the last recorded finite ordered PTS supplies
only a conservative media-end lower bound, without inventing a final timestamp.

Fifteen affected controls and a distinctive actual reference match pass. Root
verifies25 artifact identities, retains the original waveform/A-V/source and
successful-finalization gates, and reuses unchanged parser/ownership checks.
Proof `q03-analyzer-correction-20261012/accepted-root.aot-private.json`, SHA256
`e39f6424fd37b1d76534dd74b971b6d94297eb520051729ccd996f21c5ba43e6`.
The frozen v8 capture and waiter/analyzer remain unchanged; apply the corrected
analyzer in a separate retained review. A failed batch does not satisfy the
current whole-run closing contract. Q03 and both families remain open until
complete numeric, scene, census and ownership acceptance.

Generation v8 returned2 after13222.609 wall seconds during second-demo loading:
`device LLE duplicate physical publication`, ordinal159, entries1183312.
The conflicting newVA82EA2000 and oldVA07DFD000 both resolve toPA02EA2000.
Its finalized capture contains16390416 GP frames and764753 DMA records,
349538196 record bytes, with zero overflow/I/O error and no observer-limit stop.
The planned subsequent Start/release was not reached. This is required offline
memory/interpreter integration work under `.123.1`, not a hardware question or
evidence that the512MiB observer filled. Retain the two completed openings for
independent numeric review, but require a corrected complete closing generation.
Do not suppress the publication/pin guards or rerun unchanged sources.
Final manifest `reference-movie-batch-20261011/q03-c32000-v8/run.aot-private.json`,
SHA256 `7410e39eff0a1c22d15fccb7dfd836aed7d46b47bb8d2a7c105be5df9e0b1d69`.

Task `.123.1` reproduces the collision with an actual original constrained
allocation that relocates a page-table PFN and live user PFNs together. After
the original same-CR3 reload, the interpreter's flat recursive alias still
pointed at the old page table, so subsequent original PTE stores used reused
storage. The correction observes two exact original reload helpers, retires
their unchanged instructions, stops the engine and refreshes stale interpreter
mappings before resuming. It leaves PTE/PFN bytes, guest registers and the
existing publication/pin/collision guards unchanged. Unsupported reload bytes
or address-space changes reject before refreshing.

Five focused controls pass: genuine combined relocation with retained user
bytes and original free, live-alias rejection, altered reload bytes, a wrong
CR3, and both helpers at a one-instruction slice boundary. The last control
binds a frozen actual PIT input and counts step/provider/device calls; refresh
adds no clock pump and preserves GPR/flags/CR3. Root verifies114 identities,
both helper instruction sequences and actual generated test/profile settings,
without repeating unchanged runs. Proof
`publication-collision-20261012/root-review/accepted.aot-private.json`, SHA256
`9641e0116acb5be7c964ba6be6c7b8b550c50bfd3b969ea694efe1b0330857da`.
This accepts the bounded correction. A changed title generation must still
pass the previously failing second-demo load and remaining Q03 sequence.
The new test target is scoped to USB LLE because its bound-clock control uses
the shared PIT. Root configures both USB LLE and USB-off selections to verify
registration and preserve the existing IO-only build; no functional run repeats.

The v8 second opening has a retained closed visual/caller window: final source
picture2263, accepted Stop/Release after75.566 modeled seconds, then independently
matched title. Its final texture remains presented after the first audio Stop;
the analyzer therefore uses only distinctive pictures within actual playback
to prove source truncation. Later held pictures do not invalidate that evidence,
and post-retirement pictures alone cannot establish it. Sixteen affected checks
pass with unchanged numerical limits and other analyzer functions. Proof
`q03-held-presentation-20261012/qualified-root.aot-private.json`, SHA256
`210f3ff3d3872825ec0f76d9eb63f46ca051840ba0459db8e783b33c5cc85415`;
window `q03-v8-second-retirement-review/observed-retirement.aot-private.json`,
SHA256 `bc08bf48b363df9d92096e8361760db16e40fab3214156a4bdee98e361e60d60`.
The window alone does not qualify DMA voice ownership, waveform/A-V or Q03.

Full retained v8 analysis now passes both completed natural openings. Each lasts
75.566 modeled seconds, reaches final picture2263 and returns to title, with no
DMA byte mismatch or movie-source reread. One fixed alignment per actual Play
compares3626188 frames against caller PCM and independent encoded-rate media;
independent RMSE is at most0.004625 and correlation at least0.9998287. Absolute
A/V p95 is51.300ms and52.313ms, within the unchanged two-frame limit. Proof
`reference-movie-batch-20261011/q03-v8-retained-natural-review/review.aot-private.json`,
SHA256 `ca5968c816faa596077f5d5cd797092560ac1b9f29c7aa8e8b45d29c9d89bb7a`.
This closes uncertainty about the completed lifetimes, not the failed batch:
the publication repair, later Start/release/reentry and complete-generation
scene/census/ownership review remain required.

The changed v9 generation raises only the passive DMA-record ceiling from
512MiB to2GiB. Records remain streamed; the pending/read and output buffers,
600-second modeled duration, processing and tolerances are unchanged. At v8's
observed average,600 seconds would already exceed512MiB. Its13222.609 wall
seconds for341.467 GP seconds also projects beyond the old six-hour host cap;
the v9 launcher declares an eight-hour host allowance. These are bounded
observation budgets, not a relaxation of LLE correctness or an instruction to
optimize its speed. The final generation must pin its actual compiled limits.

Generation v9 was built from `a3f03889488828e7018d4dd198ea67f56f013c3f` and
launched in its private directory. All three isolated IO/audio/title builds pass;
root verifies the prepared executable and source/build/receipt identities.
Prepared manifest SHA256
`c93173acc41e29c1434d4b4b85e666b9bf2b14171bd58693c75ab05f873f8de0`, executable
`12faf55ded6c40bf94c860de21285b16c82c3d93a8095d6c9693560f2fa17613`.
Actual startup reports the600-second/2048MiB capture limits and delivers neutral
replay input at time zero. The original disk and verified private copy match
before launch; the run never opens the original disk for title storage. The
hidden, muted title uses cores12-15 at Normal priority with automatic reduction
while Marvel Rivals runs. Frozen numeric and scene waiters observe this single
generation. Old failure-site passage and complete Q03 acceptance are pending;
no Xbox operation or additional title is started.

The v9 short checkpoint passes from retained complete-record prefixes of
that run. Actual Microsoft voice4 has Stop/destroy and the next
independently identified artoon phase. One fixed alignment compares456000 GP
frames exactly against caller PCM; independent media RMSE is0.007314/0.007018
and correlation0.996618/0.996390. Absolute A/V p95 is49.533ms, with source
span9.910811s versus model9.917s and zero ring-byte mismatches. All declared
Q02 limits are unchanged. The snapshot excludes2912 bytes of an incomplete DMA
tail explicitly; it does not invent whole-run finalization. Root verifies the
retained prefixes. Proof `q03-c32000-v9/short-checkpoint/review.aot-private.json`,
SHA256 `1d9edb2ff2adc7be1d40f1a98030d4e882c0afc687d40f4918fe0d5cbf72beed`.

A read-only final census collector is prepared beside the existing numeric and
scene waiters. It reuses the accepted1819-record/201-generation v8 inventory,
retains actual Play/loop/Stop/Release records, and reports new observed settings
and routes for root review. It requires finalized numeric acceptance and exact
configuration/source identities; no family is automatically qualified. Its
retained-prefix recomputation agrees on194 ADPCM Plays,20 nonzero ADPCM loops,
44 configuration routes,42 Play routes and140 gain levels. The frozen corrected
collector is in `q03-c32000-v9/final-census-wait-v2`, SHA256
`6fde2c28c51b3f69d0aeaf112705e5522903a41d8dbefc444795bb8c18d451fa`.

Owner stop2026-10-12 terminated the full v9 title, supervisor and downstream
waiters. Last supervisor progress recorded8207872 GP frames (about171 seconds),
not the required600-second capture. Partial artifacts and the passing short
checkpoint are retained; there is no full-run finalization or family pass.
The owner subsequently authorized resource-bounded continuation and temporary
Z: staging. A fresh Q03 run is being prepared from the same frozen generation;
path relocation does not justify repeating unchanged component tests. Final
capture analysis still has to prove the complete closing sequence.

Temporary Z: relocation now builds successfully with zero warnings/errors:
one frozen IO translation unit is rebuilt for generated path changes, and51
accepted-generation title objects plus the existing reference-audio archive are
relinked. No component tests repeat. The title has not launched: the restartable
fresh-seed copy stopped twice when physical memory fell below8GiB. Both partial
copy receipts are retained; a full-size destination alone is not a verified
disk image. Resume the copy in a stable resource window, verify its exact seed
identity on Z:, then freeze the runner/manifest before the full observation.
Durable preparation receipt
`reference-movie-batch-20261011/z-restart-preparation-20261012/accepted-preparation.aot-private.json`,
SHA256 `8166fe3941b8ef8af4e709cd4b5e5727c65931cfcb8ec56ec8c840225aadd35b`.

The first v6 natural opening now reaches picture2263, real Stop after75.567s,
Destroy and the title transition, with zero ring mismatches or source rereads.
Its full waveform checkpoint exposed an analyzer error: requested44100 was
treated as exact44100 instead of original packed pitch-501, whose maintained
source clock is44098.205613Hz. The original constructor/frequency source and
A02 expectations establish that pitch independently of the candidate waveform.
The corrected analyzer applies that fixed conversion to both caller PCM and
independent nominal media, retaining one alignment per Play, original video
PTS, the same waveform/A-V tolerances and no fitted rate/gain. Its rational
approximation adds under0.002 frame of phase error over this full lifetime.
The retained ideal-rate failure is not erased and does not establish a runtime
defect. Corrected full-span independent RMSE0.00434/0.00462 and correlation
0.99983/0.99983 pass; A/V absolute p95 remains bounded by51.87ms. Proof
`q03-c32000-v6/first-natural-encoded-pitch/review.aot-private.json`, SHA256
`b8ec74744b90b66390cd7f70d70789bffd28456777abc73eacc3c0fa6744f5df`.
Five retained analyzer controls also pass unchanged in `analysis-controls-v3`,
SHA256 `5e593d4fab04f5c32cdbd45c2430255fd08f144afb1523d1c40262165023b836`.
The corrected analyzer is ready for the next validated generation; the v6
executable/captures are unchanged. Second-opening and Start/reentry acceptance
remain required before Q03 or either family closes.

Generation v6 then self-returned2 after3773.188 wall seconds at first-demo
startup, model133.43s: `thread LLE continuation handoff requires its real
guest-call frame`, ordinal166, entries0. The first completed opening remains
valid, but this is a genuine integrated scheduling failure. Its waiter correctly
rejected full acceptance before numeric analysis; no analyzer or title process
remains running. Root verifies all78 finalized run artifacts and exact
zero-error6150480-frame/187960-record finalization, with zero ring mismatches.
Proof `q03-c32000-v6/finalized-failure-review/review.aot-private.json`, SHA256
`9e833a07daf60c7e46d90f527f8e2cadaabe0ae27a6d4ca4c9fd94e0d7870e53`.
The bounded next task reproduces that original handoff in a short fixture and
preserves real CPU/thread/guest-frame/IRQ/FP ownership. Do not suppress the
guard or restart this unchanged title generation. Source/input/clock cadence
sweeps are not indicated by this failure.

The causal CPU fixture reproduces that same ordinal166 guard in1.062s.
Fix `30a6be6` routes selected components' contiguous allocate/query/protect/
persist/saved-data/free calls through the existing real original-thread call
transport. Root services retain their route and strict guest-frame guard.
Three actual original CPU lifetimes preserve stack/GPR/flags/x87/XMM and exact
memory/thread/handle/ledger baselines; the deliberate root-only control still
rejects before completing the original join. Seven focused checks pass, with
no title, GPU, audio or console operation. This fixes a required scheduling
integration boundary; Q03 still requires the changed full title batch.
Root source/ownership review also verifies its frozen proof and unchanged
engine transport against v6; accepted proof
`component-contiguous-20261012/root-review/accepted.aot-private.json`, SHA256
`f7c4d0effd8ff33f8793977ef0ab6ce013969b77b3a73d43deec42620f628b6d`.

Changed generation v7 now validates that integration: its actual original
worker completes contiguous allocation82E5C000/1232896 bytes, then stops at
a different required service, ordinal305, after3790.953 wall seconds.
Root verifies340 frozen artifact/source identities and the zero-error capture;
no old166 frame failure remains. Actual first-opening Stop/final Release and
title-audio restart are retained, but no whole-run numerical/scene acceptance
is claimed. Proof `q03-c32000-v7/root-terminal-review/accepted-failure.aot-private.json`,
SHA256 `7e7ed243fa93fe7759946a4759e5f88f2c84858b305c594f743b6f5c58cc9cbb`.
The analyzer/gallery waiters correctly record incomplete acceptance without
running another analysis/title. Contiguous route task `.112.2.17` is closed.

Calendar304/305 were absent from the original continuation's argument table.
Their two-pointer RET8 admission now executes the unchanged original bodies.
A short pre-fix actual CPU case reproduces305; three corrected original lives
pass independent1601/1970/leap-day fields and invalid-date output, retained
ESP/callee GPR/IF/DF/x87/XMM/canaries and exact cleanup. Four existing selected
contiguous/root-guard/delay/priority cases pass. Candidate proof
`calendar-transport-20261012/candidate/qualified.aot-private.json`, SHA256
`141be0cc7470a469b81912eff9b9a0975e932651edb40d3bf47a63ec7cf5beb8`.
The static original import audit is complete. It identifies two justified
ordinary helpers outside replaced native entries: heap-pattern comparison269
and Unicode conversion308. The sole distinct Blinx308 call disables allocation.
Both unchanged original RET12 bodies now use the retained continuation;308's
low-byte TRUE branch rejects before changing even the destination descriptor,
until nested pool publication/retirement is connected. Pre-fix CPU callers
reproduce both missing-service guards. Nine focused checks pass in1.22s:
24 original heap cases and12 Unicode cases cover independent prefix/tail,
ASCII/embedded-NUL, capacity/status, unaligned and unchanged caller bytes;
two allocating-branch controls preserve an unfinished caller and untouched
descriptors/output. Calendar and four continuation regressions also pass.
Stack/callee registers/IF/DF/x87/XMM and all memory/thread/handle baselines
remain intact. Proof `ordinary-helpers-transport-20261012/candidate/qualified.aot-private.json`,
SHA256 `4962e4a25f6a773f4d35fb51d74593691eedf353364783942009f90628346738`.

The audit is static call/source evidence, not dynamic unreachability proof.
Event-object16 also needs246/250 event lifetime ownership; crypto335/336/337
needs its current dispatch and opaque caller context, while signature-key
data323/325/354 remains unbound. IRP, FP callback, exception and firmware/error
branches retain their existing guards. These are named remaining conditions,
not fabricated successful defaults or new precision deferrals. One changed
Q03 batch can now integrate the calendar/helper fixes with unchanged cadence,
replay and acceptance bounds. Scheduling and movies stay open pending their
full required title lifetimes.

Changed generation `q03-c32000-v8` is now running at source `910b4f1`.
Its private build recompiles current IO and51 frozen title units, importing12
unchanged dependency archives instead of rebuilding those libraries or tests.
The8GiB runtime disk is a fully verified fresh copy of the original cold disk;
no v7 runtime disk is reused. Prepared proof SHA256
`467cf22c19b1e0773bea3261e4da2c948bf6b6796589bbc78b89ee38ae5bf398`,
executable SHA256
`a2160023a1ac550c44578cfc577d086c6486dc05ec967855507bde68080c9dd8`.
Initial original replay delivery, stopped common-clock route and bounded GP
capture are observed. Cadence32000, Start445000/release446000,600 GP seconds,
512MiB records and21600 host seconds are unchanged. With the worker idle,
host allocation uses four logical CPUs at Normal priority; Marvel Rivals
detection lowers it to two CPUs/BelowNormal without changing guest-clock input.
Frozen numeric and scene waiters run once after successful finalization and
skip incomplete runs. The unchanged analyzer and12 independent media files are
copied and pinned before this observation. No movie/scheduling acceptance or
Xbox operation is claimed while the run and recovery hold remain pending.

The v8 run has now crossed the former first-demo calendar305 boundary and
reached actual ADPCM voice creation and rendered 3D attract gameplay. Root
review of retained frame105 establishes progression; it does not claim new
pixel accuracy or completed movie lifetimes. The frozen executable, selected
transport/calendar sources, matched replay, prior actual305 failure and existing
independent CPU calendar controls are bound by
`calendar-transport-20261012/title-integration-v8/accepted-integration.aot-private.json`,
SHA256 `31c80d58b62eac7909c654b2d4e64f537d211eb08bc3838160303b537773a3bf`.
This closes the concrete calendar admission task `.112.2.18` at fix `1eeadf7`.
No tests or title runs were repeated for this review. Q03 and both whole-family
gates remain open while the existing run completes its required lifetimes.

A01's v8 partial census now reaches1,819 accepted calls across201 generations,
including194 actual mono ADPCM Plays,42 routes at Play (44 configured routes),
34 accepted bin-volume updates and20 looping Plays using two nonzero regions.
Sol6.1's bounded audit and root adversarial review bind21 frozen inputs and
independently recompute the prefix. Existing original packing/send arithmetic,
ADPCM region/source and A02 default composition cover the reached stage domains;
no additional VP stage is identified. The84 gain cases are generic arithmetic
coverage, not a literal-300 fixture; use the56-case source proof containing8
mono ADPCM cases. Neither post-Play setters nor absent filter/EG entries prove
hardware-active processing or unreachable raw internal original calls. Original
loop API completion and downstream bins11/12 graph/output remain in A03-A05.
Proof `voice-census-v8-20261012/accepted-root.aot-private.json`, SHA256
`3cfbd0dd7691e14e6c9319e15f984088fe39586c7467585e17352c74e200acf0`.
No tests or title runs were repeated. Final census/lifetimes and A01 closure
remain pending; the family count remains4/9 with26 precision deferrals.

The final analyzer explicitly binds required opening lifetimes to actual Play
not after the declared Start. A fixed600s diagnostic can also reach a later
opening; it retains that lifetime and its source/ring/census evidence without
inventing Stop/Destroy at capture termination or substituting it for the
required two natural and one interrupted lifetimes. All required actual
Stop/Destroy, final-picture, full waveform/A-V and source-ownership gates stay
unchanged. Seven boundary/ownership controls and the prior five analyzer
controls pass in `reference-input-ohci-20261012/analysis-controls-v4`, SHA256
`208e525b4d15a97e57de3b2ee34861f03d3819ff56d79886001c2e02758cc655`.

G02's fixed encoder-read preparation is committed in the probe repository at
`1283882cde262b299d4d83f36e00b4de30e1e47a`. Two404-byte payloads resolve only
HAL ordinal45 and perform byte reads at slave8A, commandsC4 then6C. Eleven
focused controls pass; the full offline suite is182 passes plus3 pre-existing
optional DSP-checker skips. Eight earlier payload variants are byte-identical.
Root verifies17 source/binary/original/nxdk identities and the unchanged ARM/
protocol boundary. Preparation closes `.111.1`, not G02 or parent `.111`.
Root proof `graphics-startup-consumer-20261012/encoder-root-review/accepted-preparation.aot-private.json`,
SHA256 `46a8e68c4a988afaa4b91b6634ceb7d839dc85a2a1d8fa3136391b783aa7db57`.
HAL programs its own SMBus controller and synchronization state; the payloads
request no encoder-register-data or storage write. Legacy-selected status,
true6C/C4 state and the old warm snapshot's preceding programming remain
distinct inputs. The prepared read bytes cannot be used to invent them.
No console operation has run while the recovery hold is pending.

G03's source investigation identifies a required implementation gap. Original
MMIO already publishes into the same PRAMIN store used by object/DMA/RAMHT
consumers. The missing behavior is selected-image PGRAPH load/save through
`400784/400788`, including real completion and save-before-release ordering.
Original `13F032/13F26D` produce the image; `13FC4C/13EDEA` consume it. The
inspected Linux NV2A/NV20 code initializes the image and asks hardware to perform
the transfer; the inspected xemu implementation restores only its first word
and leaves save unfinished. Neither establishes the consumed image-field map.
See the primary [Linux NV2A](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/gr/nv2a.c),
[Linux NV20](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/gr/nv20.c)
and [pinned xemu PGRAPH](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/nv2a/pgraph/pgraph.c#L135).
The bounded title needs only its reached transfer/state contract. A default-only
restriction cannot yet establish that contract: original saved program/constant
defaults differ from the model's zero arrays, and the driver can save programmed
channel state and load a selected channel again. Capture the actual first-load
image/owner/queue, then determine the reached fields and reloads. Use independent
owned state/save/load observations only for mappings that source cannot establish.
Keep this dependency separate from physical RAMFC UNK17/alias questions.
Frozen primary map SHA256
`d0442b1965630fe3d9558c2114c0d0411576081cc416899707c1e0eabfb11899`,
startup-feasibility SHA256
`c42df222562eca5882a512b0339933c114742d254b94b6d9ca85bfd55555ad1e`,
under `graphics-startup-consumer-20261012`. No context no-op is admitted.

The retained first-load investigation cannot recover that same-object image.
Its genuine original startup logs reach the instance/context producers but do
not contain the full PRAMIN, driver or queued-packet bytes. The latest retained
startup stops earlier at the CX25871 `EN_REG_RD` guard, with channel0 GET=PUT.
Neither inspected kernel RAM snapshot binds the live Blinx code/driver owner.
Proof `graphics-startup-consumer-20261012/first-load-retained/handoff-retained-dependency.aot-private.json`,
SHA256 `ebfeff06bfebabc963398ce2c502c89257286d888c843a50a3780c1794fe991e`.
Resolve G02 first, then capture actual `13F26D` return and the first context-load
request, including the complete0x37F0 image, owner/table/RAMFC, FIFO gates and
pending packets. Calling that producer on an invented driver object would not
establish the title's reached contract. No new title run is warranted before
the earlier startup dependency changes.

First-context passive collection is implemented at `24468bc` and root-reviewed:
34 compiled const-owner snapshot controls, two raw CPU observer cases and
three matching checks with observation disabled pass. Actual source/return/frame
and request instructions, complete PRAMIN/channel/PFIFO backing, four prefetched
parameters and one unread word are retained. The real+784 failure is unchanged;
+788 is absent and authored source mismatches remain incomplete. Default-null
paths avoid snapshots, files, hashes and additional CPU reads; HLE excludes the
GPU collector. Root verifies104 immutable identities without repeating checks.
Proof `graphics-startup-consumer-20261012/first-context-implementation/root-review/accepted-implementation.aot-private.json`,
SHA256 `7528e050cf020f1d7a92fefa81d84f6d8a24f7c97984cb12581b7fbde7615270`.
Bounded audit `.116.2.1.4` is closed. Actual first-load collection still requires
G02 resolution and a separately reviewed original title/input/private-disk
generation. This accepts passive tooling, not a reached image or PGRAPH transfer.

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
priority. The owner now permits temporary build/run staging on Z: to avoid
F: disk contention; source repositories and durable evidence remain on F:.
Retain owned temporary artifacts until needed results are preserved, then clean
only the owned staging tree. Only one GPU-heavy run at a time.
Lower resource use when the owner plays a game; otherwise do not throttle LLE
merely because it is slow. The current global agent instructions supersede the
earlier local-only Beads rule: use the central sync helper after durable issue
updates and allow its own integrity/transfer deadlines to complete.

Later owner update2026-10-12 supersedes the resource hold: keep competing for
resources and do not pause for host load. Memory/disk measurements are now
observations, not admission floors or automatic stop conditions. The movie
seed copy finished and its exclusive Z:-only hash matches the original8GiB
seed. When working solo the title uses all four authorized logical CPUs12-15
at Normal priority. Game-aware
reduction remains. Identity checks, actual correctness failures and declared
capture bounds still apply; no Xbox operation is enabled by this resource change.

The frozen v9 batch resumed at2026-10-12 22:30:14 PDT in temporary
`Z:/xboxlle-temp/qualification-20261012/movies/run1`, title PID46764,
prepared SHA256 `41b096f2ebad23de73bffed4ddb4cf132cbcaf113afdd9ae3fddf0acd4ffed6a`.
The executable changes only generated runtime paths for Z: storage. Cadence32000,
Start445000/release446000,600 GP seconds and the2GiB record cap are unchanged;
the accepted short checkpoint is reused. Numeric analysis follows finalized
capture once, with final scene/census/ownership review still required. Sol6.1's
runner review found possible finalization-classification and receipt-cleanup
races, not a title correctness defect. The batch had already launched, so no
runtime script was changed or title restarted. A separate read-only Windows
process handle retains exact title exit independently. Preserve the original
runner result and explicitly adjudicate any classification issue against final
capture/error evidence; never rerun or promote a failed capture for bookkeeping.
The prelaunch watcher-JSON race did not occur. The completed batch and root
acceptance are recorded above. The durable launch receipt is under
`reference-movie-batch-20261011/z-restart-launch-20261012`.
The initial gaming detector misclassified the owner's Side7StartupTests
`-Side7DevHarness` process as gameplay and selected two CPUs/BelowNormal.
A host-only allocation controller corrected both supervisor and title to four
CPUs/Normal at22:33:51 PDT without restarting or altering guest state. It excludes
that explicit test launch, still reduces allocation for actual Marvel Rivals
or ordinary Evo game launches, and records every allocation change. The frozen
title/runner manifest is unchanged.

The separately reviewed host deadline guardian completed without intervening:
the title finished at 27,843.141 seconds, before the 28,500-second trigger.
Its final receipt records no actions and no owned suspension. The prepared
supervisor-only extension and mocked controls remain retained; live suspend/
resume was never exercised. Original runner classification and independent
exit observation agree. Root review
`z-restart-launch-20261012/host-deadline-guardian/deadline-guardian-root-review.aot-private.json`,
SHA256 `da095bb6ff52e75cf016df8d339aa6a79165ed423db4e840a9bd1f1621f97f7d`.

Owner update2026-10-12: the Xbox is off. Defer every console operation until the
final hardware batch, after available offline qualification; do not ping/probe
or request another hardware session in the meantime. Keep the prior unexpected
SG write readback recorded and resume only from fresh owner-confirmed probe
readiness. Existing read/benign-write authorization remains in force, with
verified owned RAM and recovery copies. Stop after any unexpected WRITE result.
Avoid dangerous boot/HDD/EEPROM/flash operations. Do not replay failed writes.

After recovery, keep the hardware work in dependency order rather than repeating
whole graphics/audio runs between observations:

| Required dependency | Smallest next evidence | What that evidence must not be used to claim |
| --- | --- | --- |
| G02 encoder startup | Establish one coherent original startup profile from source or matching context; the Oct13 8A/C4 read failed and is not repeated; live D4 selection differs | Legacy Status bytes, live D4 values or present dashboard settings do not establish the retained 8A warm state |
| Shared audio prerequisite | Reuse the qualified owned allocation/SG mechanism, validating each fresh physical/alias/pin binding | Fresh-session ownership remains mandatory; do not repeat completed mapping research or reuse an old allocation address |
| A03 original DSP warm reset | Reuse completed configured GP/EP hold/rebootstrap observations; adapt the sole owners and exercise normal original retirement/reinitialization | No-DMA reset evidence does not establish cancellation of pending work or identical GP/EP counter retention |
| A04 active FE ordering | Bounded original2010C/20104 active control/consumer group, plus required1510/FEMEMDATA ordering with independently visible work | Idle token publication and a fixture that already stopped SE do not establish active queue completion or original retirement |
| A05 analog PCM ring traversal | Physical single/two-node silent analog rings now establish cyclic traversal and explicit stop/reset/restore;51 affected native/device checks and original codec stream/ISR lifetimes pass | Analog behavior now has its own hardware evidence; complete connected title output/timing remains A05 |
| G03/G04 reached graphics state | Capture the first real context image/queue after G02; resolve only consumed unknown fields and the enabled-noise primitive | A default-only load, method replay or disabled noise cannot replace required state transfer/pixels |

Each observation preserves its own bounded stop conditions and source identity.
Batching shares setup and retained evidence; it never proceeds beyond an
unexpected write. Implement the justified semantics before the corresponding
single connected title run. Read-only preparation and fixtures leave the
family acceptance gates open.

Close a family only when its complete bounded row set passes and the maintained
selection executes the service in title. Record source, selection, evidence and
landed commit in Beads and the burndown. At completion, report9/9 *bounded*
families with explicit limits. If essential semantics remain unavailable, report
their exact blocker and smallest required observation; do not relabel them as
qualified or continue unrelated validation indefinitely.
