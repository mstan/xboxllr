# Blinx 2-1 performance reassessment (2026-10-22)

Tracking: beads-xbox.1.7.17. The assessment below records the investigation
from baseline framework a277394. Its deliberately invalid/rejected experiments
were not retained. The subsequent [shared resource-write tracking fix](NATIVE-RESOURCE-WRITE-WATCH-2026-10-22.md)
passes focused checks, saves16.3% process CPU per update in a balanced actual2-1
comparison, and is accepted by the owner as much smoother. Occasional residual
hitches and earlier localized1-2 performance remain in beads-xbox.1.7.18. These
results do not establish zero stutter or uniform whole-game performance.

## What is established

- Stage 2-1 (guest stage 4 / rd21) emits about 20,000 very short strips per
  game update. Backend batching merges roughly two thirds of native submissions,
  but its same-process comparison saved only about 5% of process CPU time.
  Most frontend calls still prepare each original draw before merging it.
- Deferred-state reuse previously saved about 21% of process CPU time in its
  own bounded comparison. These percentages are separate experiments, not
  additive whole-game improvement claims. See the linked implementation docs.
- A sparse callback capture measured 2,307.768 ms of global callback-barrier
  waiting over 388 updates / 14.280 seconds: about 5.95 ms/update and 16.2%
  of wall time. This is one instrumented scene under contention. Eliminating
  every recorded wait could at most imply roughly 19% throughput improvement
  for that capture; callbacks are not established as the whole problem.
- All 18 observed callback queue heads in a later sample were the same
  timestamp-recording routine. Its native body stores an RDTSC timestamp;
  it makes no graphics calls. That supports a dependency-aware asynchronous
  path, but is not proof that its writes can be dropped or delayed arbitrarily.
- A fresh 20-second thread-state observation found 135 Ready, 159 Running and
  22 Waiting samples of the main game thread. Ready means runnable but not
  currently executing. Concurrent host CPU load averaged 91.2% (86.3-99.3%).
  This confirms scheduling contention during that diagnostic.
- That diagnostic intentionally retained the existing QA four-physical-core
  process mask and one-CPU guest affinity. Normal owner launches do not set
  that affinity. It therefore does not establish the owner's exact scheduling
  loss. Future shipping-performance measurements must match the owner launch.
- The thread-state query itself averaged 16 ms and sampling intervals averaged
  longer than requested. Its 42.7% Ready sample fraction is statistical evidence,
  not an ETW-derived percentage of frame wall time or a clean FPS benchmark.
  The observed 13.55 updates/s is not used as a shipping performance result.
- Windows refused the uniquely named WPR CPU trace with 0xC5585011 (profiling
  policy privilege unavailable). No ETW recording was obtained or left running.
- Low process GPU-engine activity was observed alongside these CPU-heavy runs.
  It supports CPU/submission/wait investigation, but is not per-pass GPU timing
  and does not prove that enhanced resolution or effects are free.
- The replay log shows nine 1,913,856-byte writes during roughly 20 seconds of
  this stage, rather than one such write per draw or frame. Their exact latency
  has not been attributed; replay persistence is not established as the cause.

Game CPU execution in these profiles is statically compiled. This is not an
interpreter-fallback bottleneck. Sequential settings comparisons varied too
much with host load to assign the slowdown solely to snow, particles,
widescreen, supersampling, or interpolation. In particular, stopping only the
interpolation presentation worker gave a small improvement in its same-process
sample; that experiment left source capture enabled.

## Private experiments set aside

| Experiment | Result | Decision |
| --- | --- | --- |
| Duplicate bound-texture preparation | No duplicated bindings in 61 entrance snapshots; usually one active texture | Not a priority; snapshots are not a complete draw census |
| Asynchronous timestamp callback prototype | Correctness dependency proof incomplete; worsening host load obscured comparison; no reliable gain | Archived, removed from source, not shipped |
| Append/ring strip-index uploads | GPU pixel/query/wrap fixture passed, but balanced same-process comparison was 39.73 vs 39.70 CPU ms/update, 23.25 vs 23.00 updates/s | Archived, removed from source, not shipped |

