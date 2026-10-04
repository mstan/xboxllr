# Guarded native GPU arithmetic

Tracking: `beads-xbox.1.15.11`. Source baseline: `ebd5e87`, under the active
[dashboard menu goal](GOAL-AOT-DASHBOARD.md). This experiment is opt-in and does
not establish sustained 60 FPS or full CPU AOT.

## Why this experiment

The same immutable executable compares CPU vertex processing with deferred GPU
processing plus the status summary over the complete 27B Settings/return route.
Both reproduce **270 checkpoints, 28 images, 19 AOT summaries, 5,434 region rows,
8,306 exit rows and 19 idle/device rows**. Both complete the input script with
endpoint-2 count 3,018 / aggregate 3,019 and empty stderr.

The diagnostic attributes 36.989 seconds to CPU vertex work and 32.438 seconds
to generic CPU raster preparation in arm A; these fall to 21.011 / 18.946 seconds
in GPU arm B. Its GL bucket grows from 34.100 to 95.953 seconds. GPU preparation
is 71.822 seconds inclusive, with 67.934 seconds in the blocking control read.
GPU timestamps report **23.826 seconds of VSH execution** and **14.338 seconds
of packing**. GPU times overlap CPU waits and are not additive.

This does not qualify a speed difference. Both healthy observers detect foreign
work (112 / 117 process identities); the instrumented total walls are 322.083 /
369.354 seconds. Diagnostic checkpoints alone also differ by 14.758 seconds.
There is no basis to promote the GPU route over the CPU floor. The result points
to shader execution and synchronization rather than more status-byte reduction.
Evidence is `build/menu60-vsh-cpu-r10-leg{00-A,01-B}-r1*` and
`build/menu60-vsh-native-20261003/cpu-r10-validation.json`.

## Arithmetic contract

`--nv2a-vsh-gpu-native-fp32 1` enables native single-precision multiplication and
addition only in conservative normal-value ranges. It requires a nonzero GPU
resident mode and defaults to zero. Zero selects the existing explicit-rounding
GPU implementation; resident mode zero selects the actual CPU vertex path.
The ordinary build still excludes the GPU resident experiment.

The arithmetic helpers retain integer representations between operations. NaN
and invalid-operation handling precede admission. Signed-zero multiplication
and zero-addition identities are implemented with integer bits, including
negative zero plus negative zero. For other operations:

- Multiply requires two normal operands with biased exponent sum 128 through
  378. The magnitude of the exact product lies from 2^-126 inclusive to 2^126
  exclusive, avoiding subnormal results and overflow.
- Add requires both biased exponents from 24 through 252. Each operand is an
  integer multiple of 2^-126, so a nonzero cancellation cannot be subnormal.
  The magnitude bound excludes overflow. Exact cancellation returns positive zero.
- Every other range uses the existing FP64 operation and explicit binary32
  rounding. Reciprocal, square root, overflow clamps and numeric declines retain
  their implementations. No unsupported case is returned as success.

Each native operation has a `precise` intermediate. Dot products keep their
separate rounded products and sequential adds; MAD does not become a fused
operation. Paired instruction reads, output masks, vertex/cache ownership,
triangle packing and blocking completion remain unchanged. The runtime uniform
allows toggling the arithmetic without changing program identity or using guest
addresses as admission keys. Dispatch counters identify selected dispatches,
not a claim that every operation used the native branch.

