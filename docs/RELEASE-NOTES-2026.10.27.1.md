# Native runtime optimization - 2026.10.27.1

This release integrates the owner-accepted optimization batch for Blinx and both
supported JSRF revisions. Xbox CPU execution and graphics/audio HLE were already
native; this release reduces repeated native preparation work and enables
qualified whole-program Release compilation for these titles.

- Reuse prepared strip groups, immutable shader-stream plans, validated vertex
  owner index hints and unchanged shader binding within proven lifetimes.
  Mutation, byte observation, resource retirement and real completion remain.
- Correct display-only vblank waits to avoid renderer ownership and retain
  actual display timing and callback behavior.
- Add capability-checked `PILOT_NATIVE_IPO`, off by default in the framework.
  Qualified MSVC x64 Release title builds enable it; other hosts and iterative
  profiles retain normal portable compilation. No extra ISA or fast math.

In the matched draw-heavy Blinx 2-1 NAS workload, CPU time per original update
fell from 62.126 ms to 54.975 ms (about 11.5%), with about 8.4% higher original
update throughput. The compiler increment versus preparation-only v4 was about
7.3%. Late host-load drift limits this to an approximate scene/host result.
Both JSRF revisions are neutral/inconclusive in matched native-save Garage
samples: no proven performance win and no reproducible per-game regression.
Source swaps are not simulation updates or interpolated display frames.

All 19 discovered opportunities have evidence-backed retain/reject/defer
dispositions in the [burndown](NATIVE-OPTIMIZATION-BURNDOWN-2026-10-26.md).
Sanitized raw samples, package labels and binary fingerprints accompany the
[qualification data](optimization/2026-10-27/qualification.json).
Native save/fresh-load, bounded gameplay and GPU/callback/resource checks pass
within their recorded scopes. Shared NAS fixture failures remain separately
tracked; failed checks are not counted as passes. The owner accepted both
final games visually and audibly on October 27. This is representative
qualification, not a full campaign or every-mechanic audit.

The toolkit remains source/Python tools plus prepared dependency source.
The separate Dashboard playable build is unchanged. Original game and console
inputs, private saves, personal artwork and review profiles are excluded.
Preserve the complete existing game `data` folder and external save/HDD profile
when updating; use the fresh release files without replacing that progress.
