# Native operation ownership candidate

Tracking: `beads-xbox.1.206`, optimization campaign `beads-2wy8.1`.
This is the initial implementation for the wider 27-entry architecture
burndown. Built-game performance and the final owner handoff remain required;
the producer, state and resource redesign is not complete.

The Phobos backend now owns submission boundaries according to actual native
effects. Previously every graphics ABI entry except DrawVertices flushed pending
strips, including CPU queries and repeated bindings. The runtime keeps guest
cache writes, ABI results, resource references and callback ordering synchronous,
while the backend flushes before changed native inputs, resource writes,
readbacks, queries, presentation and teardown.

Unchanged programmable shader, stream and texture bindings can retain native
input ownership. Texture rebinding still resets a sampling reinterpretation;
resource identities are validated before reuse. Geometry-dependent blend
admission remains per draw. Inline CPU bytes are still observed by the existing
draw/state/resource consumers. Actual GPU callback barriers are retained.

A capacity drain submits the existing indexed strip and restores the previous
index binding, preserving prepared shader/raster/input state. State/resource
mutations and observations still invalidate preparation. The existing 64-draw
and 4096-index bounds remain unchanged.

`XLLE_PILOT_NATIVE_OPERATION_OWNER=0` selects the prior ownership policy before
the first operation for matched diagnostics. It is resolved once; it does not
switch HLE/LLE or execution backends. The unqualified renderer retains the prior
policy through the same interface.

The focused real-GPU checks compare exact pixels and visibility samples with
batching disabled/enabled, exercise changed vertex/texture/current/raster/state
inputs and GPU completion, and cross submission capacity. An additional ABI
fixture interleaves CPU display queries, state setters and stream rebindings
with 65 original draws, checking real pixels, callee cleanup and resource
retirement. Both policies must pass. These are correctness observations, not
measurements of performance in Blinx or JSRF.
