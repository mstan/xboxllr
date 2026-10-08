# Native title reference pilot

## Full movie validation and decoder stalls (2026-10-08)

`beads-xbox.1.73` and `.98`, branch `validate/blinx-movie-av-20261008`.
The existing post-GP output tap now accepts
`XLLE_PILOT_AUDIO_CAPTURE_SECONDS=1..300` (default 30). Its `.clock.csv`
sidecar associates sample indices with the mixer's monotonic clock at about
10 Hz; it does not measure sound-card/speaker latency. Samples and clock
records are copied into bounded memory; file writing remains off the audio
processing thread. `XLLE_PILOT_AUDIO_STREAM_PROFILE=1` records actual source
cursor, queued ownership and PCM updates for diagnosing streaming gaps.

`XLLE_PILOT_MOVIE_CAPTURE=<new file>` records sparse uploaded YUV luma grids
from successful draws, associated with the next accepted native present.
Blinx draws its movie into an intermediate render target, so the record keeps
that target as well as the texture, source dimensions, draw/present clocks and
GPU checkpoint. The grid comes from the actual uploaded native mirror, not a
guest buffer that might already contain the next frame. It requires no GPU
readback and is bounded to 36000 records; its file handle is process-owned.
This is diagnostic observation, not a second production decoder. Sparse grids
do not establish every output pixel or physical scan-out timing.

Independent FFmpeg decoding of the owned SFD inputs yields 302 Microsoft,
150 Artoon, 2264 opening and 540 title frames. Actual captured opening-movie
samples match through reference frame 2263, median luma RMSE 0.146 on the
16-pixel grid (95th percentile 0.224). Uniform/repeated frames, including the
Artoon tail, cannot be uniquely identified by this comparison. The captured
180-second run continues through title/loading into gameplay without a guard.
All 35 native tests pass (15.89 s), including real YUY2/UYVY conversion and
uploaded-mirror sampling after the caller's source bytes change.

Full A/V qualification **does not pass**: the opening's 75.49 seconds of source
video take roughly 80-86 seconds in these diagnostic observations, and waveform
alignment finds repeated delays near 16384/44100 = 0.371519 seconds, one movie
ring-buffer cycle. Stream traces expose producer cursor-poll gaps exceeding a
cycle. Two targeted stack snapshots land in generated movie coefficient/motion
decoding; another is in the guest scheduling path. These snapshots suspend the
process briefly, so that separate run is not a cadence measurement. The pilot
also still forced `/Od` for Release at this checkpoint. The next experiment is
compiler optimization before replacing a decoder or altering cursor semantics.

Private evidence: `build/blinx/movie-av-reference/`, `movie-av-first/`,
`movie-av-second/`, `movie-av-stream-trace/` and `movie-av-no-readback/`, their
manifests, PCM/clock files, frame grids, comparison JSON and three stack dumps.
The first run's back-buffer-only tracing assumption was corrected; its audio
and GPU captures remain valid, but it produced no movie grid records.

## Texture ownership preparation experiment (2026-10-08)

`beads-xbox.1.97`, branch `perf/blinx-draw-preparation-20261008`, base `dba1bcc`.
With native GPU events enabled, the next capture-disabled service profile
covers 1350 gameplay frames in 61.260 seconds. Inclusive graphics services
consume 46.432 seconds; draw pixel/target preparation accounts for 10.913 and
texture preparation 6.677 seconds. The existing target preparation scans the
entire texture table twice per draw even without CPU locks; sampling scans
children even for textures already unconditionally CPU-owned.

Following Phobos's per-resource ownership approach, a conservative hint on the
texture records locks through the parent or any surface view. An unexposed
family avoids the scans. A real CPU handoff still performs the existing upload
and clears all family exposure flags before clearing the hint. Releasing an
exposed child may leave a conservative stale hint, causing one extra scan;
it cannot suppress needed work. Ordinary CPU-owned textures still compare
their actual bytes at every consumption, including changes through retained
pointers. First uploads and GPU storage aliases retain their existing paths.
No new numerical tolerance or guest-visible behavior is introduced.

`XLLE_PILOT_TEXTURE_EXPOSURE_CACHE=0`, selected before launch, retains the old
scans for comparison. The bounded original header/lock and LLE memory/texture
contracts are unchanged. Real native fixtures add repeated alias CPU edits,
sampling, later GPU writes, rebinding and alias release, verifying output
pixels and isolation from an unrelated texture. Existing tests cover target
partial clears, sibling aliases, compressed mip tails and table growth/reuse.
Both new and reference graphics fixtures pass with the D3D debug layer
(3.36/3.34 seconds); all 35 native tests pass in 16.06 seconds.

Two same-executable, capture/service-profile-disabled 200-second title runs
compare the old scans with owner hints. GPU events and callback profiling are
enabled in both; guest CPU15 affinity and the same replay controller are used.

| Measurement | Previous scans | Owner hints |
| --- | ---: | ---: |
| First 300 gameplay intervals | 28.59988 fps | 30.00556 fps |
| Complete first stage, 2328 intervals | 23.71334 fps | 26.41506 fps |
| Complete stage span | 98.17258 s | 88.13155 s |
| Median / 95th percentile interval | 36.185 / 64.171 ms | 33.441 / 44.716 ms |

Both stages end naturally, contain 2329 presents and exactly the same callback
context sequence, and complete all 75 replay writes. Neither run has a
guard/fault before harness termination. The complete-stage improvement is
1.114x (+11.39% cadence); its later scene still misses the nominal 30 fps target.
The short opening window reaching 30 fps does not qualify full original speed.
Phase boundaries use stage-audio and publisher-movie openings, not an assumed
frame count. Configuration, executable hash and interval/context data remain
in `build/blinx/exposure-comparison.aot-private.json` and
`build/blinx/exposure-whole-stage.aot-private.json`.

The subsequent captured `exposure-input-profile` observation samples Start
at 90016 ms, visibly returns to `Press START to begin`, and renders another
3D demo after idling. All 68 writes complete; 34 images and the run manifest
record no guard/fault before harness termination (180.593 s, exit 1). This run
is functional/attribution evidence, not part of the speed comparison. In its
first ten complete stage reports, target/pixel preparation totals 42.633 ms
over 1,219,968 draws; the prior profile reports 1674.881 ms over 1,194,457 draws.
Those different draw counts/configurations limit this to cost attribution.
The unchanged bounded NV2A regression also passes (0.20 s overall).

## GPU completion notification experiment (2026-10-08)

`beads-xbox.1.95`, branch `perf/blinx-native-handoffs-20261008`, base `3730def`.
With diagnostic caching enabled, a 960-frame instrumented gameplay observation
attributes 42.510 of 51.567 seconds to graphics services. Its first 301 presents
include 6.146 seconds of drawing-thread callback barriers. This directs the
next experiment toward completion notification rather than changing shaders
or dropping draws.

The existing completion owner still submits real D3D11 event queries and checks
their results. On a renderer exposing `ID3D11DeviceContext3`, it also uses
[`Flush1`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext3-flush1)
to obtain a native completion event. The DPC worker holds a duplicate event
handle in its wait set, allowing a GPU signal to wake it before its existing
one-millisecond fallback deadline. Only that worker replaces/closes the wait
handle. Query ownership retains the original event through signal/retirement;
duplicates remain valid afterward. Device teardown waits for remaining event
owners before release. Query errors remain real failures.

After delivering a callback, the worker can immediately inspect the next FIFO
entry. Actual pending GPU work still waits; no completion is inferred from
submission. The existing later-command barrier, callback context/ABI and
separate DPC registers remain intact. `XLLE_PILOT_GPU_EVENTS=0`, selected before
launch, retains the prior query/timer path including its queued-callback retry
interval. Unsupported Context3 renderers also use that polling path.

Phobos's inline callback implementation was reviewed, but its assumption of
immediate completion does not establish our asynchronous WRITE contract. No
donor callback body was copied. The bounded LLE WAIT_FOR_IDLE/software-trap/IRQ
ordering regression passes (0.26 s overall, software fixture). All 35 native
tests pass with the D3D debug layer (16.82 s). The added fixture exercised
15/16/16 actual owned event waits across repeated backend lifecycles, checks
real output pixels and handle validity after query retirement. Existing tests
also cover 70 FIFO callbacks, reentrant queries and in-flight device teardown.
Polling-fallback graphics fixtures pass (3.44 s).

Private evidence: `build/blinx/cached-services-profile/`,
`build/blinx-gpu-events-tests.log`, and the separate polling/event title runs
and comparison under `build/blinx/gpu-events-*`.

Same-executable, capture/service-profile-disabled observations measure the
first 300 gameplay present intervals at **21.73658 fps** / 38.7344 ms median
with polling, and **27.88042 fps** / 33.3388 ms with native events (1.283x).
Both windows contain the same 3600 callback-context sequence. Drawing-thread
barrier time falls from 5284.829 to 2041.884 ms; callback queue medians fall
from 1.343 to 0.296 ms. Boundary callback groups are excluded from the matched
sequence comparison. Both 120-second runs have no guard/fault before harness
termination; they reach replay files 34 and 48 respectively. This establishes
a same-stage throughput improvement, not identical entire guest executions or
full original-speed operation. Configuration, executable hash and measurements
are in `build/blinx/gpu-events-comparison.aot-private.json`.

The same executable subsequently ran for 600 seconds with five-second GPU
captures: two natural complete 3D demos, intervening publisher/opening/title
sequences and a third demo entry. All 195 writes completed, 59 images were
captured, and no guard/fault occurred before the owned harness terminated it
(601.047 s, exit 1). Comparable gameplay samples at 120/360/570/600 seconds
report private memory 751.39/751.65/754.81/753.70 MB and 617/611/605/605 handles.
This is bounded resource evidence. The two full captured stage spans each have
2329 presents and average 23.82/22.16 fps, including captures and transitions;
the shorter uncaptured 27.88 fps result must not be generalized to whole stages.
Evidence: `build/blinx/gpu-events-natural-loops/endurance.aot-private.json`.

The separate 180-second `gpu-events-input-return` run samples Start requested
at 90000 ms at 90016 ms, returns to the visible title screen, then loads and
renders another 3D demo. Its 34 presentation captures include the menu return
and reentry; no guard/fault occurs before harness termination (180.890 s).
This retains the working input route on the completion-event implementation.
Full audio/video synchronization and whole-sequence original speed remain open.

## Original NTSC frame pacing (2026-10-08)

`beads-xbox.1.96`: the original main loop masks the title state with `0x7f`.
States 4, 9 and 10 select two display ticks; other states select one. The
original vblank callback increments a counter, and the original frame loop
waits for the selected count before Swap. Under the current nominal 60 Hz
display contract this means a **30 fps target for two-tick gameplay**, with
one-tick phases permitting 60 fps. Movie frame cadence remains a separate
property of the decoded stream.

`tools/native_blinx_cadence_reference.py` executes the original register
initializers, selector, callback counter prefix and pre-Swap gate for all 256
state-byte values and four counter values each (1024 gate cases). It pins the
complete NTSC XBE hash. These are bounded fragments: execution stops before
uncovered calls; it does not replace them or claim full main-loop/hardware
execution. Earlier `1/30` fade constants alone do not establish frame cadence.

Read-only observation of the owned native pilot verifies its guest allocation
against 48 original code bytes, then samples state/divisor/counter without
writing guest memory or attaching to physical hardware. During rendered attract
gameplay it observes state 132 (masked 4) and divisor 2; subsequent movie phases
use states 128/130 and divisor 1. Evidence is in
`build/blinx/cadence-original.aot-private.json` and
`build/blinx/gpu-events-natural-loops/cadence-observation.aot-private.json`.
This establishes the application's nominal pacing selection. It does not
establish its exact real-console drop pattern, NTSC oscillator rate, or full
simulation/audio/video synchronization. The physical-console authorization
boundary in `beads-xbox.1.12` remains in force.

## Native frame-service investigation (2026-10-08)

`beads-xbox.1.94`, branch `perf/blinx-frame-services-20261008`, base `364fe52`.
`XLLE_PILOT_SERVICE_PROFILE=1` records per-thread inclusive graphics API time
and draw phases every 30 swaps. DPC execution is excluded from that thread's
accounting; waits inside a graphics call remain included. The separate
`XLLE_PILOT_CALLBACK_PROFILE` identifies callback wait costs. These diagnostics
observe the selected implementation without duplicating guest or GPU work.
Clock/logging overhead means these reports are attribution evidence, not an
uninstrumented cadence claim. First reports crossing phase boundaries are
excluded from stage totals.

The capture-disabled `draw-services-profile` run covers 750 fully in-stage
frames in 58.506 seconds: 51.978 seconds inside graphics APIs. Draw phase
totals include 5.948 seconds in the final disabled frame-capture lookup,
6.475 seconds in vertex preparation (also a disabled feedback lookup), and
6.340 seconds in shader preparation (also a disabled draw-capture lookup).
Actual submission is 1.365 seconds; deferred state forwarding is 1.599 seconds.
This directs the next experiment toward configuration lookups before broader
state-cache work. Phobos documents the same repeated disabled-lookup cost in
`hle_d3d8_state.c` and caches it.

`graphics_diagnostics.inc` applies that approach to draw/feedback/FVF/frame
capture and texture-binding feedback trace settings. Each setting, including
absence, is captured on first use and owned for the process lifetime. Set
diagnostic environment variables before launching; the configured draw-capture
trigger file remains live. `XLLE_PILOT_DIAGNOSTIC_CACHE=0` selects the previous
lookup behavior for a separate-run comparison. No GPU work, guest state or
resource synchronization is removed, so the existing LLE draw contract and
native pixel/ordering fixtures remain applicable without a new tolerance.

Private profiling evidence: `build/blinx/{frame-services-profile,draw-services-profile}/`
and their `run-*.log` files. The first 180-second observation's final CDB stack
collection timed out; raw logs/resources and `recovered-setup.aot-private.json`
record that limitation. The subsequent 120-second run has a complete manifest.

The same-executable cache-disabled/enabled comparison measures the first 300
gameplay present intervals with capture/service profiling disabled, callback
profiling enabled, guest CPU15 affinity and a real replay controller:

| Configuration | Cadence | Median frame interval |
| --- | ---: | ---: |
| Repeated environment searches | 13.71919 fps | 65.0012 ms |
| Cached diagnostic configuration | 21.18739 fps | 40.9403 ms |

That is 1.544x cadence and 37.0% lower median frame time. Both measured windows
contain the same sequence of 3600 GPU callback contexts. This is a same-stage
throughput comparison, not proof of identical entire guest executions. Both
120-second observations end by harness termination (exit 1), without an earlier
guard/fault. The candidate reaches replay file `pelon037.rec`, the reference
`pelon022.rec`. No draw, callback or resource handoff is bypassed by this change.
The graphics backend/ABI tests pass (3.50 s); the initial profiler also passed
all 35 native tests (16.70 s). Evidence and executable hash are in
`build/blinx/diagnostic-cache-comparison.aot-private.json` and the two run folders.
This improves one measured gameplay window; full original-speed operation
throughout the attract sequence remains unproven.

The same build's subsequent `diagnostic-cache-visible` run retains configured
draw capture (32 real draws) and five-second presentation captures. Start at
120000 ms is sampled at 120016 ms, followed by the visible `Press START to begin`
screen; idling loads and renders a second 3D demo. Its 210-second observation
has 40 presentation captures, 45 completed writes out of 45, and no guard/fault
before harness termination (elapsed 210.422 s, exit 1). All 32 captured draws
pass the existing independent NV2A CPU-reference/fresh-GPU comparison at the
unchanged `2e-4` tolerance. The run manifest, images, draw inputs and comparison
log are retained in `build/blinx/diagnostic-cache-visible/`. This diagnostic run
is functional evidence; it is not used for the speed claim above.

## Natural attract repetition observation (2026-10-08)

`beads-xbox.1.62`, executable built from the `ba89637` input/capture checkpoint.
`attract-natural-loops` ran for 900 seconds with a real replay controller:
Start at 15-16 seconds to interrupt initial startup, then no further buttons.
Ten-second GPU captures and thirty-second process resource samples cover the
observation. Two 3D demos end without input and restart the publisher/opening
movies, title screen and loading sequence; a third rendered demo is underway
when the harness stops the title (elapsed 901.531 s, harness termination exit 1).
There are no earlier guard/fault events, three stage entries, two natural movie
restarts, 89 captured frames and 193 completed writes out of 193 submissions.
This is a bounded repetition result, not completion of the entire goal.

The following event times use the first present as their clock origin; file
openings identify phase boundaries rather than exact movie start/end timestamps:

| Event | First pass | Second pass | Third pass |
| --- | ---: | ---: | ---: |
| Stage-start audio opened | 66.198 s | 430.110 s | 778.516 s |
| Natural return to publisher movie | 281.524 s | 644.074 s | Not reached before observation ended |
| Subsequent title audio opened | 374.577 s | 740.108 s | Not reached |

Comparable gameplay process samples at 180/570/840 seconds report private
memory 752.54/752.41/753.49 MB and 607/605/605 handles. Memory falls during the
intervening movie/title phases. This does not establish leak freedom for an
arbitrary run length or isolate all GPU allocations.

The full opening movie runs twice. Independent FFprobe decoding of `op01.sfd`
reports 2264 frames, average 30000/1001 fps and 75.493 seconds of video
(the nominal header rate is 60000/1001). Observed file-open-to-next-title-audio
intervals are 76.437 and 79.384 seconds, including transitions. Their 3669/3744
presents are not unique decoded-frame counts; complete frame-delivery and
audio synchronization qualification remains open under `.73`.

Private evidence: `build/blinx/attract-natural-loops/endurance.aot-private.json`,
`resources.aot-private.json`, `run.aot-private.json`, `op01-probe.json`, captured
PPMs and `build/blinx/run-attract-natural-loops.log`. Original-speed 3D cadence
remains unproven; the last uncaptured measured stage is 13.87 fps. JSRF remains
deferred until Blinx meets the full goal.

## Attract input validation setup (2026-10-08)

`beads-xbox.1.93`, branch `validate/blinx-attract-input-20261008`, base `0b66ab1`.
Timed host input requires **both** `XLLE_PILOT_INPUT=replay` and
`XLLE_PILOT_INPUT_REPLAY=<existing replay file>`. Supplying a replay filename
alone leaves the default physical XInput provider selected. The recent vertex
range comparisons and `scene-qualified-extended` actually ran with zero
controllers attached; their logs and measured timings remain valid, but those
runs are automatic attract playback, not input validation. Earlier
`timer-reference`, `timer-highres`, `scene-first` and `scene-viewport` logs do
confirm enabled replay and one controller.

`[INPUT REPLAY DELIVERED]` now reports each sampled transition with its requested
and actual elapsed milliseconds. This drives the existing XAPI input path;
no game state or control flow is patched. The private extended-run harness
selects/requires the replay provider whenever a replay file is supplied and
records startup configuration and executable hash in its run manifest.
`XLLE_PILOT_PRESENT_CAPTURE_INTERVAL_MS` selects a capture interval from 100 to
60000 ms (default 1000), retaining the existing 180-image bound. It permits
visual coverage of a longer attract/menu-return run without increasing the
capture count; diagnostic capture remains unsuitable for cadence benchmarks.

Actual `attract-input-return` uses Start at 15-16 seconds to interrupt startup,
then at 210-211 seconds during rendered 3D attract. The latter press is sampled
at 210109 ms, release at 211062 ms. Capture 69 precedes that press; subsequent
captures show the visible `Press START to begin` screen, while title/movie/audio
assets reload. Idling then loads `round1.adx` / `01_StageStart.adx` a second time
and visibly re-enters the 3D demo. The bounded 330-second observation has no
guard/fault before harness termination. This establishes gameplay interruption,
title-screen return and re-entry, not natural end-to-end completion or stable
repeat loops. Private evidence is `build/blinx/attract-input-return/`,
`build/blinx/run-attract-input-return.log` and `input-attract-return.txt`.
The five input contract/guard tests pass; no new service implementation was
needed for this transition.

## Draw-range vertex synchronization experiment (2026-10-08)

