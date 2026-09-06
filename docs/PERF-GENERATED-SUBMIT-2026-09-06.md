# Generated submission: measure before choosing an upload optimization

Tracking: `beads-xbox.1.15.14`. Baseline: `f183874`. The objective remains
faithful LLE at 60 FPS with headroom; this experiment selects work, not a new
performance claim. No external code or captured assets were imported.

## Instrument and interpretation

`XLLE_GENERATED_SUBMIT_DIAGNOSTICS` defaults OFF. An ON build accepts
`--nv2a-generated-submit-census` with `--perf-accounting` and the OpenGL renderer.
The recorder splits the generated-submit CPU body into entry, ensure, textures,
records, buffer, uniforms, state, draw, barrier, cleanup, error and finish.
Reports carry cumulative guest-checkpoint anchors, raw and clock-floor-compensated
time, valid calls, discarded intervals, submit count and packed record bytes.
The summarizer requires complete, monotonic endpoints and rejects discarded
intervals. Early returns count as submissions but need not visit every phase.

These are inclusive CPU phases: entry can flush witnessed work; ensure can
resolve/upload surfaces. They must **not** be added to, or subtracted from, the
exclusive GL partition. They are not GPU execution durations. Fenced clock-floor
subtraction does not remove recorder bookkeeping or scheduling perturbation.

GPU queries previously followed `perf::g_enabled` with no separate control.
`--no-nv2a-gpu-timers` now disables query issuance independently, while retaining
CPU accounting. The report distinguishes supported-but-disabled timing from
unavailable timing. Defaults preserve existing query behavior; pending queries
still drain at shutdown if timing is disabled after issuance. This does not add
GPU issuance-window ownership: that remains open.

## Settled-window diagnostic

Private reports are under `build/generated-submit/`. Run the 16 B clock-accept
replay at 250 M checkpoints; compare the exact 14.5–16 B window. The diagnostic
executable SHA-256 is
`2566170ddb0dbf8f04c19bcab6cccff179e6be4881bae358f6aa7dfc0d59803a`.

With CPU census ON and GPU queries OFF (`cpu-on-gpu-off-r1`), wall time is
12.5530995 s. There are 861 submissions, each visiting all twelve phases,
61,247,872 packed record bytes, and zero discarded intervals.

| Phase | Compensated CPU milliseconds |
|---|---:|
| Textures | 358.438 |
| Ensure program/color/depth | 151.724 |
| Error check | 56.438 |
| Record packing | 3.474 |
| Barrier/flush | 2.617 |
| Buffer upload/bind | 2.117 |
| Fixed state | 1.094 |
| Uniforms | 0.671 |
| Cleanup | 0.232 |
| Draw call | 0.209 |
| Entry | 0.142 |
| Finish | 0.079 |

Total is 577.234 ms compensated / 577.337 ms raw. The companion with CPU census
OFF and queries OFF (`cpu-off-gpu-off-r1`) takes 12.1620189 s in the same window.
All 72 PPMs and stable guest state/timeline match exactly. Both report supported
GPU timing disabled, zero issued queries, and no detected foreign compiler or
emulator at the harness checkpoints. These are single diagnostic companions,
**not** an interleaved retention set or a precise probe-overhead estimate. The
host census cannot rule out short-lived work between its samples.

The CPU-ON/query-ON companion (`cpu-on-gpu-on-r1`) takes 11.822605 s with the
same 861 submissions and record bytes, zero discarded intervals, and identical
72 images/state/timeline. Its generated body totals 573.054 ms compensated;
textures 360.321 ms, ensure 145.629 ms, error 57.011 ms, buffer 4.257 ms.
Queries are explicitly enabled and issued. The different run ordering and
whole-window drift prohibit calling this a query-timing speedup. The two
instrumented profiles agree on the small direct buffer/uniform opportunity.

## Decisions from the evidence

