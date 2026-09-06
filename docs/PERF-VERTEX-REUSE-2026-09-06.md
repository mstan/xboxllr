# Decoded vertex reuse: first shadow candidate

Tracker: `beads-xbox.1.15.11`. This is a diagnostic, not a transform-output cache.
Every eligible shader still executes, and no input fetch or output emission is
skipped. The table is allocated only with `--nv2a-vertex-reuse-census`; adding
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

Normal-runtime overhead of adding the disabled diagnostic branch has not yet been
measured in an interleaved set. Further measured runs are paused while the owner
uses their independent play copy of upstream `123b7a8` (outside the build tree).
