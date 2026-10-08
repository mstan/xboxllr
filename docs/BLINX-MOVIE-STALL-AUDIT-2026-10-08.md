# Blinx movie stall investigation, 2026-10-08

`beads-xbox.1.98`, with the scheduling regression tracked in `.71`.
Branch: `investigate/blinx-audio-tail-20261008`, based on `57417e6`.
These experiments do **not** establish an original-speed or complete A/V pass.
The validated baseline retains the independent display-wait correction.

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
across several runs after alignment, and needs separate decoder/output
qualification. A quiet reference tail is not permission to discard it or
declare a tolerance without establishing its cause.

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

[FFmpeg's ADX decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adxdec.c)
was reviewed to distinguish the independent decoder comparison from the native
PCM service contract. No FFmpeg decoder implementation was imported. Its
comparison output alone does not prove the original Xbox decoder's exact tail.
