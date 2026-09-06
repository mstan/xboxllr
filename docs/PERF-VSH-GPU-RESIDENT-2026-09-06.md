# GPU vertex buffer handoff

Owner: `beads-xbox.1.15.11`. This follows
[combined batch validation](PERF-VSH-GPU-BATCH-2026-09-06.md) and targets the
resident primary-color population selected by the
[overlap census](PERF-VSH-GPU-OVERLAP-2026-09-06.md). Faithful LLE 60 FPS with
headroom remains the goal; this bridge is not a performance promotion.

## Implemented boundary

`XLLE_VSH_GPU_RESIDENT` defaults OFF. An enabled build accepts
`--nv2a-vsh-gpu-resident 0|1|2`: off, verify then draw the CPU buffer, or verify
then draw the GPU buffer. Normal builds refuse nonzero modes before boot.
The compiled CPU VSH reference is required, and unsafe VSH probes are refused.

Each eligible producer batch owns one decoded program and one constant bank,
256 input bytes per actual VSH execution, and the submitted-reference mapping.
The existing direct cache's hits, replacements, and guest fetch ordering remain
authoritative. The owned reference travels through `DrawJob`; its capacity is
included in queued-job accounting. No guest program fingerprint admits a batch.

The first compute dispatch executes shared-data VSH with explicit FP32 rounding.
The second classifies triangles, preserves culling/winding, and compacts accepted
triangles in source order into the **actual resident graphics VBO layout**:
96 bytes of position, diffuse color, and four texture coordinates. This is not
the separate compute fallback's packed vertex layout. Shader-storage and vertex
attribute barriers precede consumption by the existing VAO, vertex/fragment
shaders, draw order, and X8 alpha repair pass. Binding indices stay within 0..7.

Mode 2 skips the CPU's final 96-byte VBO staging and upload. CPU VSH execution,
transformed vertices, CPU triangle expansion, diagnostic readbacks, and exact
comparisons still run. Therefore this mode cannot establish the saving from
removing those CPU stages. The initial pack dispatch is serial, and buffers are
respecified for each batch; neither is assumed efficient.

## Admission and fallback

Host validation checks bounded shapes, reference IDs, operand/destination array
indices, swizzles, masks, and paired-operation layout before dispatch. Address
and relative operands remain on CPU. ARL/EXP/LOG/LIT produce explicit GPU program
declines; numeric uncertainty also declines rather than approximating.

Finite positions/colors, W classification, degeneracy, and front/back culling
follow the CPU triangle path. Lists ignore incomplete final triangles. Dropped
or culled triangles compact away without rejecting the batch. A declined
execution anywhere in the batch conservatively forces fallback, including one
only referenced by an incomplete final triangle.

For large targets, accepted XY must lie on a 1/16 lattice within +/-62,500.
Differences scaled by 16 are integers of magnitude at most 2,000,000; the area
numerator is below 2^43. Binary64 products and subtraction are consequently
exact in this admitted domain. Outside it the whole batch uses CPU fallback.
The fixed-target path retains the CPU's rounded-coordinate bound.

Both dispatches write private scratch only. Status/control values, unused
output guards, every accepted raw/postprocessed VSH word, and every packed
vertex are checked before destination surface preparation. Declines draw the
existing CPU packet. A verification mismatch stops the draw; it cannot silently
be counted as a successful fallback.

## In-process compiler finding

The initial synthetic renderer probe passed, but the first canonical replay
failed to link the shared VSH program at the first eligible dashboard batch,
after 9,427,655,781 guest instructions. Fixing the existing compute link error
handler to read the **program** info log exposed NVIDIA's `C9999` allocation
failure. The preceding empty shader log did not identify the cause.

Opt-in shader preparation now occurs after installing the unchanged 768 MiB
hard process limit and before allocating guest RAM/TBs and resident surfaces.
This addresses overlap with compiler temporary allocations without increasing
the limit. Success of a standalone GL probe alone is insufficient evidence that
the renderer can operate inside the emulator's memory budget.

## Validation and next work

