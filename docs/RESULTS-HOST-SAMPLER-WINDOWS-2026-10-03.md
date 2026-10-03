# Host sampling across the full dashboard route

Tracking: `beads-xbox.1.38`. Source baseline: `b204ffd`, following the
[qualified membership improvement](RESULTS-AOT-MEMBERSHIP-2026-10-03.md).
The [dashboard goal](GOAL-AOT-DASHBOARD.md) remains active.

## Problem and change

The previous sampler had eight 1.5B guest-clock buckets. Its final bucket
absorbed every sample at or beyond 10.5B, but printed a 10.5B-12B interval.
The symbolizer independently reconstructed the same misleading upper bound.
Complete aggregate sample counts remained useful; the final bucket could not
describe an individual post-boot interval.

The sampler now preallocates **32 tables** before starting its worker: 31 finite
1.5B intervals and an explicitly unbounded tail beginning at 46.5B. No table is
allocated or resized while sampling. Each table records the minimum and maximum
sampled guest clock, inclusive, separately from its half-open nominal interval.
Maximum 64-bit progress cannot wrap into an earlier bucket. Backwards progress
updates retain actual extrema. Empty tables produce no invented samples.

The report identifies `window_layout=2`, nominal bounds, range kind, sampled
extrema and allocated table bytes. The symbolizer uses these recorded values
instead of assuming a width or deriving bounds from the bucket index. It exports
them to JSON and rejects contradictory new-format bounds. Older last buckets
are reported as `legacy-clamped-tail`, with an unknown upper bound and unknown
sampled extrema; their sample counts are preserved.

The fixed table allocation is **33,558,784 bytes** on this host, about 32 MiB.
The increase over the old eight-table allocation is about 24 MiB and occurs
only when the optional sampler is enabled. This is diagnostic storage; there
is no product-speed claim from this change.

## Qualification

All **76 public tests pass**. New tests cover every interval boundary, the
former 10.5B clamp, 27B, backwards/zero progress and `UINT64_MAX`. A real Windows
sampling probe exercises finite buckets and the unbounded tail, verifies the
binary identity, complete leaf/sample denominators and the allocation bound.
Parser tests cover recorded and non-default intervals, malformed bounds, old
clamped tails and truncated leaf tables. Re-reading the previous real profile
preserves all **101,633 samples**, while correcting its final range description.

The real `sampler-route-profile-r1` run completes **27B** with healthy quiet-host
observation: **2,296 observations**, maximum gap **124.76 ms**, no detected
competing processes. All **270 checkpoint rows and ten images** match the scalar
reference. Compared with the prior qualified profile, all **302 AOT summary/
registration rows, 13,530 region rows, 18,576 exit rows and 55 idle/device census
rows** agree exactly. Stderr is empty; the script completes at the same guest
clock, controller report counts and endpoint state.

The new profile has **101,462 fully accounted leaf samples across 18 separate
windows**, with verified binary identity and zero overflow, suspension/context
failures or missing leaf counts. Achieved mean period is **2.469 ms**, versus
the requested 2 ms. Every observed minimum/maximum lies inside its recorded
nominal interval. The 25.5B-27B window, for example, contains 7,629 samples from
25,500,199,989 through 26,999,956,767. It lies after the verified dashboard return
at 24,488,199,989 and measures sustained main-menu animation.

Private executable `build/aot-20261003/xlle-sampler-o2.exe` has SHA-256
`8c4a79fe2d07d01705ea770c95f7406a476dbd2c7bd73c381e930d5552470c86`.
Evidence is retained in `sampler-route-validation.aot-private.json`,
`sampler-route-symbols.aot-private.json` and `build/sampler-route-profile-r1*`.
The ordinary build has private input cleared, census disabled and the qualified
membership lookup enabled.

## What the corrected windows tell us

The steady main-menu window gives these direct leaf shares of its **7,629 total
samples**:

| Host function | Share of window samples |
| --- | ---: |
| Generic combiner rasterization, outer function | 5.19% |
| Compiled vertex-shader execution | 5.06% |
| Vertex-batch submission | 4.60% |
| Native admission/execution wrapper | 3.66% |
| Generic combiner rasterization, inner lambda | 3.04% |
| Register read implementation | 2.87% |

Library leaves are separate: 21.01% are in `ntdll` and 10.09% in the CRT. The
top printed stack-walk pairs attribute 637 `ntdll` samples to nonresident
generic-combiner submission, 214 to resident submission, 97 to texture upload
and 69 to readback. These pairs are a truncated subset, not a complete partition
of library time. They support inspecting surface residency, transfers and shader
work for qualified renderer reuse. Native state transfer and admission remain
measured CPU candidates.

These are instrumented host samples, not GPU hardware timings, a speedup, or
distinct displayed FPS. Fixed windows can straddle scripted phases; their
boundaries must be compared with the actual guest input/capture clocks before
making a phase claim. Full AOT, all required menu interiors, UI audio and
original-speed 60 FPS remain open. Renderer work continues from the working
OpenGL implementation and [qualified donor reuse](RENDERER-REUSE-2026-10-02.md).
