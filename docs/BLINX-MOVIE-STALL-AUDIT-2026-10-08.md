# Blinx movie stall investigation, 2026-10-08

`beads-xbox.1.98`, with the scheduling regression tracked in `.71`.
Branch: `investigate/blinx-audio-tail-20261008`, based on `57417e6`.
These experiments do **not** establish an original-speed or complete A/V pass.
The validated baseline retains the independent display-wait correction.

## Follow-up: the quiet tail is present before HLE playback

Branch `investigate/blinx-audio-ingress-20261008`, based on scheduler candidate
`1d563af`, adds an opt-in exact PCM capture. `XLLE_PILOT_AUDIO_INGRESS=<new file>`
requires the existing audio capture. It records caller writes and successful
XAudio2 submissions under audio ownership into a bounded 128 MiB allocation;
file output happens after detachment, outside that lock. Each native buffer
has a unique capture ID so allocator reuse cannot merge lifetimes. Overflow
is explicit. Playback, decoding, cursor and completion semantics are unchanged.
The initial 64 MiB observation overflowed and was rejected as complete evidence.

The replacement `movie-audio-ingress-full` observation stores 68,264,228 bytes
with zero overflow. Opening voice 6 contains 3,333,960 submitted stereo frames
at 44.1 kHz; all 7,560 submissions match the latest caller-written ring bytes
exactly. The independently decoded ADX reference differs already in those
guest-supplied bytes: source second 74 has half-second RMSE 57.10/64.31 PCM16
units, including a mean difference of 50.30/57.36. This precedes the native
resampler, mixer and GP processing. The original game decoder remains executed;
FFmpeg's independent decode is not an exact-output oracle for that decoder.

Resampling the *captured game PCM* to 48 kHz for comparison instead gives
post-GP output RMSE 0.0000204/0.0000206 full scale at source second 74, with
correlation 0.99775/0.99776. At second 75, RMSE is 0.0000116/0.0000111.
The comparison uses one fixed 21.030667-second capture offset, not a new
quiet-tail alignment. These are measured differences, not a blanket audio
tolerance. Different resampling filters and physical speaker latency remain
outside this comparison. No HLE correction is justified for the quiet-tail
difference against the independently decoded reference.
Across the entire 75.541-second source interval, the comparison against captured
game PCM has RMSE 0.00803/0.00797 full scale and correlation 0.99943/0.99949.
The maximum sample error is 0.189/0.151; the quiet-tail figures alone must not
be presented as the whole movie's error bound.

The same run observes four queued chunks in all 4,533 opening position samples,
no empty queue during playback, and a maximum fresh-write gap of 17.9 ms.
Uploaded video reaches frame 2263 with luma RMSE median 0.146/p95 0.224.
Nonflat source seconds 2..73 have median picture lag 31.8 ms, p95 42.9 ms and
maximum 168.1 ms relative to the mixer clock. The fitted wall/source slope is
1.000058. Static/repeated images make nearest-frame matching ambiguous; the
first final-frame match is still 76.176 seconds after audio starts, followed
by 0.546 seconds of final-frame presentation. Do not turn whole-phase duration
into a uniform slowdown estimate or claim complete A/V timing qualification.

Private evidence: `build/blinx/movie-audio-ingress-full/`, executable SHA-256
`83eecee63d6109177ac27652be86d09beb881649b60fa5eaf9eb73eec1870d8f`.
Reports: `ingress-comparison.aot-private.json`,
`transport-comparison.aot-private.json`, `av-timing.aot-private.json`,
`video-comparison.aot-private.json` and `timeline-summary.aot-private.json`.
`tools/native_audio_ingress.py` independently verifies/extracts the capture:

```powershell
py -3 tools/native_audio_ingress.py <ingress.bin> --log <run.log> --buffer 6 --report <new-report.json> --pcm <new-opening.s16>
```

The buffer ID is observation-specific, not a title binding. Select it from the
file-open/timeline interval. The verifier rejects overflow and incomplete files;
submission is not consumption, and a buffer can have multiple play intervals.
Captured/extracted title data remains private. The broader resource, repetition,
video timing and gameplay cadence requirements remain open.

