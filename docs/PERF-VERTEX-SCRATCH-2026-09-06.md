# Generic vertex buffer capacity experiment

Tracker: `beads-xbox.1.15.15`. Starting revision: `b806e52`.
Goal: faithful LLE at 60 FPS with headroom; this is one bounded experiment.

The vertex/draw overlap census identifies resident primary-color preparation
as a material CPU population. Its timer includes copying and allocation as
well as geometry math. Test allocation reuse independently before attributing
that whole interval to work a GPU could remove.

`--nv2a-generic-vertex-scratch` is opt-in. The negative spelling restores fresh
local vectors. Reuse capacity for the generic packet's owned vertex vector and
the shared resident primary/constant-color triangle expansion. Keep the
original copy, finite-value tests, W handling, area arithmetic, winding,
culling, and renderer contract. Texture expansion and renderer staging are
separate possible experiments.

The two local vectors lease storage from their owning `Nv2a` instance. Every
exit restores empty logical sizes through RAII. Accepted primary expansion
swaps the two vectors in the candidate arm, preserving both capacities.
Retain at most 65,536 vertices per buffer (7 MiB each with the current
112-byte vertex); larger buffers are released after use. Disabling releases
retained storage. No worker may share these buffers: generic execution is
currently fenced and inline on the emulation thread.

Counters report packet/expansion leases, nonempty-capacity reuse, oversize
releases, and retained bytes. They do not claim an allocation-time saving.
Validate early-return ownership, successful expansion exchange, empty batches,
and the retention cap. Existing real-GL fixtures compare recycled packets
against software surfaces; fake-renderer snapshots must survive later draws.

Then run a quiet noperf same-binary interleaved 3+3 canonical A/B, requiring
all checkpoint pixels and checked guest state to agree. Promotion requires
both total-wall median and minimum improvements of at least 5%, followed by
interactive navigation validation.

The first candidate is implemented and passes all 39 CTest tests. A verbose
rerun confirms the real GL fixtures executed without a skip. Root review found
that the helper's first nested test used two different scratch members; an
additional test now borrows the same member recursively and checks that the
outer vertices survive. Disable/re-enable epochs prevent a still-active old
lease from returning storage after the owner has discarded it.

Candidate executable SHA-256:
`9ed21fa9b2dac2b78d135c87c90bef7f0658ef2f6eda99fae5251de6f0aa83d0`.
## Canonical result

`canonical-r1` stopped before leg 3 because another host build became active;
its preceding A2 leg was contaminated. Preserve the attempt for fidelity
evidence, but do not use it for a timing verdict.

`canonical-r2` runs the same binary with opposite flags in one session. Its
whole-pair retry protocol accepts attempts 1, 3 and 4, replacing attempt 2
because A2's post-run census detected an unrelated CMake/Ninja build. The
selection is recorded before timing analysis and never filters by speed.

| Total 12 B wall | Scratch off | Scratch on |
| --- | ---: | ---: |
| Median seconds | 48.0602267 | 46.8616963 |
| Minimum seconds | 48.0483679 | 46.6856004 |
| CV | 0.749% | 0.982% |

Improvement is **+2.494% median / +2.836% minimum: REJECT** at the required
5% gate. Retain the reproducible opt-in experiment, with both defaults OFF.
There is no interactive promotion claim. This result does not justify treating
the entire measured packet/triangle interval as allocation cost, nor adding
these savings to an overlapping future GPU offload estimate.

All six selected legs match all 28 PPM checkpoints and the checked guest
state/timeline fields. The three selected B legs report identical counters:
19,702 packet leases, 17,607 expansion leases, 37,307 reuse hits, zero oversize
releases, and 9,433,536 retained bytes. A emits no scratch census.

Private evidence: `build/vertex-scratch/canonical-r2-pair-selection.json`,
`canonical-r2-abba-gate.json`, `canonical-r2-fidelity.json`,
`canonical-r2-counters.json`, reports and build/test logs. The next GPU numeric
probe is separate and leaves CPU emulation authoritative.
