# Real HLE render worker: parked after a mixed dashboard screen

Branch `spike/render-worker-20261004`, parent `54c102a`;
tracking `beads-xbox.1.53`. See [ownership and ordering](HLE-RENDER-WORKER.md)
for the NoRain-derived design, build selection and failure contract. This
actually executes rendering on another thread; it is not the earlier parked
GPU-presentation experiment.

## Validation

80/80 tests pass, including both synchronous and asynchronous GPU qualification.
The worker test exercises 1,000 ordered queued draws, caller mutation, readbacks,
clear/resource lifetime, raw formats, real backpressure and delayed shader
failure propagation. It reaches the 128-job limit instead of discarding work.

Real interactive 27B Settings/return route: all 14 events complete, exit 0,
empty stderr, zero presentation failures. **All 28 captures match the baseline
byte-for-byte.** Native/fallback/idle CPU counts match exactly. Renderer draws,
vertices, uploads and uploaded stream bytes match: 580,540 draws, 175,461,689
vertices, 1,105 surface uploads and 5,736,323,872 stream bytes. Readbacks are
2,350 versus 2,351; there is no claimed decrease in required rendered work.

Worker counters: 580,540 submitted draws, 9,207 synchronous requests, 107,505
backpressure events, maximum queue 128, worker busy time 20.264 s, caller fence
wait time 7.807 s. These are component observations, not additive frame costs
or a causal proof that any one mechanism explains all elapsed time.

## Timing and decision

This single worker run follows the state-cache A/B and uses its unmodified
`54c102a` A arm as the shared baseline. It is not a dedicated adjacent worker
A/B pair. All runs are contaminated by foreign workloads; foreign compiler
activity also changed. Avoid a precise speedup claim.

| Frame-window time | Shared baseline | Worker |
| --- | ---: | ---: |
| Whole route | 122.2557 s | 117.7112 s |
| Last four windows, 491 vblanks | 21.6567 s | 23.2182 s |
| Final window, 123 vblanks | 5.8232 s | 6.0696 s |

The full route is nominally 3.7% faster, but the last four windows are 7.2%
slower and the final menu interval is 4.2% slower. This is mixed evidence,
not a demonstrated substantial menu speed gain. **Park; do not integrate.**
The implementation is available for reuse if a different workload/profile
establishes sufficient independent rendering work. Queue batching is a possible
future experiment, not an excuse to start an unbounded optimization loop now.

Private evidence: main `build/r2-worker-r1-leg00-A-r1*`, shared baseline
`build/r2-state-r1-leg00-A-r1*`, full checks `build/r2-worker-tests.log`.
No private images or guest code are committed. The faithful LLE path is unchanged.