## What the new observation rules out

`XLLE_PILOT_AUDIO_TIMELINE=<new CSV>` accompanies the existing PCM capture.
It stores at most 100000 records under the existing audio owner lock. The
capture worker detaches the array and writes it after capture, outside that
lock. Each record has the real voice sample count, ring offsets, queue depth,
operation, source rate and caller CPU time. Overflow is reported explicitly.
It does not change completion, advance a cursor, duplicate playback or keep
recording indefinitely. `XLLE_PILOT_AUDIO_CAPTURE` is required to start it.

The opening's native queue does not run empty in the captured observations.
Initial `empty` events occur before Play at zero samples. In the first trace,
4546 position samples all see four chunks queued, with no polling gap over
50 ms. Fresh decoded PCM writes nevertheless pause for 550 ms alongside a
790 ms present gap. Increasing the native queue has no evidential basis here.

## Controlled comparisons

Private evidence is under `build/blinx/<label>/`, with the matching
`run-<label>.log`, run manifest, executable hash, PCM/clock capture, timeline
and sparse uploaded-frame grids where enabled.

| Observation | Result | Decision |
| --- | --- | --- |
| `movie-null-log` | Same executable with stdout/stderr discarded still has a 703 ms present gap, 400 ms write gap and a 45.35 ms audio alignment step. | Redirection alone does not resolve the failure. Guard logs are unavailable in this run. |
| `movie-gpu-calls` | No instrumented query/create/end/flush call exceeds 50 ms, despite a 1.03-second present gap. | Do not assume the native driver is spending that interval inside these calls. |
| `movie-host-phases`, `movie-wait-phases` | Callback barriers sometimes wait hundreds of milliseconds before the first callback poll. Swap also stalls outside its instrumented donor Swap/CopyResource calls. One present-log write takes 89 ms. | Trace host waiting/logging separately from GPU execution. |
| `movie-multicpu` | Opening reaches final frame 2263 in 76.660 seconds, but stalls after the movie in the previously observed thread-priority path. Audio still shifts by 315.65 ms. | Retain the single-CPU configuration; removing affinity is not a fix. |
| `movie-events-off` / `movie-events-on` | Same executable: opening presentation spans 77.1135 / 76.7232 seconds; both reach frame 2263 with luma RMSE median 0.146, p95 0.224. Off still has a 1.055-second present gap. | No sustained speed improvement established; keep the experiment off the validated baseline. |
| `movie-local-media` | Same executable/settings as `movie-events-off`, with only the media root changed. Opening reaches frame 2263 in 76.3726 seconds. Sampled audio windows 0..72 seconds retain one offset; maximum write gap falls from 733 to 114 ms. A 574 ms picture gap and tail mismatch remain. | Storage is a useful lead, not a complete fix; repeat before attributing the improvement. |
| `movie-local-repeat` | Opening reaches frame 2263 in 76.3429 seconds; sampled audio 0..72 retains one offset. Maximum fresh-write gap is 22.0 ms and present gap 73.2 ms. | Reduced producer starvation repeats with local media; tail mismatch remains. |
| `movie-local-visible` | 260-second visible run, Start at 190031 ms, 51 captures. Opening reaches frame 2263 in 76.3760 seconds; sampled audio 0..72 retains one offset; maximum fresh-write gap 22.7 ms and present gap 91.2 ms. Captures show 3D gameplay, then Press START after interruption. | Confirms visible menu return. Another demo begins loading before the deadline, but rendered reentry is not captured. This is not a complete repeat or cadence pass. |

The event-trace pair uses executable SHA-256
`2867894f49fc13b4965a192271e2f9344481c3efe23ac264c18159b21b672312`.
Both runs disable GPU screenshots and per-buffer/service text profiling.
They use the same CPU15 affinity, media, PCM/timeline/YUV capture settings.
Host load was not controlled; this single pair cannot quantify small changes.
These windows were hidden, so they do not qualify visible scan-out cadence.

