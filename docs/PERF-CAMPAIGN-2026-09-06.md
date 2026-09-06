# Campaign amendment: faithful LLE at 60 FPS with headroom

Owner direction, 2026-09-06: finish the existing plan, then keep optimizing toward **60 FPS
with headroom**. LLE fidelity is the floor. HLE is opt-in and must be checked against that
floor. A subsystem is not exempt from investigation because an earlier session called it
"done". This amends the 10 FPS milestone in `NORTH-STAR.md` and the interpretation of older
performance conclusions; it does not erase their measurements.

## Resuming optimization after the external review

The owner's next request is a whole-pipeline reassessment and concrete borrowable
A/B candidates. Source and measurements were rechecked at `38eba85`. The
independent play instance was kept separate and had exited before the new timed
sets began. Measured runs require a quiet host and must not reuse that instance.

The first bounded candidate, informed by sp00nznet's bulk constant-buffer
submission, is generated-stage uniform batching: at most sixteen `glUniform4f`
calls become two `glUniform4fv` calls. This changes host submission, not guest
shader arithmetic. Packing cost and every other GL call remain. It is a candidate,
not a promised 5% improvement. A generated-path upload ring is the next candidate
only if buffer-upload/stall attribution justifies it; the witnessed path already
has a four-buffer ring.

Before timing uniform batching, verify linked active array extents. Current code
caches element zero then adds 0..7 to its location. Source declarations of eight
elements do not prove all eight remain active after linking. Cache valid element
locations or reflect the active extent for a safe scalar comparator; test inactive
arrays, short arrays, both arrays with different constants, and alternating
programs. Do not confuse a correctness repair with batching's performance effect.
Khronos specifies that array uploads beyond the array end are ignored; the
reviewer's suggestion that a count of eight alone causes an error for a shorter
active array was rejected. This is a source-level concern, not an observed driver
failure. See [glUniform](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml)
and [active uniform sizes](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetActiveUniform.xhtml).

Run this six-part reassessment before choosing larger changes:

| Question | Evidence / next action |
|---|---|
| Are we measuring the product? | Extend settled animation, Settings and return windows; identify newly submitted guest frames separately from repeated presents. Record guest-clock anchors and p50/p95/p99 frame intervals/capture-to-present latency. Equal pixel hashes do not necessarily mean the same guest frame. |
| Can we trust attribution? | Preserve noperf headline runs. Compare timer/query-enabled and disabled diagnostics, fence/calibrate short CPU scopes, and assign delayed GPU query results to their issuance windows. Separate CPU API time, GPU work and synchronization. |
| Where does the next frame wait? | Correlate emulation-thread work, raster-worker completion, GPU queue progress, resolves and presentation. Summed worker time is not wall time, and overlapping CPU/GPU savings cannot be added. |
| Which work is repeated unnecessarily? | Count calls and bytes per guest frame: generated constants, buffer repacking/uploads, triangle setup, state changes and ownership transitions. Measure hit rates and cost per attempt before adding caches; the failed vertex-reuse probe remains a counterexample. |
| Is the work on the right processor? | The dominant 23-op VSH is a candidate for exact program specialization or an integrated GPU path. Include CPU triangle-setup consumers and transfer costs; a synchronous GPU round trip may erase the savings. Keep unsupported semantics on the floor. |
| Does an improvement survive the whole workload? | Same-session interleaved 3+3, min and median, canonical behavior checks and per-state navigation results. Re-profile after each retained change. Revisit TCG/MMIO/architecture using the new critical path, not a fixed historical percentage. |

The corrected diagnostic settled window is approximately 24.6% vertex, 23.0% GL
submission, 20.7% generic body/setup, 8.8% dispatch, 8.5% texture, 6.7% TCG and
3.1% resolve. It has zero software pixel-loop calls: the generic bucket must not
be interpreted as software rasterization. This is one attribution run, not a
noperf retention set. Details and probe perturbation are in
`PERF-TIMERS-2026-09-06.md`.

The first checks were the disabled vertex-diagnostic overhead set and the
uniform correctness/call-count A/B; their completed results are below. Larger
GPU vertex work follows the critical-path and data-ownership review. The returned-dashboard
capacity previously measured about 12 guest-equivalent FPS; reaching 60 requires
roughly five times that capacity, and a proposed 20% reserve roughly six times.
These ratios describe the gap in that workload, not proof of a ceiling or a
prediction that any listed experiment will close it.

