# Owned HLE render worker

Experimental build selection: `XLLE_RENDERER=HLE`, `XLLE_HLE_WORKER=ON`.
The default is off. The LLE renderer and the synchronous HLE renderer remain
separate choices; there is no live renderer replacement.

The design reference is NoRain211's DOAXVB runtime at `aa1d3c8`, especially
`recomp-runtime/d3d_presenter_thread.cpp`: producer ownership, a bounded queue,
ordered work and backpressure. We do not copy its continued execution after
some draw declines. A failed required draw makes this worker fail visibly.

`Nv2aHleDraw` contains owned geometry, constants, textures and initial surface
data. The worker copies it before submission returns and executes all work in
FIFO order. Submission means accepted work; it does not promise completed
pixels. The queue is bounded to 128 waiting jobs and 64 MiB of outstanding
packet storage, with a single oversized packet permitted when empty. It waits
for capacity instead of dropping draws. Counters report draw submissions,
fences, backpressure, maximum depth, worker busy time and caller fence wait time.

The producer maintains logical surface residency, including prior queued draws.
This allows the next packet to refer to an earlier GPU target without asking
the worker to stop after every draw. Logical residency is not pixel completion.
Surface aliases and invalidation follow submission order. A readback whose
range intersects a resident plane runs on the worker behind all prior work;
the returned bytes are complete before the guest sees them. Non-overlapping
readbacks need no fence. Clears retain the existing return/fill contract and
therefore also execute through an ordered synchronous request. Kicks and
resource invalidations are ordered queue operations.

The graphics context is detached on the creator thread before worker use and
detached on the worker at shutdown. The queue drains before destruction; the
window and renderer are destroyed on their creator thread. Failures are sticky
and surfaced by later API use/readback. An unobserved final queued failure
terminates visibly rather than letting destruction report success.

The real-renderer test is run both synchronously and with `--worker`. The worker
variant tests caller mutation after submission, surface readback/clear/free,
stream formats and 1,000 queued ordered draws before readback. It also submits
an unsupported shader and requires the subsequent fence to report failure.
The qualification run reaches queue depth 128 with 857 backpressure waits;
1,028 draws execute, plus the deliberately failing submission. This validates
actual worker execution and failure propagation, not dashboard performance.

Do not infer speed from the existence of the thread. Keep/park depends on the
real dashboard replay; per-draw copies, driver contention and required fences
can outweigh overlap. A frame-level packet batch is a possible later direction
only if counters identify queue overhead as the limiting cost.
