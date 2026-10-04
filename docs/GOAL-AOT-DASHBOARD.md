# Goal: faithful Xbox floor, enhanced native execution and playable games

Owner clarification, 2026-10-04. Central tracker: `beads-xbox.1.25.5`;
planning decision: `beads-xbox.1.40`; active menu gate: `beads-xbox.1.17`.
The owner targets original Xbox execution speed and content cadence. Exploratory
Blinx bring-up now proceeds before dashboard performance qualification; the
dashboard remains an unresolved goal and shared-change regression workload. A universal 60 rendered
FPS requirement is superseded. Full AOT remains a separate final gate; donor
HLE remains eligible. The dashboard's distinct-frame cadence is not yet verified.
The renderer choice is now a whole implementation: `XLLE_RENDERER=LLE` or
`XLLE_RENDERER=HLE`, using separate build directories. See
[HLE-RENDERER.md](HLE-RENDERER.md).
Runtime progress is recorded in STATUS. Follow the latest owner instruction for
execution and delegation: work solo unless newly authorized. The owner resumed
work on donor graphics integration after clarifying the HLE policy. Follow
[the shared HLE policy](../../../recomp-template/HLE.md): build-time selection,
compatible caller behavior, freedom to remove internal work and approximate
arithmetic/timing. No live switching or instruction-history equivalence is
required. The old FP32, status-readback and DXT-cache experiments remain parked.
Active integration issue: `beads-xbox.1.45`.
The remaining donor opportunities have now received bounded implementations;
see [the latest experiment results](RESULTS-REMAINING-DONOR-SPIKES-2026-10-04.md)
for their measured scope, saved branches and remaining full-subsystem work.
The separate whole HLE renderer completes the live input route and showed a
provisional 1.87x throughput improvement on a shared host. See
[the current results](RESULTS-WHOLE-HLE-RENDERER-2026-10-03.md).
XDK service replacement and original-speed output remain unfinished.

**The target ecosystem has a faithful, runnable LLE floor and interchangeable enhanced
implementations. Qualified enhanced paths are the intended shipping defaults,
with an explicit reference opt-out.** Any system or subsystem can be replaced,
including rendering, audio, kernel services and title functions. Internals may
be substantially different when the selected boundary's observable contract is
preserved, including documented enhancement tolerances. No stubs or fabricated
success. The repository name and amount of original code retained are not goals.

Get the animated stock dashboard main menu to its original execution speed and
content cadence with working selection/input, while pursuing an exploratory Blinx
launch and repeatable scene for a second performance workload. Use the current hybrid CPU path and compatible enhanced implementations.
Full AOT with dynarec removed remains the final goal; it is not a prerequisite
for menu performance. JSRF is the next generalization target. Completing every
dashboard interior remains a broader product requirement, separate from the
immediate menu performance gate.

## Acceptance

Keep the implemented LLE execution path independently runnable. Its current
dashboard success is bounded evidence, not proof that every device or title is
faithful. Close actual reference gaps when a proposed replacement or game needs
them; a missing reference implementation cannot be hidden behind HLE success.

Reference execution retains the real kernel and hardware path. Enhanced execution
may replace a kernel service or initialization operation with a complete native
implementation, including its observable state transitions. No forced
acknowledgements, ignored required work or captured-frame playback. The explicit
unlocked HDD policy remains the owner's narrow documented exception; provisioned
locked startup also works. Per-title build defaults can enable qualified
replacements; opting out means selecting the LLE build and rebuilding.

The product route must include sustained animated main-menu idle, selection
changes, entry/return through Memory, Music, Settings and the locally available
Xbox Live flow. Real guest offline/error behavior is valid; invented service
responses are not. Pin the input identities and check actual reached screens.
Script labels alone do not prove navigation. UI audio, input and transitions
must be qualified; the current absence of host audio remains an open gap.

The performance target is original-speed guest execution at the original
content cadence for the selected title, region and video mode, counting distinct
frames and input latency separately from host presents and guest vblanks. A
30 FPS title on a 60 Hz output still needs 60 guest vblanks per real second;
halving the emulated clock would slow gameplay. The existing 12.7-vblank/s
screening result is not 12.7 measured distinct frames/s. Preserve the native
baseline and measure each optimization against it on a quiet host. Additional
headroom remains desirable; 20% is a planning proposal, not an agreed gate.
Preserve each game's simulation rate; presenting a 30 Hz game twice is not a
60 Hz gameplay enhancement. Faster internal execution must not speed up gameplay.

