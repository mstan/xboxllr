# VSH dot-prefix experiment

Tracker: `beads-xbox.1.15.11`. Baseline: `feab59c`. Objective: remove measured
CPU vertex execution overhead while preserving the LLE arithmetic floor.

## Candidate selection

The generated-submit census found no substantial direct buffer/uniform target.
The prior vertex census instead found one 21-step / 23-operation program
responsible for 98.75% of settled programmable executions after the existing
index cache. That program already uses the pre-decoded executor. Its total body
time is not a prediction of removable work.

A private diagnostic capture confirmed a leading run of independent dot products
with identity-swizzled input/context operands and scalar temp/output destinations.
The capture is kept under ignored `build/vsh-specialize/`; neither the raw guest
program nor a transcription of its complete instruction sequence is published.
Temporary capture code was removed before implementing the candidate. The first
capture attempt stopped because PowerShell treats stderr diagnostics as terminating
errors in this harness; a stdout capture completed and passed replay validation.

The candidate generalizes the observed opportunity: recognize a safe prefix from
the actual decoded operation records, cache its routing, and execute the remaining
suffix through the existing compiled executor. It does not dispatch on a guest
fingerprint. Constant values and input values remain dynamic on every vertex.

## Required invariants and experiment

- Accept only unpaired DP3/DP4 operations, identity swizzles, no negation or
  relative addressing, input/context reads, and one scalar temp/output write.
  Stop at the first unsupported operation; preserve the suffix and its pair order.
- Validate register bounds/alignment and refresh the plan with program/start
  changes. Exclude context-writing programs and unsafe probes.
- Call the existing vendored arithmetic functions. Preserve input multiplication
  by positive one, including signaling-NaN behavior; do not substitute a raw copy.
- Reuse sources only within the prefix, whose distinct register files prevent
  writes from changing its input/context values. Keep zero initialization and
  downstream output consumption unchanged.
- Record matched compilations and batch-aggregated executed vertices/operations.
  A flag without nonzero execution counts is not a tested optimization.

The default-off `--nv2a-vsh-dot-prefix` toggle provides a same-binary comparison.
Before timing, compare synthetic exceptional values and mutated plans against the
old executor, then run the interpreter verifier through the entire canonical
replay. Headline acceptance remains same-session noperf interleaved 3+3, minimum
and median at 5%, exact guest-state/frame comparisons, and product navigation
windows after a canonical pass. The completed canonical gate below rejects this
candidate; it remains off by default.

## First validation checkpoint

The initial candidate executable is
`5e6da145a12a3961a99f17623a26a7357cabb833fdbf578ee67d6ed572c6f25e`.
CTest passes 36/36, including 2,048 finite-input variations, isolated finite
overflow, exceptional bit patterns, a dependent paired suffix, bounds, and
plan rebuild/decline cases. Disassembly of the helper test confirms the
positive-one multiplications remain instructions.

The 12 B `verify-r1` replay passes with a 100 M vertex verifier budget:
38,337,230 vertices checked, zero mismatches, budget not exhausted. The general
prefix path executes 37,397,827 vertices / 295,356,670 operations, with 17,867
matched compilations and 166,778 planned operations across those compilations.
These aggregate counters demonstrate broad execution coverage; they are not
per-fingerprint counts. No per-vertex timing is used to claim a speedup.

`canonical-r1` encountered unrelated psxrecomp CMake builds in all four completed
legs. They pass individual replay checks but are timing-ineligible. The owned
benchmark processes were stopped, with partial outputs and process observations
preserved. `canonical-r2` also encountered a foreign build and was stopped.
Neither attempt is an accepted A/B set. The harness now refuses a known-busy
host before launching when `-RequireQuiescence` is set, and records
`emulator_started=false` with `reason=busy_before_launch`. Work starting during
a leg still requires contamination rejection. Its preflight and pair-retry
PowerShell tests pass. This preflight also applies when pair retry is requested;
it avoids repeatedly launching into a known-busy host.

Review found a pre-existing cache issue: disabling and re-enabling compiled VSH
while a parsed program remains cached can leave execution on the interpreter
until the program changes. This preserves results but misses the requested
optimization. The fresh-process benchmark legs do not exercise that transition.
The setter now invalidates the parsed program as well as its compiled state.
The functional transition test initially failed because its isolated PFIFO
fixture omitted the pusher DMA object and instance; no vertex commands executed.
After restoring the known-good setup, it checks compiled/interpreted execution
counts, re-enable without a program upload, and the interpreted output hash.
The full release CTest suite passes 36/36.

The final candidate executable is
`521edcab6900f033141bf351059b9354ebadb8da521bc9dacc3a63a92584c135`.
Its `verify-r2` canonical replay again checks all 38,337,230 vertices with zero
mismatches and reproduces the execution counts above. The test-only fixture
correction does not change this executable. `canonical-r3` uses this same binary
with the prefix toggle off/on, no verifier, and required host quiescence.

## Completed same-binary gate

`canonical-r3` completes all six interleaved noperf legs in one session. All
recorded before/after process censuses are clean, and all legs use the final
executable above. The candidate counters are zero in A and reproduce the
37,397,827 vertices / 295,356,670 operations in every B leg.

| Total 12 B wall | Prefix off | Prefix on |
|---|---:|---:|
| Median seconds | 50.1972097 | 50.2715856 |
| Minimum seconds | 50.1545969 | 50.1728989 |
| CV | 0.644% | 0.564% |

Improvement is **-0.148% median / -0.036% minimum: REJECT** at the required 5%
gate. Only one of three pairs favors B. The final-four indicator is effectively
flat (+0.020% median); it does not override the total-wall gate. This is evidence
against this routing/prefetch implementation as a speedup, not evidence that
vertex execution is optimal. No product timing or default promotion follows.
The opt-in implementation and differential fixtures remain available to make
the negative experiment reproducible.

All **28 checkpoint PPMs per leg** and the checked guest-state fields agree
exactly across the six legs. A separate fidelity-only comparison includes the
published `feab59c` integration leg and `verify-r2`: retired state, scanout,
flips, controller endpoint, idle/generic/seam censuses, and checkpoint timelines
agree. That cross-session comparison supplies no timing claim. The publication
tree secret scan checks 163 files and finds zero secrets; private capture and
measurement artifacts remain ignored.

## Next selection

Do not repeat the same prefix A/B without a changed implementation. A helper
suggested specializing its DP3/DP4 arithmetic next. That is a new hypothesis,
but the old whole-operation envelope is not a measurement of savings in this
prefix. It also carries more numerical risk than the routing-only experiment.
Exceptional-value agreement remains mandatory, and a broad opcode switch is
already disproven by earlier measurements.

Prioritize an opt-in census of the overlap between programmable vertex work
and generated GPU draws, carrying program identity and vertex counts through
CPU triangle preparation. Count clipping, culling, and refusal outcomes as well
as accepted draws. Measure the CPU preparation portion for that same population;
do not add unrelated vertex and generic bucket totals as predicted savings.
Only then select a bounded GPU vertex-plus-setup experiment. Moving VSH alone
would leave CPU consumers of its results and may introduce a readback bottleneck.
Distinct guest-frame presentation and latency tails remain a separate open
part of proving 60 FPS with headroom.
