# HLE semantic PGRAPH context transport

The modern stock boot reaches a real SAVE at 6,062,421,649 instructions,
instance111d. Owned kernel control flow requests target2 (graphics suspend),
disables FIFO and waits idle, saves the previous context, then restores
control/FIFO operation. This branch does not immediately read or LOAD the image.
SAVE cannot be ignored or implemented by replaying the original LOAD source.

The build-selected HLE transport preserves actual context-local values and
known masks in one authoritative ContextState capsule. Existing member aliases
refer to this stable owner; assignment changes its values, never its identity.
Nv2a remains noncopyable. The retained LOAD image is an optional value, so live
and saved images have independent mutable ownership. A saved instance can be
restored after supported frontend reset; fresh contexts get a new conservative
owner and do not inherit old bindings, known masks or cold-W eligibility.
Same-instance restore carries only its exact saved eligibility and values.

GPU RAM, tiling/compression tags, global PFIFO/MMIO control, clocks, interrupts,
completion, telemetry, renderer objects and pending draw queues are outside the
capsule. Guest-local flip and zpass state remain context-local; their global
telemetry does not rewind. SAVE/restore require admitted control/PMC profiles,
closed primitives, idle engine and disabled FIFO. Host residency is resolved
before transfer; derived shader/sample caches are invalidated on restore.
No IRQ, packet retirement or invented completion is supplied by the transfer.

At most16 instances and8MiB aggregate capsule payload are admitted. Capacity
failure precedes saved-slot/raw-byte mutation. Repeated same-key SAVE replaces
its slot; overlapping saved allocations invalidate the older owner. CPU writes
of any width overlapping a saved allocation invalidate its capsule. Unrelated
PRAMIN writes preserve it. Supported frontend resets preserve retained PRAMIN
owners and unknown-byte provenance; the bounded idle-discard PMC cycle described
below is admitted only in HLE.

Raw serialization is separate. The measured36A0 prefix is marked unknown and
its initialized bits cleared; only independently mapped surface words are
projected and re-marked known. Constants are preserved semantically but are
not claimed as exact silicon SAVE: eight observed words have unresolved
anomalies (GFX-D25). CPU reads, GPU descriptors/RAMHT/object binds and fresh
LOAD reject unknown raw bytes even after the active image is cleared. Explicit
CPU writes establish only their actual bytes; adjacent unknown bytes remain
guarded. Strict LLE SAVE rejection is unchanged.

Authored tests exercise method-mutated A/B surface/clear state and actual
program/constant red/green HLE draw payloads through an authored renderer, nonrewinding telemetry, independent LOAD-image
ownership, reset/restore, fresh-context noninheritance, partial CPU writes,
unrelated writes, CPU/GPU unknown-read rejection, no-save failure guards and
bounded replacement/capacity. This is semantic HLE transport, not silicon
context-byte, timing or cross-build/netplay qualification.

Reopen raw projection when an actual consumer needs saved bytes, a validated
mapping contradicts the semantic state, or a title requires unsupported
context-local fields. Implement the consumer before admitting its route.

## Idle context discard via PMC

The dashboard disables PGRAPH bit12 at9,093,661,490 after disabling its FIFO.
Run046 established idle engine, empty pull/draw queues, closed primitives,
empty vertex/index buffers and clear fault flags. The zero remaining saved
slots and mutation serial36390755 versus saved36390738 identify invalidation
of the saved allocation by CPU writes. This supports interpreting the request
as intentional boot-context discard. Requiring an unchanged SAVE blocked it;
it was an overly conservative HLE admission rule, not a hardware requirement.

HLE therefore admits this explicit bit12-only reset for an owned loaded context
with those idle/safety conditions, whether mutated, rewritten or never saved.
Unchanged-SAVE eligibility remains mandatory for the separately scoped saved
subengine-reset path; it is not needed to intentionally discard a live owner.

A qualified disable resolves host residency, clears the active frontend, and
uses existing reset control/IRQ handling while retaining PRAMIN, valid saved
capsules, raw unknown bytes, storage layouts/tags, PFIFO queues and telemetry.
A pending-cycle flag admits the matching re-enable. Fresh-context preparation
permits the original reset/FD-clear sequence before conservative actual LOAD.
Prefetched packets are never consumed to make reset eligible. Active/FIFO,
open-primitive, faulted and pending-queue reset attempts remain rejected.

Strict LLE keeps its prior PMC guard. This is an explicit semantic HLE policy;
xemu's PMC reference does not implement this reset and no silicon reset/timing
claim is made. Reopen on a consumer requiring omitted reset side effects.

After semantic reset clears the active LOAD owner, queued methods (including
reference/semaphore methods) are retained and rejected until a context LOAD
establishes a new owner. This gate is inactive for never-loaded legacy fixtures.
Current-save eligibility also tracks both bound2D surface DMA descriptors;
rewriting either revokes eligibility just like a bound3D descriptor.