Private build, probe, replay, and fidelity evidence is kept in
`build/vsh-resident/`. The 14-case real-renderer probe checks 11 GPU-buffer draw
cases and three exact CPU fallback cases (numeric, geometry bound, malformed
operand). Visible lists, strips, fans, cull compaction, cache collisions/hits,
incomplete triangles, signed-zero W, and X8 alpha repair are covered. A larger
198-execution structural case crosses workgroups; its tiny triangles cover no
pixels, so it is not counted as evidence of visible rendering. The other visible
cases must change their initialized framebuffer. Mode 1 must verify without
incrementing GPU-draw counters. Returned triangle counts are checked separately.

The first full mode-1 run completes under the unchanged memory cap, with
11,794 batches, 13,737,910 actual executions, 57,610,149 submitted references,
and 30,227,559 accepted expanded vertices. It verifies 11,789 batches, declines
five, and reports zero mismatches. All 28 images and checked guest state/timeline
fields match the published normal build.

The initial full mode-2 run also matches every image and makes 11,789 GPU-buffer
draws, but the strict comparison catches an incorrect triangle result counter:
it still used the now-empty CPU staging vector. The draw counter now uses the
actual resident vertex count, and the probe asserts the result. The corrected
mode-2 replay matches all 28 images and the complete checked guest state/timeline
against mode 1 and the published normal build. It draws all 11,789 verified
batches through GPU-produced buffers. The five declines are geometric; layout,
numeric, and opcode decline counts are zero on this trajectory. Mismatches are
zero. These are canonical 12 B results, not interactive navigation coverage.

Final diagnostic executable SHA-256:
`761341bfcb7e25875a8b9fadd366f2e03d2548ee6b53a229846eef3af3efdca0`.
Final renderer probe SHA-256:
`e82f5c39da129185cf7eeca9e1e09d684ca528a0c817176b6e67f6d1af9d113e`.
Evidence: `mode1-r3`, `mode2-r2`, `mode2-r2-fidelity.json`,
`resident-probe-r4.log`, and `ctest-diagnostic-r2.log` (41/41).
The unchanged factored arithmetic also passed the existing 10,039-record
complete-program and 19-batch combined regressions, with expected declines and
zero accepted-output mismatches. There is no interleaved performance gate for
this diagnostic bridge and no claim that it accelerates emulation.

Restoring all VSH capture/probe/resident CMake options to OFF yields normal
executable SHA-256
`f332084085fecc76dc809ccea019da814d941d2bcf1e9b94b05b6fb599a0b116`.
All 41 CTest tests pass, the normal build refuses nonzero resident modes before
boot, and its canonical 12 B replay matches all 28 image checkpoints and checked
guest state/timeline against the preceding published normal build and both
validated diagnostic modes. See `ctest-default.log`, `default-refusal.log`, and
`default-final-fidelity.json`.

First attribute the GPU VSH dispatch, ordered pack, and synchronous readback
costs in this real renderer path. A correct GPU interpreter is not assumed to
be fast; CPU work removed cannot pay for arbitrary dispatch/transfer overhead.
Before making a performance candidate, separate vertex-independent renderer
admission from CPU output construction, retain actual fetched inputs for whole-
batch CPU fallback, and remove reference execution/expansion only for admitted
work. Do not refetch guest attributes on fallback or duplicate cache hits.
Measure status synchronization, stable parallel compaction, buffer reuse, and
upload volume together with the removed work. Require complete image/state/
timeline fidelity followed by quiet same-session interleaved 3+3 noperf gating
on both minimum and median, then distinct interactive frames and latency tails.

## Reproduce

Follow-up: [parallel triangle packing and cost attribution](PERF-VSH-GPU-PACK-2026-09-06.md)
identifies and removes the serial pack bottleneck while retaining this verified
handoff and the serial counterfactual.

Configure `XLLE_VSH_GPU_RESIDENT=ON` using the README's Release toolchain, then:

```powershell
.\build-release\xlle_vsh_resident_probe.exe
.\tools\run_stockdash_replay.ps1 -Label resident-local -ExtraXlleArgs @('--nv2a-vsh-gpu-resident','2')
```

The renderer probe requires real OpenGL execution. Exit 0 means all comparisons
passed; unavailable GL/build capability returns 2, and comparison failure returns
1. Restore the CMake option to OFF and validate the normal build before shipping.