The disabled vertex-diagnostic set is now complete: all three pairs favor the
pre-diagnostic executable, with 1.66% median / 1.50% minimum regression. All 28
images and stable guest state agree across six legs. With 4.06% / 4.41% CV and
within-session drift, this does not isolate a precise branch cost, but it fails
to establish free instrumentation. The diagnostic is now isolated behind
`XLLE_VERTEX_REUSE_DIAGNOSTICS`, default OFF; both builds pass CTest and the ON
smoke verifies observations. Preserve its measured negative settled reuse
result. Uniform comparisons keep the diagnostic configuration equal in both
arms. No speedup is claimed from compile-time isolation alone. Details:
`PERF-VERTEX-REUSE-2026-09-06.md`.

The generated-uniform A/B is complete on the final normal build: 4,744 scalar
calls become 593 bulk calls with all 28 images and stable state exact across
six runs, but median/minimum wall regress 1.56% / 0.71% and the 5% gate rejects.
The earlier contaminated set is retained separately. Bulk stays default off;
safe scalar locations and their GL tests are retained for correctness. The
final reference replay accepts 31/31 checks (two identity skips) and reproduces
all 28 PPMs from the accepted reference run. See `PERF-UNIFORMS-2026-09-06.md`.

The follow-up source audit confirms a gap in generated-submit CPU attribution:
the broad GL scope includes program/surface ensure, texture identity and upload
calls, triangle-record packing, SSBO submission, uniforms, fixed state,
draw/barrier and cleanup/error checks. The `gpu_timed_*` wrappers measure GPU
query intervals, not a CPU subdivision of those operations. The existing named
non-resolve CPU counters account for approximately 1.1363 s of the settled
2.7294 s GL bucket; the 1.5931 s difference is unassigned, not demonstrated
uniform overhead. Add opt-in generated CPU scopes and issuance-window GPU query
tags before choosing buffer reuse or packing changes. Do not drain the GPU to
make a timing report easier to collect.

The generated CPU census is now measured; see
`PERF-GENERATED-SUBMIT-2026-09-06.md`. Query-OFF settled attribution puts only
2.117 ms in buffer upload/bind and 0.671 ms in uniforms over a 12.553 s window.
The proposed generated ring is deferred for lack of a measured target. Surface
preparation and texture uploads dominate this function, whose inclusive phases
do not explain the rest of the exclusive GL bucket. Three same-binary diagnostic
companions preserve all 72 images and guest state; they are not throughput gates.
Next attribute resident/legacy submission and evaluate exact CPU specialization
of the dominant VSH program. GPU ownership and presentation-tail work remain
necessary for the 60 FPS/headroom objective.

GPU VSH needs an integrated path: `submit_vertex_batch` materializes CPU
positions/varyings, then generated raster setup consumes them for finiteness,
culling, area, bounding boxes, depth slope and packed triangle records. The
first candidate must include those consumers or measure the cost of returning
results to the CPU. Exclude context-writing shaders and raw-output diagnostics
until modeled. The 2.248 s instrumented VSH-body census sizes an opportunity; it
is not a demonstrated end-to-end saving, especially given measured probe
perturbation. Exact CPU specialization of the dominant program is a separate
candidate that avoids that ownership transition.

## Acceptance and budgets

Twenty percent headroom is the proposed planning threshold pending owner preference:
capacity for 72 guest frames/s, or 13.89 ms of host work per 60 Hz guest frame. Guest speed
must be at least 1.2 in the uncapped capacity run. A capped interactive run must sustain
60 distinct guest frames/s with pacing and latency tails recorded. Successful SDL calls
alone do not establish this: they can repeat images or be discarded by the compositor.

Measure stable dashboard animation **and** scripted navigation at guest-anchored windows.
Keep cold boot and the canonical 12 B replay as separate regression workloads. Continue
the existing same-session interleaved 3+3 protocol, noperf headline numbers, min and median,
and 5% retention gate. Do not cherry-pick an aggregate gain that hides a slower navigation
state. Define stage budgets from the new per-window measurements before assigning predicted
savings; gains with overlapping causes must not be added together.

The existing instrumented 52.501 s partition covers about 16.36 guest seconds. At 20%
headroom the corresponding capacity budget would be about 13.64 host seconds. This is
planning arithmetic, **not** a noperf baseline or a claim that boot and steady-state have
the same costs. It shows why a few percent improvement cannot complete the campaign.

