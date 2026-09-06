# Generated texture conversion and upload identity

Tracker: `beads-xbox.1.15.12`. Branch: `perf/measurement-first-2026-09`.
This experiment follows the settled-dashboard attribution in
`PERF-GENERIC-2026-09-06.md`: 861 generated-path setup calls in the 14.5 B to 16 B
window cost 2.842 s. The old `fallback_setup` name includes preparation for GPU
draws; it does not imply CPU pixel rasterization.

## Coherence and ownership

The cache sits **after** the existing texture resolve, DMA read and DXT decode.
Its lookup compares the entire fresh source byte vector and its length, width,
height, linear/swizzled interpretation and A8 expansion flag. It uses no hash,
guest address or unproven memory epoch to establish source equality.

Those are all inputs to the generated texture conversion. The Morton lookup is
a function of width/height. Linear setup has already reduced the source to the
same tight prefix the old converter copied. DXT format and pitch have already
affected the decoded source bytes. Border, wrapping and projective state are
still taken from the current draw. Every draw packet owns its output vector.
Cache eviction cannot mutate a submitted packet or bypass a GPU resolve.

There are eight entries with an LRU policy and a 32 MiB retained payload budget,
including source and converted bytes. Oversize entries are not retained. This
is a cache payload bound, not a bound on total process memory or driver storage.

Nonzero content IDs identify converted snapshots across device instances and
cache lifetimes. Exact source-byte matches reuse the ID; misses allocate a new
one. The shared atomic allocator saturates at zero after exhausting all nonzero
IDs, then uses legacy FNV identity. IDs and hashes are separate renderer
namespaces, including when their numeric values happen to match. This avoids
hashing every fresh multi-megabyte image merely to identify an upload. The
disabled path retains the earlier FNV behavior.

Instrumented conversion traces compute FNV for analysis, so those runs do **not**
measure the noperf hash-elimination gain. No claim is made that this change removes
guest memory reads, DXT decoding, GPU resolves or necessary uploads of changed data.

## Counterexamples and review

Release CTest passes 30/30. Coverage includes full source-byte mutations at the
same address, different shape/layout/A8 interpretations, size changes, eviction,
oversize bypass and allocator exhaustion. The draw fixture tests both swizzled
PROJECT2D and tight linear PROJECT2D packets, preserving a deliberate texture /
framebuffer alias while resetting prior blend output. Border changes retain the
conversion but change packet state.

A real OpenGL 8x8 draw/readback test renders red, reuses that content ID, renders
green with a new ID and the same zero hash, switches to blue under legacy FNV,
then renders white under an ID numerically equal to the preceding legacy hash.
It checks both pixels and actual texture-upload counts. This exercises stale
upload prevention across namespace transitions on the same renderer.

GPT-5.5 High performed read-only audits. The primary rejected the initial proposal
to introduce memory invalidation epochs for this patch: exact snapshots already
provide a narrower correctness boundary. The helper's working-set estimate was
then checked against an actual conversion trace, rather than used as a timing claim.

## Experiments

All timing gates use same-binary interleaved A1/B1/A2/B2/A3/B3, noperf, with cache
off/on flags. Both total 12 B wall median and minimum must improve by 5%.
The foreground process filter excludes Python after identifying the resident
Beads MCP processes. All 14 measured resident processes consumed zero CPU across
the first gate; raw before/after counters are retained. No compiler, test runner
or second emulator overlaps measured legs.

| Variant | Total median gain | Total minimum gain | Decision |
|---|---:|---:|---|
| 16 MiB, conversion/FNV memoization | 1.1419% | 1.1873% | REJECT |
| 32 MiB, conversion/FNV memoization | 4.18% | 4.45% | REJECT |
| 32 MiB, conversion plus content IDs | 5.7018% | 5.7289% | PASS; enabled by default |

The first gate has 28/28 identical PPMs in all six legs, with matching device,
input, deterministic-clock, DMA, generic and seam counters. Its median wall is
51.0800 s off versus 50.4968 s on. The final-four-window indicator improves 5.27%,
but that does not override the total-wall rejection. B has 296 hits, 631 misses
and 628 evictions. The second variant raises reuse to 746 hits / 181 misses,
retaining 30,539,776 bytes at the final checkpoint.