The bounds address the [previous native-float counterexample](PERF-VSH-GPU-NUMERIC-2026-09-06.md):
normal inputs alone can still produce subnormal intermediates and alter W-sign
processing. They do not prove identical rounding on every GPU. GLSL permits
subnormal flushing and does not select the CPU's rounding mode; `precise`
constrains operation ordering and contraction. Device qualification is still
required. [Khronos precision and operation rules](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#range-and-precision),
[precise qualifier](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#the-precise-qualifier).

All three pinned xboxrecomp `d3d8_vsh.c` emitters were re-read at MUL, ADD, MAD
and dot generation. They use native HLSL arithmetic and `dot`, reinforcing the
opportunity to use the hardware's arithmetic. Their code does not supply this
boundary's exceptional-value or operation-order qualification. This change
adapts the native-arithmetic approach within our existing shared helpers; it
imports no additional donor translator or HLE shim.

## Numeric and program qualification

The numeric probe retains the existing broad finite, subnormal, signed-zero,
NaN, overflow and rounding corpus, with **20,812 additional boundary cases**.
Four new independently fixed CPU-oracle results straddle normal/subnormal
cancellation and product admission. The probe sets and reads back the selected
uniform before dispatch. Full-program, captured-program and actual resident
rendering checks use the same helper implementation.

On the RTX 3080 Ti / NVIDIA 616.86 host, both explicit rounding and the guarded
native path compare **323,447 of 323,452 numeric records** with **zero mismatched
lanes**. Both preserve all five expected NaN/invalid declines and all sentinel
guards. Fourteen independent fixed-value CPU-oracle checks pass. Exit code 4
reports expected declines, not silently accepted results.

The native program self-test revalidates **10,039 programs**, with two numeric
and one opcode decline and zero mismatched lanes. The private dashboard capture
revalidates **2,623 vertices**, with six numeric declines and zero mismatched
lanes. The batch probe matches **19 batches / 260 executions / 263 references**,
including five expected declined batches and all ordered geometry/guard checks.
Logs are `numeric-{reference,native}-r1.log`, `program-native-r1.log`,
`captured-native-r1.log` and `batch-native-r1.log` under
`build/menu60-vsh-native-20261003/`.

All **79 Release tests pass** in the private-input experimental build, in
**81.79 seconds**. The real resident renderer passes **30 cases**, including
24 GPU-draw cases, six genuine declines and zero mismatches, in verification,
deferred and serial-pack modes. The arithmetic opt-out passes the same cases.
Native selection is recorded on 29 dispatches; the malformed-layout case never
dispatches. Verification compares raw vertex outputs and packed vertices in
addition to full rendered bytes; deferred execution retains actual GPU draws.

The private executable `xlle-native-r1.exe` has SHA-256
`e749abb3d9cbf6dd8c6e99c66f9593e3660117093e5843abeadaa252195d5cd`.
It grows by **2,154 bytes** over the preceding experimental executable. Logs
are `tests-r1.log` and `resident-*-r1.log` in the same evidence directory.
The specialization combination also passes all 30 rendered cases and complete
CPU/GPU comparisons, with seven successful compiles, 27 specialized dispatches,
20 cache hits and zero failures. No private captures, guest programs or machine
assets belong in the source commit.

## Live result and decision after the owner check-in

The existing r2 pair has now been validated; no new emulator run was needed.
Both arms exit zero, have empty stderr, and exactly reproduce the reference's
**270 checkpoints, 28 images, 19 AOT summaries, 5,434 region rows, 8,306 exit
rows and 19 idle/device rows**. Both complete all 14 input events, including the
return to the dashboard, with endpoint-2 count 3,018 / aggregate 3,019. The final
image SHA-256 is
`4044cbd73dec3e8afbb5c69d3ba75cab0e436597f9e6bedc320812ac4a8ee671`.
The native option is observed on all 118,736 dispatches in B and zero in A.
Deferred mode does not compare every live vertex against the CPU; its
`verified=0` counter is preserved and the route comparisons are bounded evidence.

| Diagnostic (seconds unless stated) | Native option off | Native option on |
| --- | ---: | ---: |
| Final settled-menu window, 123 guest vblanks | 24.496 | 24.862 |
| Guest vblanks per host second in that window | 5.021 | 4.947 |
| Cumulative instrumented exclusive wall | 317.472 | 309.667 |
| GPU vertex shader timestamps, entire route | 21.577 | 10.195 |
| GPU packing timestamps, entire route | 12.607 | 11.355 |
| Host blocking control-read time, entire route | 62.434 | 55.320 |

These are not qualified timing results. Healthy observers recorded **100 / 77
foreign process identities**; both arms have `timing_usable=false`, and detailed
profiling was enabled. The final menu window is 25.5B-27B, after the return at
24,488,199,989. The frame-series and exclusive timers have different accounting
boundaries; do not mix their walls. GPU durations overlap host waits and must
not be added to the exclusive partition. Guest vblanks are not displayed FPS.
Evidence: `build/menu60-vsh-native-r2-leg{00-A,01-B}-r1*` and
`build/menu60-vsh-native-20261003/route-r2-validation.json`.

**Decision: keep the option off and park further GPU vertex arithmetic or
specialization expansion.** The shader mechanism shows a useful signal, but
the settled menu is not faster in this diagnostic. The busy host prevents
concluding either a true speedup or regression. Even the observed 11.382-second
shader reduction is only 3.6% of A's cumulative instrumented wall before
considering overlapping waits; it is not a route to a 60 FPS claim by itself.
Reopen this branch only with a menu-level measurement or a different measured
submission/residency opportunity, not another arithmetic test corpus.

Next, answer one already-implemented question: does bulk CPU register transfer
improve the settled menu with the reference renderer? Use the same executable,
public-register opt-out and unprofiled quiet-host paired runs. The CPU-floor r10
final exclusive window attributes 5.422 seconds to native bodies, 1.823 to
native handoff and 1.394 to CPU fallback, out of 26.083 seconds. Eliminating all
handoff would save at most about 7% of that diagnostic window; register transfer
is only part of it. This bounds the opportunity and does not predict a win.
Make that decision before adding another optimization. The applied loop is
recorded in [PERF-METHODOLOGY.md](PERF-METHODOLOGY.md).

No new build or emulator benchmark was launched for this follow-up. The source
candidate is uncommitted and disabled by default. The local `build-release`
configuration is still the private-input GPU experiment; restoring and checking
the ordinary configuration is pending before any eventual candidate integration.
