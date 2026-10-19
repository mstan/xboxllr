# Native worker delay, wait and teardown contracts

Tracking: `beads-xbox.1.137`. Shared implementation in `event_objects.inc`,
`thread_objects.inc` and `thread_scheduler.inc`; title profiles select native
scheduling. These services contain no dashboard addresses or title-specific
success substitutes.

`KeDelayExecutionThread` (99) accepts ordinary native-thread, nonalertable
relative 100ns intervals and zero, snapshots a fully readable eight-byte
interval, calls real `NtDelayExecution`, and returns with Xbox RET12.
Desktop `STATUS_NO_YIELD_PERFORMED` (`40000024`) maps to the original success
status on completed zero/expired delay paths; other NT statuses are retained.
The original captured/source delay reference returns success even without
peer progress on zero delay. The Windows host independently reproduced
`40000024` on eight zero-delay calls and success on a relative delay; a native
fixture caught this difference before the actual title check.
WaitMode and Alertable use their original byte widths. Positive absolute dates,
guest APC delivery, DPC execution and original-scheduler contexts stay guarded.
The observed dashboard worker requests a relative 100ms interval through its
original Sleep wrapper. Existing original-kernel delay/peer-progress evidence is
retained in [scheduling qualification](../pilots/native-title/SCHEDULING-LLE.md)
and `thread_delay_cpu_contract_test.inc`; the new native path does not replace
that reference or establish physical timing precision.

Thread waits hold an independently duplicated **real execution handle** under
the thread table lock. They check the complete monotonically assigned guest
identity and open-handle state, rather than borrowing a slot pointer or using
the earlier `finished` bookkeeping flag. A duplicate survives guest close,
actual host exit, table reclamation and later slot reuse. New lookups after
close fail. Single waits (233/234), multiple waits (235) and signal/wait (230)
share that reference transport; every operation releases its duplicates.

Ordinal 230 preflights its arguments and acquires both operation references
before any signal. It calls the paired desktop NT primitive with its four host
arguments, preserving Xbox's five-argument RET20 ABI. Event signals are the
admitted signal type. Actual thread termination is a persistent wait target;
non-thread blocking handoffs remain guarded before mutation. The original
software reference separately retains five zero-time event outcomes, including
invalid later handles leaving the signal state unchanged.

Do not describe the desktop primitive as globally atomic. Microsoft's
[SignalObjectAndWait documentation](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-signalobjectandwait)
explicitly declines that guarantee across processors. The native admission
uses persistent thread completion and the bounded zero-poll component;
precise original competing handoff ordering is a separate question. No pulse
or transient event handshake is qualified by these checks.

The guest scheduler releases its CPU permit while 99 or 230 actually waits,
as it already does for 233–235. Real OS completion determines readiness; there
is no fabricated wakeup or shortened delay. The native fixture checks lower
priority guest progress during a real delay and real worker completion during
signal/wait, alongside stale identities, exact handle/VM baselines and register
and stack preservation. Native deadline checks retain the existing explicit
one-millisecond early-expiration tolerance; no upper bound assumes an idle host.

Validation: the optimized native title and contract executable build with all
103 generated CPU objects unchanged. The final source-fix rebuild took 12.50
seconds for those two targets (host load is variable). All 21 combined checks
pass in 6.18 seconds: the two new positive fixtures, eight new guards, existing
event/mutant and two DPC guards, thread lifetime/scheduler, audio
source/headroom/notifications, storage and media checks. The initial run caught
the host zero-delay status difference and an incorrectly targeted guard fixture;
both are corrected, with the success assertions retained.

Actual native executable
`cbb7ffa049691ea73e074a2bead57789d46d11350c8dd216d5106f5ab1ca551d`
passes the previously reached periodic delay, creates all three stereo ADPCM
ambient buffers and their workers, and issues subsequent file reads while the
main thread opens the dashboard font and loads another section. It then stops
explicitly at guarded network consumer `0007e125`, after 201,721 entries on the
stopped thread; this is not a unique-function coverage percentage. The child
exits 2 before the 20-second deadline, with no captured frame. Preparation,
staging and process startup are outside that child's observation deadline.

These checks do not qualify an interactive menu, audible ambient streaming or
performance, Credits navigation or same-framework Blinx behavior. Original
kernel references remain unchanged, and the full release still requires FINAL
linking and both titles on the same tested framework revision.
