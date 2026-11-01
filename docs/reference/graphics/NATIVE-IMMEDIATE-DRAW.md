# Native immediate vertices

Tracking: `beads-xbox.1.163`. This service implements the reached classic XDK
Begin/End and SetVertexData2f/4f path through the shared native draw owner.
It does not execute a guest GPU interpreter or introduce title-specific drawing
branches. Original API bodies remain private and hash-bound by the title profile.

## Ownership and state

The retained NV097 model defines persistent current attributes: a completed
position write snapshots all sixteen float4 registers into a vertex. Both the
legacy position4f packet and generic attribute zero have that effect. A 2f
attribute write supplies Z=0 and W=1; the original 4f API alone maps register -1
to its legacy position packet. Non-position attributes may be changed before
Begin and survive End. Starting another batch does not invent new defaults.

The native service reuses the existing current-value owner and stores independent
host snapshots. Allocation grows with checked arithmetic. Failure retains the
preceding batch and stops explicitly; it does not silently discard vertices at
a fixed 4096-vertex limit. No guest RAM allocation is used for host staging.

End supplies a host vertex span to the ordinary draw service. Render targets,
textures, deferred state, viewport/raster placement, depth/blend, native queues
and completion retain their existing owners. Transient GPU uploads restore the
previous stream binding. The float-input wrapper uses the existing FVF vertex
core; colors retain their floating values through interpolation instead of
being packed prematurely into UBYTE4. Shader and staging allocations are released
with their respective device owners.

The profile verifies the four complete API identities, original stack cleanup,
packet encodings, shared device pointer, Begin's flush helper and device flags.
Native Begin/End preserves the observable active flag at device offset 8. Other
state calls inside the admitted batch are explicit boundaries. Exact physical
driver packet timing is not claimed by this whole-service abstraction.

## Qualified scope and remaining boundaries

The initial consumer is pretransformed XYZRHW FVF with admitted optional colors
and up to four coordinate sets, using existing triangle/list/strip/fan/quad
submission. Normal-bearing XYZRHW is rejected by the existing FVF format mask.
Original native D3D defaults are inherited from the existing current-value owner;
this does not establish cold-silicon default values.

Programmable/XYZ immediate vertices, other attribute APIs, partial list/quad
tails, nested batches and diagnostic input serialization remain explicit
boundaries. Original End's conditional internal SetFence(1) path is guarded:
it must not be mistaken for the public InsertFence ABI. These are required work
if an actual supported sequence reaches them, rather than waived output.

## Evidence

The authored storage fixture exceeds 4096 vertices, injects initial and growth
allocation failures, checks independent float snapshots, reuses storage and
checks teardown. It remains active in optimized builds.

The GPU fixture checks complete 64x64 images for optional FVF colors/coordinates,
flat/Gouraud output and RHW=.5 over recreated devices. The API fixture checks
the real generated dispatch ABI, flags, persistent colors, 2f completion,
position zero/-1 emission, independent snapshots, empty restart and complete
quad pixels. Shared buffered/FVF/indexed/deferred-state regressions cover the
same common draw service. Actual JSRF advancement is recorded separately in the
title's native attract burndown; component passes alone do not establish attract.

The JSRF Unleashed reference has host UP submission, not these XDK immediate
APIs. Phobos's separately licensed shadow implementation informed the staging
design; no implementation body was copied, and its silent fixed-capacity
dropping was not adopted. Existing prepared-renderer notices remain intact.