The content-ID variant's median wall is 51.2442 s off versus 48.3224 s on;
minimum wall is 51.0963 s versus 48.1690 s. All six legs pass, all 28 PPMs agree
byte for byte across the set, and the stable input/device/determinism/DMA/generic/seam
censuses match.

### Interactive product validation

The separate interactive 22 B dashboard/Settings/return set (`product-id`, 3+3)
passes the total-wall gate: median 139.6816 s off versus 116.6783 s on (**16.4684%**),
minimum 139.4064 s versus 116.2076 s (**16.6411%**). All six runs pass, all **98 PPMs**
agree byte for byte across all six runs, and the same stable input/device/
determinism/DMA/generic/seam fields agree. Every pair favors B. A/B total-wall CV
is 1.07%/4.20%; the slowest B run is retained and still beats every A run.

Aligned product windows use actual guest instruction clocks and checkpoint wall
intervals, excluding the interval ending at the start boundary:

| Window (billions of guest instructions) | Off median wall | On median wall | Off/on median guest-equivalent FPS |
|---|---:|---:|---:|
| Settled dashboard, 14.5–15 | 4.3483 s | 3.4880 s | 9.410 / 11.732 |
| Settings, 18.5–19 | 3.9678 s | 2.7512 s | 10.313 / 14.873 |
| Returned dashboard, 21–22 | 8.4790 s | 6.7790 s | 9.645 / 12.064 |

These are short capacity windows, not a sustained 60 FPS result. Whole-run host
presentation-call rate rises from median 12.52/s to 15.23/s, with zero present
failures. Median run-average capture-to-present latency is 729.7 us off versus
654.0 us on; the worst observed maximum is 6570.1 us off versus 5937.9 us on.
These aggregates do not establish distinct-frame cadence or p95/p99 latency, and
the present samples lack absolute instruction anchors for phase attribution.
The remaining campaign must measure those directly before claiming 60 FPS.

### Budget selection from a trace

`trace16` logs 3,223 conversions over 16 B. Its large 1280x960 linear shape has
2,354 accesses to 506 output identities; two 256x256 swizzled identities have
868 accesses. The replay model uses output FNV only as a **diagnostic identity**.
It reproduces every actual 16 MiB hit/miss, supporting capacity predictions for
this captured sequence. The engine itself continues to compare exact source bytes.

| Simulated budget / 8 slots | Hits | Misses | Post-14.5 B hits / misses |
|---|---:|---:|---:|
| 16 MiB | 1,280 | 1,943 | 369 / 492 |
| 24 MiB | 2,630 | 593 | 738 / 123 |
| 32 MiB | 2,714 | 509 | 738 / 123 |
| 40, 48 or 64 MiB | 2,714 | 509 | 738 / 123 |

32 MiB is the smallest tested budget at the trace's reuse plateau. Sixteen slots
at 32 MiB add no hits. These are replay predictions, not measured runtime gains
or claims about every dashboard state.

Artifacts: `build/texture-cache/canonical*`, `canonical32*`, `canonical-id*`,
`trace16*`, `working-set.json`, `simulate.py`, and the build/CTest logs. The runtime
switch is `--[no-]nv2a-gpu-generic-texture-cache`; it defaults on after both gates.
Product artifacts include `product-id*`, the per-window summary, and explicit
98-image/state equality results. The default-on release build passes CTest 30/30.
`default-reference` reproduces all reference profile hashes and the final image
against `golden-noperf-defaults12-2026-09-05d.json`: report OK,
31 checks accepted (29 executed; executable/Git identity are the two configured
diagnostic skips). A separate checkpoint comparison finds the already documented
single-pixel FP32 scatter at 11 B and 11.5 B (and the scripted settle capture),
so the report's 31 accepted checks must not be described as 24 byte-exact
checkpoint images. All 28 reference PPMs and stable census fields are byte-identical
to the pre-cache `build/generic-fix/reference` run. The golden was not re-pinned.
