# Deferred CPU vertex and triangle work

Owner: `beads-xbox.1.15.11`. This builds on the
[parallel GPU pack](PERF-VSH-GPU-PACK-2026-09-06.md). The objective remains
faithful LLE 60 FPS with headroom. This opt-in experiment is not promoted to
the normal build or enabled by default.

## Contract

`XLLE_VSH_GPU_RESIDENT=ON` adds mode 3 to `--nv2a-vsh-gpu-resident`.
Modes 1 and 2 retain complete CPU reference execution and comparison. Mode 3
keeps attribute fetches and the existing index-cache mapping on the CPU, saves
actual execution inputs, decoded operations and dynamic constants, and defers
CPU vertex materialization. It does not shade duplicate cache-hit references.

The existing resident-primary state predicate is shared by CPU and deferred
packet preparation. Eligible batches execute VSH and triangle packing into
private GPU buffers. Preparation reads every execution's status and the
16-byte packed control record before permitting a draw. This includes unused
trailing executions, whose numeric/program declines must still trigger the
same conservative fallback. Complete output downloads, CPU reference outputs,
and sentinel uploads are absent in mode 3.

Acceptance produces a renderer-local, single-use receipt and a bounded vertex
count. The existing resident graphics VAO, shader, blending and X8 alpha repair
consume the prepared VBO. A receipt from another renderer, an overwritten
preparation, or a consumed draw cannot authorize another submission. A zero
vertex count is an accepted no-op. Preparation does not write the destination.

If state, backend, opcode or geometry is unsupported, saved compiled operations
run on saved inputs and constants to reconstruct CPU vertices, followed by the
existing triangle preparation and draw path. Fallback never refetches guest
attributes. Context-writing programs and diagnostics requiring CPU output stay
on the existing CPU path. No guest fingerprint selects the new path.

`verified` counts only complete CPU/GPU comparisons. `accepted` counts mode-3
preparations and must never be reported as verification. Machine counters
separately expose deferred executions/references, preparations, and CPU fallback
executions. Buffer allocation is distinct from upload traffic: only calls with
non-null source data contribute to uploaded bytes.

## Validation and measured rejection

The real-renderer probe passes all 20 existing cases in both parallel mode 2
and parallel mode 3: 16 GPU draws, four declines, zero mismatches. Mode 3
reports zero verified batches. Cross-renderer and consumed-receipt rejection
are checked. These tests validate the renderer boundary, not removal of CPU
work in the emulator.

Machine tests pass accepted batches with no CPU vertices or compiled execution,
exact saved-input fallback packets, and 96 indexed references with only three
actual executions. The indexed CPU, deferred and declined runs produce the same
guest read address/size stream. A late backend rejection of a prepared strip
matches software rendering and cannot submit raw strip references as expanded
triangles to the nonresident backend. All 41 CTest tests pass: the initial run
passed 40, and an existing source guard's adjacency assumption was corrected
before its successful rerun.

The first canonical 12 B run matches all 28 images and checked guest
state/timeline against the published normal build. It reports:

| Population | Count |
| --- | ---: |
| Deferred batches | 16,280 |
| Saved actual executions | 19,028,101 |
| Submitted references | 78,123,819 |
| CPU fallback batches / executions | 1,449 / 1,381,236 |
| GPU preparations attempted | 15,350 |
| GPU preparations accepted | 14,831 |
| GPU geometry declines | 519 |
| Resident GPU draws | 11,789 |
| Packed vertices drawn | 30,227,559 |
| CPU-verified batches / mismatches | 0 / 0 |

The difference of saved and fallback executions is 17,646,865. These avoided
CPU executions are a denominator, not a speed claim. Fully culled batches and
geometry declines now reach GPU preparation before their outcomes are known;
the verified bridge previously reached preparation after CPU expansion, so its
11,794 preparations and five declines describe a different population. The new
mode must pay for this additional GPU work as well as status synchronization.