Do not implement the proposed generated SSBO ring yet. Only 2.117 ms of this
12.553 s window sits inside its upload/bind phase. Buffer lifetime may affect
where driver work is paid, but no observed stall justifies that theory here.
The already-rejected bulk uniform candidate similarly targets a very small
measured phase. Preserve those negative results instead of repeating them.

The larger generated phases concern residency and texture uploads. Their inputs
are already materialized before this function. Color residency can then copy
rows again for vertical orientation; depth uses conversion staging. The phase
totals do not isolate those copies from driver calls. A GPU-to-GPU transfer
experiment needs exact source/destination ownership, format/layout and overlap
conditions, with the existing CPU route as its comparison arm.

The census does not explain the rest of the GL bucket. Resident generic submit,
witnessed submit/flush, clears, resident row uploads and legacy generic submit
remain separate paths. The helper suggested a fresh BGRA toggle set, but the
primary rejected it: that default optimization is already shipped, and the
toggle bundles conversion, allocation reuse and upload-format changes. No
changed prerequisite makes repeating it the next useful throughput test.

## Next experiment selection

1. Attribute the remaining resident/legacy submission paths and correlate CPU
   work with GPU ownership transitions. Preserve separate CPU API, GPU and wait
   costs; do not infer a driver bottleneck by subtracting incompatible totals.
2. Size exact CPU specialization of the dominant VSH program against the
   existing decoded interpreter. Compare full program identity and current
   constants; preserve floor arithmetic and all downstream triangle consumers.
   GPU vertex execution is a separate candidate that must retain those consumers
   on the GPU or account for returning results to the CPU.
3. Promote a bounded candidate only after same-session noperf 3+3, min and median
   gates, canonical behavior checks, and settled/Settings/return measurements.
   Add distinct guest-frame presentation and latency tails to judge headroom.

The approximately 12 guest-equivalent FPS previously measured in the returned
dashboard is capacity, not distinct displayed FPS. The remaining gap to 60 is
not evidence that the current architecture has reached its ceiling.

The helper's follow-up confirms `a83505a475320999` already uses the pre-decoded
executor; specialization would remove remaining operand routing, swizzle/mask
decoding and dispatch, not repeat the shipped decode cache. Do not dispatch on
the fingerprint alone or on `vsh_cache_fingerprint_`, which currently has no
assignment. First inspect the complete executed program in a private diagnostic
artifact. Keep constant values dynamic, fetch paired MAC/ILU inputs before either
store, preserve zero initialization and consumed outputs, and retain the
vendored arithmetic helpers. Decline unsupported addressing/context effects.
The estimated 2.22 s target-program body is total measured work, not savings.
Verify exceptional inputs and exact program/constant mutations against the
interpreter before running the noperf toggle comparison. Do not publish a guest
program dump as part of the optimization.

## Normal-build integration control

Both diagnostic ON and OFF release builds pass CTest **35/35**. The live GL
census fixture executes on this host, and the normal build refuses the census
flag with exit 2 before renderer creation. Normal builds omit phase clocks and
recorder members. GPU timing's default behavior remains enabled only alongside
performance accounting.

`integration-r1` compares the preserved `f183874` executable with the restored
normal build: same-session noperf interleaved 3+3, all pairs clean at the recorded
censuses. All 28 PPMs per leg and stable guest state/timeline agree exactly.

| Total 12 B wall | Published baseline | New normal build |
|---|---:|---:|
| Median seconds | 49.9485866 | 49.4800532 |
| Minimum seconds | 49.8413649 | 49.4471356 |
| CV | 0.742% | 0.605% |

Median/minimum improvement is +0.938% / +0.791%; the formal 5% improvement gate
returns **REJECT**. No performance promotion or speedup claim follows. Retain the
opt-in diagnostic and independent query control as measurement infrastructure;
this integration set found no default-path regression, not universal proof of
zero overhead. The publication-tree Gitleaks scan reports 159 files, zero findings.
