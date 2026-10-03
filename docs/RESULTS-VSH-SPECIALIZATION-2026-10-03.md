# GPU vertex programs and status readbacks

Tracking: `beads-xbox.1.15.11`. Source baseline: `e8508f8`, under the active
[dashboard menu goal](GOAL-AOT-DASHBOARD.md). This is an experimental GPU path;
it is not a 60 FPS result or a default-path promotion.

## Change and boundary

The [existing deferred GPU path](PERF-VSH-GPU-DEFERRED-2026-09-06.md) already
skips CPU vertex execution and triangle expansion for admitted work. Its earlier
measurement found that GPU interpretation, uploads and synchronous status reads
outweighed the removed CPU work. This experiment specializes shader execution.

With `XLLE_VSH_GPU_RESIDENT=ON`, `--nv2a-vsh-gpu-specialize 1` emits direct calls
to the existing qualified arithmetic helpers for each decoded operation. It
eliminates the GPU opcode loop and operation-buffer upload. It does not replace
those helpers with approximate arithmetic. Paired MAC/ILU inputs are fetched
before either result is written, and decline checks retain their original order.
Screen-coordinate processing and output packing are shared with the interpreter.

The cache owns and compares the complete operation vector; no guest address or
hash alone establishes identity. Constants and fetched inputs remain dynamic.
Compilation is bounded to 64 operations and 32 cached identities. Larger or
unsupported programs retain the generic GPU interpreter. Failed compiles are
reported and cached, also retaining the generic path. Unsupported execution or
geometry still materializes the actual CPU result from the owned inputs.

The option defaults off. `--nv2a-vsh-gpu-specialize 0` selects the existing GPU
interpreter; `--nv2a-vsh-gpu-resident 0` selects the CPU vertex path. Nonzero
specialization requires a nonzero resident mode. The ordinary build keeps the
resident experiment disabled. No CPU AOT coverage or guest-time policy changes.