The same-session interleaved 3+3 noperf comparison against CPU mode 0 rejects
this candidate. Both arms use executable SHA-256
`a603c6f70213f39cfa5fd92f444eea0645a266ebe865c1c9300637d2b496cd7e`.
Arm B adds mode 3 and parallel packing. All three pairs pass the process census
on their first attempt, with no excluded legs. All six legs match all 28 images
and checked guest state/timeline against the published normal build.

| Total 12 B wall | CPU mode 0 | Deferred mode 3 | Improvement |
| --- | ---: | ---: | ---: |
| Median | 50.032 s | 52.308 s | -4.549% |
| Minimum | 49.983 s | 52.299 s | -4.634% |

Removing CPU reference work alone does not pay for this GPU path. It remains
an opt-in experiment; the normal build keeps the CPU implementation.
Private artifacts are under `build/vsh-deferred/`: `cpu-ab-r1-abba-gate.json`,
`cpu-ab-r1-pair-selection.json`, and `cpu-ab-r1-fidelity.json`.

## Remaining cost

A separate profiling run preserves the full image/state result and the same
mode-3 population. It measures GPU VSH at 2.824 s and packing at 1.200 s, with
all 15,350 queries collected for each pass. The global query pool dropped four
other queries; aggregate GPU totals are therefore incomplete. No nested query
was skipped, and the resolve bracket check passes.

CPU preparation totals 6.767 s: uploads 0.268 s, first status read including
wait 5.846 s, control read 0.444 s, and status/control validation 0.122 s. These
CPU times include GPU waits; they must not be added to GPU durations. Actual
uploads total 5,183,069,432 bytes and downloads 75,195,152 bytes. Complete output,
candidate and vertex readbacks have zero calls. See `cost-r1` artifacts and
`cost-r1-fidelity.json`.

A CPU-mode attribution companion also matches full fidelity. Its inclusive
vertex-transform region is 6.071 s; deferred mode still spends 4.399 s there.
This region includes attribute fetch and input/vertex construction, so the
difference is not an isolated VSH measurement. It disproves treating the whole
CPU vertex region as removable by this offload. Resident CPU vertex staging
falls from 14,464 calls / 0.149 s to 2,675 calls / 0.005 s. The CPU companion's
global GPU pool drops 98 queries, so its GPU aggregate cannot support precise
cross-run GPU savings. See `cpu-cost-r1` and `attribution-fidelity.json`.

The next bounded shader experiment is specialization by exact decoded operation
vectors, preserving the existing arithmetic helpers and dynamic constants. Its
VSH-only ceiling is the measured 2.824 s, which cannot by itself establish a 5%
win over CPU mode 0 given the current deficit. Compilation cost belongs in the
total replay gate, including late compiles under the 768 MiB cap. Cache misses
or failed specialization must retain the generic GPU path. Buffer reuse and
status synchronization remain separate measured opportunities.

## Normal-build integration check

The restored normal build has `XLLE_VSH_GPU_RESIDENT=OFF`, passes all 41
CTest tests, and rejects mode 3 at startup. Its executable SHA-256 is
`3963a0162f9add71a551212fc3fe8eba723b3104dbd4ec63d8ec64b810e0004d`.
A canonical replay and a separate interleaved 3+3 comparison against the
published `2e2fdab` normal binary match all 28 images and checked guest
state/timeline. All six comparison legs pass the process census without
retries or exclusions.

| Total 12 B wall | Published normal | Current normal | Improvement |
| --- | ---: | ---: | ---: |
| Median | 48.471 s | 48.447 s | +0.051% |
| Minimum | 48.406 s | 48.255 s | +0.310% |

This is a default-path integration check, not a demonstrated optimization:
the 5% performance gate formally rejects it. The common preparation refactor
shows no measured slowdown in this comparison. Artifacts are
`default-ab-r1-abba-gate.json`, `default-ab-r1-pair-selection.json`,
`default-ab-r1-fidelity.json`, and `ctest-default.log` under the private
`build/vsh-deferred/` directory.

The next CPU-side audit covers repeated DMA descriptor decoding and attribute
input construction. Any candidate must preserve individual guest fetch order,
first-fault behavior, cache-hit suppression, and persistent inactive inputs.
Copy removal alone has an unmeasured ceiling; it is not presumed to satisfy
the performance gate.