The diagnostic event gate is selected at startup:
`XLLE_PILOT_EVENT_TRACE=1` restores present, vblank, GPU-callback, texture-upload
and asynchronous-read completion text. On this investigation branch the gate
defaults off. File opens, write completions, errors and explicitly requested
profiles/captures remain available. File-open records now include a monotonic
timestamp so movie phases do not require a per-present text log. All 35 native
tests pass with event tracing off (16.93 seconds). The later timestamp-only
change was rebuilt and exercised by both full-movie observations.

`XLLE_PILOT_GPU_CALL_PROFILE=1` reports instrumented native calls over 50 ms.
`XLLE_PILOT_SERVICE_PROFILE=1` also separates renderer ownership, callback
barrier, implementation and unlock time, and reports long display-timer and
present-log calls. These are cause diagnostics, not production timing evidence.

## Limits and remaining work

The queue stays supplied with real ring contents; that does not mean the guest
has supplied fresh contents on time. Full waveform comparisons still find
variable alignment steps. The low-level tail difference is almost identical
across several runs after alignment. The subsequent exact ingress capture above
establishes that this particular difference already exists in the game's
decoder output; it does not establish correctness of every decoder, mixer or
gameplay-audio path.

One triggered stack snapshot lands in generated movie decoding. A separate
snapshot of the actual multicore post-movie stall finds the main/media paths
in `sub_000F8178` / `GetThreadPriority`, with another worker suspended at its
real checkpoint. Debugger attachment suspends execution briefly. Those runs
are cause evidence; measurements after attachment are not cadence evidence.

The full local installed-media copy contains 3197 files / 2235000402 bytes.
Every source/local file SHA-256 matches; the private manifest is
`build/blinx/media-local-validation.aot-private.json`. No title file was
synthesized or modified. The first comparison above changes only the media
root. The repeat and visible run preserve the reduced starvation. The visible
run has no logged terminal guard/fault. Private memory is 710.2 MB at 180 seconds
(gameplay), then 683.7/683.9 MB at 210/240 seconds (menu), with 614/615/615
handles. Different phases and capture-buffer release prevent treating those
numbers as a repeat-loop resource qualification. The harness terminates its
own child at the observation deadline; that exit is not natural game completion.
The timeline analyzer separates Play/Stop lifetimes because native allocation
addresses can be reused after destruction; address alone is not a buffer ID.
The full Blinx repeat/input/resource/cadence goal remains open; JSRF has not
started. No source-queue enlargement, fake completion, discarded audio or
clock substitution was implemented.

