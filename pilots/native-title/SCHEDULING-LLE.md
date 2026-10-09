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
of performance-counter time. That assertion was stronger than the bounded
original contract: timer insertion starts from the last 1 ms clock tick.
[Microsoft's Timer Accuracy documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/timer-accuracy)
also allows native relative expiration up to one system-clock period early;
that host allowance must not silently enlarge the Xbox contract.

An independent native NtWaitForSingleObject measurement recorded 128 consecutive
10 ms waits (minimum 14.5674 ms, none early), then 48 waits with varied entry
phase (minimum **9.6430 ms**, two early, none earlier than 9 ms). All returned
STATUS_TIMEOUT. Private evidence: `event-host-deadlines.aot-private.json` and
`event-host-phase-deadlines.aot-private.json` under `build/blinx/`. This explains
why repeated passing runs alone did not settle the previous failure.

The regression now permits at most the independently established **1 ms**
early margin and logs requested duration, actual elapsed time and status.
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

## Next offline work

Connect the maintained clock/PIC route to original expiration and blocked-thread
wakeup, then qualify thread ready/block/wake, priority and callback eligibility.
Retain the native implementation as the title's active scheduling backend until
the original service route can perform those operations. Any genuinely
silicon-only question is appended to the deferred end of the burndown; GPU
startup is not a prerequisite for this work.
