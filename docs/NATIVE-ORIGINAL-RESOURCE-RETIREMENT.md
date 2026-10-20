# Original allocator resource retirement

Tracking: `beads-xbox.1.137`. Returning from Music in the native dashboard
reached destruction of a vertex buffer created by original title code. Its
header belongs to the original process heap and its data to an original
contiguous allocation. Native renderer companions do not own either allocation.

The shared service now distinguishes native-created storage from original
storage. A proved profile supplies the original unregister and tagged-free
callables; those bodies execute through the existing generated native CPU.
There is no interpreter added to this path and no replacement heap algorithm.
The profile verifies complete helper bodies, their call sites, allocation tags
and cleanup conventions before emitting this capability.

For admitted original vertex buffers, inline index buffers and CPU-backed
textures, explicit destruction or the final public/internal reference queues
retirement after private wait leases finish. The service records the real GPU
use serial, destroys the host companion and releases its backing lease, then
waits for actual GPU completion. It invokes original unregister and free on
the originating API thread, with the renderer lock released. Physical storage
must be an exclusive allocation base; archive interiors or shared allocations
are not silently passed to a free routine. Unsupported resource kinds and
GPU-only aliases retain explicit guards.

Register-call adapters retain the actual caller stack separately from their
small canonical service frame. Original callbacks use that committed stack,
preserve the enclosing CPU/FP/SIMD context, and validate stdcall cleanup and
callee-saved registers. Nested graphics calls remain possible. A retirement
marker prevents re-adoption while callbacks run, and device Reset/final Release
cannot discard an active retirement. Native-created objects continue to free
through their native owner. Discarding a host view with a retained caller
reference leaves the caller's original storage intact.

The authored runtime fixture checks real GPU submission/completion, explicit
destruction, final public release, binding and private-wait retention, an index
never adopted by the renderer, nested unlocked callbacks, CPU/FP preservation,
actual allocation retirement and two device lifetimes. Its allocator callbacks
are authored transport fixtures, not executions of the original heap. Separate
guard cases reject shared backing, incorrect stack cleanup and nested Reset.
Source/profile tests reject incomplete or altered helper proofs and altered
metadata migrations.

Actual native dashboard execution supplies the integration evidence: a bounded
TCP batch opens Music, Settings and Memory and returns home from all three,
running the original unregister and heap-free bodies. The real rendered
screenshots were inspected. This qualifies those ordinary opening/back routes;
it does not qualify every deeper menu operation, resource type or offline
feature. Audio, Credits and the same-framework Blinx regression remain separate
acceptance work.