## References and attribution

Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` was reviewed for real XAudio2
queue ownership (`src/hle/audio_output_xaudio2.cpp`), buffer clocks and stall
handling (`src/hle/dsound_buffer_model.c`), and cached diagnostic selection.
Its elapsed-time buffer consumer discards long stalled intervals; that behavior
was not adopted. The diagnostics and event gate here are independently authored.
Existing borrowed rendering/DSP material retains its prior attribution and
the private pilot's shipping exclusion.

The next donor candidate is Phobos
`05040ec103dfbaca5e3daed28bbc569bd896f59f` (fetched 2026-10-08; the prepared
renderer remains at its qualified pin). Its shared guest scheduler documents
the same CRI ADX priority/suspend race seen in our affinity-off run. Relevant
files are `src/kernel/xbox_memory_layout.c`, `kernel_bridge.c`, and
`kernel_thread.c`: strict priority selection, immediate readiness on resume,
cooperative suspension, and retaining a CPU reservation across nonblocking
calls. Remote suspension must not hand the CPU to a lower-priority thread
between suspending the spinner and restoring the caller's priority.

This is a candidate, not an adopted or validated fix. The donor also has a
500-ms starvation escape, expiring nonblocking-call reservations, and a timed
callback acquire that can run guest code without the lock. These escape paths
are incompatible with treating the scheduler as our asserted contract. Review
and adapt the family without importing those behaviors. Upstream performance
claims are not measurements of this pilot. No new donor body was imported in
this investigation.

Follow-up: `fix/blinx-priority-scheduler-20261008` implements a bounded native
scheduler using this behavioral lead and unmodified original-kernel queue
tests. Its matched same-executable local-media comparison reproduces the
post-opening stall with scheduling off and advances into the title movie
with it on, both without host CPU affinity. A separate visible 330-second run
preserves input return and rendered reentry, with 63/63 writes. This is
functional qualification, not a speed or complete attract-loop pass; the native
pilot README records remaining scheduler scope and actual evidence. The fixed
audio tail difference persists in both scheduler configurations.

[FFmpeg's ADX decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adxdec.c)
was reviewed to distinguish the independent decoder comparison from the native
PCM service contract. No FFmpeg decoder implementation was imported. Its
comparison output alone does not prove the original Xbox decoder's exact tail.

## Current default HLE opening acceptance - 2026-10-11

The native priority/readiness scheduler is now the default when
`XLLE_PILOT_GUEST_SCHEDULER` is unset. Explicit0 remains a diagnostic comparison;
the selected original-kernel scheduling build disables the native scheduler so
the actual kernel retains sole CPU ownership. The maintained scheduler body,
original movie decoder, queue sizes and timeout rules are unchanged. Earlier
working movie runs explicitly selected1; an unset current native v61 observation
still stopped after Microsoft. The correction is required default behavior,
not an elective setting.

The fresh immutable muted v62 observation runs193.875s, ending at the harness
deadline after reaching Microsoft, Artoon, op01, h_title02a and gameplay. It
has no terminal guard/fault. One opening completes normally: its distinctive
reference frame2263 appears in36 real presents with luma RMSE0.09449; the real
op01 voice consumes3,332,197 frames/75.560s, stops, and is destroyed at96.894s.
The next title movie opens at97.399s. Reference terminal PTS is absent and remains
null; actual frame content, voice lifecycle and transition support completion
without inventing an encoded EOS timestamp.

All36,347 ingress records are complete; every recorded submission equals its
prior caller-owned ring bytes, with zero mismatched/unknown bytes or overflow.
The opening voice retains7,560 submissions/13,335,840 bytes at44.1kHz. Current
149.96s PCM capture is finite, and fresh fixed-offset output versus nominally
resampled current caller PCM has full75.6s RMSE0.008011/0.007947 and correlation
0.999434/0.999491. Quiet74s windows have RMSE about0.0000193/0.0000196. These
are current mixer-output comparisons, not inherited metrics from the earlier GP
scratch packing. There is no per-window realignment or assumed exact independent
ADX decoder identity. Representative source pictures reach the actual final
frame;850 separated nonflat A/V matches have median28.1ms, p9542.3ms and
max74.1ms error within the captured mixer/present clocks. Physical monitor/
speaker latency and exact scanout are outside that observation.

Private final proof is
`movie-default-v62-analysis-20261011/qualified-offline-opening-final.aot-private.json`
(SHA256 `1749136daaa56722319f92e4dc5cfa7db819a4401091dc0c3292aaa25819d58b`).
It verifies25 run artifacts and104 frozen snapshots across52-input pre/post
source manifests. Those manifests differ solely in thread_scheduler.inc.
The earlier v61 audit captured current source after the concurrent default edit;
it is not relabeled as pre-edit native compiler provenance. Its unchanged
negative runtime artifacts remain valid, while complete prior native build
identity is explicitly limited. The old original-LLE executable is not used as
the old native executable. The first parser failure and corrected missing-PTS
handling are retained.

This qualifies one current default-native opening and normal title transition.
Whole movies/A-V remains open for current interruption/menu/reentry, more than
one complete opening lifetime and composed-original-LLE startup/pacing. The
minimal source-bound acceptance plan is retained under `.73.1`; no further
human still/ear review or playback run is needed to establish this bounded case.
No physical Xbox operation or audible playback occurred.

## Current default HLE repeat and interruption - 2026-10-11

One immutable current default-native observation now passes two natural op01
lifetimes and a real Start interruption during the third. The scheduler setting
is unset; files/storage/memory/display remain native. The550.109s muted,
invisible observation ends at its explicit deadline with no STOP/guard/fault.
No game decoder, queue size, timeout, voice clock or GP state was replaced.

Distinct voices6/369 consume75.5800/75.5700s, both reach the distinctive final
source picture2263, then Stop/Destroy and transition to the next title movie.
Each voice has exact initialized caller-ring byte provenance and its own full
fresh GP-output comparison: RMSE0.008010/0.007946 and correlation
0.999434/0.999491. One fixed alignment spans each actual Play lifetime and quiet
tail. Active-picture A/V medians are31.66/28.68ms, p9543.17/39.89ms within
captured producer clocks. These nominal-resampling residuals are measured
differences; they are not a universal original-silicon tolerance. The caller
streams agree through75.5414059s;64 later bytes and prefetched lengths differ.
Both comparisons use their own actual PCM, without claiming identical tails.

Actual Start/release arrive at445016/446016ms during the third opening.
Voice732 consumes9.8200s, stops and is destroyed once, with no subsequent
same-ID ingress. Title/menu assets, actual `demoplay/r01s01demo.dat`,61 completed
save writes and numeric captures24/26 support title return and fresh demo
reentry. Capture26 shows gameplay with the previously reviewed pause overlay.
This does not assert manual menu navigation or completion of that later demo.
Existing focused lifetime contracts are reused; instantaneous VM/process leak
baselines and full attract stress were not measured in this run.

Three finite backend-relative output windows complete without overflow,
partial records or nonfinite PCM. The late-window byte snapshots are tested
against authored live/new rings; unknown initialization now fails verification.
One unrelated mono gameplay voice359 has two custom-loop observations/682bytes
that the capacity-wrap verifier cannot interpret without actual loop-span
metadata. This is explicitly excluded from the movie-voice proof and tracked
in `.113.20`; it is not declared a playback defect or silently passed.

Final private proof
`movie-repeat-20261011/qualified-native-repeat-final.aot-private.json`, SHA256
`053fd458bbdff57e4b7487ef6c6c5ec58c7a0c79b69145968ee361f135519698`,
binds348 primary artifacts, eight reference inputs and231 retained snapshots.
The native build has343 unchanged pre/post identities. Stronger control proof
`qualified-capture-controls-v2.aot-private.json`, SHA256
`53995f8c887e8c3b8521e724226d63ba63b8d1031e9e94f8e2652b8cc2fff31f`,
retains the causal old-verifier false pass and new unknown-byte rejection.

The current default-HLE repetition and interruption subgates are complete.
This also satisfies `.98`, the specific HLE ring-cycle/stalled-opening bug:
the independent display wait, default scheduler and current producer-readiness
fixes now have two fresh complete waveform/video lifetimes and stable replay.
Closing that bug does not close the movie integration family.
The whole movie gate remains open for completed composed-original-LLE movie
execution and pacing. Physical speaker/monitor timing stays separately scoped.
No new still/ear review, physical Xbox operation or audible playback was needed.

## Composed-original movie observation and consumer replay - 2026-10-11

The rebuilt original profile now runs901.984s to its explicit observation
deadline without STOP, guard or fault, past the former325.422s interpreter
allocation failure. Its1,018 accepted-present luma records identify forward
Microsoft frames3->298, Artoon1->96 and opening0->2119. The opening reaches
sourcePTS70.704667 over662.271s of captured present wall time. Final picture2263,
natural voice retirement and the subsequent title transition are not established.
The deadline is not a completed movie.

All three250s audio windows finish with complete records and zero
ingress/timeline overflow. Independent PCM witnesses identify the actual
Microsoft, Artoon and opening lifetimes; reused host pointers and familiar
ring shapes are not phase identities. Every recorded submission agrees with
the initialized, caller-owned ring, with zero unknown or mismatched bytes.
That ownership result does not establish timely replenishment:

| Opening observation | Window2 | Window3 |
|---|---:|---:|
| Fresh source progression |28.3632s|27.6898s|
| Wall time between fresh-source witnesses |247.4068s|248.9518s|
| Successful bytes resubmitted before another caller write |89.438%|89.987%|
| Actual consumption |about250s|about250s|

The actual sink advances near44,100frames/s with queued buffers. At the last
captured opening position it has consumed564.230s since Play, while the latest
identified caller packet is only at source62.386s. The late windows contain no
seek, stop or destroy that could explain this as a new lifetime. Current
post-GP witnesses match their actual submitted stream; they do not remove
the sustained replay or establish full source/output synchronization. This
observes a throughput deficit, without proving a decoder or byte-transport
logic defect. An unchanged longer run could reach EOS but cannot establish
the missing A/V contract.

Root proof `original-display-pulse-20261011/qualified-observation-v75.aot-private.json`
has SHA256 `7cdaa89bfb96d22b97145371e40dc4955d747ed0e19bdc4f4eb405add7119cf3`.
It checks264 unique current/frozen identities, the72-input compiled generation,
the actual run and all three completed windows. The retained audio report
`v75-audio-analysis/qualified-v75-audio-v2.aot-private.json` has SHA256
`ff06fae3997d964a0af58a08a410cec7f2b11b70bff07232ba34c82f2638ec4a`.
Earlier pointer-join and periodic-alignment drafts remain separate; their
incorrect associations are superseded by the lifetime-bound final analysis.
The native repeat/interruption proof above is reused unchanged.

The owner's LLE requirement is correctness, without real-time performance.
The next task `.123` is therefore coherent reference time, rather than another
longer playback observation. The original transport already accepts an input
clock; the maintained machine drives PIT/display/APU from selected machine
time. This private mixed profile instead supplies wall-derived original time
and an independently device-paced XAudio2 consumer. A qualification timeline
must distinguish host observation time from simulated device time and govern
the original reference clock and its software audio consumer together. It must
retain original execution, real ring/cursor ownership and genuine completion;
it must not advance time according to producer writes, fabricate completion,
discard missed samples or realign separate windows to hide replay.

Use bounded common-time cases with deliberately different host execution
delays before the next complete original movie lifecycle observation. Keep
the default HLE live device path performant and reuse its current qualification.
This does not authorize a canonical machine TSC/replay re-baseline or qualify
retail GP/EP boot/output. It is software reference-composition work, rather
than a hardware precision question. `.73` and the whole movie gate remain
open; `.98` and the bounded allocator correction remain closed. No console
operation or audible playback occurred.

## Controlled reference clock and audio consumer foundation - 2026-10-11

Task `.123` now has a maintained controller-step seam separate from its pure
clock observer. Two original-kernel timeout cases pass with identical actual
PIT/IRQ/wait progression:35 explicit input steps,39,294 observed engine block
entries and35ms guest elapsed. The delayed case adds35 host stalls and70
observer reads; neither advances the input clock. The combined current batch
passes90/90 in60.43s. Default runtime QPC and native HLE audio remain unchanged.
The100-block fixture opportunity is an authored input schedule, with no claim
about retired instructions, hardware cycles or canonical TSC policy.

The retained failed experiments matter to composition. A fixed100ms input jump
exhausts the bounded original callback budget; automatically limiting each
controller pump to the next PIT deadline also fails. Time cannot be advanced
at every observer/pump opportunity without allowing original IRQ work to run.
Both experimental generations are excluded and restored; no PIT interrupt
count or fabricated wait result was added.

Actual XAudio2 StopEngine/StartEngine preserves its queued source and current
DSP carry through host delays. Its pass-end notification can overshoot to
1,920/2,880 processed frames, so it is unsuitable as an exact-frame controller.
The existing maintained APU supplies an alternative without an audio device:
source DMA, persistent rate conversion,2D mixing and current DSP produce
byte-identical output across different pull partitions. Three150ms stalls
preserve the actual CBO, source bytes, graph state/storage and partial32-frame
carry. The nominal44.1kHz case uses actual quantized pitch-501, or
44,098.205613Hz; this is not an exact44,100Hz hardware claim.

Root proof `reference-clock-deadlines-20261011/qualified-clock-seam.aot-private.json`
has SHA256 `9b2811c451cc3e4224ebe7028ff0933f11fee0e9631e251f463bd27dd5076d23`.
It verifies464 unique current/frozen identities, including73 current source
inputs, two fixture programs and the retained consumer experiments. The next
work is a compatible controlled audio adapter and shared clock/capture
integration, then actual original movie completion. Native compute handoffs
and full close/reopen ownership remain to be integrated. No full title was
rebuilt or run for this foundation. Movies and `.123` remain open;4/9 families
and26 precision deferrals are unchanged.

## Original positions and composed clock/audio - 2026-10-11

`tools/native_audio_position_reference.py` executes the complete original public
PCM position chain, including its voice-table lock, actual maintained APU CBO
read and original byte conversion. All168 cases pass across PCM8/16, mono/stereo,
rate conversion and full/partial loops. The playing read cursor is actual source
CBO in bytes; its full-ring write cursor leads by32 source frames modulo capacity.
Source prefetch is distinct from emitted output. Authored stopped fields qualify
the returned arithmetic and optional pointers, not original Stop/completion.

The private caller-driven adapter and actual original33ms kernel timeout now
share one explicit input timeline. Fast and host-delayed cases both retire35
input steps,39,375 PIT ticks and1,680 actual48kHz output frames. Actual source
fetches are1,568 frames;52 GP blocks leave16 frames of carry. PCM bytes, source
read ledger, cursor and DSP state are identical through the added host stalls.
The CPU is stopped while the consumer advances; clock observation is pure.

Root proof `reference-clock-audio-20261011/qualified-common-clock.aot-private.json`
has SHA256 `6e2d5c760998060e7c5d780b907a017cebe70e4a6ce21f3fc88b65be73b80699`
and binds290 verified identities. This is an executed composition foundation,
not full-title acceptance. The movie's ordinary controls, runtime input/capture
integration and complete original lifecycle remain required. Default HLE is
unchanged; movies remain open at4/9 families with26 precision deferrals.

## Maintained reference consumer and bounded capture - 2026-10-11

The private pilot can now select `APU_REFERENCE` at build time with original
scheduling and an explicitly specified external block schedule. Its software
consumer retains actual maintained APU source DMA, persistent rate conversion,
2D routing/packed attenuation and original installed GP processing. It creates
no host audio device or independent clock thread. The default HLE path keeps
its existing live consumer. Reference Stop/resume preserves fetched history;
explicit Seek admits a clean binding. Dirty pitch/loop changes and natural finite
completion positions remain guarded rather than silently guessed. The current
movie uses separately qualified ordinary clean setup and looping controls.

The maintained common-clock module advances actual consumer frames before
publishing PIT input. Consumer detach/reopen retains device time while discarding
retired audio state. Original idle/compute compositions match actual source/DSP
state across fast and delayed host execution, and20 focused profile checks pass.
Reference USB SOF and XGSCNT now share that same PIT input. The final runtime
fixture verifies a host stall leaves them unchanged and one admitted millisecond
advances one real OHCI frame and48 samples. This does not establish silicon
cadence or resolve the separately open canonical TSC policy.

`XLLE_PILOT_REFERENCE_CAPTURE` optionally records actual initialized rings,
caller writes, source DMA bytes, Play/Stop/Seek/destruction and six-channel GP
output. Each engine carries its actual PIT attach epoch; QPC observation stamps
stay separate. Capture is bounded to300 emitted seconds and128MiB of records;
overflow/I/O failure stops the generation. The independent
[reference verifier](../tools/native_audio_reference_capture.py) rejects missing
lifetimes/bytes and partial records. Six actual-consumer/negative checks pass:
two96-frame engines, independently expected mixed output, exact source bytes,
empty-engine silence, missing snapshot, truncation and a changed caller byte.
Both affected maintained runtime/consumer tests then pass in0.422s.

The first immutable full-title run used an8,000-block external schedule, chosen
separately from the100-block authored unit fixture. It captured original movie
grids and actual consumer output in one pass. The earlier35s100-block startup
check stops at its observation deadline without a fault or guard; it is not full
movie evidence. Private artifacts are under
`build/blinx/reference-clock-maintained-20261011` and
`build/blinx/reference-movie-batch-20261011`. Final picture, natural retirement,
title transition, reuse and interruption on this reference selection remain
required. The current HLE opening/repeat/interruption proof is reused unchanged.

Independent audio diagnostic `.113.20` is now closed at commit `aba91ea`:
16 verifier checks and an isolated actual3.359s capture verify71,506 submitted
frames across three buffer lifetimes without mismatches. Its observed loop
metadata fixes the old capacity-only diagnostic interpretation, with missing
metadata still rejected. This is not whole-audio or whole-movie closure.

The8,000-block generation `c8000-v1` was deliberately stopped after691.688s host
time. It has no successful capture finalization and is excluded from movie
acceptance. Its complete captured prefix shows opening footage advancing23.091s
while simulated PIT time advances37.479s; the corresponding host interval is
218.054s. Exact source-DMA comparison has zero ring-byte mismatches, but the
opening voice rereads3,222,016 of9,943,936 bytes before another caller write
(32.4%). Microsoft also rereads19.9%; Artoon has no repeated-before-write bytes
in that prefix. These are prefetch witnesses, not speaker retirement timestamps.

This distinguishes expected slow host execution from an unsuitable relative
CPU/device input schedule. Increasing host speed is not the remedy or a
qualification requirement. Keep original execution and a common stopped device
timeline, give the original work a conservative external execution budget, and
accept longer host runtimes. Do not advance time based on audio writes or hide
repeated data. The explicit schedule still makes no hardware CPU-cycle/TSC claim.
No idle-loop acceleration is required for this qualification. Complete original
movie lifecycle acceptance remains open; existing successful HLE playback is
unchanged. Prefix feedback is retained under `c8000-v1/preview-first` and
`c8000-v1/preview-audio-first`.

## Bounded movie family closure - 2026-10-13

The composed original-scheduling/reference-APU Q03 gate now passes, completing
movie family `.73` with the retained matching default-HLE repeat/interruption
proof. Root acceptance is `build/blinx/q03-final-20261013/qualified-root.aot-private.json`,
SHA256 `aee59a1cab5ee2e8e26d6d818f5ddad010b87150cf2e98c0b4bdfc0d2d301ea7`. Earlier entries saying this composed gate is open are historical.

The single changed run completed 600 GP seconds with no capture overflow or
runtime stop/fault. Both op01 lifetimes reach distinctive source frame 2263,
actual Stop/Destroy and title return after 75.566/75.567 seconds. Full-lifetime
independent-media RMSE <=0.004625 and correlation >=0.999828 pass the frozen
0.02/0.99 bounds; A/V p95 52.296/52.269 ms passes 66.800 ms. One alignment per
actual Play includes the quiet tail. Exact movie ring-byte provenance and zero
movie rereads-before-write pass across the retained movie lives.

Actual Start at 445 seconds interrupts Artoon voice 731, which stops/destroys at
445.014; release arrives at 446. Root identifies title 339, loading 349 and fresh
3D Blinx demo gameplay 393 in the retained 12-image sheet. This differs from the
retained default-HLE run's interrupted third op01; both satisfy the declared
active-movie interruption contract. The later diagnostic voice 1094 remains
playing at cutoff and is not counted as completed teardown.

The original run manifest, frozen analyzer and raw reports remain unchanged.
Independent exit observation confirms host termination after successful bounded
capture; no runner adjudication or title rerun was needed. Matching default-HLE
proof `movie-repeat-20261011/qualified-native-repeat-final.aot-private.json`
(SHA256 `053fd458bbdff57e4b7487ef6c6c5ec58c7a0c79b69145968ee361f135519698`)
is reused. This closes the bounded movie contract without asserting physical
speaker/display timing, indefinite endurance or original audio/GPU/graphics
family acceptance. No default-HLE runtime change is introduced by closure.
