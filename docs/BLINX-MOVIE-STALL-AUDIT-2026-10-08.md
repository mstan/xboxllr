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
