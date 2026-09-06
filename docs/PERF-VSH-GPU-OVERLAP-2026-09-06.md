# Vertex and GPU draw overlap

Tracker: `beads-xbox.1.15.14`. Starting revision: `7a65654`.
Goal: faithful LLE 60 FPS with headroom. This census selects a combined GPU
vertex-and-triangle-setup experiment; it is not itself a performance promotion.

The CPU dot-prefix candidate executed broadly and verified exactly, but its
clean 3+3 gate was flat. The next question is how much programmable vertex work
feeds existing GPU draws, and how much CPU triangle preparation those same
draws require. Adding whole vertex and generic buckets would not answer that
question, because they include different populations and nested work.

## Measurement contract

`XLLE_VSH_DRAW_OVERLAP_CENSUS` defaults OFF. Normal builds omit the metadata,
recorders, and machine hooks. A diagnostic build accepts
`--nv2a-vsh-draw-overlap-census` with `--perf-accounting` for calibrated clocks.
The census remains separate from the expensive per-vertex timing census.

- Copy program identity and vertex counts into each draw job. Never retain a
  pointer to a mutable program cache entry. Reuse parse-time identity rather
  than hashing program storage on every batch.
- Separate submitted references, actual VSH executions, and index-cache hits.
  Fixed-function and context-writing programs are distinct populations.
- Count each logical job once, including inline, deferred, nonexecuted, and
  faulted jobs. Renderer success and empty/no-work handling are distinct.
- Separate generated admission, first collection, and final outcome. GPU
  refusal and differential verification can rerun primitive preparation.
- Time only the first generated collection pass at batch granularity. Exclude
  verifier reruns, fallback software raster, surface resolves, and GPU submits.
  Report raw and compensated CPU time with discarded-delta counts. This time
  includes nested work in that pass; it is not another exclusive bucket.
- Separately time generic fixture packet construction, vertex copying, state
  recognition, and triangle expansion/clipping/bin construction. Start before
  constructing the packet and stop before resident admission. This excludes
  earlier texture preparation, subsequent hashes/snapshots, renderer calls,
  resolves, and packet destruction. A fixture ID is a CPU proposal; the final
  outcome records whether resident or snapshot submission actually succeeded.
- Preserve overflow counts explicitly. Missing rows, absent diagnostics, and
  zero observed work must remain distinguishable in reports.

## Validation and selection

Validate the recorder's lifetime and conservation properties, then exercise
real vertex submission and retirement. Counterexamples include program/cache
mutation, table saturation, declined and empty GPU work, verification reruns,
and deferred completion. Diagnostic companions must reproduce every image and
the guest-state timeline. Normal builds must reject the unavailable flag and
pass the complete test suite; a quiet noperf interleaved integration set checks
their default behavior against the preserved published executable.

Use matching cumulative checkpoints to report settled-dashboard overlap.
Do not call a counted vertex removable work: existing CPU consumers still need
its position, colors, and texture coordinates. A GPU candidate must either move
their clipping/culling/triangle preparation too or measure the transfer back.
Any candidate then needs the canonical minimum-and-median gate and interactive
navigation validation. Actual distinct-frame presentation and latency tails
remain part of the final headroom goal.

## Measured selection

Two complete 16 B diagnostic replays (`census-on-r3`, `census-on-r4`) used the
same executable, SHA-256
`3c1230c59becec3dca77a2cae4206248b10d69fafa3577ae3f9d1ff1aaf6dbe5`.
The exact cumulative 14.5–16 B window has 17,848,776 actual VSH executions.
Both runs produce identical logical populations:

| Final draw outcome | Actual VSH executions | Share |
| --- | ---: | ---: |
| Resident generic GPU | 17,576,085 | 98.4722% |
| Snapshot generic GPU | 150,429 | 0.8428% |
| Generated GPU | 122,262 | 0.6850% |

