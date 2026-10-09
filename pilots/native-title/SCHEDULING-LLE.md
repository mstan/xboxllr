# Scheduling, threads, events and timers LLE

Active family `beads-xbox.1.112`, under the revised `.102` burndown. The owner
explicitly deferred real-console-dependent work on 2026-10-09; this family
proceeds independently of GPU startup `.109` / hardware observation `.111`.
The total remains four qualified families and five open.

## Existing evidence and required route

- Calendar conversion `.103` is qualified through original kernel execution
  in the native title build.
- `tools/native_scheduler_reference.py` executes original ready-queue selection
  and base-priority routines: 31 priority/FIFO/exclusion cases, immediate
  higher-priority selection and 11 priority arithmetic cases. It does not
  execute complete thread context switches, dynamic boosts or interrupt timing.
- `thread_scheduler.inc` supplies native single-CPU ownership and bounded
  priority scheduling. Native wait/suspend/resume and callback tests exist.
  These tests alone do not establish the complete original scheduler contract.
- `io_device_lle.c` already runs original event operations for the qualified
  file/device route. Reuse that original kernel/RAM/interrupt transport rather
  than inventing another event model or copying HLE behavior into the floor.

First batch: event creation/state transitions, event lifetime, immediate waits,
timer insertion/cancellation and the caller-visible timeout contract. Original
execution and the compiled shared-RAM route now pass the bounded cases below.
Broader thread/preemption/timer integration remains open; do not promote an
event slice into full family qualification.

Any question that requires real-console observation belongs at the end of the
shared burndown, with its affected contract left explicit. No console operation
is required or authorized by this offline family.

## Original events and timer queue - 2026-10-09

`tools/native_event_timer_reference.py` reuses the original device-manager
reference. It executes unmodified NtCreateEvent, NtQueryEvent, NtSetEvent,
NtClearEvent, NtWaitForSingleObject/Ex and NtClose. Three independent RAM/CPU
lifetimes cover **96 event lifecycles**: both event types and initial states,
previous-state writes with canaries, repeated set, reset, signal consumption,
zero-time polling and closed-handle rejection without output mutation. It
raises the reference caller through original KeSetPriorityThread, as the file
fixture does, to exclude competition with unrelated captured boot workers.
Original handle identities may be reused after closing; native monotonic handle
identities are an allowed internal difference, not a required original policy.

The same reference executes original KeInitializeTimerEx, KeSetTimer/Ex and
KeCancelTimer for **72 relative-deadline cases** across those lifetimes,
including sub-tick values, tick boundaries and a duration exceeding 32 bits.
It checks actual timer-queue insertion, rearming's prior-inserted return,
cancellation, and immediate signaling of an absolute deadline in the past.
Export-derived KeInterruptTime and KeTimeIncrement supply independent expected
values. The captured increment is **10,000 units of 100 ns (1 ms)**; relative
deadlines equal the last published interrupt time plus the requested interval.
No elapsed clocks, expiration interrupt or blocked-thread wakeup is synthesized.