`beads-xbox.1.92`, branch `perf/blinx-vertex-ranges-20261008`, base `bb04d2e`.
The visible-scene run repeatedly samples the drawing thread in whole-resource
`memcmp`; registered buffers reach multiple megabytes. Phobos's
`src/hle/hle_d3d8_vertex.c` instead submits the vertices consumed by each draw.
The candidate applies that range strategy to our existing multistream binding:
one initial full upload, then compare/update only the validated
`first * stride` through `(first + count - 1) * stride + attribute_extent` span.
Guest CPU storage, handles, declaration, draw order and resource lifetimes
remain the existing ABI. Later draws synchronize any newly consumed mutations.

Changed spans use the borrowed renderer's DEFAULT buffers and immediate-context
[D3D11 UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource),
which snapshots source bytes while preserving earlier queued work. Real GPU
serials track each update. `XLLE_PILOT_VERTEX_RANGES=0`, selected once at startup,
retains whole-resource synchronization for an A/B reference; there is no live
switching or duplicate execution. Existing LLE draw fetch/stride bounds remain
the floor; no newly required hardware operation is replaced with success.

The real GPU fixture checks disjoint/overlapping mutations, unchanged spans,
an earlier queued copy observing old contents, later range use, full-sync
interoperation and slot reuse. Actual multistream/depth/flat-shading pixel tests
also use partial updates. Graphics tests pass with the D3D11 debug layer;
all 35 native tests pass (16.07 s).

The first captured comparison, anchored at opening `01_StageStart.adx`, has
28 presents and 327 callback records in both runs: full uploads 3.90323 fps /
254.8003 ms median, ranges 16.62740 fps / 59.9860 ms median (4.26x). The candidate
still renders the world/character and reaches `pelon062.rec` during a bounded
300-second observation; full uploads reached `pelon049.rec` in 600 seconds.
Both were stopped by the harness, without an earlier guard/fault. This does
not establish a complete attract loop or stable long-term resource use.
Private evidence: `build/blinx/vertex-range-comparison.aot-private.json`,
`build/blinx/{scene-qualified-extended,vertex-ranges-candidate}/`, and the
corresponding `run-*.log`.

Same-executable, capture-disabled A/B observations use identical CPU15 guest
affinity, no connected controller, media and callback profiling. The first 28
stage presents measure full synchronization at 3.51427 fps / 284.7463 ms median
and range synchronization at 13.87240 fps / 71.1403 ms median: 3.95x cadence,
75.0% lower median frame time. Each measured 27-frame interval contains 324
callback records. Boundary callback groups differ at the loading transition;
this is a same-stage throughput comparison, not a deterministic frame replay.
Both 180-second runs have no guard/fault before harness termination
(elapsed 181.125 / 181.156 s, title exit 1 from the harness).
`build/blinx/vertex-ranges-nocapture.aot-private.json` retains executable hash,
timings, callback sequences and run manifests. Cadence remains below the goal;
complete attract playback, input return and stable loops remain unproven.

## Visible attract scene and scalar shader reference (2026-10-08)

`beads-xbox.1.89` / `.91`, branch `investigate/blinx-scene-20261008`, base
`ac8077e`. Actual NTSC Blinx now renders its textured 3D world and character
in attract play. The missing handoff was reserved shader constants 58/59:
viewport scale/offset remained zero, collapsing the scene's XYZ positions.
Phobos's `shadow_viewport_constants` identifies the service boundary. The
independent adapter follows the original emitter and existing LLE NV097
registers, preserving the original pixel bias, depth interval and zero W.
Hardware-reserved writes do not overwrite the SDK user-constant query cache.

`tools/native_viewport_constants_reference.py` executes the complete original
NTSC emitter and viewport query, including reserved/no-reserved branches,
without replacing calls. Image-qualified addresses stay in this reference
tool; shared runtime uses generated revision metadata. The native viewport
fixture checks the constants uploaded to the real backend.

The first actual draw comparison exposed a reference defect: the permissive
NV2A decoder represents scalar ARL with a zero vector mask, and the borrowed
CPU emulator discarded that write. `cmake/Nv2aVshArl.cmake` generates a corrected
copy with source/prepared hashes and leaves the donor originals intact. The
compiled LLE executor now stores the scalar result too. Eight signed/fractional
indices independently select distinct constants on CPU and GPU; a real LLE
pushbuffer test retrieves the same three positions through negative/positive
relative addresses in interpreted and compiled modes.

All 32 captured `scene-viewport` draws agree with the corrected CPU reference
and fresh GPU replay at the existing 2e-4 scaled-output tolerance. No production
duplicate execution or instruction-history requirement is introduced. All 35
native tests pass (15.85 s), the bounded software NV2A test passes (0.31 s;
OpenGL fixtures unavailable in this build), and 64 pipeline guards pass.
Private evidence: `build/blinx/scene-viewport/`,
`build/blinx/viewport-constants-reference.aot-private.json`,
`build/scene-arl-draw-reference.log`, `build/scene-arl-native-tests.log`.
The 180-second captured title run has no guard/fault before the harness stops
it. Full attract loops, interruption/menu return and original cadence remain
unproven; the timer measurement below predates the visible-scene correction.

## Callback retry timer checkpoint (2026-10-08)

`beads-xbox.1.90`, branch `perf/blinx-callback-timer-20261008`, base `a56b056`.
The shared relative wait uses the same high-resolution native timer already
used by presentation waits, with the existing coarse API as a fallback when
the flag is unsupported. Guest deadlines, timer signaling, callback ordering
and real GPU completion conditions are unchanged. See Microsoft's
[CreateWaitableTimerExW contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).
There is no borrowed implementation or new guest ABI behavior in this change.

Two fresh actual-title observations use identical CPU15 guest affinity,
startup replay, media and callback profiling, without image capture. The
matched first 28 stage presents contain 326 callback records each:

| Run | Presented fps | Median frame ms | Median callback queue ms |
| --- | ---: | ---: | ---: |
| `timer-reference` | 2.929867 | 340.2453 | 11.8565 |
| `timer-highres` | 4.165738 | 239.6022 | 1.2655 |

The 42.2% cadence gain is accompanied by shorter measured native retry waits.
Maximum polls rises from 3 to 8 because checks happen sooner; readiness is
still determined by the real query result. Both observations reach their
180-second bounds without guard/fault before harness termination. The faster
run reaches the time-stop sound asset. All 35 native tests pass (15.84 s).
Private logs and `callback-timer-comparison.aot-private.json` are under
`build/blinx/`. These are presented-frame measurements with unresolved scene
rendering, not original-speed or complete-attract validation.

## GPU completion progress checkpoint (2026-10-08)