There are no generic-empty outcomes in this settled window. Population keys
include program identity, transform mode, compiled/interpreted execution,
context writes, proposed fixture, and final draw outcome. Shares use actual
VSH executions, not draw counts or submitted index references.

The dominant population is program fingerprint `a83505a475320999` feeding
fixture 4, `kResidentPrimaryColorTriangles`: 17,503,392 VSH executions from
65,637,720 submitted references, with 48,134,328 index-cache hits. CPU fixture
preparation produces 35,922,519 output vertices. Its measured preparation time
is 1.406 s and 1.296 s in diagnostic windows of 12.712 s and 12.571 s. These are
inclusive diagnostic costs, not noperf headline results or predicted savings.
The timer includes packet copying as well as triangle work; it cannot identify
the saving from eliminating either one alone.

This selects resident primary-color VSH plus triangle preparation for the next
combined GPU experiment. The textured clipping loop and generated path are
smaller populations in this workload. Preserve the existing index reuse: a GPU
implementation that shades all 65.6 million references would do substantially
more vertex work than the current 17.5 million actual executions. Admission
must be based on supported program/state semantics, never a guest fingerprint.

## Corrections and evidence

The first private diagnostic (`census-on-r1`) incorrectly classified an empty
resident batch as a successful GPU fixture submission. Its aggregate GPU
claim was withdrawn. The corrected schema distinguishes empty, resident,
snapshot, generated, software, and fault outcomes. The final reports reject
unknown outcomes, faults, discarded clocks, missing rows, and row overflow.
An inferred input-minus-collected triangle count was also removed: clipping
can expand geometry, so that difference is not a valid cull count.

All 72 PPM checkpoints and checked guest-state/timeline fields match across
the preserved published `7a65654` reference, both final diagnostic ON runs,
and a runtime OFF companion using the same diagnostic executable. The ON
build passes all 38 CTest tests, including live program/cache mutation and
standalone recorder conservation tests, plus eight Python report tests.
Private evidence is under `build/vsh-overlap/`: `companions-fidelity.json`,
`companions-settled.json`, the reports, and build/test logs. Guest images and
raw captures remain ignored.

The normal OFF build also passes 38/38 tests and rejects the diagnostic-only
flag with exit 2 before renderer creation. `integration-r1` compares it with
the preserved published executable in one quiet noperf interleaved 3+3 session.
Every leg matches all 28 PPM checkpoints and the checked guest-state/timeline
fields. A median/minimum are 50.9262332/48.7380071 s; B are
50.2619797/48.7190203 s. Improvement is +1.304% median / +0.039% minimum:
**REJECT** at the 5% gate. Arm CVs are 2.91% and 2.21%. This validates the
instrumentation integration, not a claimed speedup or optimization promotion.
The OFF executable SHA-256 is
`d80f10b8b06c2d2bb0d12ece5bb6893736212eab59247caf1bce711ef5e0b2e5`.
The publication-tree secret scan covers 168 files with zero findings.

## Next implementation boundary

A combined path needs owned input-register records, a reference stream with
the current batch-cache reuse, decoded program/constants, and primary draw
state. Keep attribute DMA fetch and its faults on the emulation thread. Move
VSH output rounding/W clamping and primary triangle preparation together;
otherwise CPU consumers require a readback. Preserve resident surface
ownership, draw order, clipping, culling, blending, and the X8 alpha pass.

Start with decoded programs whose supported operations and operand bounds can
be established. Decline context-writing programs and CPU output diagnostics.
Before a production draw, define how GPU-detected exceptional values or
W-plane crossings can decline without partially changing the destination.
A shadow computation with CPU-authoritative output can validate this contract;
its cost is not a production performance result. The helper's proposed
fingerprint-specific admission was rejected.

Independently test whether reusable packet/triangle buffer capacity removes
allocation churn in this measured preparation region. Preserve owned packet
lifetimes, clear logical sizes, and bound retained storage; keep this separate
from arithmetic or GPU-offload claims.