## Ordered work

| Step | Work and evidence needed | Completion criterion |
|---|---|---|
| 1 | **DONE:** stage g, same-binary interactive A/B, native-format/coherence tests, pre-change/new headless A/B, reference golden | See `PERF-PRESENT-2026-09-06.md`: 31/31 golden and 28/28 images; capture 453.2 -> 8.8 us, present calls 16.50 -> 18.76/s. Formal 5% gate rejects (+1.07% median/+1.02% min); explicit capability exception with the headless cost and DMA-history difference recorded |
| 2 | Re-establish the **product** baseline: steady animation and each navigation state, host presents vs guest flips/vblanks, guest speed, p50/p95/p99 frame intervals and latency | Repeatable guest-anchored workload and explicit headroom target; no boot-average substitution for steady-state capacity |
| 3 | Repair the two structural and three numeric generic-evaluator divergences, one family at a time; independently check specialized assumptions against the register/texture-stage semantics | The generic CPU evaluator and specialized path agree behaviorally for explained reasons; targeted counterexamples pass before enabling GPU routing; same-session GPU A/B shows whether residency savings survive |
| 4 | Calibrate the remaining instruments, then re-profile witnessed/generic raster, vertex, and GL/resolve work **per product window** | Fence and cost the exclusive timer before trusting small regions; distinguish executable content from build identity; measure frequency, cost per operation, ownership transitions and cold/warm costs before selecting repeated work to remove |
| 5 | Audit TCG costs on the actual workload: hook metadata lookup, register spills, clock/deadline work, PC ring publication, softmmu and MMIO slow paths | Production evidence separates necessary guest semantics from removable host implementation cost; a bounded faithful patch passes clock/interrupt/input checks and a 3+3 gate |
| 6 | Evaluate GPU-to-GPU texture transfer, batching/state/upload reuse, and other bounded changes against those measurements | Each has a measured ceiling, counterexample/fidelity tests, runtime reversal where appropriate, and accepted/rejected/undetermined result; do not book modeled savings as measured wins |
| 7 | Reassess architecture if the sum of verified opportunities still misses the product budget | State the remaining gap and changed prerequisites explicitly. WHPX's measured MMIO-exit cost remains adverse; replacement is a new decision, not a conclusion obtained by extrapolating the old ALU benchmark |

Read-only GPT-5.5 High audits for steps 3 and 5 are complete; findings and rejected shortcuts
are below. They do not complete either implementation step. The primary agent checks the
proposed semantics, searches for counterexamples, and owns all
implementation decisions and timed runs. One subagent at a time; no child agents. Builds and
benchmarks never overlap.

## Claims that need narrower wording

Validated progress after the initial audits: stage g is committed; independent
PROJECT2D admission for witnessed families 2/5 is repaired; the Settings workload
and aligned product-window tool are committed. Exact post-resolve texture
conversion reuse plus content IDs passes canonical (+5.70% median / +5.73% minimum)
and interactive navigation (+16.47% / +16.64%) gates and is enabled by default.
See `PERF-TEXTURE-CACHE-2026-09-06.md`. The current returned-dashboard capacity is
about 12.06 guest-equivalent FPS at the median, so this does not close the 60 FPS
goal. The small-region clocks are now calibrated and the vertex-reuse shadow
census is committed: its direct-mapped candidate is rejected on observed lookup
cost versus potential savings. See `PERF-TIMERS-2026-09-06.md` and
`PERF-VERTEX-REUSE-2026-09-06.md`. The owner's subsequent source exploration is
recorded in `PERF-EXTERNAL-REVIEW-2026-09-06.md`: next measure the generated GL
submission gaps, then select bulk constants/upload reuse or a carefully bounded
GPU vertex experiment. No external renderer is treated as a fidelity oracle. Numeric
generic families 3/4/7 and direct distinct-frame/tail-latency measurement remain open.

* **"There is no swapchain": false.** The interactive SDL D3D11 backend already calls DXGI
  `Present1`. Stage g measures and improves it. Sharing the NV2A GL context is a different
  optimization, with a transfer cost that still needs to justify the work.
* **"Nothing on the CPU menu clears 5%": not established.** `DESIGN-CPU-TIER.md` section 5.1
  itself lists the entire block-hook envelope at 4.80 s / 7.5% of its old workload. Removing
  the hook while preserving time and observability would retain some replacement cost, so
  this is not a proven 7.5% win. But it also is not evidence of a sub-5% upper bound.