Full AOT is a separate final gate: all remaining guest CPU code in the declared
supported workflows executes from prebuilt native artifacts. Neither enhanced
nor reference opt-out mode may depend on CPU dynarec/interpreter execution for
those workflows at that gate. HLE is counted separately, never reported as
translated guest instructions. Dynamic discovery/capture is a development aid;
final qualification starts with prepared artifacts and zero runtime translation.

## Interchangeable subsystem contract

An ABI is the call shape; the contract also includes memory/state effects,
errors, resource ownership, callbacks, completion order and guest-visible time.
The same request plus starting state must produce compatible observable results.
Implement the first concrete renderer boundary before generalizing a framework.

| Contract part | Required policy |
| --- | --- |
| Identity and inputs | Versioned boundary; supported image/profile identities; typed arguments and owned or explicitly synchronized input resources |
| Outputs and state | Return values, guest-visible memory, handles and persistent state remain compatible; replacement-private caches are permitted |
| Timing and events | Preserve relevant ordering, IRQ/fence/wakeup semantics and declared logical-time behavior; host wall time cannot silently replace guest time |
| Numerical/presentation tolerance | Document per-operation tolerances and intended enhancements; higher resolution/filtering can differ, missing geometry or corrupted gameplay cannot |
| Unsupported input | Implement it, explicitly narrow supported build scope, or use the LLE build. No fabricated success. A scoped implementation may share existing helpers; a general live fallback mechanism is not required |
| Opt-out | Select the LLE implementation at build time and rebuild. No runtime HLE selector, live handoff or cross-build savestate requirement |
| Evidence | Compare reference/enhanced results at the contract boundary, record effective backend and fallback reasons, and exercise opt-out in regular smoke runs |

Enhanced runs need not execute the reference path alongside every operation.
Capture representative inputs and compare offline. Existing instruction-clock
and image hashes remain reference regression tools; HLE changes execution paths,
so cross-mode acceptance uses logical events, state/output contracts and stated
tolerances instead of requiring identical retired-instruction histories.

CPU coverage is independent of HLE/LLE build selection. The active whole HLE
renderer replaces CPU vertex programs and software rasterization; the earlier
`XLLE_NATIVE_DRAW` subdivision remains parked. Guest XDK calls and CPU attribute
staging still run. The borrowed phobos XDK symbol scanner identifies candidate
call boundaries, whose ABI still needs verification against the actual binary.

## PSX/Tomba precedent checked

The local `F:/Projects/psxrecomp/TombaRecomp/game.toml` selects OpenGL by default.
The runtime's `gpu_render.h`/`gpu_render.c` provide a shared rendering interface,
backend selection and software initialization fallback. Tomba also retains its
software backend and separate optional presentation enhancements.
`runtime/src/bios_hle.c` explicitly returns unhandled BIOS services to LLE;
the framework documents a default-enabled, opt-out HLE tier and temporary CPU
fallback while native artifacts grow. These are the architectural patterns to
reuse. Source inspection is not a fresh Tomba performance or fidelity test.

## Renderer reuse

Existing OpenGL and software rendering remain the reference starting points.
The enhanced path may use ordinary GPU rasterization, shader execution, native
filtering, batching and persistent GPU resources instead of reproducing our
software renderer's arithmetic operation by operation. Our current pixels are
regression evidence, not hardware truth. A GPU implementation can also preserve
LLE semantics; the classification does not itself determine speed or fidelity.

xemu's renderer, shader translation and surface/texture code are the current
NV2A donor. All three linked xboxrecomp repositories remain active candidates:
**sp00nznet, NoRain211 and phobos665**, including relevant title runtimes such as
NoRain211's DOAXBV implementation. Check their distinct changes at each relevant
performance spike, record the useful ideas and reuse decision, and revisit as
upstream or measured costs change. HLE/native services and substantial backend
reuse are eligible at compatible boundaries. The no-stubs rule excludes their
unimplemented behavior, not their whole codebase. Replace shim dependencies with
real services. Kernel HLE is now explicitly eligible when its LLE counterpart
and observable contract exist. No title-specific captured-frame replay.

Reuse can include substantial backend code when its real dependencies are wired
and behavior passes qualification. Preserve upstream licenses and provenance.
The existing combined-executable distribution restriction remains tracked in
`beads-xbox.1.21`; it does not prevent local qualification work.
See the [refreshed all-donor assessment](RENDERER-REUSE-2026-10-02.md), including
fork-specific code, fresh compiler tests and limits of each candidate.

