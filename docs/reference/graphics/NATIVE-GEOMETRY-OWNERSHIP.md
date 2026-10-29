# Native geometry ownership capacity

Tracking: beads-xbox.1.157; actual JSRF failure beads-xbox.2.3.

The original JSRF geometry manager expects its create method to return a real
resource wrapper, then locks that wrapper. Native diagnostics established that
both shared CPU vertex and index registries repeatedly returned out-of-memory
at their fixed 256-owner limits. A later original caller dereferenced the failed
wrapper. Native GPU vertex handles had a separate identical limit. These were
host bookkeeping limits, not established Xbox resource limits or exhausted RAM.

All three registries now grow geometrically using checked allocation and zeroed
extensions, as the existing texture registries do. Original guest headers,
physical/index bytes, native objects and allocation-failure status retain their
existing contracts. A failed metadata allocation claims no slot. Real guest or
GPU allocation failures still fail normally; the change creates no placeholder
resource or success flag.

Identity remains a slot index or slot-plus-one handle. Bindings, state blocks
and completion wait leases retain those identities rather than a pointer into
a movable array. A waiter keeps its resource alive while another operation
can grow the registry. Element pointers remain local to operations that cannot
grow that same registry. Device close checks ownership before freeing the
metadata; recreate begins with empty tables. Vertex cache invalidation visits
the complete grown native table.

The focused real-runtime fixture holds 513 vertex buffers and 513 index buffers
simultaneously, crossing two growth boundaries. It preserves a first resource's
binding and zero-user-reference wait lease during growth, checks old and new
CPU/index/native GPU bytes, reuses released slots without aliasing the retained
resource, and verifies final release and true allocation-failure cleanup.
The fixture repeats across three actual native device lifetimes. The actual
title now creates 375 vertex and 373 index buffers without the previous false
out-of-memory or null-resource stop. This establishes the capacity correction;
it does not establish full title/attract completion.