The maintained C bridge now admits the corresponding original timer calls and
four-argument NtWaitForSingleObjectEx. Its interrupt-time adapter retains the
original EDX:EAX return. `pilot_event_timer_lle` runs **32 event lifecycles and
24 relative timer cases**, plus past-deadline signaling, through that compiled
bridge. This uses the existing original files-worker configuration and kernel
namespace; it is executable component evidence, not a newly selected complete
LLE scheduler in the title. Production events/timers remain native for now.

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_event_timer_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output build/blinx/events-timers-original.aot-private.json
cmake --build build/blinx/host-calendar-lle --config Release --target pilot_contract_test xlle_native_title --parallel 4
ctest --test-dir build/blinx/host-calendar-lle -C Release --output-on-failure
```

The complete mixed LLE/native contract suite passes **47/47 in 23.62s**, including
existing native events, timers/DPCs, thread services and scheduler tests. Logs:
`build/blinx/event-timer-{lle-build,full-tests}.log`. The owned capture, generated
observations and executables remain private; existing interpreter/license
restrictions are unchanged.

The rebuilt mixed title ran for **190.235s**, producing 18 captures with no
PILOT STOP, audio-stop or fault log. Images show title, attract gameplay, return
to title after the replayed Start input, and a subsequent demo entry. Both Start
press/release pairs were delivered (15/16s and 110/111s). The bounded harness
stopped the process; this is not a natural process-exit or long-run leak claim.
Evidence: `build/blinx/scheduling-events-attract/run.aot-private.json`,
`contact.png`, and `build/blinx/run-scheduling-events-attract.log`.
This preserves existing HLE attract behavior; it does not claim that the title
has switched to the still-incomplete original scheduler.

Contract executable SHA-256:
`c3eb784fa9a07cc321b813bd3a95200728b9b0afd680b0cfb7b9a9e7e45430e2`.
Title executable SHA-256:
`8bf63a668135d1fffa228bd0cb0cc6705a86e50a499048d49083356ce5f6a716`.

## Relative wait tolerance and issue .99

The old native test required a 10 ms relative wait to consume at least 10 ms
of performance-counter time. Native NT can return earlier; the original
timer-insertion fixture alone did not prove that the Xbox also does so.
[Microsoft's Timer Accuracy documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/timer-accuracy)
also allows native relative expiration up to one system-clock period early;
that host allowance must not silently enlarge the Xbox contract.

An independent native NtWaitForSingleObject measurement recorded 128 consecutive
10 ms waits (minimum 14.5674 ms, none early), then 48 waits with varied entry
phase (minimum **9.6430 ms**, two early, none earlier than 9 ms). All returned
STATUS_TIMEOUT. Private evidence: `event-host-deadlines.aot-private.json` and
`event-host-phase-deadlines.aot-private.json` under `build/blinx/`. This explains
why repeated passing runs alone did not settle the previous failure.

The regression permits an explicit HLE timing tolerance of at most **1 ms**
early, matching the independently established Xbox clock resolution, and logs
requested duration, actual elapsed time and status. This is a tolerance for
native NT, not an assertion that original Xbox expiration behaves identically.
It still fails a timeout earlier than 9 ms; it does not accept an arbitrary
host clock period. No native wait implementation or production deadline was
changed. Late scheduling under host load is not assigned an invented upper
bound. This settles the overly strict assertion for the measured configuration,
not timer-interrupt timing on real silicon or every possible host configuration.

Twenty further real native event-test processes pass in 1.96s (40 timeout
observations, 11.0504-21.5996 ms; no wrong status or value outside the bound).
Log: `build/blinx/event-tolerance-repetition.log`. These repeats validate the
changed assertion; the separately captured early returns and original deadline
arithmetic provide its rationale.

## PIT-driven expiration and real thread wakeup - 2026-10-09

`tools/native_timer_expiry_reference.py` executes the original kernel's clock
initialization from its owned, hash-bound image: command `0x34`, count `0x0465`
on ports `0x43/0x40`. This establishes binary mode 2, 1,125 input ticks per
interrupt. INIT was retired before the RAM capture, so its unmodified page runs
at its original address on a separate initialization CPU. Every OUT reaches our
maintained PIT; no clock register, timer queue or kernel function is patched.

The maintained PIT/PIC then drives the captured original IRQ0 gate, EOI, clock
update, timer expiration and DPC dispatcher. Explicit clock inputs make this a
slow-capable correctness route. They do not claim instruction/cycle calibration
or wall-clock performance. The compiled shared-RAM bridge now accepts that
kernel-owned IRQ0 gate and executes the same 12 expiration cases, including
original KeWaitForSingleObject consuming synchronization timers while leaving
notification timers signaled. Its IRQ wrapper verifies interrupted registers,
stack and flags. The existing native backend remains selected for title timers.

Independent observations:

| Contract | Original result |
| --- | --- |
| Relative durations 1, 9,999, 10,000, 10,001, 100,000 and 330,000 in 100 ns units; both timer types | Expire after 1, 1, 2, 2, 11 and 34 supplied clock interrupts respectively; no early signal, Inserted clears |
| Periodic timer, 3 ms requested | Five actual DPC callbacks at ticks 1, 5, 9, 13 and 17 in the fixture; original reinsertion remains live until cancellation |
| DPC arguments/eligibility | Actual timer DPC receives its object/context and sampled system-time halves at IRQL 2; cancellation prevents later callbacks |
| Eight event lifetimes, both reset types | Original wait blocks and a real lower-priority thread runs; timeout returns STATUS_TIMEOUT; worker signal wakes the caller with STATUS_SUCCESS and proper consumption |
| Delay and thread lifetime | Original KeDelayExecutionThread yields/resumes; original PsCreateSystemThread/start/context-switch/termination and wait/join/close execute successfully |

The original fixture executes **260 interrupts and 17 blocking waits**, including
eight timeout/signal pairs and one delay. Its authored worker and DPC are ordinary
test programs: their instruction bodies do real counting, signaling and recording;
they do not replace any original implementation or fabricate completion. The
worker is created at the original default priority 8, raised through the original
API to 30, and competes with the priority-31 fixture caller. This bounds unrelated
captured boot-worker interference; it does not qualify arbitrary contention,
equal-priority fairness, suspend/resume or dynamic boosts.

The block-driven fixture's 12-13 ms observed elapsed values for a requested
10 ms wait include execution time assigned to kernel blocks. They are not a
host-speed or real-console latency measurement. The separate single-step timer
observations expose the bucket boundary: even an exact 1 ms relative deadline
expires on the second supplied tick. Consequently the earlier queue-arithmetic
evidence must not be cited as proof of early original expiration.

### HLE comparison and remaining integration

Our existing native tests cover the same state, wait status, signal consumption,
cancel/rearm, periodic callback and system-time argument contracts. They pass in
the combined suite. `dispatcher.inc` uses host deadlines and serial guest DPC
ownership; it need not replay IRQ0 internally. The first expiry batch exposed
a periodic-cadence difference, resolved by the follow-up below: the original
3 ms case has a 4 ms interval, while native reinsertion requests 3 ms after its
own expiry. A per-deadline margin does not automatically justify accumulating
phase or callback-count differences; the follow-up fixes the native deadline.

Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, `kernel_sync.c`, was
rechecked for reusable timer services. Its Win32 timer-queue approach is a useful
native reference, but its callback forwards existing DPC argument fields, while
the original timer passes sampled system time. Our already implemented dispatcher
passes the original argument shape and preserves serialized guest execution.
No new donor implementation is imported for this batch.

PIT hardening removes the unprogrammed periodic edge, fixes first LSB/MSB read
ordering and unread latch retention, normalizes mode aliases and stops mode-0
repetition. The mode-2 test includes 64 individually serviced interrupts, masking,
coalescing and terminal-count behavior. Initial load phase/gates/BCD/read-back
commands and mode-3 count readback are outside this bounded qualification; this
is not exhaustive 8254 emulation. Exact board phase questions, if needed, join
the deferred hardware queue rather than blocking other families.

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_timer_expiry_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_storage_reference.dll --dependencies C:/msys64/mingw64/bin --image build/blinx/files-lle-installed-hdd-v3.img --kernel ../xboxlle/build/kernel-live.bin --output build/blinx/timer-expiry-original.aot-private.json
```

