# Native immutable pipeline-state ownership candidate

Tracking: `beads-xbox.1.206`, architecture burndown R08. Game performance and
final combined-package qualification remain pending.

The admitted Phobos renderer already retained compiled shaders and multiple
sampler objects. Blend, depth and raster state retained only the current object;
changing material state destroyed it and created another. This candidate gives
those immutable objects one bounded native owner across material changes.

The canonical donor preparer embeds the authored cache in `d3d8_states.c`.
Each state kind retains at most 64 objects, keyed by the entire initialized
descriptor, including byte length. The unchanged-current fast path remains.
Stencil reference stays dynamic and separately bound. Failed creation preserves
existing cached objects. Bounded replacement and device shutdown retire owned
objects; current state pointers are borrowed. The underlying RHI state wrappers
contain only their native immutable state-object pointer. No guest resource,
memory contents, command or completion is omitted or cached here.

`XLLE_PILOT_NATIVE_PIPELINE_CACHE=0` resolves the original uncached ownership at
startup for matched diagnostics. There is no live ownership switch. The cache
uses portable C and typed factory/destructor callbacks. Baseline prepared
dependencies are unchanged; use a freshly prepared isolated dependency to build
this package. Its preparation manifest hashes the embedded implementation as
part of the complete prepared state source.

A CPU contract checks complete descriptor identity (including colliding cheap
identities), copied keys, failed creation under capacity pressure, bounded
eviction and device retirement. The actual prepared renderer builds, and all 29
selected backend GPU/pixel checks pass with the cache enabled and disabled.
These checks support behavior and lifetime; they do not establish a speedup.
D3D11 can already share underlying objects, so removing repeated factory calls
still needs matched built-game CPU, startup and memory measurement.

Diagnostic `pilot_native_pipeline_counters[12]` records requests, hits, factory
attempts and evictions for blend, depth and raster in that order. It is observed
only from the matching private PDB. Counts are not inclusive CPU costs.
