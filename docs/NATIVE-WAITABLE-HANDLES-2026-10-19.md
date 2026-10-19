# Native mutex and mixed-wait ownership

Tracking: `beads-xbox.1.137`. This shared native extension addresses the actual
dashboard's `NtCreateMutant` stop during ambient WAV loading. Source lookahead
also finds an immediate worker dependency on mixed event/mutex waits. It uses
the original contracts recorded in the [reference batch](NATIVE-MUTANT-REFERENCE-2026-10-19.md).
No title address is part of the implementation.

| Xbox API | Arguments / callee cleanup | Native owner |
|---|---|---|
| NtCreateMutant 192 | 3 / RET12 | Unnamed real NT mutex; low-byte initial ownership |
| NtQueryMutant 213 | 2 / RET8 | Count and two Boolean fields; exactly six output bytes |
| NtReleaseMutant 221 | 2 / RET8 | Real ownership/recursion release and optional previous signed count |
| NtWaitForMultipleObjectsEx 235 | 6 / RET24 | Leased native objects passed to the desktop five-argument multiwait |

Events and mutants share a tagged, monotonic handle registry. Closing a guest
handle retires its lookup identity; an in-flight operation retains a duplicate
of the real host object. Stale identities are never reused. Event query,
set/reset and file-completion transport require the event kind, so a mutex
cannot accidentally become an I/O completion event. Generic single waits admit
either kind. Registry kind and thread-context admission are checked before a
mutex wait acquires a host reference.

Bookkeeping is allocated before real initial ownership is granted. Create
publishes a handle only after success. Mutant outputs receive complete writable
range checks before a state-changing operation. Query copies the six original
field bytes, preserving guest struct padding. A non-owner release returns
`C0000046` without writing PreviousCount: the original API's own SEH handler
catches that kernel exception and returns its code. The reference observed the
raise and proves the handler by source; it does not claim completed SEH execution.

Mixed waits snapshot their argument list and retain every host reference before
the first wait. A bad later handle releases those references without acquiring
an earlier mutex or consuming an event. The real NT kernel performs WaitAny or
atomic WaitAll, including recursion and synchronization-event consumption.
Blocking waits yield the existing guest scheduler token. There is no polling
loop or fabricated completion. References are released after every returned
wait status.

The admitted domain is ordinary native guest threads, which have distinct host
threads. Original-scheduler and DPC mutex ownership remain guarded. Mixed waits
admit 1..64 native event/mutex or supported file-wait handles, WaitMode 0/1 and
nonalertable waits. Thread-handle multiwaits, original handles under files-LLE,
duplicate WaitAll entries and guest APC delivery remain guarded. Named/rooted
or inherited mutex creation is outside this batch; a null attribute descriptor
or an unnamed descriptor with only case-insensitive metadata is accepted.

The ambient source's conditional teardown also uses signal-and-wait with a
thread handle. That operation remains a separate required capability if actual
menu/navigation reaches it; this batch does not invent its success.

The native fixture exercises Xbox stack cleanup, Boolean width, query canaries,
recursion, previous count, non-owner status, real host contention/abandonment,
typed file-event references, mixed waits, stale handles and outstanding-object
lifetime. Two child fixtures check unsupported DPC single waits. Existing event,
file async/write and scheduler checks cover the affected shared paths. Original
zero-poll evidence and native host contention evidence have different scopes;
neither establishes precise original competing-thread wakeup timing.

The optimized dashboard and native contract fixture build successfully. Seven
combined mutex/event/media/scheduler/thread-lifetime checks pass in 3.37 seconds;
the separate storage check, including file writes, also passes. The actual
immutable, muted run `20261019-152140-lva5gm1k`, executable `d42f2101`, passes
the reached mutex startup operations and opens the native audio device. It
then stops at an unowned audio buffer, before a menu frame. This run does not
establish that the predicted ambient worker multiwait or teardown executed.