## Fast-win delivery order

Use bounded implementation checkpoints, not an open-ended framework rewrite.
Pursue real Blinx launch alongside dashboard cost attribution. Neither is gated
by original-speed qualification of the other, 100% AOT or every menu interior.
Parked experiments remain available for title-specific retesting.

| Order | Deliverable | First concrete result / decision |
| --- | --- | --- |
| 1 | Reference/enhanced selection around the existing renderer interface; short repeatable workload and opt-out smoke | Same live dashboard request reaches both implementations; effective backend, CPU/renderer cost, real guest progress and frame pacing are visible |
| 2 | Borrow a substantial renderer path | Run one actual dashboard rendering workload through donor shader/raster/resource code and our real memory/resolve boundary. Measure against the previous path, validate tolerances, and expand useful coverage. Preserve reference fallback; no custom pixel-exact rewrite required |
| 3 | Qualify original-speed dashboard menu | Measure settled animation and selection/input at original guest speed and content cadence. Accelerate the measured costs using renderer reuse, native/HLE services or CPU work as evidence warrants; preserve real reference opt-outs and qualify outputs. Report distinct frames, presentation pacing and response, not just vblanks |
| 4 | First real Blinx launch with partial AOT | Resolve the pinned media/ATAPI/loader path and capture title entry plus the first actual blocker. Execute unsupported CPU instructions on the existing floor during development; full AOT and full renderer coverage are not prerequisites |
| 5 | Enhanced playable slice and measured performance | Title screen, input and a reproducible gameplay scene; borrow or implement the actual reached audio/device contracts. Replace the measured expensive service/function next, validate opt-out and save/state behavior, then qualify original-speed output and latency |
| 6 | Decide the long-term compiler/runtime foundation from a live comparison | Compare current xboxrecomp-generated execution with our current generator against the same implemented LLE/enhanced boundaries. Preserve useful broad frontend/tooling reuse; select on adaptation cost and measured workload, without repo loyalty. Bring this comparison forward if measured menu CPU cost makes it the useful next experiment |
| 7 | Remove CPU fallback on the supported routes | Capture remaining code identities, compile reachable code and kernel paths, qualify transitions/save-reload, and run strict AOT in both profiles. Keep this final gate explicit while shipping useful intermediate development builds |

Steps 1-2 are the first performance experiment. Bound donor investigation to a
concrete input/output path and an executable result before widening scope; if a
full xemu state adapter dominates the work, first adapt its useful shader/format
code at our existing packet boundary, or choose the better-matching donor. A
large transplant remains allowed when it is cheaper than that adaptation.

The whole HLE renderer has demonstrated a provisional shared-host speedup;
original-speed output remains unqualified. Steps 3 and 4 are active independent
workstreams. Blinx may expose shared costs, but a donor draw count or AOT
percentage does not establish a speed gain on either workload.
Once Blinx starts, use its actual launch trace to choose the next missing service.
Do not fabricate disc authentication, mutate the XBE's media constraints or
force device readiness to create a boot milestone. An enhanced loader is eligible
only with an implemented reference contract for the chosen media/firmware route.

For iteration, reuse the existing packet captures, probes and short scripted
windows. Do not build a full save-state system as a prerequisite. Replay a
captured rendering request for development, then validate it in live execution;
captured output is never substituted for game execution. Full reference replay
remains a landing check for changes that affect scheduling/device state.

Every performance checkpoint reports real guest speed, CPU/renderer wall time,
frame pacing, distinct new frames and input latency, plus separate AOT, CPU
fallback, HLE and idle accounting. An original-speed dashboard is a useful win
even with partial AOT. No current evidence proves it is one change away.
Use executable milestones to establish velocity before assigning calendar dates.

## Active menu performance gate

Engineering qualification workload: at least 60 seconds of guest main-menu
animation after warmup, including real selection changes, repeated on a quiet
declared host. Keep startup timing separate and verify the reached screen.
Measure capacity without detailed profiling, then verify interactive presentation
at original speed. Establish the original content cadence; 60 Hz output alone
does not establish 60 distinct frames/s. A 60 Hz tick budget is 16.67 ms and
a 30 FPS content budget is 33.33 ms; report interval
distributions, stalls, distinct guest-produced frames, host presents and input
response with their denominators. A mean vblank rate alone is insufficient.
This duration is a working measurement protocol, not an owner-specified tolerance.

