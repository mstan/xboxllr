# Ordered timing and actual compensation

Tracker: `beads-xbox.1.14.3`. Follow-up to the texture-cache integration at
`123b7a8`, with the continuing goal of faithful LLE at 60 FPS with headroom.

The exclusive timer used bare RDTSC even though the local dispatch micro probe
had demonstrated collapsed short-region deltas with unordered reads. The clock
now uses LFENCE on both sides of RDTSC plus compiler barriers. Both scale and
minimum read-cost calibration use that same implementation. This orders local
execution; it does not wait for GPU completion or globally drained stores.

The accounting also overstated the amount subtracted from bucket time:
`clock_reads * overhead_ticks` counts reads from no active region and rejected
intervals, and charges a full floor for intervals shorter than that floor.
`Profile::compensated_ticks` now accumulates only the amount actually removed.
Raw wall still includes timer overhead. The existing `unaccounted_ns` remains
wall minus bucket time; new `residual_ns/pct` also removes actual compensation.
Both remain visible, along with discarded intervals. Neither subtraction nor
fencing makes an instrumented run an unperturbed throughput measurement.

Direct vertex parse/compile/fetch/execute census scopes use the same plausibility
clamp and subtract one minimum read-pair cost. Invalid direct deltas increment a
vertex-local discard counter. Fetch/execute timing remains opt-in. The always-on
parse and compile timing is also affected by the fence change, so a same-session
noperf comparison is required before claiming no shipping regression.

The texture conversion FNV trace now requires
`--nv2a-gpu-generic-texture-trace`. Ordinary `--perf-accounting` no longer computes
the hashes removed by the accepted cache; otherwise the next profile would
measure a materially different conversion path.

## Validation

Release CTest passes **31/31**. GPT-5.5 High wrote the bounded executable test;
the primary reviewed it and ran all builds/tests. Synthetic cases verify actual
compensation for 50/10/5-tick intervals with a 10-tick floor, invalid/backwards
clamping, disabled/re-entrant/nested regions, no-region report reads, cumulative
and interval snapshots, and the raw-wall/compensation/residual identities. The
real clock test checks monotonic reads and calibration without fragile duration
thresholds. Existing source guards still validate instrumentation placement.

The noperf interleaved 3+3 comparison (`canonical`) has median wall
50.1390 s before / 49.6276 s after (+1.0201%) and minimum 49.6075 / 48.8751 s
(+1.4764%). It formally **REJECTS the 5% optimization gate**; this is retained
as an instrument correctness fix, with no accepted speedup claim. All six legs
pass replay validation, all 28 PPMs agree byte for byte, and the stable
input/device/determinism/DMA/generic/seam fields agree. Both measured aggregate
criteria show no noperf regression; the set is too small to assert a precise
sub-percent cost. CV is 0.65% before / 1.93% after.

The corrected per-vertex attribution (`steady-perf-v2-16b`, diagnostic n=1) covers
14.5 B through 16 B, excluding the interval ending at 14.5 B. It records 13.7436 s
raw wall, 0.141147 s actual clock-floor subtraction (1.027%), 0.001097 s residual
(0.008%), and zero discarded exclusive or vertex deltas. All 72 PPMs agree with
the previous product profile. The raw vertex bucket is 4.6830 s, but includes
per-vertex probe perturbation; a companion without those probes is recorded below.

The existing index cache serves 49,455,717 of 67,646,187 vertices (73.1%). Of the
remaining work, 17,848,776 programmable executions have 2.247974 s compensated
body time (125.95 ns each); 18,190,101 fetches have 0.564894 s (31.06 ns each).
One compiled 21-step / 23-op program, fingerprint `a83505a475320999`, accounts for
17,625,654 executions (98.75%); no context-writing vertices occur in this window.
These measurements justify a bounded exact-input shadow reuse census, not an
assumption that memoization beats a roughly 126 ns execution. Existing index hits
are already removed and cannot be counted again as potential savings.

The companion `steady-perf-novertex-16b` (same binary, diagnostic n=1, per-vertex
timers off) records 11.8662 s in the same window, 0.141148 s removed floor,
0.001114 s residual (0.009%), and zero discarded deltas. Its main exclusive
buckets are:

| Bucket | Seconds | Raw wall share |
|---|---:|---:|
| Vertex | 2.9176 | 24.59% |
| GL submission | 2.7294 | 23.00% |
| Generic body/setup | 2.4616 | 20.75% |
| Dispatch | 1.0446 | 8.80% |
| Texture | 1.0102 | 8.51% |
| TCG | 0.7924 | 6.68% |
| GL resolve | 0.3698 | 3.12% |

There are again zero software pixel-loop calls in this settled window. The two
diagnostic profiles illustrate that per-vertex timing materially perturbs the
vertex bucket (4.6830 s with it, 2.9176 s without it). They are not an interleaved
retention set and do not establish an exact profiler overhead percentage or a
shipping speedup. Use the companion for coarse attribution and the explicitly
compensated census for selecting short-body experiments. GPU query totals currently
exist only at shutdown, so they cannot yet be attributed to this settled window.

The first attribution
invocation (`steady-perf-16b`) failed before emulation because its input-script
filename did not exist; `steady-perf-v2-16b` uses the recorded prior argv's
`tests/scripts/clock-accept.txt`. The failed launch is retained, not a timing sample.

Artifacts are under `build/timer-fix/`; `xlle-prefenced.exe` preserves
the accepted pre-change executable with SHA-256
`41c08e56eaeb00fe0fe351f3a9a0fde3f142a0b8d0ca9a73285a67435b81d897`.