The rebuilt mixed native/LLE suite passes **47/47 in 20.88s**. Evidence:
`build/blinx/timer-expiry-{bridge-build,full-tests,title-build}.log` and the
private JSON above. Original-code/RAM inputs and generated observations remain
private. No console is needed for this batch, and family count remains **4/9**.

The shared machine/PIT change also passes **4/4 in 2.11s**: PIT, optimized AOT
machine/interrupt state, idle fast-forward and block-hook guards. Logs:
`build/blinx/pit-regression-{build,tests}.log`.

The rebuilt title ran **190.171s**, with 18 captures and no stop/audio-stop/fault.
The contact sheet shows demo gameplay, Start interruption/loading, return to
the title and another demo entry. All four replayed press/release transitions
were delivered. The bounded harness terminated the live process; this is not
a natural exit or long-run resource-stability qualification. Evidence:
`build/blinx/scheduling-expiry-attract/{run.aot-private.json,contact.png}` and
`build/blinx/run-scheduling-expiry-attract.log`. Title SHA-256:
`16769cd2799915daea1fc2a2ba1b109d90f406f8523f1a05fce590ac1ba3146d`.
This regresses the existing mixed title configuration, not original scheduler
selection in production; the PIT is unprogrammed there until explicitly selected.

## Original DPC/native callback bridge and HLE cadence correction

The maintained bridge now runs timer and ordinarily queued DPC callbacks through
their generated native bodies. The original kernel remains responsible for
expiration, queue insertion/removal, dispatch eligibility and actual invocation.
Admission checks the hash-bound original call sites, active DPC dispatch at
IRQL 2, the dequeued object's routine/context, and enabled interrupts/clear DF.
An unknown callback or state stops explicitly.

