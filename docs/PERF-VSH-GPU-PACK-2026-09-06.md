# Parallel resident triangle packing

Owner: `beads-xbox.1.15.11`. This follows the
[verified resident handoff](PERF-VSH-GPU-RESIDENT-2026-09-06.md).
The target remains faithful LLE 60 FPS with headroom. Optimizing an optional
verification bridge does not establish that target or a shipping speedup.

## Measure the actual bottleneck

The resident bridge's existing GPU timer grouped both compute passes with
graphics draws. Compute queries now carry separate VSH and pack labels while
remaining part of the existing aggregate draw total. Results print after the
existing shutdown query drain. Dropped/nested queries and query availability
remain visible; each complete measured run collected all 11,794 VSH and 11,794
pack queries, with zero dropped or nested queries.

CPU phases use the existing profiling-gated `ScopedAdd` timer. These are raw
inclusive wall times: `total` includes its child phases plus setup/destruction.
They are not new exclusive buckets, and GPU durations overlap blocking CPU
calls. Never add CPU readback wait to GPU execution time. Each buffer readback
has a distinct label so the first call's synchronization is not confused with
the number of bytes it transfers. No clocks or queries are issued by these
additions when profiling is off.

Two canonical 12 B attribution observations selected and checked the change:

| Region | Serial pack observation | Parallel pack observation |
| --- | ---: | ---: |
| GPU VSH | 1.970 s | 2.930 s |
| GPU pack | 60.465 s | 1.467 s |
| CPU complete verified handoff | 67.894 s | 10.836 s |
| CPU uploads | 0.547 s | 0.540 s |
| CPU first output readback, including wait | 64.248 s | 7.151 s |
| CPU packed-vertex readback | 0.797 s | 0.845 s |
| CPU comparison | 0.516 s | 0.486 s |

Both upload 11,692,174,436 bytes and download 7,883,785,072 bytes, including
diagnostic sentinels and complete reference verification. Both check 13,737,910
actual executions, emit 30,227,559 verified resident vertices, draw 11,789 GPU
batches, and fall back on five geometric declines. All 28 image checkpoints and
checked guest state/timeline fields match the published normal configuration.
These instrumented observations establish where time is spent; the interleaved
noperf set below is the performance gate. They do not establish a VSH saving:
the VSH observation increased, and its cost still needs investigation.

The initial claim that verification traffic should be optimized first was
rejected by measurement. The serial pack consumed about 60 seconds of GPU time,
which the first readback waited for. Simply reading fewer bytes would still
wait for that kernel before the framebuffer could be drawn.

## Stable parallel algorithm

`--nv2a-vsh-gpu-pack-parallel 0|1` selects serial or parallel packing in an
`XLLE_VSH_GPU_RESIDENT=ON` build with nonzero resident mode. The normal build
refuses the enabled experiment. Both options initially default to OFF.

One 256-thread workgroup processes contiguous rounds of up to 256 candidates.
Every lane participates in all shared-memory barriers, including unused tail
lanes. An inclusive prefix scan computes accepted counts and combines decline
bits. Each accepted triangle writes to its prefix-derived position after the
previous round's output. Completion order cannot change triangle order.

Only lane zero advances the shared output base, after the round's writes and
before the next round. Capacity overflow emits only the first whole triangles
that fit, marks later accepted candidates as overflow, and sets the same batch
decline bit as the serial algorithm. All output remains private scratch until
the unchanged verification/fallback boundary accepts it.

Both shaders share the same geometry, explicit floating-point semantics,
culling/winding, packing helpers, bindings, uniforms, and control layout.
The original serial main remains available as the counterfactual. The scan
uses two shared arrays and two barriers per step; it is not presumed optimal.
There is still only one pack dispatch and one pack workgroup per batch.

## Validation

The actual-renderer probe passes 20 cases with either algorithm: 16 GPU-buffer
draw cases and four exact CPU fallbacks. Additions cover 255/256/257 candidates,
769 candidates with mixed culling, the 5,461-triangle maximum list, and a late
geometry decline after a complete 256-candidate round. Per-triangle colors vary
so reordering cannot hide behind identical vertex contents. The renderer checks
all packed vertex bytes against CPU expansion as well as final framebuffer bytes
and triangle result counts. Existing strip/fan, cache, signed-zero, malformed
operand, numeric fallback, and X8 alpha repair cases remain covered.
The renderer allocates three output vertices per candidate, so a deliberately
undersized output capacity is not exercised by these renderer cases. Overflow
equivalence was reviewed in source; it is not claimed as a device-tested case.

All 41 CTest tests pass. Attribution logs, exact binaries, probe results, and
complete fidelity comparisons remain private under `build/vsh-resident-cost/`.
The same-binary interleaved 3+3 noperf gate passes. Arm A uses resident mode 2
with serial packing; arm B changes only `--nv2a-vsh-gpu-pack-parallel 1`.

| Total 12 B wall | Serial | Parallel | Reduction |
| --- | ---: | ---: | ---: |
| Median | 118.142 s | 61.355 s | 48.066% |
| Minimum | 117.205 s | 60.301 s | 48.551% |

All three pairs passed the process census on their first attempt, with no
contamination or excluded legs. All six runs use executable SHA-256
`76b3bf1f2949ebbfdbb5f0843264f4a90fba874dbb75e57e5aee26db783f6d68`.
Every run matches all 28 images and checked guest state/timeline against the
published normal build. Evidence: `pack-ab-r1-abba-gate.json`,
`pack-ab-r1-pair-selection.json`, and `pack-ab-r1-fidelity.json` in the private
directory above. The preserved renderer probe SHA-256 is
`c9b0c7c652b2de4b67c14e98ce2e5fd0668b7a684cb5d5573fd87e38f9699355`.

This is a reduction in the experimental verified bridge's runtime relative to
its serial counterpart. CPU reference work and verification transfers remain;
the result does not establish a speedup over the shipping CPU vertex path.

Restoring all VSH diagnostic CMake options to OFF produces normal executable
SHA-256 `8331a8921963e7cd6e58b89d0752a073357ddd45c91f11fd5225770c70f10bc8`.
It passes all 41 CTest tests, refuses the enabled experiment before boot with
exit 2, and matches all 28 canonical images and checked guest state/timeline
against the preceding published normal build and the parallel diagnostic arm.
See `ctest-default.log`, `default-refusal.log`, and
`default-final-fidelity.json`. The publication-tree secret scan covers 192
files with zero findings; binaries, screenshots, and raw captures remain private.

## Next decision

The verified bridge still executes CPU VSH and triangle preparation, uploads
sentinel-filled scratch, and reads back complete GPU outputs. After this gate,
measure the remaining GPU execution/synchronization against the CPU work that
can actually be removed. Investigate specialization of decoded VSH operations
without changing explicit FP32 semantics if dynamic GPU interpretation is too
expensive. Then remove CPU materialization only behind state admission and
saved-input fallback, preserving guest fetch ordering and index-cache reuse.
Headless fidelity and distinct interactive frames/latency remain separate proof
requirements. No default shipping behavior is promoted by this checkpoint.

Follow-up: [deferred CPU vertex/triangle work](PERF-VSH-GPU-DEFERRED-2026-09-06.md)
implements saved-input fallback and actual CPU work removal. Its first complete
CPU-versus-GPU gate rejects the candidate; the GPU preparation and synchronization
still cost more than the work removed.
