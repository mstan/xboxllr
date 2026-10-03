# Exact AOT membership lookup: qualified startup improvement

Tracking: `beads-xbox.1.37`, under the active
[dashboard goal](GOAL-AOT-DASHBOARD.md). Source baseline: `931ef76` and the
[scalar checkpoint](RESULTS-AOT-SCALAR-2026-10-03.md).

## Finding and implementation

The fallback block hook asks whether each candidate PC belongs to a registered
AOT region. The scalar input has 246 regions; the existing implementation walks
an ordered tree on each query. This is a membership question only: the executor
separately checks context, code translation, live byte identity and instruction
entry before executing anything.

The experiment replaces only that membership lookup with an exact sparse bitmap.
An address splits into a 10-bit directory, 10-bit page and 12-bit page offset.
Each allocated page uses one bit per registered byte. Absent directories/pages
return false; present pages answer with one indexed bit. Registration still
rejects overlaps, empty/wrapping/oversized regions and wrong ABIs before adding
bits. The authoritative ordered region map and execution guards are unchanged.
No guest-state eligibility result is cached, and no guest instruction is skipped.

For the real 246-region input, this allocates two directories and 34 pages:
**41,984 bytes** on the 64-bit host, plus allocator metadata. The candidate
executable is **512 bytes larger**. Host disassembly confirms two directory/page
lookups and a bit test, with no tree traversal. The compile-time
`XLLE_AOT_FAST_MEMBERSHIP` switch retains the prior path and is **ON by default**
after passing correctness and quiet-host performance qualification.

The retained control and original baseline have identical executable `.text`
sections, SHA-256
`3d37ebf539e3e9972ca7323b76bb5e36382ce2f82d5eeab64bcf7aef0623ebb9`.
This isolates the lookup implementation when comparing the control and candidate.
All use the same 246-region scalar input; no region is removed to improve timing.

## Qualification

Public candidate tests pass **74/74**. The expanded registry tests compare
membership with an independent union of intervals, including every byte of the
authored ranges and 100,000 additional addresses. They cover zero/top addresses,
64-byte, 4 KiB and 4 MiB boundaries, 64 KiB regions, misses followed by additions,
shuffled insertion, gaps and rejected registrations. The authored RET bodies
include unreachable padding: membership must retain the previous range semantics
without confusing registered bytes with valid native entries.

The existing 257-region machine cycle still runs strictly through AOT with no
hidden fallback, preserves per-region counters and rejects live code changes.
Both optimized and unoptimized generated fixtures pass in the census build and
with the conservative lookup selected. Full **27B candidate O2/O0** dashboard
qualification preserves all **270 checkpoint rows and ten images**, matching
the scalar native and floor references. Device/DMA/IRQ/TSC endpoints and every
native adapter counter agree. Native work remains **1,815,586,408** instructions,
fallback work **7,191,748,216**, and proven idle skips **17,992,665,376**.
The 94,298,273 admission attempts and 48,856,092 handoffs are unchanged.
Private evidence: `dispatch-validation.aot-private.json`.

## Measurement record

`dispatch-baseline-profile-r1` is **excluded from acceptance timing**. It completes
27B with the expected final image, empty stderr and all 270 checkpoints matching
the scalar reference. The observer is healthy (2,437 samples; maximum gap
138.63 ms), but detects seven transient Python processes during the leg.
The retained symbolized diagnostic verifies the binary and accounts all 107,582
samples; `AotExecutor::contains` receives 6,452 samples. These contaminated
samples nominate an experiment, not an accepted performance percentage.

The subsequent `dispatch-membership-12b-r1` set uses six uninstrumented,
same-session interleaved **A/B/A/B/A/B** legs with census and host sampling off.
All three pairs have healthy quiet-host observation and are accepted on their
first attempt. No pair is excluded or retried. Observer sample counts range
from 609 to 752 per leg; maximum gaps range from 125.00 to 139.13 ms, below the
1,000 ms limit. Twelve individually inspected, idle Beads MCP Python processes
are declared residents by PID and creation time. No process tree or general
Python exclusion is used. The sampled observer retains its documented inability
to detect arbitrarily short competing processes.

| Round | Tree control, seconds | Exact membership, seconds |
| --- | ---: | ---: |
| 1 | 81.2213 | 66.2312 |
| 2 | 79.2211 | 66.1236 |
| 3 | 80.0442 | 65.7207 |
| Median | 80.0442 | 66.1236 |
| Minimum | 79.2211 | 65.7207 |

