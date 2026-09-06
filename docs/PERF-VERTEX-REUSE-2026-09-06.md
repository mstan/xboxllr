# Decoded vertex reuse: first shadow candidate

Tracker: `beads-xbox.1.15.11`. This is a diagnostic, not a transform-output cache.
It requires a dedicated build configured with
`-DXLLE_VERTEX_REUSE_DIAGNOSTICS=ON` (default OFF). Normal builds omit the probe's
members/API and hot-loop branches, and clearly reject its CLI flags before
renderer creation or emulation. Every eligible shader still executes, and no
input fetch or output emission is skipped. In a diagnostic build, the table is
allocated only with `--nv2a-vertex-reuse-census`; adding
timing uses `--nv2a-vertex-reuse-timing` (which also enables the census).

The candidate keeps the most recent exact program/start/constants/compiled-mode
state and 1,024 direct-mapped decoded-input tokens. State changes advance a
generation rather than clearing the whole table. Five input words select a slot;
all 256 input bytes must match before a hit is counted. State snapshots and input
tokens use byte copies, preserving signed zero and NaN payloads. Existing batch
index-cache hits are counted separately because their shader work is already gone.
Context-writing programs, unsafe probes, verification, raw-output capture, and
the separate vertex-body census are excluded. CLI use of both vertex-body and
reuse probes is rejected because their timing scopes overlap.

GPT-5.5 High wrote the helper and counterexample tests; the primary removed an
unnecessary temporary input copy on hits and changed float state assignments to
explicit byte copies. Release CTest passes **32/32**. The diagnostic 16 B replay
passes, with all **72 PPMs identical** to the preceding accepted-path profile.

## Settled window, 14.5 B through 16 B

Artifact: `build/vertex-shadow/census-16b`, summarized in `steady.json`.
This is one diagnostic run, not a throughput retention set.

| Measurement | Result |
|---|---:|
| Eligible batches | 12,792 |
| State changes | 12,546 |
| Observed programmable executions | 17,848,776 |
| Within-batch exact hits | 42,681 |
| Cross-batch exact hits | 123 |
| Combined hit fraction | 0.2398% |
| Misses | 17,805,972 |
| Occupied-slot collisions | 10,147,254 |
| Existing index-cache hits, excluded above | 49,455,717 |
| Compensated state-check time | 3.339 ms |
| Compensated lookup time | 384.474 ms, 21.54 ns per observed vertex |
| Object footprint including snapshots/counters | 283,880 bytes |
| Invalid timing deltas | 0 |

At the previous census's roughly 126 ns average shader body cost, these observed
hits correspond to only about 5.4 ms of potential removed work, before storing
or copying any outputs. That estimate is far below the measured lookup cost.
**Do not implement this candidate as a default optimization.**

This does not disprove all vertex reuse. High collisions can hide later hits;
keeping only the last state misses reuse across interleaved states, and comparing
all constants/program words includes values the shader may not read. Those are
different candidates requiring new evidence. The counters report requested
memcmp lengths, not the number of bytes libc actually inspected. Report `batches`
means eligible batches; `bytes` is the entire diagnostic object's footprint.

## Disabled diagnostic overhead

After the independent play process exited, `build/uniform-ab/shadow-20260906-r1`
compared preserved pre-diagnostic and diagnostic executables, noperf, interleaved
3+3 in one session. The diagnostic was disabled in every leg. Actual executable
SHA-256 prefixes are `f7cc37fa6f7e3113` (A) and `586d2cf3b9973447` (B); these are
preserved build artifacts, not binaries rebuilt from today's working tree.

| Pair | Before (s) | Diagnostic compiled in, disabled (s) |
|---|---:|---:|
| 1 | 56.7650816 | 57.5706795 |
| 2 | 56.7222820 | 57.7101464 |
| 3 | 60.8274881 | 62.1530655 |

Median wall regresses **1.6649%**, minimum **1.4957%**; all pairs favor A.
The formal 5% improvement gate rejects. CV is 4.06% / 4.41%, with noticeable
within-session drift, so this is not a precise causal measurement of a branch's
cost. It does not support a zero-overhead claim. The process censuses are clean,
with idle Beads/Python residents excluded by the recorded narrow pattern.
All 28 PPMs and stable retired/scanout/flip/input/idle/device/DMA/generic/seam and
checkpoint timeline fields match across all six runs (`*-fidelity.json`).

Decision applied: preserve the diagnostic and disproved cache candidate, but
isolate its integration at compile time. Both ON and OFF native release builds
pass CTest 33/33. The ON-build 500 M smoke replay exercises 942,199 observations
and 762,004 already-cached vertex bypasses; this short startup workload does not
replace the settled-window reuse measurement above. Both flags on the OFF build
exit 2 with an explicit build-option message before renderer/emulation. The
standalone shadow helper and its counterexample tests remain available in either
configuration. These checks establish the build separation and capability;
they do not establish a measured speedup from compiling it out. The final
uniform experiment uses the same OFF configuration in both arms.
