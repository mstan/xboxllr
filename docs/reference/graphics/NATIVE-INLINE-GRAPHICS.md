# Native inline graphics commands

Some original XDK callers write Kelvin packets directly through CDevice's
current/limit pointers. Whole native graphics API replacements therefore need
a shared packet owner as well as their normal service implementations.

The command arena owns bounded guest staging memory and immutable host
snapshots of published commands. Before each subsequent native API operation,
the owner validates and consumes preceding inline packets. Reserve and MakeSpace
consume old packets before reusing staging storage. KickOff submits the actual
native queue. None of those operations fabricates GPU completion, advances a
physical FIFO read pointer, or retires a guest fence. The existing native query
and fence machinery retains that responsibility.

The portable parser supports aligned incrementing/nonincrementing subchannel-0
Kelvin packets and zero-count NOPs. It preflights an entire published span
before applying its first method. Invalid extents, partial packets, object
rebinding, control transfers and unsupported methods stop explicitly. A failed
native application latches the owner so a retry cannot replay already submitted
work. Guest recording mode remains outside this linear staging contract.

The initial method consumers reuse the renderer's existing simple render-state
conversions and implement the zero vertex-buffer cache-break command. Cache
break invalidates vertex-source upload reuse; the next native draw refetches
its source bytes while previous GPU work and existing shader/binding ownership
remain intact. Readback of already uploaded data remains valid. Unknown methods
are implementation work if reached, rather than silently accepted state.

Zero texture CONTROL0 packets now disable each of the four native sampling
stages. The admitted runtime transaction is the original inline NULL setter:
original software has cleared the selected guest cache and removed its internal
resource reference before publishing the packet. The command consumer retains
the prior native owner until that packet drains, then unbinds the actual native
texture and reconciles host ownership without releasing the guest reference a
second time or rewriting its last-use fence. Several pending stage NULLs may
share one texture. Subsequent whole-API rebinding uses the same resource owner.
The source-proved complete SetTexture body and reached inline caller both
perform this transaction; no title addresses enter the shared implementation.
Live guest bindings, missing original reference releases, enabled CONTROL0 and
other texture register forms remain guarded rather than being treated as NULL.

Title profiles supply the command ABI and device/global layout. The binder
checks the original CreateDevice initializer's zero-default stores, reserve
routine's device/current/limit/margin references, MakeSpace's half-window call,
and KickOff's CDevice register argument against complete image-bound bodies.
For the current dashboard profile, these establish a 512 KiB arena budget,
32 KiB reserve window and 516-byte threshold margin. Shared runtime code
contains no title address. Other profiles and retained Blinx headers do not
gain the optional command-buffer admission.

Only the exact preceding record/header/wrapper may gain this metadata; drift
and feature removal reject before generated writes. The actual dashboard
refresh changes one metadata header among 119 generated files, retaining all
CPU bodies and wrappers. It is idempotent.

Validation includes strict GCC and MSVC portable parser builds, original-body
profile checks, real renderer refetch/readback, guest register/stack adapters,
packet/API visibility, duplicate-drain prevention, bounded recycle, real native
submission/fences, repeated complete lifetimes and explicit unknown/record-mode
child guards. Fourteen profile tests and eleven combined Windows checks pass;
the Windows checks take 2.46 seconds. All 103 generated CPU objects are reused.

Actual native executable `461f0c76...` passes the formerly stopped space request
and continues to a missing indirect native CPU target at `0004eaf0`. It exits 2
before its deadline with 254635 stopped-title-thread entries and no captured
menu. This proves startup advancement, not full scene rendering or menu QA.
Original hardware is not needed for the current native-code discovery gap.

The later NULL-binding batch passes seven combined Windows checks in 1.85s,
including real native sampler state, all four stages sharing one resource,
guest reference/fence preservation, rebinding, destruction, repeated lifetimes
and five guard children. Its actual executable `9cfa3b45...` passes the NULL
packet and reaches an original-created declaration-only vertex shader instead
(255389 stopped-title-thread entries, exit 2 before deadline, no menu capture).
All 104 generated CPU objects are retained. Original object adoption and its
packed-normal fixed-function consumer remain required implementation work.
