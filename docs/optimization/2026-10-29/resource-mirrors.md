# R03: qualified resource mirror ownership

Native vertex storage can reuse unchanged consumed pages through the existing
CPU/native-writer observer. A certificate names the storage generation, source
allocation and layout. Capture compares the complete owned page intersection
against the actual native CPU mirror; equality of a consumed subset is
insufficient. Full/range writes, refetch, destruction and slot reuse invalidate
certificates. Misses retain the original byte comparison/upload path.

Texture preparation shares the allocation identity contract while preserving
independent GPU write revisions and live alias authority. CPU cleanliness never
overrides GPU-produced data. Metadata is capped at 1MiB per vertex object and
16MiB globally; unsupported observations, hot pages, active native writers and
budget refusal use the byte fallback. The existing serialized graphics owner
owns this metadata. Portable observer fallback remains functional.

## Per-game selection

The startup selector `XLLE_PILOT_RESOURCE_MIRROR_OWNER` defaults to **0**. A
qualified title profile may set it to **1** through its existing `[runtime]`
table. Normal releases leave `XLLE_PILOT_RESOURCE_OWNER_PROFILE` unset/0;
setting it to 1 enables diagnostic counters and is excluded from timing results.
Lookup counters are disabled in normal runs; live/peak metadata gauges remain
maintained independently.

| Title | Qualified package |
| --- | --- |
| JSRF USA | Owner enabled by its title runtime profile. |
| Blinx | Owner disabled; keep the existing byte-check path. |
| JSRF rev4 | Owner disabled; targeted correctness only, no duplicate profiling suite. |

Do not enable the owner globally from the USA result. Games receive different
packages; an aggregate benefit cannot offset another game's regression.

Combined release catalogs retain the complete runtime policy for each revision.
Selection and reselection replace that policy, so the USA owner setting reaches
its actual child process and cannot leak into rev4. Catalogs from older releases
without revision-specific policy retain their original shared-policy behavior.

## Measured built-game evidence

The actual source baseline is 225d8a8197158c547ea2a455710c4a893c7d10e3
(L02, PR 3); runtime candidate is 13649ef9a45e188d8b2cd4865baa8840e5eab37e.
Later commits add documentation and per-revision release selection; native C
runtime sources remain identical to the measured candidate. Both games use the
same guard-free generated inputs, MSVC optimized FINAL/IPO configuration and
one build worker. Each matched block stays on one Windows host/configuration
under a capacity reservation, with real DSP muted, matching private saves and
mod plans. Separate diagnostic runs are excluded from comparisons.

JSRF USA CPU milliseconds per source update were 15.1302/15.1838 baseline and
14.2494/13.8600 enabled. Adjacent comparisons are -5.82%/-8.72%; ratio of medians
is -7.27%. Baseline repeat variation is 0.35%, candidate 2.77%. Source cadence
remained approximately 60/s with no new pacing stall in this bounded route.
This is CPU work saved at capped cadence, not an uncapped throughput claim or
a guarantee for every scene/host. Diagnostic observation showed 416286 hits in
428417 vertex lookups and 83264 bytes peak metadata; these cumulative process
totals include navigation/warmup and are not timed-window traffic.

Blinx with the owner enabled had adverse paired CPU direction +2.78%/+3.04%
amid 7.39% baseline variation; that package is not qualified for Blinx. Its
disabled-path block had mixed CPU direction -6.29%/+9.77%, and a targeted
confirmation was +0.78%. Treat that package as neutral/inconclusive, not a gain.
An initial 106–149ms polled arrival burst coincided with 99–100% host CPU. It did
not recur in the confirmation: maximum arrivals were 61.50ms fallback and
61.43ms baseline, with no missed increments. Preserve the remaining caveat:
confirmation cadence was 29.40 versus 29.79/s and p99 arrivals 60.29 versus 47.32ms
under unequal host load. Polling includes scheduling uncertainty and does not
measure exact frame duration, GPU timestamps or scanout. No reproducible severe
fallback stall or material regression was established by the independent review.

## Validation and limits

Current-core GCC/MSVC native and portable component checks pass. Ten affected
contracts cover both owner modes: real watched unaligned/cross-page vertices,
source-base changes, native writer leases, queued old/new GPU pixels, refetch,
slot retirement/reuse, full GPU backend/batching, texture alias/write authority
and focused guest vertex ABI. The independent validator verified their source,
binary and log identities and independently recomputed the game samples.

The broad runtime `graphics` fixture remains failed/unqualified at its existing
original allocator destruction guard. An identified historical7426 reference
reproduces stop 2; the guard is byte-identical in 7426/225/136. That reference is
not the matched timing baseline. These passes do not qualify the separately
tracked shared observer concurrency failure 207 or add a concurrency claim.

Private reproducible evidence is retained under central issue beads-xbox.1.206,
campaign beads-2wy8.1: `r03-matched-build-003-result.json`, current component 002,
contracts 003, runtime review 001, Blinx fallback reviews and the independent
validation records. Raw per-game samples, source/binary/settings identities,
host telemetry, diagnostic observations and pacing caveats remain available.
Final combined owner visual/audio approval is still required before merge or
publication. This result does not complete the rest of the campaign.