* **"The remaining CPU cost is genuine work, therefore not optimizable": unsupported.**
  The 8.25 s residual is inferred from a synthetic register-ALU benchmark; that instrument
  explicitly cannot split it. Guest loads and stores are necessary, while a particular
  softmmu implementation's overhead is not automatically necessary. Measure the difference.
* **"TCG cannot get below 5 s, therefore tuning is unjustified": an old selection rule, not
  the new product criterion.** Rank production-proven savings against remaining frame budget
  and correctness risk. A replacement is not required to try a bounded improvement to TCG.
* **"This lever is done": configuration-specific.** A vertex or batching result under one
  residency pattern is not a universal result after the generic evaluator or GPU ownership
  changes. Preserve failed numbers and require a changed prerequisite before repeating them.

The owner explicitly authorized committing the accumulated work and pushing it to
main/master on 2026-09-06. Integrate validated commits from the performance branch;
continue subsequent experiments there. This does not authorize hardware access or
a replacement CPU arc. Real-console capture still needs the owner's approval.
Do not reintroduce a rejected Unicorn per-access hook or mix positive-count and zero-count
execution. Never use Unicorn timeout.

## Adversarial review of the first two audits

The GPT-5.5 High helper performed read-only source audits; it ran no emulator, compiler,
benchmark, or tests. Its output is **not** treated as validation of a patch.

### Generic texture-stage admission

The two structural failures occur before a generated shader can execute. At
`Nv2a::rasterize_generic_combiner_batch`, texture references come from combiner ICWs, but
the swizzled PROJECT2D/CUBEMAP admission also requires `stage_count >= 2` unless the whole
shader register is 0 or 1. That count is a **combiner** count, not the number of texture
shader stages. The refused words decode as `[PROJECT2D,NONE,CUBEMAP,NONE]` and
`[PROJECT2D,CUBEMAP,CUBEMAP,NONE]`; these families consume only T0.

The helper's initial suggestion to admit just those two witnessed shapes was challenged.
A better candidate decodes all four modes, admits only independent NONE/PROJECT2D/CUBEMAP
programs, and handles referenced stages without using combiner count as a texture-semantic
rule. Preserve the existing all-zero legacy interpretation separately; it is an existing
compatibility behavior, not evidence that hardware NONE means a texture sample.

The candidate is still **unvalidated**. Unreferenced side-effecting/dependent stages must
not be silently ignored; referenced true cubemap textures need face-layout/remap support
which the current generic sampler does not provide. The two targeted T0 textures are not
cube-format textures, but that is insufficient to justify broad acceptance. Existing
fixtures around `pitch_tex5_projected` and `tex7_fan` supply initial color/depth expectations;
add a forced generic CPU comparison, border/wrap variants, and side-effect/cube-format
counterexamples. Then test GPU-disabled rerouting before generated-GPU rerouting, one bit
at a time. A disappearance of the skip counter alone is not a fidelity test.

### TCG block metadata

The helper confirmed that the 4.80 s envelope and its 0.995 s lookup component are derived
from interpolating the synthetic B0/B2/BP/BX benchmark at the production average block size,
then multiplying by production entry count. In particular BX hardcodes `icount=1`; it does
not carry production TB metadata. These are experiment-sizing models, not measured savings.

The smallest proposed production experiment keeps the scheduling callback and its PC ring,
deadline, IF-window, stop-reason, and TSC work, but supplies the TB's existing icount instead
of doing the `(pc,size)` hash-table lookup. `TranslationBlock` already holds size/icount and
the edge callback exposes both. The helper suggested packing icount into the public size
argument as a prototype. **Do not ship that shortcut:** silently changing `UC_HOOK_BLOCK`
argument semantics can break other consumers. Use an explicitly opted-in, versioned metadata
path, retaining the current callback as the A arm.

Required counterexamples include varying block lengths, bootstrap repayment, SMC/TB
invalidation, deadline crossing, an interrupt becoming deliverable after STI, and the RDTSC
timeline. Do not mark the callback `NO_READ/NO_WRITE_GLOBALS`: its conditional EFLAGS read
requires synchronized TCG state. Only a production A/B can decide whether lookup removal
is worth retaining or whether larger callback/softmmu work is justified.
