# Ordered native draw batching

Tracking: beads-xbox.1.7.17. The first measured title is Blinx NTSC Round 2
Stage 1 (guest stage 4, rd21). This builds on the
[deferred-state cache](NATIVE-DEFERRED-STATE-CACHE-2026-10-22.md).

## Contract

Compatible buffered triangle strips share one prepared native pipeline and
become an indexed strip with restart indices between the original strips.
Restart preserves winding and parity for odd and even strip lengths. Original
draw order, vertex bytes, alpha blending and logical GPU completion serials
remain intact. The optimization is shared renderer code and contains no title
addresses or game-specific geometry substitutions.

The runtime admits nonindexed programmable buffered strips. The prepared Phobos
backend further excludes feedback aliases, edge antialiasing, flat shading,
logic-copy draws and the special explicit floating-point blend path. Those
cases keep their existing native path. Other backends retain immediate drawing.

At most 64 strips and 4096 index words are retained. Each original draw still
records its own resource use and completion serial. Non-draw graphics services,
GPU observations, native binding/state changes and changed resource uploads
flush pending work before their effects. Repeated preparation with identical
inputs may retain the batch. Readbacks, queries, completion checks, presentation,
reset and teardown also flush. Index-buffer allocation failure replays the
retained original draws.

Small changed vertex ranges may upload a bounded 4 KiB page within their owned
buffer. Every later draw still compares its actual consumed bytes. Later CPU
writes in that page therefore flush previous reads before updating the native
buffer; prefetching does not make guest memory immutable or trust a dirty flag.

Set `XLLE_PILOT_DRAW_BATCHING=0` for the diagnostic immediate baseline. Ordinary
launches enable batching. `XLLE_PILOT_BATCH_PROFILE=1` logs cumulative admitted
draws, native submissions and partial vertex upload counts every 30 presents;
it is disabled by default. The matching-build diagnostic selector is used only
by the private same-process measurement harness, never by normal gameplay.

## Evidence and limits

Private evidence is under `G:/Temp/Codex/xboxllr/stage-entry-20261022`.
Automated games use separate copied saves and identity, muted hidden windows,
four logical CPUs at normal priority, and one game process at a time. They do
not use the owner's persistent save or alter simulation timing/visual quality.

`strip-qualified-022` reached the actual stage 4 courtyard. During gameplay the
admitted strip stream fell from approximately 20,000 draws per update to roughly
one third as many native submissions, with groups reaching 64 strips. A retained
screenshot was inspected. This reduction alone is not a frame-rate claim.

`strip-ab-023` alternated batching off/on in six three-second windows inside
the same running stage and build. Settings were adaptive 1080p, 2x2 SSAA, FXAA
and frame interpolation. Only the owned child's matching-build diagnostic
selector changed. Frame-weighted process CPU time fell from 58.25 to 55.15 ms
per game update (5.3%); total updates per wall second rose from 13.89 to 14.88
(7.2%). Concurrent host load was substantial. This is a modest bounded gain,
not a steady 30 Hz result or an isolated GPU benchmark. It should not be
compared directly with earlier runs under different host load.

The first overly restrictive candidate admitted no actual game draws. A later
wall-clock selector attempt entered the wrong stage. Those runs are excluded;
the harness now waits for actual game updates when pressing/releasing buttons.

Ten focused GPU tests pass together. The new fixture compares complete GPU
images with batching off/on for odd/even strips, all culling modes, ordinary
alpha blending, changed vertex/texture/state/current-input/raster constants,
readback and completion. Occlusion sample counts match across 65 strips,
including the 64-strip capacity boundary. Existing backend, alias/completion,
fixed declaration, special float blend, indexed draw, edge AA, null-texture
alpha and deferred-cache/guard checks also pass. These tests establish the
covered lowering boundaries, not full campaign or hardware equivalence.

## Optimization burndown

Batching alone did not close the owner's severe Stage2-1 slowdown. The broader
[performance reassessment](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/BLINX-STAGE21-PERFORMANCE-ASSESSMENT-2026-10-22.md)
led to [shared resource-write tracking](../runtime/NATIVE-RESOURCE-WRITE-WATCH-2026-10-22.md):
a further16.3% process CPU saving in its own balanced comparison and an owner
report of much smoother entrance gameplay. Occasional remaining hitches are
tracked separately in beads-xbox.1.7.18. The separate optimization percentages
are not additive whole-game speedup claims.

| Candidate | State | Next evidence needed |
| --- | --- | --- |
| Deferred-state categories and linear/contiguous texture checks | Implemented and measured | Combined owner entrance review accepted |
| Ordered strips and small vertex upload grouping | Implemented; modest measured gain | Combined owner entrance review accepted; covered raster cases remain in focused tests |
| Repeated native bindings and pipeline preparation | Optional follow-up | Attribute remaining CPU cost before changing ownership |
| Resource ownership and repeated texture-byte checks | Implemented with correct write/alias tracking;16.3% less process CPU/update in balanced run050 and owner-accepted smoother entrance | Retain; minor hitch follow-up is beads-xbox.1.7.18. Invalid omission remains removed |
| Shader prewarming | Optional follow-up | Distinguish first-use compilation from sustained entrance work |

The remaining candidates do not gate the current owner playtest. No particle
effect, game update or correctness guard is skipped to improve timing.