Keep reference opt-out runnable and qualify enhancement differences at their
observable boundary. No host-only menu animation, duplicate-frame FPS claims,
time acceleration or stubbed services. Audio and complete dashboard navigation
remain open product requirements; this sequencing gate does not silently expand
to every interior or full AOT. A newly reached correctness dependency still needs
implementation before its result can be accepted.

| Menu work package | Current evidence / next result |
| --- | --- |
| Runnable floor and repeatable menu route | Implemented: 27B route reaches Settings and returns to animated main menu; 270 reference checkpoints and ten images retained |
| Borrowed rendering | Separate whole HLE implementation completes the route; observed 1.87x throughput improvement remains shared-host screening evidence |
| Attribute and reduce steady-menu cost | Borrow raw vertex-stream uploads next; assess whole XDK graphics replacement and larger native CPU regions against current handoff costs. See RENDERER-REUSE-2026-10-02.md |
| Sustained original-speed cadence and input | Open: establish original content cadence, repeat quiet-host steady windows and verify actual presentation, distinct output and input response |
| Blinx launch | Exploratory launch active; pinned media findings retained |

The following diagnostics predate the whole HLE renderer and retain the former
60 FPS planning target for historical context; they do not select the next work.
That earlier contaminated timing pair gives **7.554 reference / 6.866 enhanced guest
vblanks per host second** in the verified 25.5B-27B main-menu window, not displayed
FPS. Even the reference observation is about 7.9 times below 60; this is a scale
estimate, not a qualified capacity result or a calendar forecast. The
[corrected host profile](RESULTS-HOST-SAMPLER-WINDOWS-2026-10-03.md) and
[donor checkpoint](RESULTS-XEMU-PSH-2026-10-03.md) are the starting evidence.

The decoded-texture experiment is implemented and passes 77 tests plus the full
route: donor draws increase from 1,035 to 5,349 of 8,426. Real reference opt-out
still reproduces all checkpoints and images. Single-level TENT filtering and
effect-free unused texture stages account for the wider admission. Initial R0
alpha dependencies remain guarded pending reference qualification.

Next performance experiments should address measured native body/memory-service
cost, vertex processing and host submission, preserving their real fallback and
observable contracts. The diagnostic steady window attributes 18.56% to native
bodies, 15.74% to OpenGL work, 15.34% to vertex work and 12.88% to generic raster
preparation/work. These are host costs from a contaminated instrumented run;
they select investigations but cannot predict savings or qualify a promotion.
Remaining texture declines alone do not explain the distance to 60 FPS.
Compare sp00nznet's content-checked DXT cache, NoRain's DOAXBV compressed uploads
and phobos's resource/submission code as those measured costs warrant.
Do not equate more donor draws with less frame time.

The [GPU vertex follow-up](RESULTS-VSH-SPECIALIZATION-2026-10-03.md) preserves
all 270 checkpoints and 28 captures in both status-summary settings. It removes
118,736 readbacks, but most wait time moves to the remaining control read.
Specialization's separate diagnostic shows higher GPU duration and costly
compilation. Both options remain off, with 79 tests passing in experimental and
ordinary builds. Compare the improved GPU path with CPU vertex execution before
selecting it for menu performance; these busy-host diagnostics do not establish
a speedup, and full AOT remains a separate unfinished requirement.

## Product burndown

| Gate | Status | Required evidence |
| --- | --- | --- |
| Faithful reference floor | Partial, runnable dashboard | Qualified contracts for each reached subsystem; real LLE opt-out; close known exception, media, rendering and audio gaps |
| Interchangeable enhanced renderer | Live borrowed path, experimental | Qualify whole-route tolerances and performance; maintain coherent readback/state and reference selection |
| Original-speed dashboard main menu | Active; does not gate exploratory Blinx | Original content cadence, selection/input and measured distinct output/pacing; partial AOT allowed |
| Complete dashboard functionality | Open, broader product gate | Required menu interiors, input/audio and persistence |
| Blinx launch and playable slice | Open; exploratory bring-up proceeds alongside menu investigation | Real loading/title execution, input, gameplay/audio and save/reload; partial AOT allowed |
| Enhanced performance | Open | Measured gain at equivalent logical work and accepted outputs; title-appropriate cadence and latency |
| Full AOT without dynarec | Open, final gate | Prebuilt native execution of remaining guest work in both reference/enhanced supported workflows; no hidden CPU fallback |
| JSRF generalization | Later | Second title validates reusable contracts rather than one-title assumptions |

