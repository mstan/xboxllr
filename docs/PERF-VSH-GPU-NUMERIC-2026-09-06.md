# GPU vertex arithmetic probe

Tracker: `beads-xbox.1.15.11`. Starting revision: `1befefe`.
Goal: faithful LLE at 60 FPS with headroom. This standalone diagnostic supplies
arithmetic counterexamples before a combined GPU vertex/triangle path is built.
It requires no firmware, disk, EEPROM, guest program, or console access.

Build with `-DXLLE_VSH_GPU_NUMERIC_PROBE=ON`, then build and run the
`xlle_vsh_dot_probe` target. SDL must be enabled. The tool creates its own hidden
GL 4.3 context; it is separate from the emulator and is not a CTest assertion
that every host GPU must match. Exit 0 means every sampled output lane matched,
2 means setup/dispatch/readback failed, and 3 reports arithmetic mismatches.
The option defaults OFF and adds no runtime emulator hook.

Using the compiler/toolchain setup from the README:

```powershell
cmake -S . -B build-release -DXLLE_VSH_GPU_NUMERIC_PROBE=ON
cmake --build build-release --target xlle_vsh_dot_probe
.\build-release\xlle_vsh_dot_probe.exe
```

The CPU oracle calls the actual pinned `nv2a_vsh_cpu_dp3/dp4` library after the
interpreter's operand multiplication by positive one. It uses the machine's
screen-coordinate truncation and signed W-clamp expression. The GLSL candidate
explicitly performs per-product finite-overflow handling, left-associated sums,
and final infinity handling. It avoids native `dot` and FMA and marks arithmetic
intermediates `precise`; those measures are not a proof of numerical equivalence.

Each 32-byte input record contains two raw four-lane operands. A compute
invocation emits four raw result words: DP3, DP4, screen truncation of DP4,
and W clamp of DP4. All requested words are compared; the unused output region
is initialized with sentinels and checked after readback. The tool prints first
mismatches per category/lane and input words for W counterexamples.

## Observed result

On the measured RTX 3080 Ti with GL 4.3 / NVIDIA 616.86, the final 200,013-record
corpus reports 451 mismatched lanes and an intact output guard (exit 3):

| Synthetic population | Records | DP3 mismatches | DP4 | Screen | W |
| --- | ---: | ---: | ---: | ---: | ---: |
| Bounded finite | 100,003 | 0 | 0 | 0 | 0 |
| Random normal finite, wide exponents | 100,000 | 241 | 114 | 41 | 41 |
| Frozen normal-input W counterexample | 1 | 1 | 1 | 1 | 1 |
| Subnormal | 1 | 1 | 1 | 0 | 0 |
| Infinity | 1 | 1 | 1 | 1 | 1 |
| NaN payloads | 1 | 1 | 1 | 1 | 1 |
| Other directed cases | 6 | 0 | 0 | 0 | 0 |

The bounded population is generated from signed integers divided by 1024;
zero observed mismatches in that sample does not prove all values in a range
are supported. The directed cases are selected counterexamples, not exhaustive
IEEE coverage. The frozen W case duplicates one discovered random input so it
remains an explicit regression fixture independent of the random-corpus size.

Its inputs are all normal finite floats. The CPU DP4 result is `0x80000122`;
the GPU returns `0x00000000`. Screen truncation differs by signed zero, while
the final W clamp changes from `0x9f800000` (-2^-64) to `0x1f800000` (+2^-64).
If stored in position W, that sign changes the primary fixture's behind-W
classification. This supplies a potential behavioral difference, beyond NaN
payload or diagnostic-hash disagreement. No replay image changed: this probe
never replaces CPU emulation or submits guest geometry.

## Consequence for the next experiment

An input-finiteness check is insufficient. Normal operands can produce tiny
intermediate values, and this GPU candidate loses behavior used by later
geometry handling. Either implement and validate exceptional arithmetic, or
prove conservative operand/intermediate bounds for a supported decoded subset
and preserve CPU fallback outside it. A successful synthetic arithmetic probe
still would not establish full-program dual-issue/output-alias behavior,
batch index reuse, clipping/culling equivalence, or destination ordering.

Do not select programs by fingerprint. For a later runtime shadow comparison,
sample actual VSH executions rather than index-cache hits, and keep CPU output
authoritative. A production path must move the downstream triangle consumers
with VSH and define how unsupported geometry declines before destination writes.
Neither this probe nor its readback cost is a performance claim.

Private development evidence is under `build/vsh-gpu-shadow-prototype/`.
The initial corpus reported 447 differences; freezing the discovered W case
adds four repeated lane comparisons to the final count. The additional
all-negative-zero case matches. The standalone target initially left the
allocation experiment's `9ed21fa9...` emulator executable untouched. Restoring
the normal configuration rebuilt it as
`22c2a3a33999defcf92305dbeb3f5a191a6454c99e5e99674b602571a6a5eb9d`;
do not treat those executable identities as interchangeable. The normal
configuration passes all 39 CTest tests. The rebuilt default's separate
12 B replay matches all 28 PPM checkpoints and the checked guest state/timeline
against the allocation experiment's OFF arm. This is a fidelity-only comparison;
no cross-build timing claim follows. The publication-tree scan covers 175 files
with zero findings.