All three xboxrecomp shader generators and the pinned xemu vertex emitter were
assessed. The concrete adaptation is compiled-program reuse at our decoded
operation boundary, with full-content identity and existing arithmetic. It
imports no HLE/device shim. See the [donor follow-up](RENDERER-REUSE-2026-10-02.md#vertex-program-follow-up-2026-10-03).

## GPU and live-machine qualification

All **79 Release tests pass** in the experimental private-input build. The real
GPU probe now covers **29 cases**, up from 20. Verification and deferred modes
both pass with **24 GPU-draw cases, five real declines and zero mismatches**.
Each uses seven successful specializations, 26 specialized dispatches and 19
cache hits. The added cases exercise supported arithmetic, paired temporary
aliasing, changed constants, changed operands/masks/negation, a 65-operation
generic GPU program and unsupported EXP returning to the CPU path. Verification
checks all consumed vertex outputs and packed vertices against the independent
CPU implementation, in addition to complete rendered images. Deferred mode
performs actual GPU draws without claiming CPU verification.

The live **27B** Settings/return route uses the reference OpenGL raster path,
the existing private 286-region AOT input, the public register API, idle
fast-forward and the provisioned locked HDD with COW writes. The existing
**768 MiB** process cap is unchanged.

- All **270 checkpoint rows** and **28 captures** match the preceding CPU-vertex
  reference byte for byte.
- All **19 AOT summaries, 5,434 region rows, 8,306 exit rows and 19 idle/device
  census rows** match exactly.
- The input route completes with endpoint-2 count **3,018** and aggregate
  count **3,019**. Native, fallback and idle instruction totals are unchanged.
- GPU preparation covers **118,736 batches / 148,192,733 vertex executions**.
  It accepts **116,281 batches** and makes **109,742 GPU-buffer draws**; accepted
  batches producing no visible triangles do not become invented draws.
- **111,374 dispatches** use specialized programs. There are **3 compile attempts,
  118,733 cache hits and 1 compile failure**. Geometry declines total **2,455**,
  with zero numeric/opcode declines or mismatches. CPU materialization runs for
  **5,901 batches / 5,724,235 executions**, including other admission failures.

The 23- and 24-operation live programs compile successfully. The 11-operation
program hits NVIDIA's `C9999` internal allocation failure during linking under
the memory cap. Its failed entry uses the real generic GPU program for subsequent
draws. The warning remains in stderr; this is not full specialization coverage.
It also exercises failure caching in a real driver rather than a fabricated test.

## Measurement limits and evidence

The functional run's observer is healthy: **3,268 samples**, maximum gap
**140.616 ms**. It detects **79 foreign process identities**, so timing is marked
unusable for qualification. Its final window contains 123 vblanks over
**23.892 seconds** (5.148 guest vblanks per host second); this is neither a clean
comparison nor displayed FPS. Cold compilation is charged to the route. The
9B-10.5B window, including first specialization, takes **50.384 seconds**; that
whole interval is not an isolated compiler measurement.

Compile duration uses the existing profiling-gated timer. The report explicitly
marks `profiled=0` when duration is not collected. No additional clock reads are
introduced into ordinary runs. Shader compilation can take minutes for larger
authored arithmetic programs on this host, so cached steady-state execution alone
cannot qualify the product. Uploads, packing and synchronization remain separate
costs.

Private artifacts are under `build/menu60-vsh-specialize-20261003/`:
`tests-r4.log`, `probe-verify-r4.log`, `probe-deferred-r4.log`, and
`route-r5-validation.json`. The live prefix is `build/menu60-vsh-specialize-r5`.
The immutable executable `xlle-specialize-r4.exe` has SHA-256
`65cff27544b2791068c8395dddbb6dbab89079a704d34e7611af4dd44cf0e2b2`.
An earlier `r4` launch rejected a misspelled HDD-security option before boot;
only the corrected `r5` route is used for live qualification.

Final image SHA-256:
`4044cbd73dec3e8afbb5c69d3ba75cab0e436597f9e6bedc320812ac4a8ee671`.
No private guest code, shader vectors, machine assets or captures are committed.
Quiet repeated performance, sustained original-speed 60 FPS, distinct frames,
input latency, broader dashboard/audio coverage and full CPU AOT remain open.

## Attribution changes the next experiment

A same-executable, single-pair 27B diagnostic compares generic GPU interpretation
with specialization. Both arms retain all 270 checkpoints and 28 captures, and
the same GPU preparation/draw/fallback populations. Both have host contamination;
these are attribution observations, not a quiet retention gate.

| Measured component | Generic GPU | Specialized GPU |
| --- | ---: | ---: |
| GPU vertex execution, 118,736 queries | 20.684 s | 25.369 s |
| GPU triangle packing, 118,736 queries | 12.407 s | 12.568 s |
| CPU complete preparation, inclusive | 88.524 s | 112.637 s |
| CPU shader compilation, included above | 0 | 28.542 s |
| CPU status read, including waits | 63.633 s | 63.418 s |
| CPU control read, including waits | 19.883 s | 16.206 s |
| CPU upload calls, inclusive | 2.597 s | 2.345 s |

GPU durations overlap CPU waits and must not be added to them. The observations
do not support promoting specialization: its GPU duration is higher here and
compilation remains costly. It stays off by default. Evidence is in
`cost-r6-validation.json` and `build/menu60-vsh-cost-r6-leg{00-A,01-B}-r1*`.

The two synchronous reads are a larger measured opportunity. The follow-up
`--nv2a-vsh-gpu-status-summary 1` aggregates every execution's status in the GPU
packing pass and places numeric/unsupported/invalid bits in the existing 16-byte
control result. It includes executions outside the triangle reference set and
incomplete trailing triangles. The parallel implementation uses a bounded shared
reduction after ordered packing; the serial implementation scans the same domain.

Deferred mode then reads the control record once, omitting the per-vertex status
download and CPU scan. Invalid statuses still stop execution. Numeric or opcode
declines still materialize the actual CPU result before any destination draw.
Verification modes retain full status/output reads and compare the GPU summary
with an independent CPU scan. Explicit GL barriers and the blocking control read
remain; this does not claim an asynchronous handoff or remove required waits.
The summary option is independent of specialization and defaults off.

All **79 tests pass** after this change. The real GPU probe expands to **30
cases**, including a NaN in an unreferenced trailing vertex that must still
decline. Verified, deferred and serial summary runs pass with **24 GPU-draw cases,
six genuine declines and zero mismatches**. Deferred runs omit **29 status reads**;
the malformed-layout case declines before dispatch. Generic-GPU summary and the
original two-read path also pass. Their logs are `probe-*-r7.log` and
`tests-r7.log`. Live same-executable summary qualification is recorded separately.

## Status-summary live result

The same executable runs the complete 27B route with specialization disabled,
first with the original status read and then with the GPU summary. Both arms
match all **270 checkpoints, 28 captures, 19 AOT summaries, 5,434 region rows,
8,306 exit rows and 19 idle/device rows** against the preceding reference.
Both complete the input route at endpoint-2 count 3,018 / aggregate 3,019 and
have empty stderr. All GPU preparation, geometry-decline, draw and CPU-fallback
populations above are unchanged.

The summary eliminates **118,736 per-execution status downloads**. Downloaded
bytes fall from **594,670,708 to 1,899,776**, leaving one 16-byte control result
per prepared batch. The full GPU status domain is still evaluated; this is an
implemented reduction, not an assumption that execution succeeded.

| Measured component | Original reads | GPU summary |
| --- | ---: | ---: |
| CPU complete preparation, inclusive | 74.546 s | 70.257 s |
| CPU status read, including waits | 56.739 s | 0 |
| CPU control read, including waits | 12.922 s | 66.649 s |
| CPU validation | 1.324 s | 0.423 s |
| GPU vertex execution, 118,736 queries | 21.337 s | 20.916 s |
| GPU triangle packing, 118,736 queries | 11.851 s | 13.077 s |

Most of the wait moves into the remaining control read. These waits also drain
earlier GPU work; they are not isolated transfer durations. GPU times overlap
CPU waits. Removing status bytes alone does not remove the synchronization or
the compute and raster work preceding it.

This is another **profiled diagnostic**, not a performance promotion. Both
observers are healthy (2,958 / 2,667 samples; maximum gaps 138.911 / 138.497 ms),
but record 319 / 278 foreign process identities. Both explicitly mark timing
unusable. The last windows contain 123 vblanks over 28.170 / 28.075 seconds;
these are neither distinct displayed FPS nor sustained original-speed results.
The summary remains opt-in, and the earlier rejection of the GPU path against
CPU execution has not been overturned by this comparison within GPU mode.

Evidence: `summary-r8-validation.json`,
`build/menu60-vsh-summary-r8-leg{00-A,01-B}-r1*`, and the immutable
`xlle-summary-r7.exe`, SHA-256
`2b761d5f70a68ca21482f88cd3be6ee78dd4b9a42aeb9354a1c63ebca033ba86`.

The next performance decision must compare the improved GPU path with the CPU
vertex floor, then target the remaining execution and submission costs. Further
specialization is not justified by the measured compiler/GPU result. A native
vertex-stage implementation borrowed from the donor renderers remains a larger
candidate, with its actual numerical and completion contract to qualify.

The restored ordinary Release build also passes **79/79 tests** in **82.21 s**,
with `XLLE_AOT_PRIVATE_INPUT` empty and `XLLE_VSH_GPU_RESIDENT=OFF`. Logs are
`configure-ordinary-r9.log`, `build-ordinary-r9.log` and `tests-ordinary-r9.log`
in the private evidence directory. The preserved experimental executable above
is independent of this restored build.