`beads-xbox.1.87`, experiment `perf/blinx-callback-latency-20261008`.
Allow D3D11 `GetData` to advance queued commands (flags zero) while polling real
event completion. `S_OK` and a true event result remain necessary; FIFO callback
order and WRITE barriers are unchanged. Microsoft documents that
[DONOTFLUSH can prevent queued commands from progressing](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag).
Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`,
`src/hle/hle_d3d8.c` InsertCallback, suggests prompt callback delivery, but its
immediate completion assumption and original-body execution are not adopted.
No donor implementation is copied by this change.

Matched actual NTSC Blinx runs use the same CPU15 guest affinity, startup input,
media and opt-in `XLLE_PILOT_CALLBACK_PROFILE=1`, with image capture disabled.
The first 28 presents after the first Xbox ADPCM allocation contain 326 callback
records in each log. Baseline `callback-profile-baseline` measures 0.2351486 fps,
median 4253.9725 ms/frame and maximum 255 polls/event. Candidate
`callback-flush-progress` measures 2.9471945 fps, median 339.9738 ms/frame and
maximum 2 polls/event: 12.53x faster. This contiguous window avoids incorrectly
combining distinct loading phases. Logs are private under `build/blinx/run-*.log`.
All 34 native tests pass in 14.59 s, including real GPU completion, pixels,
callback ordering and ownership. No new LLE behavior is substituted.

The candidate completes three replay-file writes and reaches the zero-byte
audio-buffer guard after 147.11 s (`beads-xbox.1.88`). It does not establish
rendered gameplay, complete attract playback or original speed. Ordinary
callback retry latency remains roughly 12–15 ms; a separate timer experiment
may address it, subject to actual-title measurement.

`beads-xbox.1.61`, 2026-10-06. This standalone experiment uses Phobos
`63f18f0334fbd8c00313c4bb565accf00627a4fd` discovery, lifting and CPU helper header.
It is outside the normal xboxlle build. **The generated executable is a private
GPL-dependent reference artifact, not the intended non-GPL shipping product.**
See [license scope](../../LICENSE-SCOPE.md). There is no install/package target.

## Audio storage detachment contract (2026-10-08)

`beads-xbox.1.88`. The reached original NTSC sound-reuse path calls Stop,
SetBufferData(NULL, 0), then Release before creating its next sound. Detachment
now retires the real host source voice and its readers/callbacks, releases only
internally allocated guest data, and preserves the guest object, device
reference, format, gain, frequency and routing for a subsequent binding.
Rebinding uses the existing real PCM/ADPCM path, with a fresh cursor and full
play/loop extent. No successful playback of an empty buffer is substituted.

Phobos `bcccf8c`, `src/hle/hle_dsound.c` SetBufferData, supplies the lifecycle
reference, not an imported body. Its model-level stop and settings replacement
map onto the pilot's real native transport. The bounded floor consists of the
complete original caller-owned settings helper, original caller argument prefix
(`tools/native_audio_detach_reference.py`), and existing MCPX VOICE_OFF
notifier/idle-IRQ test. This does not claim an exhaustive original driver/APU or
original kernel allocator run. Native tests independently cover eight active
PCM lifetimes, silence after detach, caller-owned versus internal storage,
retained settings and mono/stereo ADPCM output after rebinding. The existing
APU test passes; the original settings fixture passes five detach/rebind cases.
The final native suite passes all 35 tests in 17.92 s.
References and license boundaries are recorded in the attribution inventory.

Actual `audio-detach-progress` runs for the full 300-second observation and is
then stopped by its harness (301.5 s including stack capture; exit1 is that
termination, not successful title exit). It passes the old guard, logs at least
eight storage detachments, completes 19 actual 1,913,856-byte writes with status
zero, and opens `09_TimeStop.adx`. Capture is enabled; the last 60 presents
measure 2.738 fps, median 369.696 ms. Captures still show the demo overlay/logo
and effects over a largely black scene. Final stacks now sample the guest in
vertex upload/draw work. This is further actual progression, not a complete
attract sequence, rendered gameplay, stable repeated loops or original speed.
Missing scene rendering and remaining callback retry latency are tracked by
`beads-xbox.1.89` and `.90`. Private evidence is under
`build/blinx/audio-detach-progress/` and `run-audio-detach-progress.log`.

## Asynchronous writes and installed HDD files

`beads-xbox.1.86`: the demo-start caller submits `z:\pelon000.rec` through
XAPI WriteFile with an OVERLAPPED structure, nullable event and explicit offset.
The actual run writes 1,913,856 bytes. `native_file_write_reference.py` executes
the original routine up to its kernel CALL with 44/6000-byte fixture inputs
and checks those arguments. Those fixture sizes are not the observed runtime
write length and do not prove original FATX acceptance of every alignment. It neither
replaces the kernel nor fabricates a completed call. Existing original KeEvent
fixtures supply event state transitions. The bounded ATA floor now gathers
both DMA fragments before storing and signaling, independently reads the data
back through PIO, and rejects failed gathering without success or disk mutation.
This does not establish the complete retail filesystem/IRP pipeline.

HLE extends the existing asynchronous-read owner to real native writes. Guest
IOSB publication precedes optional event and file signaling. The file signal
marks each completed request; a separate idle event drains all requests before
close/reuse. File waits check SYNCHRONIZE and retain their host reference.
APCs and implicit async offsets remain explicit unsupported boundaries.

Following Phobos's host-file mapping, installed HDD files use native buffering
instead of imposing Windows device-sector alignment on guest byte records.
Optical opens retain their existing mode. Guest byte contents, offsets, EOF,
access rights and errors remain observable; explicit write-through and flush
are preserved. Cache residency, device timing and completion interleaving are
HLE tolerances, not claims of identical hardware behavior. Byte-sized fixture
writes establish the installed-file HLE behavior; host buffering is not a
claim that the original disk driver permits every host-accepted request.

Validation: independent host reads confirm exact writes and EOF/gap contents;
tests exercise optional events, file waits, two IOSBs, early event/file close,
access rejection and 32 lifetimes. Native suite: 34/34 in 14.77 s. Original
submission/event fixtures and LLE disk tests pass (disk 0.02 s). Actual
`async-write-attract` completes two 1,913,856-byte replay-file writes and runs
to the 420-second observation limit without guard/crash. The harness captures
stacks then kills its process (427.141 s total). Independent host file size
confirms the first write. Frames show Demo play and a green effect but no
established rendered gameplay. Final presentation measures14 intervals over
59.412 s (0.236fps), including diagnostic overhead. `.87` tracks the measured
callback-barrier wait;12 callbacks per tail frame mean the1ms queue delay
alone cannot account for the4.25-second frame interval. Full attract,
interruption/menu return and original cadence remain unproven.

## Xbox ADPCM buffers

`beads-xbox.1.85`: Blinx reaches format0x69, mono44100Hz, 36-byte blocks,
4bits, cbSize2, with its samples-per-block extension word zero. The bounded
reference uses the complete external APU decoder and its VP's actual
64-frame consumption rule. The existing machine APU register scaffold is
not being represented as a full sample decoder. Complete original NTSC
MCPX byte/frame helpers independently establish fixed64-frame block
conversion, integer truncation and RET4; they do not prove the entire driver.

Prepare `tools/native_adpcm_donor.py --donor <pinned-phobos-checkout>
--output build/phobos-adpcm`, then configure `PILOT_ADPCM_SOURCE` to that
absolute directory. This selects Phobos's unchanged NoRain-derived decoder
inside the private GPL-dependent pilot. The separate APU decoder is linked
only into `pilot_adpcm_test`. Original helper qualification is reproducible
with `tools/native_adpcm_cursor_reference.py --xbe <NTSC-xbe> --output
<private-json>` (Unicorn required).

Guest buffers retain compressed storage. Allocation/play/unlock decode into
the existing real native PCM engine; partial writes refresh complete affected
blocks. Position, seek and loop boundaries convert between compressed bytes
and PCM frames; frequency, routing, gain and downloaded DSP remain intact.
Allocation/decoding failures propagate errors. No clock-only completion or
silent-output fallback is imported. Observed formats allow one/two channels,
36 bytes/channel, 4bits and 64 played frames/block. The extension word does
not override that hardware rule. Streaming and other formats remain separate.

Tolerance: ADPCM seeks/cursors use complete-block resolution. The original
inactive driver can cache an unaligned requested byte offset; this adapter
reports the quantized playable offset instead. The checked title descriptor
does not establish a dependency on inactive unaligned round trips. This is
an explicit bounded difference, not a claim of exact full-driver equivalence.

Validation: 512 mono/stereo codec cases agree exactly with the APU reference
for the played samples, including signed index clamping, nonzero reserved
bytes and the unused final nibble. Original helpers pass block-boundary and
PCM-control cases. Native ABI tests verify actual mono/stereo device output,
partial/wrapped locks, source ownership, byte cursors, seek/loop/end and full
release. Full native suite passes 34/34 in 14.54 s. Actual `adpcm-attract`
passes the format guard and more stage reads, reaches the Demo play overlay,
then stops at asynchronous `NtWriteFile` completion for `z:\pelon000.rec`
(.86), after 246.109 s with capture. The scene under the overlay remains
black; rendered gameplay, complete attract and intended cadence are unproven.
Evidence: `build/blinx/run-adpcm-attract.log`,
`adpcm-attract/run.aot-private.json` and `adpcm-attract/latest.png`.

## Render-target feedback

`beads-xbox.1.84`: actual Blinx binds the 640x480, single-level linear ARGB8
scene texture as both stage0 input and output. The private `feedback-draw`
capture records an aligned FVF104 full-screen quad; its screen-bias correction
and UV span map each fragment to its own texel center, including linear filter
sampling. Binding overlap alone performs no texture sampling.

The bounded NV2A software fixture samples/darkens the same RAM color surface
twice, checking every pixel across both triangles with point/linear filtering.
HLE borrows Phobos's `feedback_scratch`, copy and stage restore, connecting
them to our real targets and direct draws. It copies the target once before
each feedback draw and reuses GPU scratch storage across matching draws.
There is no CPU readback, wait or duplicate raster execution in this path.
Aliasing compares underlying GPU storage, including separately wrapped RHI
images. Target transitions preserve real bindings and reinstate sampled views.

Local adaptations make scratch/copy failure terminal and release/reset the
scratch on device destruction. Drawing from the depth output remains guarded,
as do feedback formats other than single-level linear ARGB8; the donor also
rejects multisample/cube scratch. The qualified equivalence is pixel-local
feedback. HLE uses a snapshot from the beginning of the draw; cross-pixel
feedback ordering is not established as a hardware-equivalence guarantee.

Native fixtures check all output pixels, repeated inputs, one copy shared by
two stages, cached allocation/resize, both binding orders, default-surface
aliases and three device lifetimes. Full native suite passes 32/32 in 13.65 s;
the additional default-alias/COM-draw test passes in 0.91 s; software NV2A
passes in 0.25 s. Actual `build/blinx/run-feedback-attract.log` confirms the
donor copy, advances to `01_StageStart.adx` and stops on Xbox ADPCM sound-buffer
format0x69 (.85). This is stage-startup progress, not a completed attract loop.

## Empty vertex-declaration binding

The next integration run exposed a separate ABI defect (`beads-xbox.1.83`):
the donor's `GetPixelShader` metadata lists two parameters/stack8, but the
complete original function takes one output pointer and returns with RET4.
Our earlier native test helper used the same metadata for its expected stack,
so it missed the discrepancy. The original query fixture itself was correct.
This corrupted Blinx's saved state and ultimately restored a code return
address as a pixel-shader handle. The ownership guard caught it.

Binding generation now corrects the SHA-qualified query and verifies original
return cleanup for all 129 identified D3D functions, including the reviewed
FrontFace tail transfer to CullMode. The native query fixture independently
asserts caller-stack position and canary, without metadata-derived cleanup.
The corrected full native suite passes 32/32 in 13.34 s. Keep empty binding
qualification separate from drawing and from this stack-corruption fix.

`beads-xbox.1.82`: the actual rejected FVF is zero. The original XDK accepts
`SetVertexShader(0)` and creates an empty declaration: zero format flags and
texture dimensions, all sixteen attributes disabled, cached handle zero and
fixed-function hardware mode. It preserves programmable constants/program
cache. `tools/native_vertex_binding_reference.py` executes the full original
setter against native before/after captures; all 276 descriptor bytes and
query-visible state match through nonzero/zero/nonzero transitions.

The adapter reuses its existing lazy vertex binding around Phobos's pipeline.
It accepts that real state transition; draw submission still rejects an empty
format until an actual drawing contract is qualified. No new vertex pipeline
or default geometry is substituted. Native graphics ABI passes in 4.79 s.
The initial zero binding occurred under the corrupted caller stack; this is
a separately valid binding contract, not proof that the title needs empty
geometry. Actual `pixel-query-abi-attract` passes shader restoration, presents
the loading screen and reaches texture/render-output overlap (120.06 s with
capture). `beads-xbox.1.84` tracks Phobos's existing GPU feedback-snapshot path
as the next adoption candidate. No complete attract or cadence is established.

## Mapped pixel-shader constants

`beads-xbox.1.81`: the complete original setter and getter execute unhooked in
`tools/native_pixel_constants_reference.py`. They establish packed ARGB cache,
both eight-stage combiner mappings, final C0/C1, output extent, zero count and
stdcall. Register zero also emits NV097 EYE_VECTOR (0x181c). It is not diffuse
color. Existing software NV2A stage/final-constant pixel fixtures provide the
bounded raster reference.

The adapter writes the guest packed cache and mapped render states. The cache
offset is decoded into the private revision binding. Existing Phobos draw and
constant-buffer uploads consume those states; updates need neither shader
recompilation nor GPU synchronization. Phobos's stub desktop constant setter
remains excluded. Native half-up quantization allows at most one byte per
channel difference from original SSE rounding. Eye-vector state is retained;
constant-eye reflection mode 0x12 remains explicitly unsupported.

Native ABI tests check mappings, query-cache contents, unchanged shader
ownership/definition and real GPU pixels, including a subsequent constant
change without rebinding. Original setter/getter and both focused native
graphics tests pass. Full-suite validation hit the existing intermittent
process-wide vblank handle-count assertion (.72); its isolated retry passes.
Actual `build/blinx/run-pixel-constants-attract.log` passes constant updates,
submits additional GPU work and stops at an unqualified FVF (124.06 s with
diagnostic capture). No complete gameplay/attract frame or cadence is claimed.

## Logic COPY during Blinx scene startup

`beads-xbox.1.80`: the reached original caller explicitly requests COPY
(0x1503), then restores its prior logic state. The complete original setter
in `tools/native_logic_reference.py` proves FIFO enable/op packets, state150
cache, output extent and calling convention with available FIFO space.
The software NV2A floor executes COPY into real framebuffer RAM with masks,
and bypasses blending only while logic is enabled. Its new mask fixture also
corrects a reversed red/blue mask mapping. Native tests verify actual pixels,
requested blend changes under COPY and restoration after disable.

HLE reuses the existing Phobos blend state and cache; COPY selects source
replacement, and disabling logic restores the requested blend state. It adds
no GPU readback or special shader. This qualifies zero/disabled and COPY,
not arbitrary bitwise operations: those still fail explicitly. Native tests
pass 32/32 in 13.65 s; software NV2A passes in 0.29 s.
The final explicit source-alpha pixel check passes the native ABI test in
4.51 s. Actual `build/blinx/run-logic-attract.log` passes COPY and reaches
`D3DDevice_SetPixelShaderConstant` (0x1367e0), 121.06 s with diagnostic capture.
This is scene-compositing startup progress, not a completed gameplay frame.

## Flat shading for scene startup

Later integration found and corrected a stack-cleanup mismatch in the native
query metadata (.83, above); the original query fixture remained valid.

The subsequent `GetPixelShader` query (`beads-xbox.1.79`) reads the existing
bound guest handle, including zero for the fixed-function path. Its original
20-byte XDK function runs unhooked in `tools/native_pixel_query_reference.py`;
the fixture verifies a four-byte output, stdcall cleanup and no device/shader
mutation. Native tests cover actual bind/unbind/rebind, unaligned output and
unchanged shader ownership/definition. Phobos's desktop query is a real state
read, but its private combiner selector is not an Xbox guest handle. This
adapter reuses the existing shader storage and requires no native GPU query.
Original query execution passes; the two native graphics backend/ABI tests
pass in 5.46 s. Actual `build/blinx/run-pixel-query-attract.log` advances past
the query and stops at `D3DDevice_SetRenderState_LogicOp` (0x1337c0) during
scene startup. No complete gameplay/attract frame or cadence is claimed.

`beads-xbox.1.78`: Xbox state 66 accepts flat (0x1d00) and smooth (0x1d01).
Phobos's state adapter forwards shade mode but its renderer does not implement
flat colors. The native adapter keeps the borrowed vertex/pixel compilers and
caches and adds a cached GPU geometry stage only for flat draws. It broadcasts
diffuse/specular from the first triangle/strip vertex; quads use vertex four.
Positions, perspective W, texture coordinates and fog remain interpolated.
There is no per-draw CPU vertex readback, expansion or duplicate execution.

`tools/native_flat_reference.py` executes the original NTSC XDK initialization
basic block without hooks and observes NV097 method 0x09fc = FIRST. It does not
claim a full CreateDevice run. Machine NV2A software tests exercise actual
pushbuffer state, first/last triangle/strip selection, clipping and output RAM.
This bounded floor supplies the interpolation contract; native GPU fixtures
check fixed-function and programmable VS linkage, diffuse/specular, flat to
smooth changes, clipping, quads and varying texture coordinates. The native
backend covers its qualified triangle/strip/quad topologies; new line/fan or
two-sided lighting needs separate qualification. The machine's generic GPU
shortcuts remain smooth-only so they cannot silently override the flat floor.
Shader objects are cached per device and released on teardown.

Actual `build/blinx/run-flat-attract.log` passes the former shade-mode stop,
submits additional GPU work during scene startup and stops at the unimplemented
`D3DDevice_GetPixelShader` query (0x136250). Diagnostic duration 121.05 s;
captures do not yet establish a gameplay frame or original-speed attract loop.
All 32 native tests pass in 13.36 s; the final added combiner pixel cases pass
the backend test in 0.91 s, and software NV2A passes in 0.22 s.

## Shared system-time query

`beads-xbox.1.77`: `KeQuerySystemTime` writes one coherent 64-bit system time
through its guest pointer (stdcall, one argument). The shared native dispatcher
uses Windows precise FILETIME: 100ns units since 1601 UTC, also used for absolute
timer deadlines. Relative waits retain their separate monotonic clock. The
host clock may be adjusted; the query does not synthesize elapsed time per call
or change the host clock. This is a startup-selected native environment clock,
not a reconstruction of the console's persisted RTC setting.

`tools/native_clock_reference.py` executes the original kernel query unchanged.
It verifies exact output extent, nonvolatile registers, return stack and retry
when the clock's high/low words change during a read. Clock-memory values and
the simulated interrupt are fixture inputs; full RTC initialization and direct
kernel clock-data imports are outside this fixture. Native tests check the
shared epoch, real time progression, unaligned output and neighboring bytes.
Phobos's clock notes informed the host-resolution choice; its implementation
body was not copied. The related `RtlTimeToTimeFields`/`RtlTimeFieldsToTime`
services use native NT calendar conversion through local 8-short structures.
Original and native fixtures agree on the 1601/1970 epochs, leap years 2000 and
2400, non-leap century 2100, weekdays, sub-millisecond truncation and unchanged
output on invalid dates. No zero-filled conversion success is substituted.
Actual `build/blinx/run-calendar-attract.log` passes both prior clock guards,
opens `media/adx/round1.adx`, continues reads/callbacks, then stops at flat
shading (Xbox state 66 = 0x1d00). The 119.06-second diagnostic run is not a
cadence result. All 32 native tests pass in 14.30 s. A gameplay frame, complete
attract loop, input/menu return and intended speed remain unvalidated.

## Presentation reset: retained resources and immediate presentation

`beads-xbox.1.76`: original NTSC Reset replaces presentation buffers, restores
default targets/full viewport and clears depth/stencil while retaining the
device and other resources. The native adapter reuses Phobos's real RHI target
creation; its stub Reset is still guarded. The actual Blinx request keeps
640x480 ARGB8/D24S8 without MSAA and changes interval-one to immediate.
Immediate swaps retain real GPU completion/backpressure without a vblank wait;
the independent 60 Hz callback clock continues. No duplicate execution occurs.

`tools/native_display_reference.py` executes the original supplied-surface
setup helper without hooks, checking interval publication, header ownership,
pixel scale and its setup packet. A separate original initializer check proves
that Reset overwrites prior embedded-surface reference counts. The adapter
retires these superseded native presentation references; independently owned
textures/vertices and state blocks survive. The common header addresses stay
stable. This is not a full unhooked Reset or complete hardware mode-set test;
the existing NV2A RAM-to-scanout fixture supplies the bounded display floor.

Native integration exercises four resets/resizes, held back-buffer references,
retained texture/vertex bytes and bindings, state-block ownership, changed
presentation interval, real front-buffer pixels and stable guest allocations.
MSAA, alternate swap modes and caller-supplied presentation surfaces remain
explicitly unsupported in this native path. Original mode-setting remains in
the separate reference build. Title validation results are recorded below as
they are reached; fixture success alone is not attract completion.

Actual `build/blinx/run-reset-attract-v2.log` passes Reset with a held back-buffer
reference, loads further scene textures and sound banks (505 textures reached),
and stops at `KeQuerySystemTime` during loading. The 122.05-second captured run
is not a speed measurement. Final native tests pass 31/31 in 13.90 s; existing
NV2A tests pass in 0.17 s. No complete attract/gameplay or menu-return result yet.

## Earlier checkpoint - actual input and title audio (2026-10-07)

`beads-xbox.1.73`: two real NTSC runs deliver Start through the host controller
boundary at 15 seconds. Original title logic interrupts startup, enters the
animated title/music, then idles into additional game-asset loading. Both reach
`D3DDevice_PersistDisplay`; the following implementation passes that boundary
and reaches `D3DDevice_Reset` (.76).
This is not a completed attract sequence or validated menu return.

The diagnostic GP-output tap captured 1,439,520 real frames at 48 kHz (29.99 s).
All samples are finite; stereo peaks are 0.561/0.546. Independent FFmpeg decoding
of the original Microsoft SFD matches four one-second source windows with
0.99682-0.99977 correlation at exactly two-second spacings. Title ADX windows
also align at two-second spacings, with 0.97477-0.99749 correlation and different
gain/effects. This establishes actual content and sample-rate progress for the
checked segments. Full audio quality, A/V synchronization, all attract content
and title cadence remain open. Private evidence: `build/blinx/movie-audio-01.f32`,
`movie-audio-match-01.aot-private.json`, `run-audio-interrupt.log` and
`run-input-interrupt.log`. All eight focused input/audio tests pass.

`XLLE_PILOT_INPUT=replay` with `XLLE_PILOT_INPUT_REPLAY=<file>` selects a virtual
port-zero controller at startup. Each line is an increasing millisecond offset
from the first host poll and 36 hex digits containing the 18-byte Xbox pad
state. The first offset must be zero. State persists until the next row, so
include explicit button-release rows. Other ports report disconnected; no
rumble is advertised. This uses the real XAPI adapter and original title logic.

`XLLE_PILOT_AUDIO_CAPTURE=<new .f32 file>` records up to 30 seconds of real
six-channel GP output as interleaved little-endian float32 at 48 kHz. The log
reports the actual frame count. It refuses an existing file. Device close
finishes a shorter capture before teardown. No file I/O runs on the audio
callback; the optional tap pauses the engine at attachment/detachment and must
not be used for performance measurements.

## Persistent display: bounded floor and native lifetime

`beads-xbox.1.75`: the actual title saves its displayed 640x480 frame in a
1,228,864-byte contiguous surface (including its 64-byte header area), then
calls Reset. `tools/native_display_reference.py` executes original NTSC layout
and kernel AV/persistence routines without hooks. It checks four surface layouts,
saved-pointer round trips, page-spanning persistence changes, and return ABI.
The NV2A test independently scans retained BGRA32 RAM after the drawable buffer
changes, and confirms a subsequent PCRTC address change selects new pixels.

The native implementation waits for submitted presentation, reads actual front
pixels, creates the original surface header/data shape, and marks every owned
page persistent. The frozen image can remain visible through graphics teardown;
successful new-device setup consumes the old allocation. Native fixtures check
front/back distinction, exact saved pixels, three replacements without growing
memory, preserved bytes after release, and recreation cleanup. Final native
suite: 31/31 passing in 14.34 s; LLE NV2A: passing in 0.30 s. The earlier
intermittent process-handle assertion remains tracked independently in .72.

Phobos's memory-persistence and host Reset methods are stubs, so neither is
adopted. nxdk's display cleanup and Cxbx's incomplete driver discussion are
references only. This contract is bounded to the qualified displayed formats
and device lifecycle; firmware warm launch remains an explicit unsupported
boundary. The original full PersistDisplay driver is not claimed executed by
the helper fixtures. Actual runs reach Reset after 136.09/117.06 seconds; those
instrumented elapsed times are not performance comparisons. Private evidence:
`build/blinx/run-persist-attract.log` and `run-persist-repeat.log`.

## Earlier checkpoint - original movies reach the title screen (2026-10-07)

Direct GPU captures prove the Microsoft and Artoon logos, the opening movie,
and the animated **Press START to begin** screen. A 180-second run remains
active at that screen, repeatedly reading `h_title02a.sfd`, with no guard or
crash. Input interruption, attract entry, actual audio quality/synchronization,
long-run ownership and original cadence are still unqualified. JSRF remains
deferred. Private captures/log: `build/blinx/movie-onecpu/` and
`build/blinx/run-movie-onecpu.log`.

This run selects `XLLE_PILOT_TRACE=0` to suppress per-call kernel/D3D/audio
diagnostics and `XLLE_PILOT_GUEST_CPU=15` (a zero-based processor in the current
group). The latter follows Phobos's guest-only one-core experiment; native
renderer/audio workers remain unrestricted. It is optional, disabled by
default, and does not claim exact Xbox scheduling. Original kernel priority
query execution confirms ordinary and saturated +/-16 results. Native worker
affinity and priority/suspension/reference tests pass. The timing-sensitive
priority-poll stall stays open as `beads-xbox.1.71`; one successful trial does
not establish causality or repeatability.

`XLLE_PILOT_PRESENT_CAPTURE=<existing private directory>` captures the actual
backbuffer immediately before presentation, up to 180 frames one second apart.
This optional GPU readback affects timing and is not a performance run.
MSVC builds retain runtime debug information and generated public symbols so
thread/ownership state can be inspected without relifting or a full debug build.
The latest focused graphics test again hit the intermittent process-wide
vblank handle-count assertion (`beads-xbox.1.72`); it is not counted as passing.

## Earlier checkpoint - real asynchronous movie IO (2026-10-07)

`beads-xbox.1.69`: Blinx opens `microsoft.sfd` with its actual asynchronous,
nonbuffered flags and completes 29 real reads. It submits 760 presents and
feeds its movie audio buffer before stalling in repeated thread-priority
queries (`beads-xbox.1.71`). This does not establish correct playback, movie
completion or an attract loop. Desktop captures were obscured by other windows
and are not accepted as visual evidence; direct GPU capture is next.

Phobos's host-file/event approach is retained. Its read implementation does
not handle pending IO, so the independent adapter supplies persistent native
IOSBs, real pending reads and thread-pool completion waits. Guest data and
8-byte IOSB completion are published before signaling the retained native
event. File close drains pending work before releasing/reusing ownership;
guest event closure cannot destroy an outstanding operation's reference.
Actual host access rights are retained, including rejection of reads through
DELETE-only handles. Native metadata access grants no extra data access.

Bounded reference floor: original kernel KeInitializeEvent/KeSetEvent/
KeResetEvent execute in the private x86 oracle for both event types and
previous-state transitions. The in-tree optical fixture checks actual PIO/
scatter-DMA bytes before IRQ completion. This is not full retail IRP-stack
execution. Native event type/state, NT deadlines, handle references, offsets,
EOF, byte counts and completed-buffer ordering have executable tests. Host
scheduling latency and completion interleaving are permitted tolerances.
Async writes, guest APCs, file-handle waits, implicit async offsets and named
events remain explicit guards. No pending request is treated as completed.

All 31 native CTests pass (13.31 s), including 64 read/close/reopen cycles,
nonbuffered short reads and retained completion lifetime. Private evidence:
`build/blinx/event_oracle.py`, `run-movie-async-read.log`,
`run-movie-visible.log`, and `build/movie-events-final-tests.log`.

## Movie YUV state contract (2026-10-07)

`beads-xbox.1.70`: Blinx now passes its YUV enable state and reaches the movie
worker's real file reads. This reuses Phobos's already imported YUY2/UYVY upload
conversion. The native adapter preserves the original render-state cache and
requires enabled conversion when drawing packed movie textures. Disabled packed
sampling remains an explicit scope guard. No alternate decoder is introduced.

The original NTSC XDK routine and helper execute in a private x86 oracle: zero
clears NV097_SET_CONTROL0 bit 28, nonzero sets it, the guest cache retains the
exact argument, and the packet advances eight bytes. Revision binding derives
the cache index from the checked routine; this build uses 160, unlike later
XDK layouts. Runtime code contains no title address for this state.

The bounded software NV2A reference reads actual packed DMA bytes, converts
BT.601 studio-range color, and samples through the generic PROJECT2D raster
path. YUY2/UYVY, padded rows, black/white/color pairs and unchanged source bytes
pass. Native GPU upload/draw and enable/cache tests pass; one channel unit of
rounding difference is tolerated. Exact NV2A chroma interpolation/filter order
and disabled-conversion sampling remain outside the qualified floor.

Private evidence: `build/blinx/yuv-enable-oracle.aot-private.json`,
`run-movie-yuv-enable.log`, and `run-movie-async-read.log`. The last run submits
movie frames and feeds real audio buffers, but visual/audio playback and the
full attract sequence still need integration validation.

## Renderer lifetime correction (2026-10-07)

`beads-xbox.1.66`: D3D11 validation identified the repeated-device failure as a
Phobos empty-stage shader view cached across device destruction. Binding it to
the replacement device produced CORRUPTED_PARAMETER3 and left later occlusion
queries pending. The prepared donor now releases/resets this view at teardown.
The existing resource fixture also verifies every bound view's device identity.
No query results, flushing policy or readiness deadlines are changed.

The full three-device renderer test passes with D3D11 validation (1.07 s) and
production flags (1.31 s). Ten native-generation Python tests pass. The full
native suite passed 29/30: the graphics ABI test encountered its separate known
intermittent process-wide vblank handle-count assertion. No all-green claim.
`XLLE_PILOT_D3D_DEBUG=1` enables driver-layer validation for further diagnosis;
private evidence is in `build/query-debug-events.log` and
`build/query-lifetime-all-tests.log`.

## Earlier checkpoint - movie discovery (2026-10-07)

`beads-xbox.1.68`: Blinx now finds `microsoft.sfd` through real directory
enumeration, closes the search and attempts to open the movie. The next guard
is its asynchronous, nonbuffered file mode (options `0x848`), tracked in
`beads-xbox.1.69`. No movie frame/audio playback is claimed.

Phobos supplies the host-enumeration/entry-translation approach. The independent
adapter uses our already-open NT handle, preserving host cursor/filter lifetime
and converting the fixed 64-byte class-1 header plus ASCII name into Xbox layout.
It handles restart, exhaustion, independent searches, invalid handles/classes,
buffer limits and actual errors. Dot entries are omitted as on Xbox; input
masks remain untouched. Native media contract tests pass. LLE optical tests
pass a real XDVDFS descriptor, DMA directory record and PIO linked payload.

Bounded reference qualification: nxdk defines the Xbox ABI; the original kernel
IRP boundary was inspected, and the optical transport is runnable. **The retail
filesystem's enumeration state machine has not been executed end to end in this
fixture.** Installed media permits host timestamps, allocation size and ordering.
Native cursor/filter and short-buffer semantics follow host NT; exact Xbox
filesystem edge cases remain open. Non-ASCII names, reparse entries and event/APC
delivery remain explicit guards. No donor implementation bodies are copied.

Private title evidence: `build/blinx/run-directory-enumeration.log`, 23435496
function entries, actual `microsoft.sfd` entry (77 result bytes). Current native
GPU query recreation failure remains tracked separately in `beads-xbox.1.66`.

## Earlier checkpoint - contiguous memory protection (2026-10-07)


`beads-xbox.1.67`: actual Blinx now passes MmSetAddressProtect on its contiguous
buffer and reaches NtQueryDirectoryFile while discovering `d:\media\movie\`.
This is movie discovery, not playback. The adapter uses real VirtualProtect
as Phobos does, retaining Xbox cache flags per page and checking host failures.
It supplies Set/QueryAddressProtect for owned contiguous pages. The original
kernel accepts read-only/read-write plus one cache modifier; invalid protection
leaves pages unchanged. Other memory domains and zero-length requests remain
explicitly unqualified. Host coherent caching is the documented tolerance;
DMA/cache timing is not simulated in this native path.

The runnable original kernel exports 179/182 execute without function hooks in
a private Unicorn fixture: an unaligned two-page update, one-page restoration,
invalid protection, and write-combined restoration. PTE physical identity and
untouched pages survive; query outputs match native fixtures. The in-tree LLE
`aot_machine_optimized` regression passes (0.97 s), including genuine page-fault
and stale-write-permission behavior. Native memory, memory statistics and
complete graphics ABI tests pass (4.86 s total). The unrelated recreated-device
visibility timeout remains open under `beads-xbox.1.66`.

Private evidence: `build/blinx/protection_oracle.py`,
`memory-protection-oracle.aot-private.json`, `run-protect-arguments.log` and
`run-memory-protection.log`. The actual request is 4,968,000 bytes at an owned
physical alias, PAGE_READWRITE. Next family: real directory enumeration
(`beads-xbox.1.68`). No audio/movie/attract or cadence result is claimed yet.

## Earlier checkpoint - shared GPU texture aliases (2026-10-07)


`beads-xbox.1.65`: Blinx passes the formerly unowned front-buffer texture header,
executes 151 GPU callbacks and reaches MmSetAddressProtect. The adapter reuses
Phobos RHI image/view ownership, retaining the actual storage rather than
copying pixels at each binding. Storage identity follows Data/Format/Size,
not a title address. Caller-owned headers are never freed by the renderer.

The LLE fixture clears RAM-backed color storage, samples that same address,
changes the clear and verifies changed sampled pixels. Native fixtures verify
front/render-target sharing, GPU updates without rebinding, header retargeting,
BGRA readback, binding references and shared-image render/sample hazards.
Original XGSetTextureHeader executions cover 64x64 and 640x480 headers (body
SHA-256 `382e01538d49f6b152dcea4ad557e2bd8e1700b3607cafbfcb0f6309a24c4ac8`).
Scope is single-mip, single-sample linear A8R8G8B8 color storage; CPU locks and
surface views of caller-owned aliases explicitly remain unqualified. Existing
source ownership must remain valid. Arbitrary raw storage/depth aliases are
not inferred from a compatible-looking header.

The LLE NV2A fixture passes (0.31 s); native graphics ABI passes three repeated
runs. The independent native backend fixture has a repeated query timeout in
its second device lifetime, tracked as `beads-xbox.1.66`. Changing GetData flags
or reverting the front-buffer SRV capability did not resolve it. Neither
experiment remains in the implementation. Actual title evidence is private in
`build/blinx/run-surface-alias.log` and
`build/blinx/surface-alias-header-oracle.aot-private.json`.

## Earlier checkpoint - ordered GPU callbacks (2026-10-07)


`beads-xbox.1.64`: original NTSC Blinx now executes its WRITE callback through
real GPU completion and advances to an unowned texture-header guard. The title
copies `GetBackBuffer2(-1)` storage into its own texture header; shared surface
aliases are the next resource family (`beads-xbox.1.65`). The observed callback
is generated original title code, with its context intact. No title audio,
movies, menu or attract completion has been reached.

The bounded LLE floor implements WAIT_FOR_IDLE before WRITE callbacks and tests
the PARAMETER_A/B, trap 6/7, IRQ acknowledgement and following-command ordering.
Two complete original-x86 InsertCallback fast paths establish the packet/ABI
shape; refill allocation is not part of that oracle. Native uses a dynamic FIFO,
real D3D11 completion for WRITE and the existing serialized DPC worker. READ
follows host consumption of prior work. Delivery needs neither Swap nor another
graphics call; later caller graphics operations wait for pending callbacks.
The bounded adapter allows callback graphics queries without holding its host
lock. Poll latency is up to the 1 ms service interval plus host scheduling;
exact interrupt latency and arbitrary DPC rendering are not qualified.

All 30 CTests pass (13.04 s) and the NV2A regression passes. Native tests cover
70 queued mixed READ/WRITE callbacks, actual prior red GPU pixels/completion,
FIFO contexts, asynchronous delivery while the caller runs, cdecl/TLS and
reentrant queries; a final queued callback drains before device teardown.
Private evidence: `build/blinx/gpu-callback-original-oracle.aot-private.json`,
`run-gpu-callback.log`, `build/gpu-callback-all-tests.log`. Phobos's immediate
callback and Cxbx's missing-query bypass are not adopted. No donor bodies were
copied; see attribution. This remains a private donor-linked reference pilot.

## Earlier checkpoint - bounded visibility contract (2026-10-07)

`beads-xbox.1.63`: a small software NV2A reference now informs real native GPU
queries. Fresh/repeated NTSC Blinx launches pass visibility Begin and End and
stop at `D3DDevice_InsertCallback` (`0x138a40`), in 12.95/7.33 s. Both load 420
textures without entering the disc-error UI or hitting allocation failures.
Each issues one Begin/End; neither reaches GetVisibilityTestResult before the
callback guard. No new menu, movie, audio-playback or attract result is claimed.

All 30 native CTests pass (15.08 s), including three real GPU device lifetimes,
the new sample-count fixtures and original guest ABI shape. The complete NV2A
test passes with the new command-level software fixtures (0.35 s); ten Python
generation regressions pass. The existing process-wide vblank handle-count
assertion failed once before the visibility tests, then passed in isolated and
full reruns; this unrelated intermittent assertion remains open.

Private evidence: `build/blinx/visibility-original-oracle.aot-private.json`
(eight original-x86 cases), `visibility-validation.aot-private.json`
(fresh/repeat launch), and `build/visibility-*-tests.log`. The binding generator
checks the reviewed Begin/End/Get body hashes and stack sizes. No guest address
or private XDK report-cache layout is embedded in the native query service.
The next integration family is ordered GPU callbacks, using existing real GPU
completion and guest callback machinery where the original contract permits.

### Bounded visibility contract

The floor implements `SET_CONTEXT_DMA_REPORT`, `CLEAR_REPORT_VALUE`,
`SET_ZPASS_PIXEL_COUNT_ENABLE` and `GET_REPORT` through real PFIFO dispatch,
DMA resolution and the existing software rasterizer. It counts samples that
survive coverage, depth, shader kill and alpha tests. Clearing resets the count;
enable/disable and reading a report do not. Color/depth write masks do not
suppress measurement. Reports publish timestamp/count before the completion word.
Unsupported reference draws stop explicitly rather than producing guessed counts.

The original NTSC XDK routines establish the service ABI: Begin clears/enables
one counter; End disables and queues an indexed report; Get returns
`D3DERR_TESTINCOMPLETE` (`0x88760828`) without changing outputs while pending,
then returns a 32-bit count and optional 64-bit timestamp. Reads are repeatable;
index reuse replaces the report. Begin/Get and the preallocated report-slot
helper were executed as original x86. End's packets/kick were reviewed, not
executed end to end in the oracle. Begin's EAX is not a specified HRESULT;
the adapter leaves S_OK there as an unused volatile value.

The native implementation uses actual D3D11 occlusion and timestamp queries.
It submits once at End and polls without flushing or waiting in Get. Query
objects retain ownership across index reuse and release on device teardown.
The LLE software path is an offline reference; it never runs alongside the
production native renderer. Build selection chooses the original or native
graphics family. The separate machine HLE renderer has no query implementation
yet and stops explicitly when counting is enabled.

| Qualified behavior | Reference and native evidence |
| --- | --- |
| Samples | 20x20 rectangle = 400; completely hidden = 0 additional; half hidden or clipped = 200; two overlapping draws with writes disabled = 800; alpha NEVER = 0 |
| Counter lifecycle | LLE cumulative reports/enable/reset; native Begin reset, repeated End, independent retained indices, repeated reads and 70 overwrites |
| ABI and completion | Original pending/output/stack/register behavior; real GPU readiness, untouched pending outputs, optional timestamp, payload-before-completion DMA writes |
| Ownership | Three native device lifetimes including an active query at close; invalid LLE DMA range preserves pending status |

This qualifies single-sample triangle fixtures, not exhaustive rasterization.
MSAA, stencil interactions, exact edge precision and counter wrap are unproven.
Timestamp units are nanoseconds: the LLE reference uses the existing display
clock at 1/1000 interval resolution (about 16.7 microseconds at 60 Hz); native
uses the real GPU clock and rejects disjoint/error results. Clock epochs and
cross-clock phase are not matched. Qualification currently covers repeatable,
ordered timestamps, not code that compares GPU time with CPU/PTIMER time.
These explicit bounds inform HLE without requiring hardware perfection.

Wire/layout references: [nxdk hardware Z-pass tests](https://github.com/abaire/nxdk_pgraph_tests/blob/main/src/tests/zpass_pixel_count_tests.cpp),
xemu's pinned NV097 definitions and the original NTSC routine behavior.
Host query semantics follow [Microsoft D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query)
and [GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).
The Phobos 64-poll invented-count fallback is deliberately excluded. Source
provenance is recorded in [the attribution inventory](../../THIRD_PARTY_ATTRIBUTION.md).

## Earlier checkpoint - audio, movies and disc-error fix (2026-10-07)

The private PHOBOS/D3D11 build presents real Blinx frames and now passes the
previous disc-error screen, loading 420 textures before the explicit
`D3DDevice_BeginVisibilityTest` guard. This is the next graphics service family;
it must return actual GPU visibility results. Fresh and repeated launches both reach this boundary (13.87/7.55 s),
with zero error-dialog entries and no allocation failures. The title has not yet reached
buffer Play, movie playback, an interactive menu or an attract loop. JSRF remains
serially deferred. Earlier checkpoints below describe historical stops.

**Audio is ready for title integration within the identified Blinx interface.**
All 21 bound public/object entries are exercised, including real playback,
ring-buffer updates, cursor/status/looping, pitch/frequency/volume/headroom,
reference lifetime, effects replacement and now ordered mix-bin routing and
per-bin volume updates. XAudio2 feeds the real MIT DSP56300 graph; captures
measure actual 32-bin mixer input and six-channel graph output. Four graph
replacements with Blinx's real `dsp.bin` produce nonzero output and release their
resources. Six complete original-x86 setter cases verify routing/volume/ABI;
invalid requests preserve prior settings. This is service evidence, not proof
that Blinx's audio plays correctly end to end. No native DirectSoundStream
interface is identified in this revision; broader stream/codec support is not
claimed. The legacy DirectSound backend explicitly rejects custom routing.

**Movies are ready for integration, with the original compiled Sofdec decoder.**
The two NTSC PSGSFD section entries are derived from this XBE and included in the
selected generation. Seed/image hashes and reused discovery coverage are checked.
YUY2/UYVY textures use the borrowed Phobos converter, correct guest pitch and
linear texel coordinates. GPU fixtures exercise both formats, padded rows,
changing frames and the empty texture-stage case documented by Phobos. Original
XDK allocation-prefix probes corroborate six size/header layouts; these are not
full device executions. The 31 installed SFD files are inventoried; three
representative files decode completely with external FFmpeg (with timestamp
warnings). FFmpeg is a validation reference, not the production movie path.
Actual Sofdec output, audio/video synchronization, interruption and looping remain
unvalidated until startup reaches them.

**The disc message was an allocation failure.** Original NTSC code called its
error UI after texture creation returned E_OUTOFMEMORY. The VM ledger exhausted
512 records with 53,927,936 resident bytes and 262,144 cache bytes, leaving about
12.3 MiB of the 64 MiB budget. After removing that artificial limit, the native
texture table exhausted 256 slots. Both ownership tables now grow independently
of guest RAM; actual commitment still obeys the 64 MiB budget. No game address,
file result or error-dialog condition is patched. Tests exercise 600 simultaneous
small reservations and 520 actual GPU textures, preserved contents/readback and
two complete release/reuse cycles. Phobos's PAL memory/asset notes guided the
investigation; the specific table limits were local adapter defects.

Validation: all 30 CTests pass (10.56 s) with the title stopped. A concurrent run
exposed an intermittent process-wide handle-count assertion in the existing
vblank test; the isolated full run passes. This does not establish game cadence.
Private evidence remains under `build/blinx/`: `audio-readiness.aot-private.json`,
`audio-routing-oracle.aot-private.json`, `movie-texture-oracle.aot-private.json`,
`movie-assets.aot-private.json`, `movie-reference-decode.aot-private.json` and
`capacity-validation.aot-private.json`. Do not commit title assets or generated
code. Attribution and retained donor notices are in
[the inventory](../../THIRD_PARTY_ATTRIBUTION.md).

`XLLE_PILOT_TRACE_FUNCTION=0x...` enables up to eight entry/register/stack/recent
function diagnostics for one selected address. It is disabled by default and
changes no title behavior. `XLLE_PILOT_VISIBLE=1` shows the native window;
`XLLE_PILOT_INPUT=keyboard` selects real foreground keyboard input at startup.

## Earlier observed result

The original NTSC Blinx XDK 4831 and JSRF XDK 4134 both execute their own XBE
entries, create real host-backed title threads, and run early XAPI startup.
Blinx now passes timer arming, its initial CRT heap reservation/commit, and
installed-media verification against the actual NAS content. It passes the HDD
allocation-unit check, creates per-title TDATA/UDATA and persists 72 bytes of
`TitleMeta.xbx`. A fresh launch now loads the deferred title-image section,
writes `TitleImage.xbx` byte-for-byte equal to its original 10240-byte payload,
and unloads the section. A repeated launch preserves the file and skips that
copy through the title's own code. Native utility-drive setup now mounts a
persistent `Z:` cache on partition 3. Cold launch has a real null LaunchDataPage
pointer cell; Blinx configures real file caching (zero then 64 pages). The native
renderer now creates a real 640x480 D3D11 device and supplies an owned front
surface to `GetBackBuffer2(-1)`. Blinx now passes its initial graphics-state
setup and reads the full 131116-byte Arial font TGA from installed content.
The decoder now performs real native/guest nonlocal recovery after the JPEG
probe rejects the TGA, creates the real 128x256 A4R4G4B4 font texture and uploads
all nine mip levels. Native GPU readback shows the font atlas; the top level
matches the TGA within its four-bit quantization (maximum 16/255 channel error).
Blinx reads/parses the 2332-byte font metrics, creates two 112 KiB vertex
buffers and registers six caller-owned buffers against real contiguous CPU
storage. It then reads `sys_black.dds` and `sys_white.dds`, compiles all 91
requested vertex programs and creates 30 pixel-shader definition objects.
It now passes its `MEM_NOZERO` heap commitment, loads `pika.dds`, three flare
textures and `font_cgothic_bold.dds`, and uploads their mip chains. Both DXT5
textures reach all nine levels, including 2x2 and 1x1 tails. Actual GPU readback
of `pika.dds` level zero matches the complete original compressed DDS payload.
The real memory-statistics query and front-surface descriptor now pass. A
truncated initializer has been recovered and re-lifted from the original bytes;
Blinx now passes native `XInitDevices` and device enumeration, opens the real
Windows audio device through `DirectSoundCreate`, and starts its three media
workers. Guest vertical-blank waits now complete at the configured nominal
60 Hz. Blinx creates three 65536-byte stereo PCM16 buffers at 44100 Hz and
locks/unlocks their first four bytes, then loads the complete 15188-byte
`media/dsp.bin`. It now installs the real DSP graph through XAudio2, creates
640x480 color/depth textures and a 320x240 color texture, obtains their surface
views and queries the current color/depth surfaces. It reaches 96 compiled
vertex shaders and 37 owned pixel definitions. It now captures its real ALL
state block and registers its vertical-blank callback, which executes and
returns repeatedly. After loading five menu textures, it now binds its compiled
vertex shader and the donor's fixed-function pixel shader. It binds a real
stage-0 texture, updates vertex constants and passes view/projection setup.
It now passes four bump-matrix updates, binds stream 0 (stride 28), and selects
its next texture and vertex shader. Seven real `D3DDevice_DrawVertices`
triangle strips now render the 640x480 "Warping space-time..." loading image
into the native back buffer. Both fresh/repeated captures match. Its first
resource query now passes for an owned, never-submitted vertex buffer. It locks
the buffer, binds its font texture/stream, selects FVF `0x144` and submits a real
quad. It restores the previous shader/state and now reaches its first `Swap`.
The callback-completion contract guard stops before presentation. No title target
binding yet. The font quad samples transparent atlas pixels; visible title text
has not been established by this draw.
No title buffer Play or audible game output is established by those runs.
`PILOT_AUDIO=ORIGINAL` still executes the original sound-object allocations and
contiguous-page setup, stopping at `KfRaiseIrql` (ordinal 160) in audio-driver
initialization. `HalBootSMCVideoMode` is a real data export with
the same composite-cable value as the LLE SMC. The `PILOT_INPUT=ORIGINAL`
selection still executes original USB initialization and stops at
`IoCreateDevice` (ordinal 65). Broader title drawing and audible output remain unqualified.
The last ORIGINAL-renderer measurement, before the contiguous-memory service,
stopped at `MmAllocateContiguousMemoryEx`; it has not been remeasured since.
The OPTICAL build retains
and executes the original verification helper, stopping on unsupported raw
optical access. No Blinx movie, interactive menu or level yet. Function-entry counts
are diagnostics, not coverage or performance measurements.

The owner's current goal is serial: complete Blinx before further JSRF work.
JSRF's last measured result remains 12 title-thread entries at
`NtAllocateVirtualMemory`; it has not been rebuilt/run against the new VM code.

The native implementations reached so far cover thread creation, thread-handle
closure, recursive critical sections, DPC/timer structure initialization,
EEPROM user-settings queries and NTSTATUS-to-DOS-error conversion. Timer arming,
expiry and DPC delivery are implemented as described below. Callback delivery
is exercised by the contract tests. No callbacks, draws or completions are discarded.

Twenty-nine contract tests pass: concurrent threads with separate registers/TLS and
recursive exclusion, handle closure/errors and EEPROM buffer/checksum behavior;
timer/DPC/event-wait contracts; real VM reservation/commit/free/protection/query;
installed-media identity and actual file I/O; writable storage and link lifetimes;
deferred section residency and shared-page ownership;
native utility-drive persistence, formatting, eviction and failure handling;
real read-through file-cache sizing, coherence, position and budget handling;
real donor GPU clear/triangle/present pixels, guest graphics ABI and ownership;
texture GPU pixels/mips, shared views, changed-data upload and parent lifetimes;
linear color/depth target layouts, actual GPU clears/readback, CPU edits,
partial depth/stencil updates, retained bindings and safe self-rebinding;
owned state snapshots, membership, independent bytes and retained resources;
actual cdecl vblank callbacks, replacement/removal, reentrancy and joined teardown;
DXT1/3/5 compressed blocks and mip tails, texture/surface descriptions,
pixel-definition ownership/copy/HRESULT/OOM, and real `MEM_NOZERO` commitment;
memory-statistics changes with actual commitment, reservation, image, stack,
cache and pool lifetimes, including shared-page small pool blocks;
native input packing, real Windows control mapping, connection transitions,
stale-handle rejection, polling cache, rumble error propagation and 100 open/close
cycles with a test-only host provider; exact reasons for unsupported input stops;
real Windows PCM buffer storage/playback/cursors/looping, wrapped locks,
properties, ownership and explicit unsupported-audio failures;
initial/nested/self thread suspension, repeated pause/resume, priority/boost
controls and retained object references surviving guest handle closure;
created/registered vertex resources, actual GPU buffer readback, external
header/backing ownership and bounded contiguous-memory allocation/release;
real shader compilation, typed/multiple-stream GPU input, paired issue and
GPU output compared against the existing LLE vertex interpreter;
native nonlocal recovery, frame lifetime and deliberate failure conditions;
plus explicit instruction, indirect-call and
kernel-call failure paths. Passing
the other title's XBE to the Blinx artifact is rejected before guest execution.
The main LLE build's runtime source and build selection are untouched.

### Pretransformed FVF and quad checkpoint (2026-10-07)

Blinx now submits eight draws: seven loading-art strips and its first font quad.
Fresh/repeated launches reach `D3DDevice_Swap` (caller `0xaa711`, flags zero),
where the existing callback-completion guard stops. Entries 22252480/22252388;
25/22 consecutive callbacks. Both images retain the previous loading hash.
Captured font vertices occupy x=95.5..97.5, y=415.5..440.5 and sample the atlas's
transparent top-left strip. The unchanged image is expected; this is not evidence
of visible text, presentation, interactive menu, audio, attract or cadence.

The MIT sp00nznet fixed-function VS, input-layout cache, transform uploads and
pixel/state pipeline are reused. All three pinned donors were consulted for FVF
and quad handling. Adaptations preserve the original Xbox FVF descriptor/query
and compiled-shader transitions, fix coordinate width encoding (2,3,4,1), honor
texture-coordinate selection, and convert screen coordinates using the active
viewport, original screen offset and normalized depth. The revision's shared
FVF descriptor is discovered by the binding tool; no title address is embedded
in the runtime. Qualified formats are XYZRHW with optional diffuse/specular and
up to four coordinates. Normals/blend weights and pretransformed fog are guarded.

Xbox quad lists use the donor's `(0,1,2),(0,2,3)` topology, also matching the
machine LLE triangulation. A growing reusable immutable index buffer avoids
per-draw vertex expansion. It preserves first/count, restores the previous
native index binding and is released on device close. Edge-AA disable now updates
the original cache and selects the existing non-antialiased native raster mode;
enable remains guarded rather than silently approximated.

All 29 CTests pass (11.07 s). Real pixels cover perspective RHW, zero-RHW 2D
quads, nondefault viewport/depth, coordinate remapping, 1/3/4-component layout
offsets, quad batches/nonzero first, actual disabled AA and return to the compiled
shader with unchanged constants. Three device lifetimes pass. Ten full original
SetVertexShader executions match all 276 descriptor bytes and query/constant/
program-cache effects; the actual title descriptor also matches on both launches.
The complete original edge-AA disable routine matches its cache/two-mode update.

The original 12-slot pretransformed GPU program is captured only in private
output and executed by the permissive NV2A decoder. Screen coordinates include
the original 0.53125 offset, depth scales by 16777215, finite W is reciprocal RHW
(2e-5 float tolerance). Actual title font vertices have RHW zero: original RCP
produces infinite W. Native clip coordinates use a finite limiting magnitude
of 1.8446744e19, preserving equal-zero 2D positions/interpolation in GPU fixtures.
This is a documented HLE representation tolerance, not bit-exact hardware proof;
mixed/extreme RHW, clipping edges, transformed texture coordinates and broader
pretransformed fog remain unqualified. Production executes one vertex pipeline.

Private evidence: `build/blinx/fvf-validation.aot-private.json`,
`run-fvf-{fresh,repeat}.log`, `fvf-{fresh,repeat}` captures, `fvf-fixtures`,
`audit_fvf.py`, `fvf-original-program.bin`, `build/fvf-{contract-tests,gpu,original-fixtures,original-program}.log`.
Source-owned `pilot_shader_test --fvf-reference` reads the private original GPU
program. EXE SHA-256:
`1365f7834fc5728ae5c3afaac8553c6c6d30ac52ac0fad93829b0c38c2a71424`.
The prior 28 actual programmable vertices still match the LLE decoder, and
state-block/transform comparisons still pass. No GPL implementation added;
the existing private CPU artifact remains outside shipping. Next: real Swap
presentation and callback counters/flags, then validate the next title frame.

### Resource completion checkpoint (2026-10-07)

The first `D3DResource_IsBusy` call now passes for the owned 114688-byte vertex
buffer which has never been submitted. Blinx locks it, binds its font texture
and stream 0 (stride 28), then reaches `SetVertexShader(0x144)`: XYZRHW, diffuse,
one texture coordinate. That FVF transition remains guarded pending real native
pipeline qualification. Fresh/repeat entries are 22252476/22252384, with 25/21
consecutive completed callbacks. The seven loading draws remain pixel-identical.
No Swap, Play, movie, menu, attract or cadence result follows from this checkpoint.

The three pinned recomp renderers provide no usable completion-query service.
The independent implementation uses D3D11 EVENT queries for real submitted
resource work. Uploads, clears, draws and present copies record private serials;
they create no per-command query or wait. The first query needing a checkpoint
issues one event and flush, and subsequent polls use DONOTFLUSH. Queries retire
only on actual completion. A checkpoint covers all preceding submissions, so
unrelated later work can delay an idle result; reporting completion early is
never allowed. Bound streams/textures remain busy independently of GPU progress.
Surface aliases use their parent's work and clear only its canonical fence word.
Built-in swap surfaces retain their existing internal reference and remain busy.
Draw bookkeeping conservatively includes all bound inputs and outputs.

All 29 CTests pass (11.04 s). Native fixtures verify real uploads/clear/present,
changed data, idle slot reuse, shared checkpoints, no repeated idle queries and
cleanup across three device lifetimes. They do not require a fast GPU to expose
an intermediate busy result. Guest fixtures verify stdcall/nonvolatile registers,
bound resources, surface-parent identity, canonical fence clearing and lifetime.
Forty complete original-x86 executions independently check binding bits, parent
aliases, unsigned fence ordering including wrap, and cache side effects. The
native serial representation intentionally differs from original fence arithmetic.

Private evidence: `build/blinx/completion-validation.aot-private.json`,
`run-completion-{fresh,repeat}.log`, `completion-{fresh,repeat}` captures,
`audit_completion.py`, `completion-oracle.aot-private.json`, and
`build/completion-contract-tests.log`. EXE SHA-256:
`973d0477d5003f469b984e0c955161e8914f37d985b2f613016539ccc1e0c787`.
All 28 actual shader vertices per launch still match the permissive LLE decoder;
state-block and transform comparisons also pass. No GPL implementation added.

Device-wide busy/fence insertion/waits remain unqualified: their whole-device
ordering contract includes work outside this resource family. The next title
boundary is the pretransformed FVF renderer, followed by presentation/callbacks.

### Native draw checkpoint (2026-10-07)

Seven real title triangle strips render the 640x480 "Warping space-time..."
loading image into the native back buffer. Fresh/repeated launches stop next
at `D3DResource_IsBusy`, `0x00136d40`, resource `0x036a0000`, caller
`0x000f1643`, after 22252475/22252383 entries and 23/21 consecutive callbacks.
The final PPM pixel captures match (SHA-256
`f0666140650234d4092a603a21d5a814a6db07c9ae3a238e236666042b370ce0`).
These are actual rendered title pixels, not a diagnostic replacement scene.
No Swap/presentation, title Play/audio, movie, interactive menu, level, attract
loop or cadence is proven yet. JSRF remains deferred.

The adapter converts Xbox pixel XY and 24-bit integer depth into native clip
coordinates for the active viewport, retaining authored W for perspective
interpolation. It passes full four-component texture coordinates; the donor
pixel shader divides by Q after interpolation and normalizes linear textures
by their dimensions. Undeclared homogeneous components use 1 and unwritten fog
uses 1 at this raster boundary; the raw decoder diagnostic retains its prior
zero initialization. Colors clamp at the native interface. Native subpixel
precision and rounding are allowed, not forced to repeat original arithmetic.

Phobos's screen-space conversion was inspected as a behavioral reference.
The conversion here is independently implemented against the existing LLE
screen/clip handling and captured title outputs. The MIT donor supplies actual
texture sampling, state objects, pixel compilation/cache and constant upload;
its desktop DrawPrimitive body is excluded because its primitive numbering,
count convention and vertex preparation do not match the Xbox entry. The new
submission uses the already owned native shaders and buffers. Xbox types 5/6
are triangle list/strip; the previous checkpoint's type-6 "fan" description
was incorrect and has been corrected. Other topologies remain guarded.

Draws validate used stream extents/start/count, upload live changed vertex and
texture bytes, prepare CPU-exposed render targets, batch vertex constants,
apply actual native state and submit work. The compiled raster shader executes
once in ordinary production. Raw shader compilation is lazy and occurs only
when a diagnostic probe requests it; captures/readback are opt-in. No GPL
implementation was added; the pre-existing CPU pilot remains a private
GPL-dependent artifact outside shipping.

All 29 CTests pass (10.42 seconds). Native GPU fixtures cover nonzero first
vertex, streams 0/7, triangle-strip coverage, perspective W/Q, linear texture
coordinates, a nondefault viewport/depth range, real depth rejection, changed
buffer uploads and specular argument selection across three device lifetimes.
All 28 actual vertices from each title launch match the pinned permissive LLE
decoder: 1064 scalar comparisons per launch, observed maximum scaled error 0,
tolerance 2e-4. Actual transform/state-block captures still match their earlier
original-x86 oracles. The screenshot is evidence of initial rendering, not full
raster equivalence or a hardware image comparison.

Remaining boundaries include resource ownership/completion queries, Swap and
callback completion, other topologies, programmable pixel selection, broader
FVF/state application and zero/negative homogeneous edge cases. The admitted
profile still uses non-MSAA 24-bit depth. Next: implement IsBusy with real
binding ownership and native completion, then validate the next title step.

Private evidence: `build/blinx/draw-validation.aot-private.json`,
`run-draw-{fresh,repeat}.log`, `draw-{fresh,repeat}/draw-*.bin`,
`draw-{fresh,repeat}/draw-*.ppm`, `draw-{fresh,repeat}/loading-screen.png`,
`build/draw-gpu.log`, `build/draw-title-validation.log` and
`build/draw-all-contract-tests.log`. The source-owned `pilot_shader_test
--draw-capture <files...>` compares captured original inputs/constants with the
LLE decoder. Executable SHA-256:
`a5e37c7abcb42fcc2ea69d29ed259cc4e7c5dc2ee24d48a98cb66da1ec3cc012`.

### Bump-environment checkpoint (2026-10-07)

Six real bump states preserve the original three-argument callee-cleaned ABI
and guest texture-state cache. Xbox matrix order is 00,01,11,10; its operations
25/26 map to desktop operations 22/23. The donor's existing state storage,
texture resources, compiler/cache and pixel constant upload are reused. All
three pinned recomp variants were inspected; none supplies the fixed-function
bump effect. The independently authored extension performs dependent texture
coordinate sampling and luminance modulation on the GPU using the public
[Direct3D formulas](https://learn.microsoft.com/en-us/windows/win32/direct3d9/bump-mapping-formulas).
It preserves incoming color/alpha at the bump stage. No GPL implementation
was imported and no required sampling work is replaced by success.

All 29 CTests pass (10.80 seconds). Actual GPU pixels distinguish all four
matrix coefficients on stages 0, 1 and 2, including Xbox's swapped final two
entries. Luminance scale, offset, lower clamping and alpha preservation pass,
across three device lifetimes. Fixtures use admitted UNORM textures with
explicit coordinates; additional signed/luminance formats remain guarded.
The donor's pretransformed FVF path ignores TEXCOORDINDEX remapping; these
fixtures provide all four coordinate sets explicitly. Guest FVF transitions
are still unqualified and must resolve that contract before activation.

The native guest fixture checks all 24 stage/state combinations, restores
their values, and verifies adjacent words, stack cleanup and preserved
registers. A separate oracle executes the complete original 99-byte routine
48 times, with fixed-function and programmable-pixel selection: exact cache
updates and real packet/stage remapping match the bounded contract. No
original GPU-space allocator is bypassed in that test; adequate command space
is supplied. Programmable-pixel binding/effects remain unqualified. The native
fixed-function implementation associates the effect with its logical stage;
its internals do not reproduce the driver's physical stage remap.

Fresh/repeated Blinx passes all four matrix updates (stage 1, diagonal 4.0,
off-diagonal zero), binds stream 0, and reaches its first DrawVertices request
at `0x00136040`, caller `0x00033811`: type 6 (triangle strip), first 0, count 4.
Entries 22252454/22252362; 20/17 consecutive callbacks; 96 vertex shaders,
37 pixel definitions and five menu assets retained. The actual transform and
5460-byte state-block captures still match their original-x86 oracles. The
draw is not submitted yet. No title frame, Play, movie, menu, attract or cadence
is established. Next: qualify vertex-to-raster conversion and draw submission.

Private evidence: `build/blinx/bump-validation.aot-private.json`,
`run-bump-{fresh,repeat}.log`, `bump-{fresh,repeat}/`, `audit_bump.py`,
`bump-oracle.aot-private.json`, `build/bump-gpu.log` and
`build/bump-all-contract-tests.log`. Executable SHA-256:
`cdc1abd9b0f085811c5c5dfdeb999225c1c5537d1dfe2563c36a12bebc49ca67`.

### Transform checkpoint (2026-10-07)

The adapter maps all ten Xbox view/projection/texture/world matrices to the
MIT donor's real native matrix storage. The donor's existing matrix multiply
also supplies the queryable combined projection/viewport cache. Native float
math is independent of the original x87 implementation. The original matrix
bytes, projection depth/affine flag, viewport query and derived cache remain
observable; their offsets and default screen bias are image-qualified binding
metadata, not shared-runtime title addresses. Successful viewport/target changes
refresh the guest viewport and combined cache.

Twenty-four captured fixture calls match the original complete SetTransform
execution; derived float values allow scaled error 2e-5 (observed zero for
these fixtures). Matrix storage and query output bits match. The original
viewport query independently confirms a nondefault viewport, depth interval
and the 0.53125 screen bias. Actual GPU pixels verify world, view and projection
effects using the donor's fixed-function vertex path, across three device
lifetimes. All 29 CTests pass (10.35 seconds).

Fresh/repeated Blinx passes its two actual view/projection calls and subsequent
constant writes. Both stop at `D3DDevice_SetTextureState_BumpEnv`, address
`0x00133aa0`, stage 1, state 22 (BUMPENVMAT00), value 4.0, caller `0x0002c9a4`.
Entries are 22252371/22252279, callbacks 21/17. The two actual matrices from each
launch also match original-x86 execution, and the title's 5460-byte state-block
capture still matches. No title stream/target binding, draw/frame, Play, movie,
menu, attract or cadence is established. Next: bump state/effect support and
the vertex-to-raster interface before qualifying title draws.

The admitted initial profile has no MSAA, unit pixel scaling and 24-bit integer
depth. W-buffer activation and custom screen-space offset calls still stop.
The original pretransformed-program projection helper and programmable shader
output conversion to the native rasterizer remain unqualified; the diagnostic
fixed-function triangle does not validate that interface. No GPL implementation
was imported, and the pre-existing private generated CPU artifact remains
outside shipping.

Private evidence: `build/blinx/transform-validation.aot-private.json`,
`run-transform-{fresh,repeat}.log`, `transform-{fresh,repeat}/transform-*.bin`,
`transform-fixtures/transform-*.bin`, `audit_transforms.py` and associated oracle
JSON, plus `build/transform-{all-contract-tests,contract,gpu,oracle}.log`.
Executable SHA-256:
`b5b3fde6c0e9a4a92d514cf89c45dcd8ff07dd82e3ab3c67294ebd9a33141d49`.

## Reuse and scope

### Resource binding checkpoint (2026-10-07)

The shared adapter binds four texture stages and sixteen vertex streams to real
D3D11 views/buffers. It reuses the pinned MIT donor's resource allocation,
uploads, texture metadata and SRV binding. The donor's texture COLOROP mutation
is removed because the original Xbox binder preserves texture-stage state.
Its type getter now participates in the bounded 2D texture path. All three
recomp variants were checked: their stream setters accept but ignore nonzero
streams. That body stays excluded; the adapter binds the donor-owned buffers
to each actual native input slot and explicitly clears null bindings.

Common's low sixteen public-reference bits and bits 19..22 for binding
references remain independent. Public release to zero does not free a bound
resource; another AddRef is valid while that binding retains it. Replacement
moves the native binding before collecting the old object. Registered vertex
buffers retain their caller-owned physical backing until the last reference
goes away. Device teardown drops bindings and collects internal-only objects.
As a documented HLE lifetime extension, texture self-rebinding with only a
binding reference preserves the object, avoiding the original release-before-
acquire hazard. Ordinary defined reference/cache behavior is retained.

CPU sample textures and vertex buffers upload changed contents at binding.
GPU-written render/depth textures retain their actual GPU contents and upload
only when newly initialized or explicitly exposed CPU writes require it. No
production readback is introduced for sampling. Future draw submission must
also prepare live CPU-backed inputs to observe writes through retained pointers.
Simultaneous sampling and output on the same texture remains guarded; native
D3D11 must not silently remove an expected SRV. Existing target-alias and raw
CPU-pointer limitations still apply.

Validation: all 29 CTests pass. The GPU fixture samples actual texture colors,
observes changed uploaded bytes, samples a GPU-written render target and checks
native stream slots/strides and null unbinding across three device lifetimes.
Guest fixtures cover public release while bound, AddRef from that state,
multiple binding references, self-rebind, changed vertex bytes, caller-owned
backing and twelve consecutive replacement cycles without residual allocations.
The offline original-x86 audit runs thirteen binding cases. Eleven complete
and verify cache/header state; two stop at the original destruction entry to
verify its trigger rather than pretending to execute the original allocator.
Native resource destruction and real backing reclamation are covered separately.

Fresh/repeated NTSC Blinx now passes four SetTexture calls, including the actual
stage-0 texture, followed by two SetVertexShaderConstant1 calls. It stops at
`D3DDevice_SetTransform` at `0x00131eb0`, requesting the view transform. Entries
are 22252367/22252275 and completed consecutive callbacks are 20/17. No title
stream/target binding, draw/frame, Play, movie, menu or attract/cadence yet.
Next: the transform family and vertex-to-raster interface before actual draws.

Private evidence: `build/blinx/audit_resource_bindings.py`,
`build/blinx/resource-binding-oracle.aot-private.json`,
`build/blinx/resource-binding-validation.aot-private.json`,
`build/blinx/run-resource-binding-{fresh,repeat}.log`,
`build/resource-binding-gpu.log`, `build/resource-binding-contract.log` and
`build/resource-binding-all-contract-tests.log`. The CPU artifact remains
private and GPL-dependent; no GPL implementation was imported by this change.

### Shader binding checkpoint (2026-10-07)

The programmable vertex selection now binds the actual compiled D3D11 shader,
input layout and constant buffer. It publishes the original guest handle and
descriptor, applies declaration constants and mirrors the program cache when
the original recorder is enabled. Binding does not invent a public AddRef.
Program/selection/cache offsets are decoded into the revision binding; shared
runtime code contains no Blinx addresses.

The qualified constant setters preserve their distinct contracts. The one- and
four-register fastcall entries take a hardware register in ECX and data in EDX.
The generic fastcall entries take their count in DWORDs on the stack. The fast
variant changes GPU state without changing the queryable CPU cache; the normal
variant obeys the original recorder-disable flag. Queries take signed logical
registers, mapped by adding 96. Native uploads coalesce until draw submission;
the production path performs no reference execution or GPU readback. Identical
writes do not upload again.

For `SetPixelShader(0)`, the prepared MIT sp00nznet renderer now exposes its real
fixed-function pixel compiler/state upload separately from vertex preparation.
This reuses the existing native pixel pipeline without substituting the donor's
FVF vertex shader. The pinned donor and licensing scope are unchanged. Nonzero
pixel handles still stop before returning; their definitions have been created
but their NV2A combiner pipeline has not been qualified.

Validation: twelve captured calls match execution of the original XBE routines
for current handles, program/constant caches and query outputs. The offline
oracle supplies captured program/declaration data in the original load packet
format; it executes the original load/select/cache routines. Separate real GPU
stream-output probes consume the actually bound shader/layout/constant buffer,
verify fast writes versus cached queries, and verify that pixel selection
preserves vertex binding. Batched writes produce one upload, unchanged writes
produce none. All 29 CTests pass, including the retained fixed-function pixel
readback and state tests. This is a bounded service result, not title throughput.

Fresh/repeated NTSC Blinx reaches `D3DDevice_SetTexture` at `0x001329b0`
(stage 1, null texture) with 22252334/22252242 entries and 20/17 consecutive
title callbacks. It has 96 compiled vertex shaders and 37 owned pixel
definitions. No title target bind, draw/frame, Play, movie or attract loop yet.
Next: real texture/stream binding and the vertex-to-raster interface before
enabling title draws. Raw vertex-output probes do not validate viewport undo,
pixel-input semantics or rasterization. FVF transitions, independent program
slot loading/selection, partial float4 writes and programmable pixel binding
remain guarded, as do state application and swap/callback completion flags.

Private evidence: `build/blinx/vertex-state-audit/state-*.bin`,
`build/blinx/audit_vertex_binding.py`,
`build/blinx/vertex-binding-oracle.aot-private.json`,
`build/blinx/vertex-binding-validation.aot-private.json`,
`build/blinx/run-vertex-binding-{fresh,repeat}.log` and
`build/vertex-binding-all-contract-tests.log`. The generated CPU executable
remains a private GPL-dependent reference artifact excluded from shipping.

### State capture and vblank callback checkpoint (2026-10-07)

All three pinned recomp donors were checked; none has a state-block
implementation to adopt. The independently authored native capture uses the
reviewed original recorder's CPU-only contract. ALL, PIXEL and VERTEX membership
tables and device-field offsets stay in image-qualified generated bindings.
The block contains actual cached state, constants, transforms, viewport,
materials and resource bindings in the original record layout. It owns its
storage and retains texture, stream and shader references. An allocation
failure clears the output token without acquiring references, as the original
does. Device teardown releases owned blocks and their retained resources.

Creation now exposes original initial state needed by the snapshot: ten
identity transforms, FVF XYZ, empty bindings and zero-initialized CPU constant/
material storage. Six empty/populated fixtures produce the same complete
bytes and reference increments as executing the original x86 recorder with
real emulated allocation: ALL 5460/6244 bytes, PIXEL 1936 bytes, VERTEX 2344
bytes. The actual title's 5460-byte snapshot also matches. This is an offline
ABI check, not duplicate production execution. Capture with an unqualified
light binding stops; state application, recapture and independent guest delete
entry points are not qualified by these creation tests.

The vblank service uses the existing display epoch and serialized DPC worker.
It invokes actual generated title code with its own guest stack/TLS, IRQL 2,
one cdecl pointer argument and checked stack cleanup. The argument contains
three DWORDs (vblank count, swap count, flags). The public shape is also
documented by the [Cxbx type declarations](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/XbD3D8Types.h#L650);
no GPL callback implementation is copied. Phobos's original-kernel interrupt
acknowledgement fix was a useful behavioral reminder: registering a callback
without delivering it leaves real title work blocked.

The pulse runs at the configured nominal 60 Hz independently of draw calls,
retains absolute phase and delivers pending ticks serially rather than
discarding callback work. Replacement retains phase; removal permits an
already executing callback to return. No graphics/dispatcher lock is held
across guest execution. Device release disables future delivery and joins
in-flight execution before freeing its callback data. Tests cover real timing,
register isolation, ABI/data, replacement, in-flight removal, self-removal and
release while a callback is running. Warm repeated registrations keep the
host handle count stable; the shared dispatcher stack is persistent process
infrastructure and is included before device allocation baselines.

The admitted callback path currently precedes presentation: swap count/flags
are zero because no swap has occurred with callback delivery enabled. Swap
with a registered callback, or callback registration after a presentation,
explicitly stops until completion flags and present timing are qualified.
This is not a physical NTSC 59.94 Hz qualification or a
claim that loading executes at original speed.

All 29 CTests pass. Fresh/repeated Blinx reaches shader binding at
22252332/22252240 entries, with 22/17 consecutive completed title callbacks.
Both load `h_kidougamen_haikei01.dds`, `h_kidougamen_hari01.dds`, `h_tc_bar.dds`,
`h_kidougamen_tp_us.dds` and `h_kidougamen_pw_us.dds`. No title frame, buffer
Play, audible game output, movie, menu or attract loop yet. Next: shader and
draw-input binding. JSRF remains deferred.

Private evidence: `build/stateblock-all-contract-tests.log`,
`build/blinx/stateblock-audit/state-*.bin`, `build/blinx/audit_stateblocks.py`,
`build/blinx/state-callback-{fresh,repeat}/state-1.bin`,
`build/blinx/state-callback-validation.aot-private.json` and
`build/blinx/run-state-callback-{fresh,repeat}.log`.

### Native render-target checkpoint (2026-10-07)

The reached family admits single-level linear ARGB/XRGB color targets
(`0x12`/`0x1e`, usage 1) and linear D24S8 depth targets (`0x2e`, usage 2).
Original XDK helper execution confirms headers, 64-byte-aligned pitch and
allocation extent for 640x480 and odd 37x19 fixtures, including depth. Guest
surface views share the actual parent storage and retain it. Binding retains
both views, acquires new references before releasing old ones, resets the
viewport, preserves the color target for a null color argument and disables
depth for null depth. Queries return the current owned surfaces with AddRef.

Actual texture allocation/SRV handling comes from pinned MIT sp00nznet
`1409a7d`. The adapter creates real D3D11 render/depth views, using typeless
depth storage with separately typed views. Phobos `63f18f0` was inspected for
ownership/viewport behavior; its GPL implementation is not imported. NoRain
`7adaf21`'s incomplete target methods were excluded. The main donor's target
binding method also required different ownership/error handling, so binding
is independently adapted from the original routines. The existing private
CPU pilot's GPL restriction remains unchanged.

GPU output is copied back only when an admitted CPU lock requires it. CPU
writes are uploaded before binding/clearing or exposed-view release; pending
alias writes survive additional locks, and untouched view release does not
overwrite GPU output. Depth transfers convert the existing LLE floor's
Z24S8 layout (depth high 24 bits, stencil low 8) to/from native D24S8. This
does not require duplicate production execution or matching native storage.

All 29 CTests pass. Target fixtures check actual color/depth pixels, padded
rows, XRGB's unspecified alpha, partial depth/stencil updates, CPU edits
through aliases, safe self-rebinding, client release while bound, parent/view
lifetimes and restoration of the guest allocation baseline. Fresh and repeat
Blinx launches stop at state-block creation after 20607051/20606959 entries.
Private evidence: `build/target-all-contract-tests.log`,
`build/blinx/linear-texture-oracle.aot-private.json`,
`build/blinx/target-validation.aot-private.json` and
`build/blinx/run-target-{fresh,repeat}.log`.

The default depth surface is represented as canonical linear `0x2e`, as the
existing default color surface uses canonical linear `0x12`; Blinx requested
swizzled `0x2a`/`0x06`. These are explicit HLE representation differences,
not a claim of identical hardware layout. Direct/default-depth CPU access,
retained CPU writes across GPU operations, other target formats, multisampling,
render-target mip chains and inline private device-field access remain
unqualified. Texture sampling and shader application to title draws still
need their own boundary validation. The title has created and queried targets;
binding/clear/readback evidence currently comes from GPU contract fixtures.
The next family is real state-block capture/application, not extra formats
unrelated to the observed stop. No title frame, menu or attract result yet.

### Native input checkpoint (2026-10-07)

Bind `tools/native_input_binding.py --xbe <default.xbe> --output <repo>/build/blinx
--symbols <default_xdk_symbols.json>`, then select `-DPILOT_INPUT=NATIVE`.
Full regeneration accepts `--input-symbols`. `ORIGINAL` retains all eight
original XAPI bodies and their USB driver calls. Selection is at build time;
neither mode executes the other in production.

The host mapping adapts MIT sp00nznet `1409a7d` `src/input/xinput_device.c`;
the notice is [input-sp00nz-MIT.txt](input-sp00nz-MIT.txt). We audited Phobos
`63f18f0`'s XAPI/model/host split and NoRain `7adaf21`'s controller backend as
references. No GPL input implementation is copied. The private pilot's existing
CPU-helper license restriction still applies.

The guest adapter is independently implemented from the inspected XDK4831
routines and the maintained OHCI Controller S model. It serializes a 22-byte
state; **25-byte capabilities** (subtype at 0, reserved WORD at 1, input payload
at 3, rumble at 21); and 70-byte feedback (status at 0, event handle at 4,
report header at 64, motors at 66/68). The donor's desktop-style capability
layout and its zeroed control capabilities are unsuitable here. Capability
controls/resolutions now come from the actual host query, using the
[Windows capability contract](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_capabilities).

Four real Windows XInput ports feed a Controller S mapping. There are no
configured MU/IR peripherals or automatic keyboard controller. Digital face
buttons map to analog endpoints 0/255; shoulders map to black/white; triggers
and axes retain their values. This does not supply pressure-sensitive face
buttons that the host lacks. The same 18-byte control payload is used by the
LLE XID report; full driver-vs-native differential execution is not claimed.

Current/changed/previous masks remain guest-visible. Enumeration preserves
the original insertion/removal semantics, including removal and reinsertion
between reported snapshots. A removed device invalidates existing handles;
reconnection requires a new handle. Preallocation limits and one live open per
port are enforced; failed opens call the original thread `SetLastError` helper.

Auto polling caches state for the requested interval (default 8 ms). Empty
ports are sampled at most every 250 ms, bounding normal query-driven hotplug
latency; transitions entirely between host polls are not observable. Packet
numbers change with mapped state. Rumble completes synchronously after the real
Windows operation and propagates its status. Close releases the handle and
stops its live controller's rumble. USB transfer latency is not reproduced.
Manual polling, non-gamepad host types, unknown classes, repeated initialization
and feedback requiring event-handle resolution stop explicitly. They are
remaining coverage, not successful no-ops.

Twenty-four CTests pass, including real conversion code driven by a test-only
host API provider, output guards, poll caching, reconnection, capability errors,
feedback failure, 100 handle lifetimes and exact unsupported-stop reasons.
Fresh/repeated actual Blinx launches reach the same original audio-driver
`KfRaiseIrql` stop (20,463,434 / 20,463,342 title-thread entries). No host pad
was attached during those runs; physical interaction/rumble and attract input
interruption remain unvalidated. The ORIGINAL input run still reaches
`IoCreateDevice`, confirming the preserved selection, not completed USB support.

Private evidence: `build/blinx/input-family-validation.aot-private.json`,
`run-input-family-fresh.log`, `run-input-family-repeat.log`,
`run-input-original.log`, and `build/input-family-contract-tests.log`.
Sound initialization now also reads a real `HalBootSMCVideoMode` ULONG at
ordinal 356, configured to composite (6) like `machine/smbus.cpp`.
`MmQueryAllocationSize` returns the actual rounded extent for an owned
contiguous base; other allocation classes/addresses remain explicit stops.
The next coherent family is native DirectSound device/buffer/stream services
with real mixing/output and completion semantics. No audio-driver IRQ service
is being acknowledged merely to advance startup.

### Earlier service reuse

- Phobos supplies the actual x86 discovery/lowering and register ABI. Both
  titles used their own entries and discoveries, with no donor address seeds.
  Each generated set had 14 unresolved direct targets. Their success-return
  bodies are replaced by terminal unsupported faults. Unknown indirect calls,
  unsupported instructions and missing services also stop.
- Phobos's host-lock mapping and kernel bridge signatures inform the adapter.
  Its kernel/device runtime is not linked. Cxbx `585c49a` supplies Xbox
  `RTL_CRITICAL_SECTION`, `KDPC`, `KTIMER` and EEPROM field contracts. For example,
  the Xbox critical section is 28 bytes; the timer header size is ten DWORDs.
  Host Windows structs are not written over guest structs.
- Only preload XBE sections are committed initially; Blinx's 42 deferred
  sections and JSRF's two are not loaded ahead of time. Shared boundary pages
  can already be committed. Explicit section load/unload now owns deferred
  residency as described below. JSRF has not been run against this new service.
- Each created thread receives its own stack, TLS template/zero-fill, KPCR,
  thread identity and CPU register state. Diagnostics are thread-local too.
  The bootstrap may close its thread handle and return while the title runs.
- The exploratory launch environment reports retail kernel 1.0.5838.1 and
  NV2A A1/MCPX B1 identity. This does not certify every export of that kernel.
  User settings come from the supplied 256-byte EEPROM with checksum handling.
  Encrypted/factory queries and other data exports remain unsupported.
- Native VM reservation/commit/free/protection/query is bounded as described
  below. Complete retail physical RAM accounting, scheduling/IRQL and exceptions
  are not yet qualified. Donor double-backed x87 and other CPU limits remain.
  PUSHFD/POPFD stop because arithmetic flags are not exported/imported by the
  current donor helpers. This is not a faithful replacement for the CPU floor.
- Compilation disables host optimization for short bring-up cycles. No FPS,
  full AOT coverage, or D3D8 HLE performance claim follows from this result.

The next Blinx family is pixel shaders, followed by binding and draw submission.
The native texture adapter must synchronize changed guest bytes at binding/draw
boundaries too, because Xbox unlock can be inline and guest pointers persist.
The whole native renderer now owns graphics creation and front/back resources.
Utility-drive selection/mounting now
runs through the complete native library boundary below, and file-cache control
has actual storage/read behavior. The original utility routine remains available
at build time.
The native installed-media contract below replaces the earlier optical check
with the owner's explicit authorization. The original check requests SCSI MODE
SENSE page 0x3e through IOCTL 0x4d014; that IOCTL has not executed locally. The NAS
contains both the original extracted title and ISO, plus a separate Rev 1 ISO;
pin the original input, not Rev 1. Use existing references, keeping GPL bodies external.
The inspected MIT sp00nznet code is a candidate shipping implementation source
after file provenance checks. D3D8 remains the performance target; first-scene
bindings still need Blinx's local indexed draws and JSRF's older GetBackBuffer
out-pointer ABI. A complete reference machine is not a prerequisite to those
HLE implementations, and dashboard speed does not gate title work.

## Native graphics checkpoint

The optional `PILOT_RENDERER=D3D11` replaces all 129 identified D3D8 entry
points with the shared adapter. Only qualified methods return; the rest stop.
`ORIGINAL` executes the preserved generated bodies. This is a build-time choice,
with no live switch or duplicate production execution. Revision addresses come
from a private whole-XBE/hash-bound symbol manifest; original device publication
is decoded from the reviewed CreateDevice body, not embedded in shared services.

The first qualified subset is device creation, full-target clear, viewport,
interval-one swap, front/back surface acquisition/reference ownership and
release with no outstanding surface references. Creation supports adapter zero,
hardware device, flags zero/hardware vertex processing, one non-MSAA A/X8R8G8B8
buffer, D24S8 automatic depth, default/60Hz refresh, interval one and no supplied
surfaces. Unsupported presentation configurations return failure before creating
anything. Native output uses a hidden window, RGBA8 target and progressive host
presentation; it is not analog scanout/timing qualification. The window remains
hidden until title scene qualification. No game frame rate is measured here.

Guest presentation parameters are 17 DWORDs; native HWND/COM pointers are never
cast into guest structures. Xbox clear flags are translated explicitly. Front
surface captures retain the actual image submitted at present; subsequent back
buffer draws do not change it. Pre-first-present contents remain undefined.
Guest surface headers contain linear format/dimensions and real reference
ownership. Their data address reserves GPU-owned residence in guest VA; direct
CPU access faults until a real lock/readback contract is qualified. GPU resources
are host-owned and are not yet included in the pilot's retail-memory budget.
Device release with outstanding resources, reset, partial-channel/rectangle
clear, front/back-buffer locks, and unimplemented state/draw methods stop explicitly.

The MIT donor provides the D3D11 device/state/shader/resource code, not the Xbox
32-bit guest ABI. Preparation excludes unqualified COM bodies, including
upstream success-only implementations, and hardens reached failures. Its direct
backend smoke test draws a triangle with explicit no-texture state and validates
actual readback pixels. Guest tests check success/failure stack cleanup, callee
registers, null output support, clear flag translation, distinct front/back
pixels, references and three release/recreate cycles with no retained guest
allocations. These are native API/GPU tests plus static original-routine and
external layout references, **not an executed LLE differential**. Full driver
state, default-state parity and arbitrary inline driver access remain unqualified.
Blinx has since passed its initial cull/depth/blend/texture-coordinate/viewport
setup, but still has not drawn. The state extension below records that boundary.

Prepare and select privately after generating the title:

```powershell
py -3 tools/native_d3d_donor.py --donor <pinned-main-sp00nznet-checkout> --vsh-reference <pinned-nv2a_vsh_cpu-checkout> --output build/sp00nz-d3d
py -3 tools/native_d3d_binding.py --xbe <original-NTSC-default.xbe> --symbols <default_xdk_symbols.json> --output build/blinx
cmake -S pilots/native-title -B build/blinx/host -DPILOT_RENDERER=D3D11 -DPILOT_D3D_SOURCE=<absolute-path-to-build/sp00nz-d3d>
cmake --build build/blinx/host --config Release --parallel 2
ctest --test-dir build/blinx/host -C Release --output-on-failure
```

Fresh regeneration can supply `--d3d-symbols` to `native_pilot.py`; stale wrappers
are cleared before lifting. Preserve the existing installed-media/native-cache
selections for Blinx. Donor source/prepared hashes and licenses are retained in
`build/sp00nz-d3d`; logs and repeat/reference-selection evidence are under
`build/blinx/graphics-runs.aot-private.json` and `build/graphics-contract-tests.log`.

### Native render-state and sampler extension

`graphics_states.c` translates Xbox state values to the donor's host-facing
state interface. `graphics_state.inc` owns guest state publication and the
deferred-array batch hook. The original initializer's two immutable default
tables are found through a reviewed function hash and decoded table references;
their contents and guest addresses stay in private generated bindings. The
three slots explicitly skipped by the original initialization loop remain
skipped. The native depth-enable default follows the actual allocated depth
target. Recreating the device resets the owned mode fields.

Qualified mappings cover the reached cull/depth/blend/color states, comparison
and stencil encodings, supported fog/ambient fields and bounded texture/sampler
states. Raw `SetRenderState_Simple` takes ECX/EDX packet/value arguments and
does not update the guest render-state cache; named setters do. The batch hook
samples deferred arrays before fixture draws and rejects changed unqualified
fields. **Guest draw entry points remain unsupported** and must call this hook
when integrated. Full lighting/material/default-pipeline equivalence is not
asserted merely because a donor shader accepts these state values.

The reached shader constant mode is `1`. Its reviewed original branch only
records the mode and clears the no-reserved-constants flag, with no GPU packet.
The native device already owns a real 192-float4 constant buffer. This bounded
branch preserves those guest fields; mode zero's fixed-pipeline resets, other
transitions, constant upload and programmable shader execution are unqualified
and stop at their respective unimplemented entry points.

Prepared donor fixes preserve diffuse argument zero (previously replaced with
TEXTURE/CURRENT defaults), implement W addressing in private donor slot 31
without colliding with its COLORSIGN slot, apply border/LOD values, and retain
all basic min/mag/mip filtering combinations. They reuse the donor's real D3D11
state/shader implementation. Pixel fixtures check depth rejection/disable,
channel masking, alpha blend/test, winding culling and direct guest texture
state changes; GPU sampler descriptors check actual applied settings. Alpha
blend tolerates two 8-bit UNORM levels; border float tolerance is 1e-6. These
are bounded native fixtures, not an executed hardware/LLE image comparison.

Evidence: `build/graphics-states-tests.log`, `build/graphics-states-pixels.log`,
`build/blinx/graphics-state-validation.aot-private.json`. Fresh/repeat Blinx
runs now complete the real font-file read and stop at guest setjmp recovery.

## Native file-cache contract

`file_cache.inc` implements `FscGetCacheSize` (35), `FscSetCacheSize` (37) and
`FscInvalidateIdleBlocks` (36). Size is in 4096-byte pages: default 16, maximum
2048. Resize allocates/releases real buffers; successful zero size disables the
native cache. Invalid sizes and allocation/budget exhaustion return failures
without changing the existing pool or cached bytes. The cache is read-through
and write-through: no dirty data or invented disk completion exists.

Cached file ranges carry handle identity and valid byte intervals. A hit copies
actual previously read bytes and advances the synchronous file position; a miss
uses one native read even for large requests, then retains a bounded trailing
range. No rounded read can hide EOF/partial/error results. Per-file locks cover
cursor-dependent operations. The cache lock protects buffers and resizing, and
is released during native reads. Invalidation discards all currently idle data;
no cached pointer escapes a call. Writes, size/disposition changes and close
invalidate the affected handle. Cache only handles that deny write sharing;
files permitting an external writer use native reads so host changes stay visible.
Unbuffered/unsupported I/O modes retain the existing explicit scope limits.

Cache bytes are charged to the same 64 MiB admission budget as guest VM and
section commits. Host metadata and temporary old/new buffers during replacement
are outside that guest budget; complete retail physical/MMU accounting remains
unqualified. The replacement algorithm and host filesystem caching differ from
FATX/XDVDFS kernel internals. No cache-size tuning is applied to the host OS.

The new contract test uses real file bytes and an instrumented native read entry
to verify hits actually avoid reads. It checks cursor/subrange/EOF behavior,
real commit/release, zero-size bypass, invalidation, failure preservation,
shared VM budget, concurrent reads/resizes, writes/truncation, and an actual
external writer. Eleven total CTests pass. Blinx itself now disables caching,
requests 64 pages successfully, and reaches graphics device creation. No title
cache-hit rate, rendered frame or FPS claim follows from the fixtures.

ABI/default/maximum references: nxdk and external Cxbx `EmuKrnlFs.cpp`.
Main sp00nznet/Phobos `kernel_bridge.c` only retain a size, and Cxbx's allocator
is unimplemented; those bodies are not adopted. Host behavior follows
[NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile).
These are API/native tests, not a full executed LLE differential.

## Native utility-drive contract

`cache.inc` implements the complete reviewed `XMountUtilityDrive(BOOL)` boundary:
EAX is BOOL, the return consumes eight guest stack bytes, EBX/ESI/EDI/EBP are
preserved, success leaves LastError alone, and failures call the title's actual
`XapiSetLastNTError`. The existing error helper supplies its own TLS layout and
NTSTATUS conversion. There is no duplicate execution of the original mount.

Three real cache directories (partitions 3–5) retain title ownership across
processes in `native-cache-v1.bin` under the supplied local state directory.
Existing-title mounts preserve bytes; clean requests and eviction remove the
selected directory's contents and create a real `\\??\\Z:` namespace entry.
Empty slots precede least-recently-used eviction. A checksummed, versioned
manifest is held exclusively. Formatting first flushes an invalid-slot record,
so failed/interrupted cleanup cannot later be accepted as a valid cache. An
existing corrupt manifest fails rather than silently erasing storage.

Recursive cleanup validates the exact absolute resolved partition path, then
uses pinned directory handles, relative native opens and no-reparse traversal.
Read-only child attributes are cleared for actual deletion. Open guest handles,
host sharing errors and namespace collisions report failure. Namespace collision
is checked before formatting. Only the chosen partition is cleared; installed
content and user-data partitions are retained. Native partitions share host
capacity and advertise 16 KiB logical allocation units; other cache units fail
explicitly. The native ownership format and LRU policy do not promise retail
raw-sector layout, exact FATX replacement order, quotas or physical timings.

`tools/native_cache_binding.py` uses the shared `native_binding.py` integrity/
wrapper machinery, with a reviewed whole-XBE/body hash and identified stdcall
symbol. The error helper is independently hash-checked. No title addresses
appear in the shared service. Generate the wrapper with
`--cache-symbols <default_xdk_symbols.json>` in `native_pilot.py`, or run:

```powershell
py -3 tools/native_cache_binding.py --xbe <default.xbe> --output build/blinx --symbols <default_xdk_symbols.json>
cmake -S pilots/native-title -B build/blinx/host -DPILOT_CACHE=NATIVE
```

`PILOT_CACHE=ORIGINAL` selects the original generated routine. That selection
was rebuilt and executed, reaching raw HDD partition 0; its raw I/O/FATX path
is not yet implemented in this private pilot. The normal LLE machine remains
unchanged. There is no live switching or executed LLE differential claim.

Ten CTests include actual Z: byte routing, independent persisted-byte reads,
reopen, recursive/read-only/many-entry cleanup, eviction isolation, locked-file
failure/recovery, corruption rejection, LastError callback/ABI and stable host
handle counts. Symlink creation is unavailable without privilege on this host;
that fixture reports a skip. A real Blinx run with its manifest locked executes
the original error helper and returns failure. Fresh/repeated native launches
pass utility mounting; restoring a true cold-launch `LaunchDataPage == NULL`
export advances further to filesystem-cache control. Warm launch data and
`HalReturnToFirmware` remain unsupported. A null pointer describes absence of
a previous launch, rather than a fabricated launch packet.

Contract sources: the original owned XBE's complete routine, nxdk's data-export
declaration, Cxbx's external cold-launch pointer initialization, and Microsoft's
[NtQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile)
and file-information contracts. No GPL filesystem implementation is imported.

## Installed-media contract

`media.inc` replaces the complete reviewed XAPI media-verification helper,
**not** `NtDeviceIoControlFile`. The original boundary takes no arguments,
returns NTSTATUS in EAX, consumes the four-byte return address, preserves
EBX/ESI/EDI/EBP, and returns no guest resource. Its temporary device handle does
not escape. The installed implementation keeps that interface and has a distinct,
explicitly authorized policy: matching installed content is accepted without
proving physical DVD authentication. It does not populate drive packets.

The mounted directory must open, `default.xbe` must be readable, and its streamed
SHA-256 must equal the exact image used to generate this executable. A successful
mount retains real root/identity handles for the process lifetime, denying
write/delete sharing on the identity. Repeat calls revalidate rather than
allocating more handles. Missing media returns STATUS_NO_MEDIA_IN_DEVICE;
wrong identity returns STATUS_WRONG_VOLUME; failed file access retains the host
NT error where available. All other assets are read on demand; their presence
and identity have **not** been certified by checking `default.xbe` alone.

Native file services implement read-only synchronous `NtOpenFile`, FILE_OPEN
through `NtCreateFile`, `NtReadFile`, standard/position query, position set and
`NtClose`. The host NT I/O manager supplies real seek/read/EOF/share semantics;
the adapter owns guest handles and marshals the eight-byte Xbox IO_STATUS_BLOCK.
Directory-relative opens survive closing the parent handle. Guest paths accept
ASCII D: and Device/CdRom0 names; traversal, alternate streams and reparse
traversal are rejected. The byte source is the actual installed directory.
Read-only media never becomes a save/cache directory.

Pending work remains explicit: async I/O and APC/event delivery, directory
enumeration, additional info classes, raw optical access,
and non-ASCII Xbox path mapping. Unsupported modes stop. The host filesystem
supplies file allocation metadata; FATX/XDVDFS allocation equivalence is not
yet qualified. Invalid guest-pointer exception recovery is not implemented.
HDD logical-volume geometry is explicitly translated as described below.

`tools/native_media_binding.py` checks generated-artifact integrity, identifies
the reviewed helper by its original byte hash plus whole-XBE hash, and requires
its discovered XapiInitProcess caller. It renames only the private generated
definition and emits a build-selected wrapper; direct and indirect callers keep
the same symbol. Shared service code has no revision addresses. Unknown images
or helper bodies fail binding rather than guessing. A new revision needs a new
contract review. Original generated code remains in the optical build.

For an existing private generated Blinx tree:

```powershell
py -3 tools/native_media_binding.py --xbe <original-default.xbe> --output build/blinx --symbols <default_xdk_symbols.json>
& 'C:\Program Files\CMake\bin\cmake.exe' -S pilots/native-title -B build/blinx/host -DPILOT_MEDIA=INSTALLED
& 'C:\Program Files\CMake\bin\cmake.exe' --build build/blinx/host --config Release --parallel 2
& build/blinx/host/Release/xlle_native_title.exe <original-default.xbe> <eeprom.bin> <installed-content-directory>
```

Fresh generation accepts `--media-symbols <default_xdk_symbols.json>` with the
usual `tools/native_pilot.py` arguments. Configure `-DPILOT_MEDIA=OPTICAL` to
select the original verification routine. Neither a launch flag nor an
environment variable switches implementations. Only the content path is a
launch input. Both configurations were built and run. The OPTICAL configuration
currently stops at unsupported raw optical access: preserved executable code
and ABI inspection are **not an executed LLE authentication equivalence proof**.

Validation: seven CTests, repeated actual NAS-backed Blinx startup, missing-media
title failure, unknown-binding rejection, and the original optical routine run.
The media contract test covers wrong/missing identity, nonvolatile registers and
stack, write denial, real bytes/EOF/position, bounded query writes, root-relative
handles, stale handles and 300 opens/closes without host-handle growth. Actual
missing-content startup returns NO_MEDIA but its subsequent error UI faults on
an unsupported kernel data import; that UI is not validated. No FPS claim.

References: MIT sp00nznet `1409a7d` file/path services supplied architecture and
pitfalls; [nxdk's Xbox declarations](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h)
and [Microsoft NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile)
describe the ABI and native file-position contract. No donor service bodies were
imported. The private Phobos CPU-helper licensing limitation is unchanged.

## Native HDD and namespace contract

The optional fifth argument is an **existing local writable state directory**.
The runtime creates/opens real `Partition1` through `Partition5` directories
there. It rejects a remote/reparse root and a root overlapping installed content.
Use an isolated directory such as `build/blinx/storage`; no source media or LLE
disk-image writes are required. Mounts are selected by actual guest device paths
and inherited through relative directory handles. There are no title addresses
or fixed title IDs in the service. For example:

```powershell
New-Item -ItemType Directory -Force build/blinx/storage | Out-Null
& build/blinx/host/Release/xlle_native_title.exe <original-default.xbe> <eeprom.bin> <installed-content-directory> build/blinx/storage
```

Create dispositions, writes, flushes, allocation/end-of-file/disposition changes
and file metadata operate through native NT services on owned handles. Host
failures propagate; there is no flush-success fallback. Standard, position and
network-open query structures use aligned host staging because the Xbox caller
can provide a four-byte-aligned stack object while desktop NT requires eight.
For metadata query compatibility, native handles also request READ_ATTRIBUTES;
this does not grant READ_DATA to a guest write-only handle. Tests exercise both.
Guest IO_STATUS_BLOCK remains eight bytes, and no native pointers escape.

The HLE volume policy uses 512-byte sectors and 32-sector logical allocation
units. Total/free byte capacities come from `NtQueryVolumeInformationFile` on
the **actual caller's mounted handle**, converted down to 16 KiB units with
less than one unit of rounding. Query failure stays failure. Native partitions
share host capacity; stock partition quotas are not emulated. File allocation
metadata and timestamps keep host semantics, so exact FATX space consumption
and timestamps are not qualified. The existing LLE image's E: FATX header was
read without mutation and reports 32 sectors per cluster too; this supports the
unit choice but does not constitute a running LLE API differential. These
policies are explicit HLE tolerances, not claims about physical host geometry.

`namespace.inc` owns actual case-insensitive symbolic-link objects. Create
detects collisions; delete removes the name; previously opened query handles
retain the old target even when that name is recreated. Closing releases object
ownership. Short target buffers report required length without overwriting
the buffer. File opens resolve the current namespace, including chained links,
with bounded cycle failure. D:, T: and U: are established by the title's actual
calls rather than silently recreated after deletion. ASCII names and absolute
link objects are the current bounded scope; other object types remain unsupported.
The adapter also marshals RtlInitAnsiString/RtlEqualString through host NT RTL.

Eight CTests pass, including file bytes persisted across close and independently
read by the host, flush/truncation, volume rounding, aligned/unaligned metadata,
access isolation, bounded writes, link handle lifetime and 300 link lifecycle
iterations. Actual fresh and repeated Blinx launches preserve the same 72-byte
TitleMeta content and reach the same required deferred-section load. This proves
startup progress, not complete save-game, cache-partition or attract-loop support.

Donor comparison: sp00nznet `1409a7d` and Phobos `63f18f0` encode the 16 KiB
startup requirement; the inspected NoRain code still reports 4 KiB. All three
have a fixed-capacity success fallback on host query failure and ignore errors
from host flush. Those behaviors are not adopted. The useful native-volume,
path and symbolic-link approach is adapted to the bounded ABI above using host
NT services; no GPL service bodies or libfatx implementation are imported.
[NtQueryVolumeInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvolumeinformationfile)
and [FILE_FS_SIZE_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_fs_size_information)
document the host contract; nxdk declares the Xbox structures and ordinals.

## Deferred section residency

`sections.inc` maintains per-section references and a separate page-ownership
ledger for the immutable, identity-checked XBE. Initial preload sections and
header pages own their actual page ranges; exported 16-bit boundary counters
are initialized accordingly. The old loader had set section references without
initializing those boundary counters. Deferred sections gain ownership only on
their first successful load. The same API handles any discovered section; no
title-specific addresses or section IDs appear in its implementation.

On the first load, real host pages are committed, raw bytes are copied from the
verified input, and the virtual tail is zero-filled. Additional references do
not overwrite modified bytes or add physical ownership. Final unload decommits
only pages without another section/header owner. Fragments on still-shared pages
remain unchanged, including the unloading section's fragment. A subsequent
load restores the original payload and zero-fill. Guest section and boundary
counts track successful state changes; failed admission preserves them.

The adapter shares the existing 64 MiB commitment admission check with native
VM operations. That remains a bounded host-commit count, not complete retail
physical accounting. Image pages retain the pilot's RW backing; XBE read-only/
execute permission enforcement, loaded-code invalidation and full guest fault
delivery are not qualified. External edits to section reference fields stop
explicitly. The API is bounded to original validated image descriptors and
non-overlapping section byte ranges, while page overlap is supported.

Nine CTests pass. The section test covers real data, zero-fill, repeated-reference
preservation, reload, shared head/tail pages, aliased boundary counters, actual
decommit faults, concurrent load/unload ownership, invalid descriptors, overflow
and memory-budget failure preservation. Actual Blinx performs a load, a real
file write and an unload; a separate host comparison proves all 10240 output
bytes equal the original section. A second process run keeps the same file.
This does not yet validate executable-overlay use or the full attract sequence.

nxdk supplies the function declarations. Cxbx's section implementation was an
external behavioral reference for exported counters and shared-page lifetime;
its implementation is not imported. The inspected sp00nznet section routines
only change counts under an all-resident loader assumption, so they were not
adopted. Whole-image identity verification protects the immutable byte source;
the pilot does not separately emulate retail section-signature verification.

## Native virtual-memory checkpoint

`virtual_memory.inc` owns guest reservation ranges, delegating real page
commitment, protection and decommit to the host NT services. Xbox NT functions
have fewer arguments than desktop NT; the adapter marshals guest 32-bit fields
and returns a 28-byte memory-query structure. Successful reservation does not
commit or clear its pages. Recommit preserves existing bytes; decommit followed
by recommit zero-fills. Release removes guest ownership and allows safe reuse.
Reservations use 64 KiB alignment, commitment uses 4 KiB pages, and ZeroBits /
top-down allocation constrain guest VA selection. Failed calls retain outputs.

The XBE image range, including deferred sections, and the synthetic execution
contexts are excluded from heap allocation. A 64 MiB ceiling counts observed
host-committed guest pages, including loaded image/stack pages, when admitting
VM commits. This is **not complete retail physical-page accounting**: kernel
residency, page tables, physical contiguity and concurrent synthetic context
allocations are not modeled by it. Reservations may exceed physical RAM; tests
reserve 128 MiB without committing it. At most 512 reservation records exist.

Tests check real inaccessible/read-only access faults under MSVC, page rounding,
query boundaries, retained neighbors, decommit/recommit zero-fill, release/reuse,
allocation conflicts, address limits, budget failure and ABI stack cleanup.
Guard/cache protections and reset/nozero/large-page allocations stop explicitly;
image/synthetic-context protection and lifecycle are outside this heap scope.
This is API/host-backed qualification, not an executed LLE differential test or
a complete guest MMU. The existing private CPU helper limitations still apply.

References: nxdk Xbox prototypes/flags, Cxbx guest ranges/alignment, and Microsoft
[NtAllocateVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntallocatevirtualmemory),
[NtFreeVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntfreevirtualmemory),
[NtQueryVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvirtualmemory).
The sp00nznet `1409a7d` memory bridge was inspected; its commit-as-no-op and
reservation-clamping behavior were not adopted. No donor memory bodies imported.

## Native timer/DPC checkpoint

`dispatcher.inc` independently implements the bounded caller contract using
Windows waitable timers, a serialized DPC worker with its own guest stack/KPCR,
and synchronized guest dispatcher state. The host timer primitives preserve
absolute wall-clock versus relative interval semantics without a 10 ms polling
loop. Guest waits subscribe while holding the same lock used to change state;
successful waits reserve their result before a producer can reset the event.

- Timer initialization/set/set-ex/cancel, one-shot and periodic expiry. Rearming
  clears signal state and replaces the previous deadline. Cancellation removes
  future expiry, preserving already queued/running DPC work and signal state.
- DPC initialization/insertion/removal and `KeIsExecutingDpc`. Callbacks receive
  their object/context/arguments, with timer callbacks receiving system time.
  Duplicate pending insertion is rejected; callbacks may cancel or rearm timers.
- Notification/synchronization event initialization, set/reset and timer/event
  single/multiple waits. WaitAll consumes synchronization signals only once all
  objects are ready. Timeouts use actual absolute/relative host deadlines.
- Tests execute real callbacks, periodic self-cancellation, queued callback
  cancellation races and real blocked waiters. They check ABI stack cleanup,
  guest fields, callback arguments, caller-register isolation and timeout results.

This is bounded HLE qualification against API documentation and Xbox layouts,
**not an executed LLE differential test**. Full single-CPU scheduling, APCs,
interrupt masking, retail timer-list linkage and priority boosts are not modeled.
The DPC worker owns a synthetic execution context; it does not borrow a paused
title thread. Blocking DPC waits, alertable waits, unsupported dispatcher object
types, and `KeSetEvent(Wait=TRUE)` stop explicitly. Active timer/queued DPC limits
are 128/256; exhaustion stops rather than discarding work. The new service
implementation adds no emulator source dependency, but the overall pilot still
uses its GPL-derived CPU header and remains private reference-only.

Contract references: Xbox layouts/ordinals in Cxbx `585c49a` and Microsoft
[KeSetTimerEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesettimerex),
[KeCancelTimer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kecanceltimer),
[KeInsertQueueDpc](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keinsertqueuedpc),
[KeWaitForSingleObject](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject).
The inspected sp00nznet `1409a7d` timer bridge supplied a useful architecture
reference. Its absolute-as-immediate handling and fixed polling approach were
not adopted. No GPL service bodies were incorporated.

## Native texture checkpoint

`graphics_texture.inc` owns guest 20-byte texture/24-byte surface headers and
shared CPU storage. Creation uses actual pinned MIT donor D3D11 textures/SRVs;
native pointers stay private. A surface view retains its parent; final release
uploads that mip through the donor's real unswizzle/upload path. Unchanged
bytes avoid a redundant upload. Releasing the parent before its views preserves
storage until the last owning view is released. Descriptors use Xbox's seven
DWORD layout, not the donor's desktop eight-DWORD structure.

The current bound accepts power-of-two 2D sample-only textures, four color
formats (4 through 7), and valid partial/full mip chains through 4096 dimensions.
Other usage/types/formats stop. CPU allocations count against the guest budget;
GPU and upload-shadow copies use additional host storage. Cache/tiled lock
aliases return a canonical CPU-accessible guest pointer, with the same raw
swizzled bytes. Physical-address aliases, GPU-written textures, external
registered resources and sampling draws remain unqualified. A future draw must
synchronize persistent CPU writes even without another lock or rebind.

`GetDeviceCaps` copies the reviewed original table into the full 212-byte guest
output. Capabilities describe the emulated device; unimplemented methods still
stop. `GetDisplayMode` writes the actual configured dimensions/logical refresh,
the guest linear color format, and the enhanced renderer's progressive flag.
Its five-DWORD Xbox layout differs from desktop D3D. Reporting progressive output
and canonical lock pointers are documented HLE differences; ORIGINAL rendering
remains a separate build selection.

Set `XLLE_PILOT_TEXTURE_CAPTURE` to an existing private directory for diagnostic
GPU readback after each changed mip upload. Each file has eight little-endian
DWORDs (magic 0x58544558, version 1, native handle, mip, width, height, guest
format, payload bytes), followed by linear GPU storage bytes. Capture is absent
from normal upload execution. Never commit the resulting game-derived files.
Tests independently construct rectangular Morton images and compare GPU reads
for all four formats and the nine-level 128x256 chain, including changed writes,
view aliasing, parent lifetime, RAM failure cleanup and recreation.

## Native vertex storage checkpoint

The adapter borrows the main MIT donor's real vertex-buffer allocation,
Lock/Unlock upload and destruction. Its ignored Lock range/flags are hardened,
and missing upload destinations terminate. The GL `vb_Unlock` bodies found in
Phobos and NoRain return success without uploading and are excluded. Only
whole-buffer native uploads are used here; guest Lock2 returns actual shared
CPU bytes, preserving repeated/persistent pointer access. Future draw submission
must synchronize changed bytes even without a new Lock or stream rebind.

Created resources own a 12-byte guest header, guest-budgeted CPU allocation and
native D3D11 buffer. Registered vertex resources retain the caller's header and
contiguous allocation, including nonzero offsets and shared backing. Register
applies the original Data-address transformation; final Release destroys native
ownership while leaving external header/storage with the caller. Contiguous
storage cannot be freed while registered resources retain it. GPU readback
fixtures check actual bytes before/after persistent writes, external aliases,
reference counts, cleanup, allocation failure and device recreation.

`physical_memory.inc` implements the nxdk-declared `MmAllocateContiguousMemory`
and `MmAllocateContiguousMemoryEx` ABI plus `MmFreeContiguousMemory`. It owns
real committed bytes at the Xbox 0x80000000 CPU alias, respects page/alignment
and inclusive physical-address bounds, and shares the existing 64 MiB budget.
It supports READWRITE and WRITE_COMBINE|READWRITE; cache policy is logical while
native upload supplies the CPU/GPU handoff. This bounded physical namespace
does not model arbitrary CPU aliases, page tables or device DMA. It makes no
full-MMU or executed LLE-differential claim. Unsupported contracts still stop.

The actual title now allocates two 114688-byte vertex buffers, registers six
external buffers, and performs another 8192-byte contiguous allocation. The
first vertex-shader request follows real reads of both system DDS textures.
All three desktop CreateVertexShader adapters ignore the declaration, and the
main adapter expects raw microcode rather than this call's four-byte Xbox
header. Phobos's separate HLE layer handles those differences and is a useful
behavioral reference; its GPL implementation is not copied. The next section
records the native contract now exercised by Blinx.

## Native vertex shader checkpoint

Actual Blinx now creates all 91 vertex shaders, with programs from 11 to 135
instruction slots, and stops at pixel-shader creation. Fresh/repeated launches
reach 19818529/19818437 entries. This is initialization progress, not a frame,
attract-sequence or cadence result. The shaders retain real compiled D3D11
objects and input layouts; creation does not return a placeholder handle.
Guest handles are odd 32-bit guest addresses. The adapter preserves the
HRESULT/stack contract, output bounds, declaration fields/constants and owned
guest storage. Device release destroys the native objects and guest headers.
Constant application, shader binding and title draw submission remain
unqualified; storing a declaration constant at creation does not apply it early.

The main and NoRain donor HLSL decoders extract opcodes from word zero instead
of word one and cannot decode these actual programs correctly. This path uses
the existing LLE floor's `nv2a_vsh_cpu` decoder instead: pin `561fe80`, equivalent
source tree to the main build's `863fc0f` pin. Preparation checks the revision,
retains the Unlicense and the decoder header's MIT notice, and records source
hashes. The GPU translation adapts the main MIT donor's native HLSL operation
and register approach, handling separate output masks and paired operations
that read their inputs before either writes. RCC sign/range and all EXP/LOG
components follow the bounded LLE operation contract. Production executes
native GPU arithmetic; it does not execute the CPU reference alongside it.

Declarations retain actual register/stream/offset/format data. The reached
formats include float3/float4, packed 11/11/10 normalized normals, packed ARGB
colors and signed-short pairs. Native input layouts handle both streams used
by these programs. Additional admitted float/short/normalized-short formats
are listed in `graphics_shader_source.inc`; other formats, declaration-only
shaders and vertex-state programs still stop explicitly. Native floating-point
rounding is permitted. Extreme numeric inputs and full hardware rasterization
are not certified by the current comparison.

`pilot_shader_test` compares actual D3D11 vertex outputs against the LLE CPU
interpreter in a separate diagnostic process. It captures raw outputs through
[D3D11 stream output](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-creategeometryshaderwithstreamoutput),
before rasterization, using a deferred context that preserves immediate state.
The synthetic fixture also checks independently specified packed/color/short
input results and a paired read-before-write dependency. Malformed opcode and
early-END fixtures must stop before the borrowed decoder's opcode table access.

All 91 actual captured Blinx programs pass on eight test vertices each:
27664 finite output components, maximum `abs(native-reference)/max(1,abs(reference))`
of `5.96046448e-08`, against an allowed `2e-4`. This is an executed differential
for bounded vertex-program outputs, not a full rendered-frame LLE comparison.
Optional `XLLE_PILOT_SHADER_CAPTURE=<existing private directory>` records real
declarations, microcode and generated HLSL for replay by `pilot_shader_test`.
Captures remain ignored under `build/`; ordinary title execution omits capture
and readback. Evidence: `build/shader-contract-tests.log`,
`build/blinx/shader-reference-results.log` and
`build/blinx/shader-validation.aot-private.json`.

## Reproduce privately

### Vertical blank and actual sound-buffer progress

`D3DDevice_BlockUntilVerticalBlank` is qualified by the original 36-byte body:
it clears a notification event and calls the nonalertable, indefinite
`KeWaitForSingleObject`. It submits no graphics work. The native whole-API
implementation waits for the next edge of a common monotonic display epoch,
using [Windows waitable timers](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).
It releases the graphics ownership lock while waiting. Each waiter retains its
own deadline, so one thread cannot consume another's wakeup by immediately
waiting again. Integer deadline calculation avoids repeated rounded sleeps and
accumulated phase drift. Host failures stop; success follows elapsed time.

The nominal 60 Hz matches the current machine LLE clock in
`src/machine/machine.cpp`. This is not physical NTSC field timing or measured
game cadence; interlace, PAL, callbacks and mode changes remain unqualified.
Callback registration still stops explicitly. Device release with active waits
also stops. Ordinary native presentation has not yet been unified with this
clock; that remains necessary before making an original-speed claim.

Phobos's multi-waiter notification-event findings informed the contract.
Its implementation was not copied. The main sp00nz donor's fixed 16 ms IRQ
path was also inspected; the native API does not use it. NoRain supplied no
additional whole-API wait implementation. Existing graphics tests now cover
deadline boundaries, long uptime, concurrent waiters, actual timed guest calls,
register/stack preservation and exact timer-handle reclamation. All 27 CTests
pass. Private evidence is `build/vblank-contract-tests.log`,
`build/blinx/vblank-validation.aot-private.json` and
`build/blinx/run-vblank-{fresh,repeat}.log`.

That checkpoint's effects-image stop is now passed by the integrated
[MIT DSP core and native transport](../../docs/NATIVE-DSP-DONOR-2026-10-07.md).

### Native audio and media-worker checkpoint

Select `-DPILOT_AUDIO=NATIVE` after running `tools/native_audio_binding.py`
with `--xbe`, `--output` and `--symbols`; full preparation accepts
`--audio-symbols`. The binding checks the full NTSC image and each original
body, verifies stdcall cleanup and derives the object vtables from reviewed
constructor bytes. Its 21 entries retain the original bodies for `ORIGINAL`.
Runtime service code contains no title function addresses.

The initial adapter reuses the actual Windows DirectSound8 engine for 2D PCM storage,
mixing, rate conversion, looping and device-paced cursors. It supports mono or
stereo, 8/16-bit PCM, shared device ownership, guest-owned or caller-owned data,
contiguous/wrapped lock uploads, play/stop/seek, volume/headroom and
frequency/pitch. Errors propagate from real allocations and device operations.
There is no silent output/clock fallback. Native COM pointers stay outside
guest memory. Xbox descriptors are **24 bytes**, not the desktop descriptor
or the donor's 28-byte declaration. The device interface is at object +8 and
the buffer interface at +28; original bytes establish the reference-count,
vtable and public-call shapes.

Current bounded differences and missing coverage:

- Host mixing/resampling and output latency can differ from the Xbox APU.
  Buffers use the original default 600-hundredths-dB 2D headroom. LOCDEFER may
  allocate native storage earlier. Cursors reflect actual host audio progress.
- Borrowed data is uploaded on attachment and Play; writes through Lock/Unlock
  upload their real spans. Arbitrary concurrent writes through a retained raw
  data pointer are not qualified as coherent streaming. Detached zero-length
  storage, partial loop regions, Xbox ADPCM, streams, mix-bin routing
  and 3D remain unqualified. Reached unsupported API/format requests stop.
- The LLE APU currently proves bounded voice/idle/IRQ behavior, not full PCM
  output. These tests establish ABI, ownership and actual host playback; they
  are **not** a full hardware/LLE audio differential. Game-audio equivalence
  and perceptual tolerances remain open until title audio is exercised.

All three donors were inspected for this family. MIT sp00nznet `1409a7d` and
NoRain `7adaf21` have useful API organization but also success stubs and missing
stream/effect work. Phobos `63f18f0` has device-clock/buffer/packet architecture,
but its GPL implementations and silent unsupported paths were not imported.
Windows DirectSound provides the reusable engine here; the Xbox adapter is
independently authored. Microsoft documents the
[buffer creation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418039(v=vs.85))
and [actual play/write cursor contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418062(v=vs.85)).
Xbox IMA ADPCM must not be passed to XAudio2 as Microsoft ADPCM.

### Integrated effects transport

`PILOT_AUDIO_BACKEND=DSOUND` retains the first backend. Build-time `XAUDIO2_DSP`
selects actual XAudio2 voices and the pinned guarded MIT processor for downloaded
effects. Prepare the donor and build its locked library:

```powershell
py -3 tools/native_dsp_donor.py --donor <pinned-donor> --output build/dsp-donor
& "$env:USERPROFILE/.cargo/bin/cargo.exe" build --release --locked -p dsp56300-emu-ffi --manifest-path build/dsp-donor/Cargo.toml --target-dir build/dsp56300
```

Set `PILOT_DSP_SOURCE` to the prepared directory and `PILOT_DSP_LIBRARY` to
`build/dsp56300/release/dsp56300_emu_ffi.lib`. Rerun `native_audio_binding.py`
to add reviewed decoder/location metadata to an existing generation. Whole
`PILOT_AUDIO=ORIGINAL` remains available; backends never switch during execution.
Replacing an effects image changes owned graph data at the native processing
boundary. The DSP audit records transport latency, first-use compilation,
output mapping and remaining hardware qualification.

Private actual-asset qualification uses `pilot_dsp_test <encrypted-image>
<generated-seed-hex> <private-original-decoder-output>` and
`pilot_contract_test audio-effects <encrypted-image>`. These test real graph
execution/native output, optional arguments and replacement/lifetime behavior.
No title asset or key is committed. The shared adapter is independently authored;
the private CPU pilot retains its existing GPL dependency and release limits.

Media-worker startup now uses real thread-object references, priority/boost
controls and suspend/resume counts. A reference owns a duplicated native handle
and survives `NtClose`; `PsThreadObjectType` is an opaque type identity, with
unqualified private layout access faulting. Windows priority classes approximate
guest relative priorities; exact retail scheduling order is not claimed.
Suspension takes effect at generated function entries and backward-branch
targets, after any in-flight native service returns. It never freezes a host
thread inside an allocator, I/O or runtime lock. Self-suspension waits without
holding the ownership lock. The existing 63 lifetime thread-slot bound remains;
thread-slot reclamation and broader object types are still open.

`tools/native_thread_checkpoints.py --output <repo>/build/blinx` upgrades an
existing hash-verified generation; full preparation and extent recovery retain
the pass. This image has 7354 loop targets. Four synthetic tests verify scope,
conditional branches, missing-target rejection and idempotence; six extent
tests also pass. Normal checkpoints perform a TLS/count read, with a blocking
wait only when suspension is requested. This is not instruction-history
comparison or duplicate guest execution.

Evidence: `build/audio-family-contract-tests.log`,
`build/blinx/audio-family-validation.aot-private.json`,
`run-audio-family-{fresh,repeat}.log` and `run-audio-original.log` in that private
directory. The PCM fixture copies exact bytes through real host buffers, plays
a one-shot to completion, observes advancing cursors, loops/stops and returns
guest storage to its baseline. It does not establish audible game output.
That earlier checkpoint stopped at vertical-blank waiting. The newer checkpoint
above establishes actual title buffer creation, with effects installation still
pending. Title draws, playback, movies and attract remain unvalidated.

### Memory accounting and input-startup checkpoint

`MmQueryStatistics` follows the nine-DWORD, 36-byte
[nxdk ABI](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h).
It reports this runtime's actual committed guest pages and reservation ledger,
including image residence, contiguous allocations, kernel/context backing,
thread/dispatcher stacks and the real file cache. Host graphics storage and
implementation overhead remain outside the guest 64 MiB budget. Cxbx's external
field semantics informed the mapping; its implementation is not incorporated.
The values are not retail-kernel allocator overhead or host free-memory values.
Snapshot locks cover concurrent guest commitment and allocation metadata.

The pool service implements `ExAllocatePool`, `ExAllocatePoolWithTag`,
`ExQueryPoolBlockSize` and `ExFreePool` with real guest memory and ownership.
Small allocations share committed pages; large allocations are page-aligned.
Guest payload sizes follow the reviewed 32-byte block/eight-byte overhead
contract below the 4056-byte boundary. An independent host ledger replaces
private kernel allocator structures. Final page owners decommit storage; budget
failure returns null without disturbing live allocations. Unknown/double frees
and zero-sized allocations stop. Pool pages participate in the same admission
and statistics model; no allocation success is fabricated.

Original `Get2DSurfaceDesc` also revealed a prior descriptor error: usage is
derived from format capability flags at level zero, even for sampled textures.
Those flags now come from the hash-bound image's table in the generated binding.
Display surfaces report their actual configured dimensions, pitch, format and
multisampling. Tests check output guards and the texture-versus-surface cases.

`tools/native_function_extent.py` incrementally re-lifts a reviewed extended
function using the pinned Phobos translator. It verifies image/source hashes,
rejects overlaps/non-executable ranges/incomplete decode, and changes no game
bytes. In this image, initializer `0x000e8c30` actually ends at `0x000e8c69`:
the old discovery stopped before the loop reached by its forward jump.
`build/blinx/function-extents.aot-private.json` retains the reviewed correction;
full `native_pilot.py` preparation replays it. Six synthetic tests exercise the
extent guards and metadata replay. This remains the private GPL-dependent CPU
pilot; the recovery tool does not change its distribution status.

Evidence: `build/blinx/memory-family-validation.aot-private.json`,
`build/memory-family-contract-tests.log` and `build/blinx/extent-recovery/`.
The next coherent boundary is native input initialization/enumeration at the
Xbox API, with actual host controller state, while retaining original USB
driver execution in the reference path. Returning success from `IoCreateDevice`
without a functioning device is not an input implementation.

### Pixel definitions and compressed texture progress

The original NTSC `CreatePixelShader` at `0x00136200` allocates 252 bytes,
initializes two ownership fields to one and copies the complete 240-byte
definition behind its pointer field. It performs no GPU submission or shader
compilation. The native adapter preserves that layout, a separately owned copy,
the two-argument stdcall/HRESULT contract and untouched output on allocation
failure. `XLLE_PILOT_PIXEL_CAPTURE=<existing private directory>` optionally
records definitions. Compilation is deferred until use; unqualified binding
still stops. These 30 creations are not 30 validated native pixel programs.

The MIT donor already maps Xbox DXT1/3/5 to native BC1/2/3 and uploads compressed
blocks directly. The adapter now preserves the original block-row lock pitch,
at least one block per mip, logical descriptor dimensions and shared view
ownership. Original `Get2DSurfaceDesc`, `GetSurfaceLevel2` and lock helpers
provide the guest layout reference; native block formats follow
[D3D11 block compression](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11).
Tests read back actual GPU block bytes at every mip, including 1x1 tails, change
data through persistent views and verify reupload and release. Compressed
subrectangle locks and compressed base dimensions below 4 remain explicit
unsupported cases. Ordinary uploads perform no diagnostic GPU readback.

Xbox `MEM_NOZERO` (`0x00800000`, nxdk's header) permits unspecified initial
contents. Native commitment may still provide zeroed pages; it preserves existing
committed contents, real access protection, allocation failures and the shared
64 MiB budget. The Xbox bit is never passed to Windows as `MEM_ROTATE`.
This is a documented HLE initialization choice, not a promise to reproduce
physical-RAM remnants.

Private evidence: `build/blinx/run-pixel-dxt-desc.log`,
`build/blinx/dxt-capture/`, `build/blinx/pixel-capture/` and
`build/blinx/pixel-texture-validation.aot-private.json`. Fresh/repeated runs
reached the same `MmQueryStatistics` barrier at that revision. No frame, attract loop or
original-speed result follows from this startup checkpoint.

### Decoder recovery

`tools/native_jump_binding.py --xbe <default.xbe> --output <repo>/build/blinx
--symbols <default_xdk_symbols.json>` qualifies the original CRT by body and
image hashes. It retains guest register save/restore, SEH and notification work,
then transfers the native stack using the MIT donor's adapted jump-slot bridge.
Generated caller returns release their slots; nonlocal transfer invalidates
younger slots. The current bound supports three identified setjmp callers and
two longjmp callsites. Unknown shapes, recursive jumps during unwinding,
unmatched/corrupted buffers and capacity exhaustion stop. General guest exception
dispatch/global unwinding is still unqualified where it reaches missing services.

`tools/native_cpu_binding.py` with the same arguments binds the decoder's whole
zero-argument cdecl MMX query. Select `-DPILOT_CPU_PROBE=NATIVE` to query the
configured Xbox CPUID profile; `ORIGINAL` retains the original flag-toggle
probe. Native output is the Boolean capability, with callee-save registers and
stack cleanup preserved. Incidental EFLAGS mutation is outside that HLE API
contract. The general arithmetic-flags lowering remains unsupported.

For full regeneration, pass both `--jump-symbols` and `--cpu-symbols` to
`tools/native_pilot.py` alongside the existing media/cache/graphics bindings.
Runtime fixtures exercise real host jumps and failure contracts; actual Blinx
executes the guest CRT and returns from JPEG rejection into texture setup.
This is not a claim of an executed LLE differential or a complete SEH model.

From the repository root, using your pinned donor checkout and owned inputs:

```powershell
py -3 tools/native_pilot.py --donor <phobos-checkout> --xbe <default.xbe> --output <repo>/build/blinx --data-sections 'MDL*,MAP[0-9]*'
cmake -S pilots/native-title -B build/blinx/host -G 'Visual Studio 17 2022' -A x64 -DPILOT_GENERATED=<repo>/build/blinx/gen
cmake --build build/blinx/host --config Release --parallel 4
ctest --test-dir build/blinx/host -C Release --output-on-failure
build/blinx/host/Release/xlle_native_title.exe <default.xbe> <eeprom.bin>
```

Use a separate `build/jsrf` directory and omit `--data-sections` for JSRF.
Generated code, game bytes and logs stay ignored under `build/`. Each executable
checks the complete XBE SHA-256 before loading. The output directory also pins
its image identity so two `default.xbe` inputs cannot silently share discovery.
The data-section exclusions are an explicit Blinx analysis hypothesis, not a
runtime patch or a universal rule. Unknown transfers remain visible failures.

Local evidence: `build/blinx/run-timers.log`, `build/jsrf/run-timers.log`,
`build/timer-contract-tests.log`, and each `timer-validation.aot-private.json`.
The earlier `run-final.log` and `generation.aot-private.json` retain the previous
startup/generation evidence. The newer Blinx VM evidence is
`build/blinx/run-memory-final.log`, `build/memory-contract-tests.log` and
`build/blinx/memory-validation.aot-private.json`. No source-share writes
or game patches were needed. This pilot never executes the donor's fake DVD
authentication or unconditional GPU acknowledgement paths.