The total 12B wall-time gate **passes without an override**: median falls
**17.39%**, minimum **17.04%**, both above the required 5% threshold. Every pair
favours the candidate. The final four windows are an indicator only: median
falls **2.25%**, minimum **1.44%**. This smaller late-window effect limits any
inference about steady dashboard speed.

An independent comparison verifies all six legs have identical arguments apart
from executable/output paths, all **24 frame deadlines/results**, all **25 PPMs**,
all **9,719 AOT registration/counter rows**, all idle/device/DMA/IRQ/TSC census
rows, final retired state and all 24 renderer profile hashes. Stderr is empty
and each report passes. The final 12B PPM SHA-256 is
`bdbf3917585fb0b99135fc8ed66a00ef6fc7cdc5240bb60353866ffc0b3173c0`.
The superseded historical 12B golden is not used; the unchanged control and full
27B reference qualification establish the comparison. Private proof is retained
in `dispatch-timing-validation.aot-private.json`, with every leg/report,
quiescence record and the gate under `build/dispatch-membership-12b-r1*`.

This protocol measures the startup window, not post-boot navigation or displayed
FPS. Its derived `effective_fps` field must not be read as distinct presented
frames. Whole-dashboard speed and original-speed 60 FPS remain separate gates.

## Clean diagnostic profile and next targets

`dispatch-candidate-profile-r1` subsequently completes the full 27B route with
healthy quiet-host observation: **2,295 samples**, maximum gap **125.61 ms**,
no detected competing processes. All 270 checkpoint rows and ten route images
match the scalar reference, with identical final native counters. The final
ordinary build clears private guest input and again passes **74/74 tests**.

The host sampler verifies the retained candidate binary and accounts all
**101,633 samples**, with no table overflow, suspension/context failures or
missing leaf coverage. It achieves a mean period of **2.466 ms** against a
requested 2 ms. Membership receives 723 samples (0.71%); this is a diagnostic
share, not a comparison with the rejected profile or a speedup estimate.

Separate exclusive counters provide 17 post-boot intervals between the 18.5B
and 27B capture deadlines. This starts after the verified dashboard-settled
capture at 18,034,866,659 and includes the Settings navigation and return.
These instrumented intervals total 107.056 seconds:

| Exclusive host bucket | Seconds | Share of diagnostic wall |
| --- | ---: | ---: |
| Native generated body | 19.972 | 18.66% |
| OpenGL work | 18.792 | 17.55% |
| Vertex processing | 14.491 | 13.54% |
| Generic raster work | 13.681 | 12.78% |
| Texture work | 8.466 | 7.91% |
| Slice overhead | 6.294 | 5.88% |
| Fallback CPU execution | 6.097 | 5.69% |
| GPU command dispatch | 5.598 | 5.23% |
| Native boundary/admission | 5.457 | 5.10% |
| OpenGL resolve | 3.157 | 2.95% |

These are the largest exclusive buckets, not GPU hardware timings or an
uninstrumented product-speed measurement. Inspect actual shader/texture work
for qualified donor reuse; native bodies, state transfer and admission remain
CPU candidates. No proposed improvement gets its expected gain by adding
inclusive counters or comparing this profile with the contaminated baseline.

The sampler itself has a long-run reporting limit, tracked as
`beads-xbox.1.38`: bucket 7 clamps every sample at or above 10.5B but prints an
upper bound of 12B. Its complete aggregate attribution is usable; its printed
range is not an exact post-boot phase. The table above uses the separate
500M-interval counters, not that clamped leaf bucket. Private profile validation
is in `dispatch-profile-validation.aot-private.json`; symbolization and the
original profile are retained alongside the earlier diagnostic.

Private artifacts are under `build/aot-20261003/`, with monitored profile output
under `build/dispatch-baseline-profile-r1*`. Executable SHA-256:

- Original baseline, `xlle-dispatch-baseline.exe`:
  `77872959e646dc0822a05428955b3df2b9f92a3542f4d55703aa890adc833e25`.
- Switch-off control, `xlle-dispatch-control.exe`:
  `fc8111d320553191c474e45f03bba5547025ebad7bf8c2703367ef1c70db380c`.
- Switch-on candidate, `xlle-dispatch-candidate.exe`:
  `79cda3b80f5db9baedc143fe68c19d94fb35868ba34f9a0617eb6b5e51ca9726`.

The original renderer, hardware and native instruction subset remain in use.
Full AOT, required menu interiors, host audio and original-speed 60 FPS remain open.
Next, refine phase-level leaf attribution and investigate measured native body,
state-transfer and renderer costs before selecting another transformation.