The original DPC stack is separate from the interrupted stack. The bridge checks
its three resident pages and surrounding guards, then publishes those actual
pages through the existing shared-PFN mechanism. The callback sees the original
VA and argument storage; no stack relocation, private copy or guest allocation
substitutes for it. Kernel-session ownership keeps this stack live. Existing
native callback state preservation now handles the DPC's four stdcall arguments
and `KeIsExecutingDpc` state, in addition to the ISR's two-argument ABI.

`pilot_timer_dpc_lle` executes actual original timer expiry into a native callback,
then nested original queue/duplicate/remove/requeue and cancel calls. It verifies
system-time arguments, original-stack identity, delayed invocation at raised
IRQL, cancellation preserving already queued work, and native GPR/FP restoration.
The independent Python fixture also executes ordinary queue/dequeue and IRQL
lowering, recording the second original callback call site and stack. The original
`KeIsExecutingDpc` returns a nonzero active-stack value while executing a DPC;
the contract is truth/nontruth, not the exact integer `1` used by native HLE.

### Cadence settled without an HLE interrupt loop

`native_timer_expiry_reference.py --cadence` records nine separate original
clock/kernel lifetimes and **45 actual periodic callbacks**. Requested periods
`1,2,3,10,16,31,32,33,100` ms produce intervals
`2,3,4,11,17,32,33,34,101` clock ticks respectively. Every case checks cancellation,
the original callback arguments, IRQL and interrupted-register preservation.
Evidence: `build/blinx/timer-periodic-cadence.aot-private.json`.

The native dispatcher now schedules relative expiration on the tick strictly
following the queued deadline, using its established clock origin. Periodic
reinsertion follows that same rule. The guest-visible DueTime retains the queued
deadline, not the later expiration bucket. Host waitable timers still perform
the wait; there is no 1 kHz HLE IRQ loop and no duplicate original execution.
Native tests check the independent six-duration and nine-period observations,
including phase within a tick, then execute real host waits and periodic callbacks.
Host scheduling may delay callback delivery; no exact wall-clock latency bound
is inferred from the instruction-driven reference. Absolute host deadlines and
the separately documented native NT event-wait tolerance are unchanged.

The combined suite passes **48/48 in 20.72s** including the new original/native
DPC bridge. Logs: `build/blinx/timer-dpc-{bridge-build,full-tests,title-build}.log`.
The expanded independent fixture passes 12 expiration cases, five periodic plus
one ordinarily queued callback, **257 interrupts**, 17 blocking waits and the
original worker join. `build/blinx/timer-dpc-original.aot-private.json` records
this run; the earlier 260-interrupt result used a longer fixed observation window.

The rebuilt mixed title passes a **190.141s** regression with 18 captures and no
stop/audio-stop/fault. Both replayed Start presses/releases are delivered;
images show demo gameplay, interruption/loading, title return and another demo
entry. As before, the harness ends a live process after its bounded observation,
so this is not a natural exit or long-run resource-stability claim. Evidence:
`build/blinx/scheduling-dpc-attract/{run.aot-private.json,contact.png}` and
`build/blinx/run-scheduling-dpc-attract.log`. Executable SHA-256:
`3ddf453577df3bc69b80462087ed18a223b48592beba6ae248cf77705342f83a`.

This closes the callback transport and observed cadence differences. It does
**not** select original timers for the whole title yet or close scheduling.
Native host waiters currently consume native dispatcher state while releasing
the guest CPU. A static original timer/DPC selection must connect those wait
ownership rules coherently, alongside original thread execution, rather than
letting two schedulers mutate the same queues. This is offline integration work;
it does not depend on GPU startup or new real-console evidence.

## Next offline work

Use the now-executable original expiry/wakeup and native DPC callback routes to
integrate original thread execution and static timer/DPC selection with coherent
wait ownership. Qualify remaining priority, contention and suspend/resume cases.
Start with one original thread/native body handoff using the existing bridge
and worker fixture. The synchronous service bridge currently supports nested
callbacks, not non-LIFO returns between blocked native threads. Establish real
thread identity, original stack and return-boundary ownership before enabling
title-wide selection; do not add a second scheduler that guesses the original
kernel's choice. Reuse compatible Phobos thread transport where it fits.
Retain the native implementation as the title's active scheduling backend until
the original service route can perform those operations. Any genuinely
silicon-only question is appended to the deferred end of the burndown; GPU
startup is not a prerequisite for this work.