The append upload experiment reduced discard maps but did not reduce meaningful
CPU cost. Passing a GPU test is not a reason to retain an ineffective optimization.

## Which restrictions matter

| Restriction or current choice | Effect on performance | Recommendation |
| --- | --- | --- |
| LLE defines observable behavior; HLE must satisfy it | Requires correct ordering/results, not reenactment of every intermediate step | Keep; this permits many-to-one native services |
| Full preparation of every tiny draw before backend batching | Repeats state, resource and ABI work about 20,000 times/update | Move batching above preparation; shared batch interface, revision-bound title adapter if needed |
| Whole callback queue drained at almost every graphics service | Prevents useful CPU/GPU overlap even for independent operations | Replace with explicit effect/resource dependencies and real observation barriers |
| Recompare consumed guest texture/vertex bytes because writes may bypass APIs | Safe but repeatedly expensive | Track resource write versions/aliases, retaining byte comparison where ownership cannot be proved |
| Shared renderer implementation | Requires reusable interfaces, not a one-title renderer rewrite | Keep; title-specific recognition/data adapters may feed the shared interface |
| Fixed original simulation and all effects retained | Prevents hiding missed updates or dropping content as a speed fix | Keep for the correctness path; elective quality controls remain separate |
| 1080p plus 2x2 supersampling and interpolation defaults | Additional rendering/capture work; 1080p plus 2x2 SSAA renders 3840x2160 at 16:9 | Measure independently; these defaults can be discussed without changing LLE integrity |
| QA CPU affinity and competing host jobs | Can conceal gains and exaggerate stutter | Match shipping scheduling for performance QA; do not terminate other work |

The excessive granularity and conservative barriers are implementation choices,
not requirements inherent in a low-level correctness foundation. We should
relax those choices by proving larger units of observable behavior, rather than
weaken the expected behavior itself. The existing native register model is not
shown to be the dominant problem and a general CPU rewrite is not justified by
this evidence.

## Proposed next burndown

1. **Reproducible shipping-profile attribution.** One short entrance capture
   with shipping affinity and diagnostic logging disabled; record actual game
   updates and CPU cost, separate loading/first-use stalls from sustained spawn
   slowdown. Retain host-load context. Do not repeat broad campaign QA.
2. **Move batching above repeated frontend preparation.** Establish which state
   and resources remain stable across one original mesh/strip group. Snapshot
   them once, then lower the ordered group through a shared native batch API.
   Validate pixels, depth/alpha, queries, changed data and logical completion
   at the boundaries the change actually affects. Preserve the ordinary path
   for groups that fail admission. This is the primary architectural candidate.
3. **Dependency-aware CPU/GPU overlap.** Make callback read/write effects explicit
   and wait at genuine consumption/observation/resource-reuse points. Preserve
   the original callbacks and actual native GPU completion. An unqualified
   timestamp-only bypass is insufficient; the rejected prototype is not this fix.
4. **Resource versions if still material.** Avoid unchanged-data comparisons
   when all aliases/writers are tracked. Unknown ownership retains the checked
   path. Do not start memory-protection machinery without alias/portability work.
5. **Owner visual/performance check and integration.** Compare entrance plus one
   representative gameplay section. Update Blinx's framework pin only for a
   retained measured improvement. Quality/pacing changes require an explicit
   decision, not an implicit substitute for the renderer work.

This is a larger frontend HLE step, not a proposal for a new renderer, a complete
CPU lifter rewrite, or removal of the LLE contract. No reliable completion ETA
can be derived from the current evidence.

## Follow-up: measured texture-check headroom and actual entry