These are unequal work packages, not percentages or a calendar forecast.
The detailed compiler stages remain in the
[coverage board](PLAN-AOT-COVERAGE-BURNDOWN.md).

Current evidence: 77 public tests pass after the borrowed-renderer checkpoint. The
[MMX checkpoint](RESULTS-AOT-MMX-2026-10-03.md) adds actual native
menu execution beyond the startup loops. The
[corrected 27B route](RESULTS-CPU-DEADLINES-2026-10-03.md) reaches Settings and
returns, with identical 270-row execution/device/DMA histories and all ten
same-clock images across floor/O0/O2. The observed digest gap is resolved.
[All eight idle phases](RESULTS-IDLE-PHASES-2026-10-03.md) now qualify against
the fully executed 27B route: all ten images and full device/DMA histories
match. This removes another 4.294B executed idle instructions. Native menu work
is unchanged; its share of remaining executed main-menu idle instructions is
26.04%, versus 10.02% with four phases. The subsequent
[general x87 checkpoint](RESULTS-AOT-X87-2026-10-03.md) raises this to **39.33%**
through additional compiled guest work, with idle skips unchanged. The
[expanded x87 checkpoint](RESULTS-AOT-X87-EXPANSION-2026-10-03.md) now raises
main-menu idle to **47.92%**, with five selected regions and 872,134,242 native
instructions over the full route. Final floor/native O0/O2 retain all 270
checkpoint rows and ten images, including the previous floor, with identical
native counters. The subsequent
[kernel integer checkpoint](RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md) adds five
regions using qualified ROL/ROR, OR and BSWAP. Settings-entry native share rises
from **29.33% to 61.32%**, with main-idle unchanged at **47.92%**. Native work over
the route reaches **1,379,683,118** instructions; floor/native O0/O2 retain every
checkpoint and image. The
[scalar checkpoint](RESULTS-AOT-SCALAR-2026-10-03.md) expands to 246 real regions
with qualified shifts, branches, exact narrow reads and immediate stores.
Main-menu idle reaches **70.53%** native execution and Settings entry **76.72%**;
full-route native work reaches **1,815,586,408** instructions. Floor/native O0/O2
again retain all 270 checkpoints and ten images. The subsequent
[exact membership checkpoint](RESULTS-AOT-MEMBERSHIP-2026-10-03.md) preserves
every native counter and qualifies a **17.39% median / 17.04% minimum reduction**
in total 12B startup wall time across six quiet interleaved native-versus-native
legs. Menu performance remains unqualified. Six census intervals reconcile with zero overflow. These are observed
comparisons, not full machine or hardware equivalence. Full menu AOT and
60 FPS are unproven.

The earlier [22B menu census](RESULTS-DASHBOARD-PHASES-2026-10-02.md)
qualified six idle/navigation intervals under the previous CPU reference.
The two startup regions executed nothing in those intervals. MMX transfers,
x87 math and kernel operations are prominent first-code gaps. General LEA
lowering is qualified. Production MMX and bounded x87 arithmetic/transfer
services now exist. Broader x87 operations, exception/stack semantics and
extended-state qualification remain open.

The clean candidate profile also retains every checkpoint and route image.
Post-boot exclusive intervals identify native bodies, OpenGL, vertex and generic
raster work as substantial costs. The subsequent
[sampler correction](RESULTS-HOST-SAMPLER-WINDOWS-2026-10-03.md) resolves the
clamped last range: a quiet 27B profile retains every checkpoint/image and
accounts 101,462 samples across 18 actual intervals. The final interval is
verified as sustained main-menu animation after the return. Other fixed windows
can straddle scripted phases and must retain that limitation.

The [memory integer checkpoint](RESULTS-AOT-MEMORY-INTEGER-2026-10-03.md)
qualifies memory-read ALU/CMP/TEST, register NEG/NOT and LEAVE without an ABI or
runtime-guard change. Preserving all prior regions and adding 40 yields **286
regions**, **77.60% main-idle** and **82.08% Settings-entry** native execution.
Floor/native O0/O2 preserve all 270 checkpoints, ten images and device histories;
all 76 public tests and four census scalar/native-required tests pass. Attempts
and handoffs increase, so the coverage gain carries no new speed claim.

The evidence above is retained as the starting point. The fast-win order in this
document now governs scheduling: real Blinx launch, measured dashboard cost
attribution and compatible subsystem replacements take priority over broad
opcode expansion. Revisit parked candidates when a title supplies a relevant
repeatable workload; preserve the old result as workload-specific evidence.