A deliberately incorrect private cost-removal experiment (run 039) omits
ordinary CPU-owned texture-byte checks at draw consumption. Binding preparation,
render-target texture preparation, draw calls, deferred state and raster logic
remain. This bypass misses possible writes through retained CPU pointers and
must never ship. It estimates the potential payoff of correct resource tracking.

Eight balanced same-process windows (0,1,1,0,0,1,1,0; three seconds each) use
shipping inherited CPU affinity, adaptive 16:9, 1080p, 2x2 SSAA, FXAA, 8x AF and
60 Hz interpolation. Host CPU load averages 20%. Both modes remain capped near
30 game updates/s. Baseline CPU cost is 28.733 ms/update; omitted texture checks
cost 22.786 ms/update: about 5.95 ms / 20.7% less process CPU time. This is
headroom evidence, not a qualified optimization, FPS gain or guarantee that a
correct tracker will recover the full amount. The bypass was archived on G:,
removed from source, and the restored runtime was rebuilt successfully.

The primary monitor is 3440x1440 (43:18). Earlier 16:9 runs do not cover its
full adaptive aspect. Run 040 uses the unchanged owner a277394 executable at
43:18 with the same enhancements and shipping scheduling. Its log verifies
5160x2160 internal rendering. At host CPU load averaging 31%, observed update
rates are 28.61 (first four measured seconds), 29.08 (seconds4-10) and 29.76
(seconds10-16). The 4-16 second CPU cost is 26.94 ms/update. A retained screenshot
and selector establish Round2 Stage1 / guest stage4. These quiet-host results
still do not establish the owner's reported severe slowdown is resolved.

A measurement gap was identified: the original harness starts detailed timing
after loading and its input-settle updates. Run041 instead samples original
game updates and native swaps every approximately30ms beginning before the
stage-select button press. Using the unchanged owner build, 43:18 and shipping
scheduling, the largest observed initial swap/update gap is 0.587s, with
0.625s process CPU consumed across native threads. Player allocation appears
about1.04s after the button press; the first subsequent swap is observed by
about1.195s. Later observed change intervals reach about0.107s. Polling only
bounds these intervals; it does not provide exact per-frame durations. This
capture does not reproduce a long frozen load and cannot assign the initial
CPU work to shader compilation, asset decoding or resource creation yet.

The apparent G: game folder is a junction: assets resolve to the existing
F:/Projects/xboxlle/wt-native-title-pilot/build/blinx/media-local directory.
Asset storage therefore remains on F:, despite G: scratch/profile/log paths.
This is a possible loading-contention contributor, not a measured disk-latency
root cause. No asset copies or owner folder changes were made.

The next performance change should prioritize correct resource-write/ownership
tracking alongside higher-level preparation reuse: there is now measured cost
to recover. Keep loading/first-use stalls separate from sustained rendering
cost. Any further performance capture must include the actual transition and
shipping scheduling at the owner's adaptive aspect.

A shared [resource-write tracking candidate](NATIVE-RESOURCE-WRITE-WATCH-2026-10-22.md)
is now implemented with focused GPU/memory/file tests, measured and delivered
for owner review. The owner accepts a substantial entrance improvement; the
deliberately incorrect bypass is absent. See that document for the final result
and explicit remaining follow-up.

## Evidence locations

Private data: `G:/Temp/Codex/xboxllr/stage-entry-20261022`.
Assessment runs 024-032 and `assessment-summary-20261022.json` contain the
previous CPU/GPU/settings/barrier comparisons. Runs 033-038 cover resource and
callback observations, rejected prototypes, failed WPR startup and randomized
thread-state sampling. Prototype source/binaries remain only in ignored private
G: archives. All owned hidden/muted children were stopped; owner saves/settings
were not modified.

Related implementation evidence:
[deferred-state cache](NATIVE-DEFERRED-STATE-CACHE-2026-10-22.md),
[strip batching](NATIVE-STRIP-BATCHING-2026-10-22.md), and
[native CPU/HLE assessment](NATIVE-HLE-OPTIMIZATION-2026-10-18.md).
