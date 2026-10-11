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

## Original thread to native body handoff - 2026-10-09

The shared bridge now executes original PsCreateSystemThread/Ex, initial
suspension/resumption, ready selection, context switch, exit, join and object
cleanup while invoking the created thread's generated native body. Admission
checks the successful original creation, current original ETHREAD, arguments,
IRQL/flags and the two hash-bound original entry call sites. Both start forms
are exercised: a one-argument start returning to the original system wrapper,
and a two-argument SystemRoutine calling original PsTerminateSystemThread.

The native body uses the original stack, TLS, PCR and thread extension through
shared physical backing. The bridge retains an actual original object reference
and retires publication ownership after original termination and final handle/
reference release. A native termination request unwinds its host callback, then
resumes the actual kernel termination routine on the original stack; it never
terminates the host thread carrying the blocked original join.

`pilot_thread_lle` passes **72 lifecycles**, exceeding the bridge's 64 concurrent
ownership slots to check reuse. Cases cover both ABIs, zero/64/8,192-byte TLS,
default/explicit stacks, a writable thread extension, nested suspend counts,
close-before/after-join, explicit object references, exit status, native caller
register/FP restoration and original stack decommit. Original thread/handle
statistics return to baseline after every lifecycle. This is bounded lifetime
evidence, not general host or guest memory endurance.

`pilot_thread_lle_blocked` verifies that an unsupported blocking native callback
fails without writing a success result or completing the waiting original call.
The independent `tools/native_thread_reference.py` executes **12 lifecycles**
with authored x86 bodies, observing the same start ABI, TLS/stack, suspension,
exit, cleanup and statistics without the C callback transport. Neither fixture
replaces any original thread or scheduler implementation.

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_thread_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_storage_reference.dll --dependencies C:/msys64/mingw64/bin --image build/blinx/files-lle-installed-hdd-v3.img --output build/blinx/thread-native-original.aot-private.json
```

This evidence found a concrete HLE mismatch: zero TLS size must publish a null
KTHREAD.TlsData, including before the suspended worker starts. The native
runtime now does so, while retaining its real host thread and stack. The native
thread test verifies that value both before start and inside the generated body.
The combined suite passes **50/50 in 20.57s**; logs are
`build/blinx/thread-native-{bridge-build,full-tests}.log`.
Final termination-frame bounds tightening passes both original thread checks
again in 0.19s; it affects the component route, not the title's selected backend.

The rebuilt mixed title ran **190.125s**, producing 18 captures with no
stop/audio-stop/fault. Both replayed Start presses/releases were delivered;
the images show gameplay, interruption/loading, title return and another demo.
The harness terminated the live process at its time bound; this is not a
natural-exit, cadence or long-run resource-stability qualification. Evidence:
`build/blinx/scheduling-thread-attract/{run.aot-private.json,contact.png}` and
`build/blinx/run-scheduling-thread-attract.log`. Title SHA-256:
`77023b73fb7559d33d9289200b2de7f46d12839fe170f05e9783711371cc9094`.
The title still uses native scheduling. This run regresses the shared bridge
and null-TLS correction; it does not claim title-wide original thread selection.

Phobos `kernel_thread.c` at the existing `bcccf8c` pin was reviewed again. Its
host wrapper is useful for native execution, but ignores TLS/extension size and
the second start context. Those omissions cannot implement this original
contract; our existing HLE already carries both contexts. No new donor body is
imported. The original-kernel route reuses the established device/Unicorn bridge.

This remains a component qualification. Creation currently requires the
priority-31 reference caller, with priority-30 workers, to exclude unrelated
captured boot workers. This first handoff admitted nonblocking observations and
termination; the following batch adds cross-thread blocking continuations.
Full initial FP/NPX state, general preemption/contention and title-wide static
selection remain open. No HLE fallback completes an unsupported call.
The native runtime also still uses a cumulative 63-thread creation budget;
unlike the original lifecycle, closing/joining does not reclaim native slots.
Correct native reuse and its memory/scheduler ownership need a separate bounded
follow-up before claiming the whole family contract. Hardware is not needed
for either follow-up.

## Blocking native continuations under original scheduling - 2026-10-09

Original threads now keep their generated C call stacks on separate host workers.
These workers are execution transport: they wait until actual original execution
reaches their entry or the return address of their pending kernel service. Only
that worker then runs. Host priorities, host wait timeouts and the native HLE
ready queue do not choose an original thread or complete its guest wait.

The generated caller's real original stack holds every pending service frame,
including its unmodified guest return address. Original wait/signal, suspend,
resume, priority and clock/dispatcher services execute against that stack and
PCR. The engine admits a return only for the current original thread's pending
frame, checking stack, callee-saved registers and IF/DF before returning all GPRs,
including EDX:EAX, to the waiting native body. Thus A can resume while B's
SetEvent or ResumeThread call remains suspended. No C-stack unwind guesses the
original scheduler's choice. Host transport handles retire when the native body
finishes; original termination/reaping and object references retain their own
separate lifetimes.

The compiled `pilot_thread_wait_lle` covers **18 workers and 432 wait/signal
rounds** across nine lifetimes, with higher/lower/equal priorities. It verifies
actual self-suspension and peer resumption, ordered handoffs, private GPR/FP state,
joining one thread while the peer's native call is still suspended, and cleanup.
Both original thread/handle counts and host handle counts return to their
respective baselines. The enclosing caller has HLE scheduling enabled, which
does not schedule these original workers.

`native_thread_reference.py --handoffs` runs the same 18-worker/432-round ordering
through authored x86 bodies without the C transport. It independently confirms
the three priority orderings, three self-suspend/peer-resume lifetimes, the
partially finished peer at the first join, and original resource restoration.
Private observations: `build/blinx/thread-handoffs-original.aot-private.json`.

`pilot_thread_timeout_lle` executes **12 blocked native waits**, covering
NtWaitForSingleObject and Ex with six relative durations. A runnable peer supplies
102 explicit PIT clock inputs; actual PIC/IRQ0, original clock/timer expiration
and context switching deliver every timeout and restore the peer. Expiration
occurs at the independently observed next-tick bucket, including exact tick
boundaries. These are controlled clock inputs, not host-speed or silicon-phase
measurements. They do not qualify the all-threads-blocked idle path.

The timeout test exposed a real transport bug: publication can invalidate a
cached Unicorn stack alias before an interrupt arrives. Direct uc_mem_write
does not invoke a missing-memory hook. Interrupt-frame delivery now materializes
the actual PTE-backed aliases before writing the frame; it does not relocate
the guest stack or invent backing memory.

The earlier `pilot_thread_lle_blocked` guard now specifically verifies rejection
of the synchronous root-service API from a native worker without its guest call
frame. Blocking through the new generated-call boundary is supported. Unknown
service families still fail before returning completion; allocator/file lifetime
postconditions must be connected before those services enter this boundary.

The existing 72 lifecycle cases and the combined native/LLE suite pass:
**52/52 in 20.47s**, `build/blinx/thread-continuation-{build,full-tests}.log`.
Existing HLE event, timeout, timer/DPC, suspend/resume and scheduler tests remain
part of that suite. No new HLE production scheduling behavior changes in this
batch. This resolves the blocked-native-continuation transport gap; whole-family
qualification and static title selection remain open.

The rebuilt mixed title ran **190.171s** with 18 captures and no
stop/audio-stop/fault. Both replayed Start press/release pairs were delivered;
captures confirm demo gameplay, interruption/loading, title return and another
demo. Evidence: `build/blinx/scheduling-continuation-attract/{run.aot-private.json,contact.png}`
and `build/blinx/run-scheduling-continuation-attract.log`. Title SHA-256:
`438e3b900afe4e5a2f170c8948daf3007e556384a401f4bc3141b9d82ec845b1`.
The harness ended a live process at its time bound. The title still selects
native scheduling; this is a shared-transport regression, not original
scheduling selection, a new cadence result or a long-run stability claim.

## Guarded title selection and deferred startup dependency

`PILOT_SCHEDULING=LLE` now builds a separate experimental title selection.
`NATIVE` remains the default. Original scheduling requires original files,
memory and USB/PIC; the native dispatcher cannot start under this selection.
The private loader creates a real original bootstrap thread with the XBE's
stack/TLS sizes, keeps its original default priority, and waits through the
original kernel. It publishes the original thread-type object instead of the
native identity token. The loader adapter is not a retail XBE-loader claim.

The first actual launch (`build/blinx/scheduling-selection-startup/`) creates
thread ID 20, handle `0x18`, then fails before executing any generated title
body. Original execution reaches `OUT 0x80cf,5` at `0x8003512f`. Inspection of
the captured original routine shows a high/low GPIO pulse with two calls to
the original delay routine; a nearby caller enters another kernel routine.
That observation establishes programming, not its board-level effect. No
constant readback, ignored write, priority boost or queue edit is substituted.
The prepared build SHA-256 is
`3428791343ba208e5f0a6726c8d35be607bfe00adfd26fc4ad4e126c81b28b21`.

This identifies an independent device dependency; it does **not** establish
that new console evidence is necessary. Preserve the guarded experiment and
investigate existing hardware documentation/reference evidence when returning
to scheduling. Do not repeat this startup experiment as a prerequisite for
audio or other independent families. Any part ultimately requiring console
observation belongs at the end of the burndown.

The bootstrap body, remaining reached service families, original clock progress
and callback integration are still unqualified. The mixed component executable
does not inherit the title's selection macro. Its **52/52** tests pass in
**20.95s**; this is not evidence that the experimental title can run Blinx.
The rebuilt default selection ran **190.141s**, with 18 captures, no
stop/audio-stop/fault and both replayed Start press/release pairs delivered.
Captures verify demo gameplay, interruption/loading, title return and another
demo. Evidence is `build/blinx/scheduling-selection-default-attract/` plus its
run log; executable SHA-256:
`c3b8b74abb7bb75802a82948f019dd1858b25a45d52a088f034f39a5ce92bb7c`.
The harness ended a live process, so this is neither natural termination nor
new cadence/endurance qualification. Stable Blinx pin `439a45b` is unchanged.

## Remaining offline work

Use the now-executable original expiry/wakeup and native DPC callback routes to
integrate original thread execution and static timer/DPC selection with coherent
wait ownership. Qualify remaining priority, contention and suspend/resume cases.
Original native bodies now block and resume under the actual kernel, with real
identity, stack, TLS and cleanup. Next connect static title selection coherently:
bootstrap identity, the remaining reached kernel-service families, callback
context/dispatch, and clock progress when all native bodies are blocked or one
computes without a kernel call. Retain explicit guards until each dependency
works; a flag alone cannot connect two dispatcher namespaces. The native
slot-lifetime limit and initial x87 control/status are corrected below; full
original/native FP callback transport remains separately scoped.
Reuse compatible Phobos transport where it fits.
If unrelated captured boot workers become runnable, qualify their actual device
dependencies rather than editing the ready queues. The earlier unmodeled
`0x80cf` access is a lead for that integration: xemu's pinned ACPI GPIO window
covers it, but its default zero response is not evidence of that register's
contract. This does not establish a new real-console requirement; first examine
the original caller and existing modeled GPIO/programming evidence offline.
Retain the native implementation as the title's active scheduling backend until
the original service route can perform those operations. Any genuinely
silicon-only question is appended to the deferred end of the burndown; GPU
startup is not a prerequisite for this work.

## Reusable native lifetimes and original initial FP context

The native runtime now retains its real execution handle independently of guest
handle closure. A slot becomes reusable only after actual host exit, guest
closure and final object-reference release. New guest handle generations reject
stale close/resume/reference operations. Retained finished objects remain valid.
Reclamation occurs at ownership teardown and creation, without a lifetime scan
on dispatch checkpoints. The concurrent retained-slot bound remains explicit;
cumulative creation no longer consumes the process's entire slot allowance.

For mixed LLE stacks, retirement executes original `NtFreeVirtualMemory`,
retires publications and Unicorn aliases, and only then decommits host storage.
Live admitted DMA pins postpone retirement. `pilot_thread_lifetimes` verifies
96 repeated lifetimes with alternating stack/TLS sizes, zeroed reused storage,
exact committed-byte/handle baselines, retained references, and a host held after
its finished marker. The mixed fixture also checks three real original stack
pin/unpin/release/re-reserve lifetimes.

`native_thread_fp_reference.py` reuses the original thread creator, both start
ABIs and twelve original lifecycle/resource checks. Each fresh saved FX context
contains CW `0x027f`, cleared status/abridged tags, MXCSR `0x1f80`, and zero
x87/XMM storage. This resolves startup values offline. Native TLS now starts
with CW `0x027f` and cleared condition status instead of `0x037f`/`0x4000`.
`pilot_thread_fp` checks four actual fresh title-thread contexts and poisoned
caller/child isolation. It does not establish empty-tag representation, full
MXCSR/lazy-NPX transport, or extended arithmetic.

The coherent CPU batch passes **44 mixed LLE entries, 30 native entries and nine
APU entries** with stable source hashes. Private evidence is
`build/blinx/services-foundation-v3-20261010.aot-private.json`, incorporating
the compatible passed v2 entries without rerunning them, plus
`thread-fp-original-contexts-20261010.aot-private.json`. Initial broader attempts
remain preserved: one mistakenly admitted old shader test and an old realtime
native audio comparison failed; missing native test executables were subsequently
built and passed. Neither old failure is relabelled as qualification of this
generation. After Marvel Rivals exited, fresh graphics/native-audio follow-up
passed both entries on four cores at Normal priority. The representative muted
title run then stopped after 144.062s and five captures at HRESULT `0x087a0001`
(`DXGI_STATUS_OCCLUDED`). The old hidden-window presentation guard rejected
that documented nonfailure status. Preserve this stopped run; it does not count
as a completed 190s regression. No scheduling/audio stop or fault was logged.
Evidence is `services-foundation-fresh-followup-20261010.aot-private.json` and
`services-foundation-attract-20261010/run.aot-private.json` under `build/blinx/`.
Commit `e366831` corrects host HRESULT handling while retaining actual GPU
checkpoint/display retirement. Its single muted follow-up reaches **190.172s
/18 captures**, delivers all five replay rows and logs no stop/audio-stop/fault.
Representative captures show Demo gameplay, interruption/title return and the
next Demo loading; second-Demo gameplay is not captured in this bounded run.
The harness terminates a live process. Final evidence is
`build/blinx/services-foundation-qualified-followup-20261010.aot-private.json`
(`b7c1614398218be06bc4627618e60779c84d614f2971d5e6cbd2d2412f9d21ff`).
The bounded lifetime and initial-FP tasks are validated; whole-family original
scheduling remains open.

## Original clock, integer checkpoints and retail GPIO15

The clock batch now supplies one original-engine owner and retained native C
continuations. The controller exposes the actual maintained PIT position and
next deadline, requests an integer checkpoint while native work computes, and
samples device input time between original execution slices. A checkpoint
transfers the actual native continuation PC, eight GPRs and materialized EFLAGS;
service-entry `2|IF` is not substituted. Original PIC acknowledge, IDT framing,
IRQ0, DPC and context-switch/return decide which continuation resumes. Clock
slice exhaustion cannot complete an original service. The actual captured
idle/boot worker remains selected by the kernel, without queue or priority edits.

The runtime starts the clock by executing the hash-bound original retired INIT
PIT routine at `800786D4` on the same engine. Its three actual byte OUTs program
mode 2/reload 1125. The temporary INIT map is retired and the complete prior
engine context restored before binding monotonic input time. No second guest
clock thread or scheduler is added to the original scheduling profile. Existing
PIT edge coalescing remains explicit; this is not a new silicon-cadence result.

The pilot retains Unicorn 2.1.4 through a reviewed source-built package, using
the same small inhibition helper as the full-machine 2.1.1 engine. Each compiles
against its own upstream private headers. The pilot's block hook now consults
the actual engine inhibition state in addition to IF. Engine checks pass eight
STI/block/split/context lifetimes and 338 executed CMP cases, independently
checking arithmetic flags and retained IF/DF. See
[package build and source identity](unicorn-engine/README.md).

The earlier `OUT 80CF,5` stop is resolved for a bounded retail board profile.
NVIDIA MCP-1 0.15 section 5.15 supplies the GPIO15 control location; Xbox
hardware specification 1.02 section 2.6.5 leaves this development flash-enable
pin externally unconnected on retail boards. The maintained static-output
primitive retains each 04/05 low/high command, shared with the existing AV
output model. It does not grant ROM access, invent register readback or admit
alternate modes. The independent board read `44` is preserved as current state,
not a reset value. No physical Xbox GPIO write was attempted.

The combined source-bound mixed CPU qualification passes **47/47 entries** in
10.35s, including the existing original thread/wait/timeout/DPC/USB/memory/file
cases and three new clock cases. The no-peer blocked timeout passes both an
explicit provider and the actual runtime INIT/monotonic provider; the selected
original worker executes six high/low GPIO pulses. Two authored integer compute
lifetimes resume their actual C continuation after IRQ0 wakes a higher-priority
peer, with exact PC/GPR/FLAGS preservation. CLI checkpoints retain the pending
interrupt until IF is enabled. Invalid PC/debug flags/stack controls are rejected
without mutating the accepted state. Static GPIO/AV component checks also pass.
Private evidence is
`build/blinx/original-clock-batch-validation-20261010.aot-private.json`, with
engine/device reports and the focused clock prototype attempts alongside it.

Preserved failures include the initial unsupported GPIO stop, an upstream
optional archive symlink build, an incorrect build-command option, missing
unbuilt test executables, an overlapping fixture duration/deadline field and
the mistaken unprogrammed-PIT sentinel. The sentinel is the maintained API's
documented zero. No failed attempt is relabelled as a passed run.

This qualifies the bounded clock/IRQ/integer continuation foundations. Generic
generated title loops still need certified PC/FLAGS lowering and full FP/SIMD
context transport; an uncertified requested native checkpoint stops explicitly.
Service/callback publication and lifetime postconditions still need connection
under original title scheduling. Those are required integration work and cannot
be deferred as numeric precision. `.112.2` and the whole scheduling family
remain open, with **4/9** service-family gates qualified.

One rebuilt original scheduling startup confirms the GPIO dependency is passed
and the runtime clock starts. It stops before generated title entry on the
captured worker's byte `OUT 0072,10` at `800187B8`. This is the next RTC/extended
CMOS bus dependency; it is not a qualified original title run. The maintained
full-machine route incorrectly marked extended72/73 absent. That earlier
assumption is corrected by the CMOS continuation below; it was not silicon
confirmation. Unknown ports still must not return arbitrary successful values.
Private startup report is
`build/blinx/original-clock-title-startup-20261010.aot-private.json`.

The single rebuilt default HLE follow-up reaches **192.969s /18 captures**,
with all five replay rows and no stop/audio-stop/fault. Inspected frames10/11
show Demo gameplay/pause, frame13 title return and frame18 next Demo loading.
No second-Demo gameplay, natural termination, audio-ear or exact-cadence claim
is made. The harness ends a live process; no additional owner appearance review
is needed for this service-only change. Evidence is
`build/blinx/original-clock-qualified-followup-20261010.aot-private.json` and
`original-clock-default-followup-20261010/run.aot-private.json`.

### CMOS/SMBus board integration and original title admission

The captured original CMOS checker selects full-byte indexes through72 and
reads71. It checks96 bytes at10..6F and128 bytes at80..FF. Its original repair
routine writes the same224-byte alternating55/AA signature. The maintained
machine's absent72/73 decoder was therefore a real software-model dependency,
not an operation to ignore. Pinned xemu
[`482088490c2d5983ed5e1eafbd09906157c1eca0` RTC](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/rtc/mc146818rtc.c)
maps70..73, with a seven-bit70 selector, eight-bit72 selector, and shared data
aliases. The authored model now implements that reference profile. The main
machine and original/compiled pilot accesses use the same maintained RTC.

The private original-code bridge qualifies actual checker/repair execution
across **three fresh RTC device lifetimes**. Each checker consumes224 bytes;
twelve corrupted first/last low/high-bank cases fail, are repaired by224 actual
original stores apiece, and then pass. Three actual-artifact controls recreate
the absent extended selector and fail. Caller stack/FLAGS/callee-saved registers
and unrelated CMOS contents are preserved. Authored full-index/data-alias checks
and compiled high-bank/width/NMI/clock guards pass. This qualifies initialized
CMOS RAM under the declared reference profile, not oscillator, update, IRQ8,
NMI delivery, power-loss retention or exact silicon alias behavior. The existing
main RTC has a deterministic date and no advancing clock. The pilot explicitly
rejects clock/status/NMI data accesses rather than using that date as evidence.
Private reports: `rtc-cmos-focused-20261010.aot-private.json` and
`original-rtc-cmos-reference-20261010.aot-private.json` in `build/blinx/`.

One physical selector observation passed fresh v0.4.0 identity, sent one benign
OUT72,10, and read71/73 once each. Both returned23 rather than the expected55.
The host stopped immediately: **one selector write, two reads, zero CMOS data
writes**, no further selection, recovery write or retry. Equal current data is
not proof that the selector took effect, nor that every CMOS byte has the boot
signature. This is an inconclusive physical result, preserved separately from
the passing original/reference evidence. The owner inspected a normal screen;
a subsequent fresh PING returned the expected v0.4.0 identity, confirming the
agent is responsive. No further EXEC, selector write or recovery operation was
sent. The exact fixed payloads and all stop
paths pass five offline tests; the combined probe suite passes **111/111**.
Current-state/retention precision is appended as LLE-D03; required clock/state
transport and any reached unsupported mode remain active integration work.

After CMOS, one original startup reaches SMBusC004. The pilot now connects the
maintained AMD756 controller and guarded SMC to its shared PIC, with real IRQ11
completion and acknowledgment. Actual original HAL read/write calls pass
**three call rounds in one controller/transport lifetime**: version reset and
P11 reads, composite AV-pack read, scratch write/read, and retired IRQ/status.
The fixture's initial diagnostic called these lifetimes; that label is corrected
without changing behavior. Unconnected EEPROM/encoder transactions and reserved
register/width accesses stop before starting work. An independent copied EEPROM
would diverge from native settings ownership, so storage integration must supply
one shared owner before admitting it. Existing explicit GPU device bindings
retain their ownership precedence; this board adapter is the fallback. No host
event or native scheduler manufactures a completed original transaction.

Tightening the SMC command/protocol guard exposed a real missing owner: the
captured worker reads tray register03. The earlier permissive run inherited
the SMC's unbound zero response; that run cannot qualify this connection. The
storage bridge now exports the same maintained IDE device's actual tray state,
and the SMC reads it through one bound owner callback. There is no copied tray
or mounted-media state. A missing owner is rejected before transaction start.
The final original HAL fixture includes the actual installed-HDD profile's
tray read, rejects an unbound tray owner before attachment, and confirms null
rebinding leaves the owner intact. Unmodeled SMC commands and word protocols
remain guarded before completion/IRQ mutation. Existing independent IDE/SMC
media/open-tray checks and the RTC component test also pass.

The final coherent batch passes **49/49 CPU entries in12.218s**, including the
new CMOS and original HAL cases and retained clock/thread/IRQ/storage tests.
The source hashes are stable over that run. A separately rebuilt original
scheduling title gets through GPIO/CMOS/SMBus and admits its real bootstrap
callback/title thread: original stackD0028000..D0038000, real TLS/TIB and
entry000F48D5. It stops **before the first translated title instruction** on a
requested uncertified native compute checkpoint. The reported PC00000000 is
the unmaterialized native checkpoint diagnostic, not proof of an original
kernel jump to zero. General generated PC/materializedFLAGS and FP/SIMD
transport, coherent service/callback publication and lifetimes remain required.
Private final reports: `rtc-smbus-owned-validation-20261010.aot-private.json`
and `board-storage-context-title-startup-20261010.aot-private.json`. The earlier
permissive and strict unbound-tray attempts remain preserved; neither is
relabelled as the final qualified board path. The RTC-only intermediate stop
atC004 is retained in `rtc-cmos-title-startup-20261010.aot-private.json`.

No extra default HLE attract/audio run was needed: the changed board path is
exercised by original scheduling and CPU contracts, with the final rebuilt
original title as the applicable integration regression. The prior192.969s
default appearance result remains its own generation's evidence. Cached HLE
graphics and audio performance code is unchanged. `.112.2` remains open;
EEPROM/storage ownership under `.112.4` is also open. The family count is still
**4/9**, with **22 safely deferred questions** (14 graphics/eight cross-family).

### Shared EEPROM settings owner — 2026-10-10 continuation

The next coherent batch resolves `.112.4`'s remaining EEPROM ownership gap.
The maintained EEPROM now supports an external256-byte owner as well as its
existing owned backing. The native-title controller binds the supplied settings
store once, before starting work. Original SMBus and native settings queries
read and mutate that same store; there is no second mutable EEPROM copy. Each
byte/word access is atomic under the owner lock and wraps atFF. Native settings
queries take one coherent checksum/payload snapshot, release the owner lock,
then write guest outputs. Guest memory transport cannot reenter while that
owner lock is held. Changes stay in emulator RAM; the supplied private EEPROM
file is never overwritten.

The actual original HAL fixture reads **all256 authored bytes**, checks a word
spanningFF/00, observes native-owner updates through the original bus, and
changes native settings through original byte/word writes. A changed language
byte makes the native checksum query fail without modifying its output; writing
the corrected checksum through the original bus makes that query return the
new value. Missing/null/duplicate owners and unsupported protocols remain
guarded. The independently authored device test checks both owned and shared
backings across **three fresh lifetimes**, including serial-offset progression,
one complete callback per word, wrap and unavailable-owner rejection.

Four focused EEPROM/SMC/AV/IDE component checks pass. The final coherent CPU
suite passes **49/49 in10.906s**, with stable source identities. A separately
rebuilt original startup again admits the actual bootstrap/title thread and
stops before its first translated instruction at the existing uncertified
native checkpoint. The private input file's before/after SHA is unchanged.
Reports in `build/blinx/`: `eeprom-owner-focused-20261010.aot-private.json`,
`eeprom-owner-final-validation-20261010.aot-private.json` and
`eeprom-owner-final-title-startup-20261010.aot-private.json`. The earlier
lock-held output version and its passing attempt remain distinct evidence;
the final source releases the owner before touching guest outputs.

This completes the bounded original board/SMBus/settings-owner task, not the
scheduling family. The pinned generated CPU code still has function-local
branch/carry representations rather than a complete exported architectural
EFLAGS word. Existing zero-argument entry/backedge hooks do not carry a
certified instruction PC or full FP/SIMD state. Connecting that generator state
to the maintained clock/continuation boundary is still required under `.112.2`,
along with title service/callback publication and lifetime postconditions.
The inspected donor is the generation's actual63f18f0 pin; a newer review
checkout is not substituted for the shipped generated source.

No physical Xbox operation or extra default HLE attract/audio run was needed.
HLE graphics/cache/audio algorithms are unchanged. Exact EEPROM write-cycle
and acknowledgment timing is appended as LLE-D04; supported boot-persistence
and settings-policy changes remain explicit work rather than timing deferrals.
There are now **23 safely deferred questions** (14 graphics/nine cross-family).
The whole goal stays active with **4/9** service-family gates qualified.

### Original CPU thread execution - 2026-10-10 continuation

The LLE title now executes original thread bytes on the **existing single CPU
engine**. Its original scheduling/IRQ/device/physical-memory owners remain in
place. The native loader adapter hands over before any generated guest
instruction, retaining the actual original admission context and entry frame.
PC, all GPRs, EFLAGS and x87/MMX/SSE state then stay with that engine across
original IRQ0, blocked waits and context switches. The C worker only retains
the loader/return continuation. It does not select runnable threads or create
completion. The default HLE build keeps its fast generated native bodies.

This resolves the LLE title's need to reconstruct complete architectural flags
from the donor's function-local branch expressions. No runtime HLE/LLE switch
or state conversion is added. The older certified native integer-checkpoint
fixture remains maintained, with its existing general FP/flags guards.

The private import gates now transfer a qualified service's actual CALL frame
to its original export without resetting flags or FP state. Return checks
verify the original thread, real return PC, stack cleanup and preserved GPRs;
the original service's resulting CPU state continues directly. Lazy NPX faults
use the reviewed capture's actual #NM IDT gate and original FXSAVE/FXRSTOR
handler. They do not install a host FP initializer.

One coherent authored bytecode batch passes **18 ADD/ADC/PUSHFD lifetimes**, with
independent expected result/flag tables and three original IRQ0/preemption
lifetimes. **Six original FP workers across three lifetimes** keep live x87,
XMM and MMX values across **12 original blocked waits**. Original startup
CW027f/MXCSR1f80/zero status and sampled XMM startup state, plus distinct
per-worker controls, are observed;
results and controls remain isolated across the original scheduler/NPX route.
Wrong entry/stack, duplicate execution and calls outside admitted workers fail
without changing supplied registers. Each authored handle is independently
invalid after close, and no managed thread remains. The longer first idle
interval also lets an unrelated captured file worker close pre-existing
handle10; its original NtClose callsites are retained in the private trace.
Later lifetimes restore the settled global handle count exactly. This does not
claim that the boot capture's initial handle count stays frozen during work.

Final source-bound validation and original startup are recorded privately in
`thread-cpu-qualified-validation-20261010.aot-private.json` and
`thread-cpu-final-title-startup-20261010.aot-private.json`. Earlier arithmetic,
FP/baseline investigation and intermediate startup attempts remain separate
artifacts; failed assertions are not relabelled as passes.
The combined CPU batch passes **50/50 in 11.391s**. A preceding overlapping
title/test run failed two storage-initialization checks because both builds use
the same exclusively locked scratch HDD. Those two checks pass sequentially;
the final combined batch also runs without a concurrent title owner. The
overlapping failure remains in its original private artifact.

The original Blinx entry now runs and reaches **PsCreateSystemThreadEx255**:
actual caller priority8, unsuspended creation, SystemRoutine000F835D. The route
stops before creating that child at the existing priority/admission guard.
Next is ownership registration before a normal-priority child can start,
properly retained guest frames for nested ownership calls, then wider
service/callback and termination/lifetime integration. Do not raise the title's
priority or rewrite original queues to bypass this dependency. The diagnostic
PC000000E9 is the outer NtWait233 adapter's stop argument, not the original
CPU's instruction PC. Native entry-count/ESP diagnostics belong to that outer
adapter and do not measure executed original title instructions.

Whole title scheduling, selected native-helper interop and GPU/audio startup
remain unqualified. Numerical corner cases, unconnected exception paths and
unreached service families are not established by this representative batch.
No physical Xbox operation, default HLE attract run or audio playback was
needed. HLE graphics/cache/audio algorithms are unchanged. The family count
remains **4/9**, with **23 safely deferred questions**; `.112.2` stays open.

### Early child admission and critical sections - 2026-10-10 continuation

The original CPU route now retains a child **before the original creator makes
it runnable**, without changing the caller's priority or the original queues.
At the reviewed kernel's80025332 boundary, the successful handle is already
installed. The adapter checks the real EBP frame and ESI status, references
that handle through original ObReferenceObjectByHandle, and checks the returned
object against the creator's actual local. The nested ownership call has its
own bounded guest frame and preserves the complete CPU context. No second
ownership registration occurs when creation returns. A child may exit and close
its handle first; its retained object cannot be retired until the creator has
returned. Failed-creation cleanup retains an explicit qualification guard.

The authored parent/child programs cover **12 lifetimes /24 CPU workers** using
both original creation ABIs, actual parent priorities8 and7, real stack/TLS/
extension/context arguments and independent resource observations. Six children
run and close their handles before the creator returns. Two system children
start suspended and are resumed through the original API. Live parent x87/XMM
values and distinct controls survive creation, nested references and child
execution. Children observe their actual original FP startup state. Closed
handles are independently invalid; original thread counts and the settled
global handle baseline are restored.

An Xbox SystemRoutine must terminate explicitly: returning reaches the original
bugcheck thunk. The system-child program therefore calls actual PsTerminate
SystemThread258 with status12345678. The adapter unwinds only its retained host
continuation, then sends the **unchanged original CALL frame** to that export.
Original termination owns context switching, stack release and exit status.
Held original references expose the real child exit result. Fastcall object
dereference uses the actual ECX; it does not reuse a stale stack argument.

Allowing ordinary-priority work exposed dependencies in the unchanged captured
boot worker. Its bounded DbgPrint now enters the actual reviewed INT2D IDT
handler, which itself adjusts the saved return PC and dispatches the print
service. The host supplies no print result or instruction skip. Other debug
commands remain guarded. Its original MmMapIoSpace then reads the owned flash
image through the real page-table mapping. The256KiB image is hash-bound and
mirrored like the maintained machine profile, with read-only host/CPU backing;
ROM is separate from writable RAM/publication/DMA ownership. ROM writes and
instruction fetches remain guarded rather than establishing those contracts.

The captured worker also issues SMC LED sequence08/E1 and mode07/1 requests.
The maintained SMC now retains those raw requests for a later board-output
owner. Original HAL byte transactions and three controller rounds check that
owner, real IRQ11 acknowledgment and request replacement. No SMBus LED readback,
automatic LED state or blink phase is invented. Physical/automatic/blink details
are safely deferred as LLE-D05; they do not block the display/game foundation.

Actual original RtlInitialize/Enter/LeaveCriticalSection291/277/294 now use the
same CALL-frame transport. A separate authored batch covers **six lifetimes /
12 workers**: recursive ownership, a higher-priority contender, actual original
blocking/wakeup and ownership transfer on final release. Shared object fields,
ordering observations, canaries, invalid closed handles and resource baselines
provide independent checks. No host critical section or manufactured wakeup
implements this LLE path. The initial fixture mistakenly used suspend231 where
it needed signal225; its failed log remains preserved, and the corrected fixture
is the passing generation.

The final source-bound CPU batch passes **52/52 in14.10s**. Its preceding51-entry
admission batch and focused failures remain distinct artifacts. The separately
rebuilt, muted original Blinx startup now runs its real childD0038018 through
critical-section entry and stops at **ExQueryNonVolatileSetting24**. Connecting
that original settings query to the shared EEPROM owner, followed by reached
service/callback publication and lifetimes, is the next required work. The stop
diagnostic0000009F is an outer wait adapter argument, not the original CPU PC.
This is progress beyond child admission, not a whole-title scheduling pass.

Private final reports in `build/blinx/`:
`child-critical-qualified-validation-20261010.aot-private.json` and
`child-critical-final-title-startup-20261010.aot-private.json`. ROM and EEPROM
input files retain their before/after SHA values. The title and CPU tests ran
sequentially because their scratch HDD has one exclusive owner. No physical
Xbox operation, HLE attract run or audio playback was performed. HLE generated
CPU, rendering/cache and audio algorithms remain unchanged. One authorized
independent audio worker completed the raw SSL foundation concurrently in a
separate build, sharing the four-core Normal-priority budget. `.112.2` remains
open; family count stays **4/9**, with **24 safely deferred questions**.

### Settings owner and status mapping - 2026-10-10 continuation

Original ExQueryNonVolatileSetting24 now executes on the existing CPU against
the shared EEPROM owner. On the first supported query, the adapter retires only
the captured cache-valid flag once. Original code then loads all256 bytes through
128 real HAL word transactions and owns its subsequent cache. The adapter neither
copies captured settings nor supplies query outputs. Unknown user keys consume
no EEPROM; encrypted/factory indices remain guarded until their owner is connected.
The input ROM/EEPROM artifacts remain immutable.

Three independently authored input profiles compare original and native outputs,
including full output canaries, all19 user keys and the96-byte user block. Corrupt
USER checksum errors precede buffer-length validation. Short buffers leave type,
ReturnLength and values untouched. An all-FF image fails with INVALID_DEVICE_STATE
and retries the actual bus on the next query; a bad checksum instead retains the
loaded cache. Raw HAL storage writes do not refresh that cache. This supersedes
the earlier assumption that direct EEPROM writes immediately change native query
results. HLE now preserves that caller-visible behavior, corrects binary types
for keys2/5/18, and writes optional ReturnLength only on successful queries.

Actual title execution next reached RtlNtStatusToDosError301. The previous native
Windows mapper differed on **126 of2,354** original-domain, boundary, facility,
customer-bit and authored pseudorandom inputs: for example, status00000102 maps
to317 on this Xbox kernel and1460 on the current Windows host. Default HLE now
uses an authored sparse binary search over factual return mappings observed from
unchanged original execution. It preserves customer passthrough, HRESULT/facility
rules and unknown317 without an OS-dependent table or an LLE runtime fallback.
No original instruction body or packed kernel lookup representation is committed.
The private audit covers all **612 original lookup statuses** and the cases above;
the maintained comparison additionally checks native constants and neighboring
statuses against original execution (**2,479 comparisons**).

The source-bound combined CPU batch passes **56/56 in11.687s**. Three settings
profiles, the status mapper, existing SMBus and native contracts also pass in one
focused six-entry batch; the complete private-domain audit matches with zero
differences. Fresh muted original Blinx passes24 and301 in its real child, then
stops at **NtAllocateVirtualMemory184**. That allocator already exists, but its
successful pages/protection/lifetime must be published on the actual raw-thread
CALL/return route before admission. Do not merely allow the import or manufacture
page identities. Wider service/callback and GPU/audio startup integration remain
required; no full-title scheduling qualification is claimed.

Private reports in `build/blinx/`:
`settings-status-qualified-validation-20261010.aot-private.json`,
`settings-status-qualified-focus-20261010.aot-private.json`,
`status-map-domain-20261010.aot-private.json`,
`status-map-facts-20261010.aot-private.json` and
`settings-status-title-startup-20261010.aot-private.json`.
Earlier failed host-map and harness generations remain separate artifacts.
The sole independent audio worker's device-owned VP/GP input quantum passes
11/11 APU entries; nominal PCM24 conversion is separately deferred as AUDIO-D06.
Family count stays **4/9**, with **25 safely deferred questions**. `.112.2` remains
open. One muted native attract regression validates the default HLE corrections;
its result is recorded with the batch rather than inferred from component tests.

The final configured default-HLE regression runs **190.656s /155 captures** with
no STOP, audio-stop or fault. Actual captures show Demo gameplay, replayed Start
interruption and title return. This short batch does not establish second-Demo
gameplay or audio correctness. Its first attempt omitted the required single-CPU
scheduler setting and stopped before USB admission; that harness failure remains
preserved and is not relabelled as a production regression or a pass. The
configured run is `settings-status-attract-configured-20261010/run.aot-private.json`.
The harness stopped a live process at its bound, rather than observing natural
exit. The earlier longer scene/reentry evidence retains its original generation.

## Actual-thread VM publication - 2026-10-10 continuation

Original-thread CALL/RET now connects NtAllocateVirtualMemory184,
NtFreeVirtualMemory199, NtProtectVirtualMemory204 and NtQueryVirtualMemory217
to the same original allocation/publication owner used by C callers. Original
VADs, PTEs/PFNs, zeroing, returned ranges and protection policies remain its
decisions. A per-call host-access lease exposes existing pages to the kernel;
successful original RET publishes or retires the real pages and protections.
There is no second allocator or manufactured successful return.

Direct Unicorn API reads explicitly materialize the same owned lazy aliases as
original instructions. Nested original protection queries save the complete CPU
and use their own scratch CALL frame. Saving registers alone was insufficient:
the first fixture exposed an overwritten outer return frame. That failure and
the preceding unmapped-read failures remain separate private artifacts.

Six actual-CPU lifetimes independently check reserve/commit, initially zero
bytes, read-only protection and a real host write fault, original queried
recommit policy, partial decommit/fresh-zero recommit, whole release, pinned DMA
bytes, and original thread/handle baselines after cleanup. This kernel changes
RO to RW on a successful RW recommit; the original query, rather than a prior
assumption about retained protection, establishes that policy. A separate
zero-size whole-release case pins only the second page and stops before the
original free, retaining both pages, bytes, protections and output canary.
The zero-size guard queries original AllocationBase instead of maintaining a
parallel reservation table.

The coherent CPU batch passes **58/58 in11.859s**, and the optional GP processor
checks pass **2/2 in2.594s**. Rebuilt muted original Blinx passes184 and next
stops at **NtOpenFile202**. File-service raw-thread admission and buffer/DMA
postconditions are the next integration batch. Wider original GPU/audio/title
startup remains open; these bounded checks do not close scheduling.

Private evidence: `thread-vm-qualified-validation-20261010.aot-private.json`,
`thread-vm-final-title-startup-20261010.aot-private.json` and retained
`thread-vm-publication-fixture-test*-20261010.aot-private.log` generations.
The frozen audio worker's GP owner independently passes13 APU checks; its
original firmware prefix stops explicitly at FFFFC5=0FFF and is not a boot pass.
No hardware write, audible playback or repeated attract run is required here.
The prior configured HLE attract evidence retains its own executable identity.
Family count stays **4/9**, with the same **25 safe deferrals**.

## Actual-thread file transport - 2026-10-10 continuation

Original-thread services now admit the maintained file/cache/namespace arities
and prepare file buffers through the existing image/stack and original VM
owners. The same helper serves C and raw-thread callers, saves complete CPU
state and uses a separate nested frame before publishing actual PTE/PFN pages.
Heap buffers already belong to the original allocator. FATX/IDE/PIC performs
the real bytes, IRP/IOSB and event effects; no native completion is supplied.
File APC callbacks remain guarded before buffer/device effects.

Six authored original-CPU lifetimes exercise synchronous reads, immediate
cached asynchronous reads, and an uncached asynchronous PENDING read followed
by an actual original event wait. Both published image pages and original heap
pages receive bytes matching a separately supplied owned XBE. Original network
metadata supplies the independently checked file length. Aligned beyond-EOF
reads preserve bytes and clear the event, zero-time polling returns timeout,
missing opens and repeated closes return their original errors, and original
thread/handle baselines return after cleanup.

The initial fixture incorrectly requested desktop standard-information success;
the existing original FATX contract rejects that class. It now requests the
already qualified network-open class34. Another attempted fixture configuration
disabled all cache pages before a fresh open and observed original NO_MEMORY;
that unsuccessful generation is preserved. The final nonbuffered asynchronous
case checks both immediate and PENDING completion without inventing a delay.

With `PILOT_FILES_COMPARISON` set to the owned installed default.xbe, the raw
fixture and the existing whole-file C transport check join the coherent CPU
suite: **60/60 in14.187s**. Fresh rebuilt original Blinx passes NtOpenFile202
and next stops at **MmAllocateContiguousMemory165**. Raw-thread contiguous
allocation/publication/retirement is the next dependency. The default native
file HLE needs no new correction or extra work from this batch.

Reports: `thread-files-qualified-validation-20261010.aot-private.json` and
`thread-files-title-startup-20261010.aot-private.json`; earlier fixture failures
remain separate `thread-files-test*-20261010.aot-private.log` generations.
No physical Xbox operation, audible playback or attract rerun occurred.
Scheduling/title/audio/graphics gates remain open at **4/9**, with **25 safe
deferrals**. This raw-thread connection does not substitute for the wider
GPU/audio/startup requirements.

## Actual-thread contiguous ownership - 2026-10-10 continuation

Raw original165/166 allocation and171 release now share the C transport's
publication ledger. Original code chooses the address, PTEs/PFNs and contents;
the bridge publishes its page-rounded extent and independently checks actual
MmQueryAllocationSize180. Original173/179 queries retain physical/cache policy.
Allocation and182 protection changes save complete CPU state on separate nested
query frames and publish the original per-page access policy to host bytes.
Raw178 persistence and1/4 saved-display pointer calls use original bodies.
This does not qualify firmware relaunch or persistent RAM across a console boot.

GPU/USB consumers can now use those original contiguous owners without creating
native allocator records. The shared DMA adapter checks the entire transaction,
actual PTE/PFN identity and host protection before copying or acquiring a lease.
GPU copies remain synchronous under the existing original-driver/FIFO lifetime
contract. USB retains a lease until actual controller reset releases it;171
refuses borrowed storage before the original free. Freed/unowned/out-of-range
accesses fail without changing bytes. Existing user-page pinning remains its
separate lifetime under the same publication ledger.

Six authored actual-CPU lifetimes cover both allocation signatures, a valid
impossible allocation returning NULL, non-page size5001/actual extent8192,
high-memory alignment, ordinary and write-combined policy, metadata/persistence/
saved-pointer calls, cross-page USB read/write, mixed RW/RO transaction rejection,
controller reset, RW restoration, original release, real address reuse and
settled thread/handle baselines. A separate borrowed-free control proves rejection
before storage or output changes. No assertion assumes fresh contiguous bytes
are zero. Three existing GPU transport lifetimes additionally execute a real
FIFO jump, held/released semaphore and protection/free rejection; the third uses
a solely original allocation with no native allocator record.

The combined batch passes **62/62 CPU checks in13.016s** and fresh maintained
GPU transport in0.25s. The older GPU build first failed its required IRQ-engine
package check; the final build selects the existing reviewed source-built engine,
and retains the reference renderer DLL unchanged. The first authored CPU fixture
omitted test-entry registration and failed callback admission; that failure is
preserved separately. The corrected entry uses the existing original CPU owner.

Fresh rebuilt original Blinx passes allocation165 and persistence178 and now
stops at **ordinal49**, before an unqualified service executes. Input ROM and
EEPROM file hashes are unchanged. Reports are
`thread-contiguous-qualified-validation-20261010.aot-private.json` and
`thread-contiguous-final-title-startup-20261010.aot-private.json`; the earlier
startup at178 and all earlier fixture/build failures retain their identities.
The default HLE algorithms are unchanged; no Xbox operation, playback or attract
rerun occurred. The nine-family goal remains active at **4/9**, with **25 safe
deferrals**; wider service/callback/GPU/audio integration remains required.

The next startup guard is verified as **HalReturnToFirmware49**, rather than
another ordinary import to enable. A diagnostic-only guard observes actual
return PC000F28D1 and routine2 (quick reboot). The original EBP frame's caller
is000F2A13, with arguments0/0/1/4D530013/D0049134. Unchanged private launch
helpers and generated import names corroborate this path; its launch reason
remains under investigation. The installed-media selection currently replaces
a complete helper in generated native code; its application to the actual
original CPU route must be checked before implementing firmware transition.
No reset is executed and no RET is supplied. The final observation is in
`firmware-caller-title-startup-20261010.aot-private.json`, with unchanged input
ROM/EEPROM files. Earlier62-entry qualification keeps its source identity.


### Installed-media selection on the original CPU - 2026-10-10

A bounded original-CPU entry/return trace established the quick-reboot cause.
The reviewed optical helper at000F749D returned C0000010. XapiInitProcess
followed its error path through the original fatal-launch helper and
XLaunchNewImageA into HalReturnToFirmware49. The build's approved INSTALLED
selection previously applied only to generated C callers. The original CPU
executed the optical body regardless of that selection.

The same reviewed complete helper now selects INSTALLED on the existing CPU.
Generated metadata binds its address, full222-byte extent and original SHA256;
registration verifies owned resident bytes, and admission rejects later changes.
The retained native thread continuation executes this zero-argument helper
outside the stopped engine. Its internal file, VM and close services use actual
CALL/RET frames below that original thread's stack workspace, the same PCR,
original scheduler and original FATX/IDE/PIC owner. A4KiB per-thread stack lease
keeps paths and output records separate without a shared scratch lock across
blocking calls. The engine retains actual x87/MMX/SSE state. Completion checks
the unchanged return frame and every undeclared GPR, restores the original flags,
and applies only EAX plus the declared RET0 stack effect.

Installed verification streams the actual HDD default.xbe, compares its SHA256,
then frees its real VM buffer and closes its original file handle. It retains
real errors, including C0000012 for a wrong identity. It supplies no optical
authentication packet and does not execute or turn a firmware request into RET.
The OPTICAL selection retains the original helper. Default fast HLE uses its
existing generated complete helper and does not incur this CPU transport.

Three authored actual-CPU lifetimes cover successful identity, wrong identity
and successful reuse, with exact flags/x87/XMM and settled original thread/handle
baselines. Wrong registration hashes fail before binding. Three terminal controls
reject changed original bytes, an undeclared register change and return-frame
corruption before the original caller resumes. The coherent CPU batch passes
**66/66 in12.969s**. Fresh original startup verifies real installed identity,
returns zero to000F75EF and next guards **ordinal289**. Wider original services,
GPU/audio startup and firmware transitions remain required; **4/9** gates and
**25 safe deferrals** are unchanged.

Evidence is in `media-launch-trace-title-startup-20261010.aot-private.json`,
`media-component-final-qualified-20261010.aot-private.json` and
`media-component-frame-final-title-startup-20261010.aot-private.json`. Earlier
65-entry and intermediate startup observations keep their source identities.
A private validation-helper BOM decoding failure is also preserved; it did not
run a check or change product code. No physical Xbox operation, playback or
attract rerun was performed.

### Counted-string construction and comparison - 2026-10-10

Unchanged original289/290 independently execute18 null/empty/short/boundary
cases. Xbox stores wrapping16-bit byte lengths: ANSI65535 bytes produces
Length65535/MaximumLength0, and UTF16 byte65534 produces65534/0. Strings
past that boundary wrap again. Desktop RTL saturates instead, disagreeing on
six cases. The caller's Buffer pointer is retained exactly; no string storage
is allocated or copied. The default HLE now implements those observed rules
directly, including the previously missing Unicode initializer.

The next actual import is RtlEqualString279, with three arguments; critical
initialization is291. An independent original observation executes316 on
all256 byte values and279 on808 counted-string cases. Xbox byte folding maps
ASCII a-z and Latin1 E0-F6/F8-FE to their uppercase bytes, preserves the other
observed bytes, and maps FF to3F. Desktop RtlEqualString disagrees on64 of
these cases. The authored default HLE uses those observed rules, a direct
memcmp for sensitive comparison, and the caller's exact16-bit Length. Embedded
NULs remain counted bytes, MaximumLength does not limit comparison, unequal
lengths reject before reading buffers, and only the BOOLEAN's low byte selects
folding. No original implementation or packed character table is copied.

Both initializers and279/316 now use the same original C/raw-thread import
transport with their actual argument counts and CALL/RET frames. Eighteen
original-thread/native initializer cases retain actual VM Buffer pointers,
unaligned UTF16, output/source canaries and original ownership cleanup. Three
additional lifetimes each execute808 original equality cases and256 original
uppercase cases on real guest threads, then compare the actual native dispatch
ABI. They include full65535-byte loops, low-byte BOOLEAN values, input-record
and byte preservation, handle/thread baselines and original VM retirement.
The maintained runtime clock permits long original execution across bounded
CPU slices; no completed return is inferred from an instruction budget.

The coherent CPU batch passes **68/68 in14.312s**. One rebuilt, muted original
startup runs8.375s, passes the counted-string imports and next stops on a real
GPU read **FD001804 at original PC0013BD90**. The GPU/MMIO owner is not yet
connected to this actual-thread route. Original scheduling/title/GPU/audio
integration remains required; gates stay **4/9**, with **25 safe deferrals**.
The default HLE has no original-CPU fallback or per-call desktop RTL lookup.

The reproducible authored oracle is `tools/native_counted_string_reference.py`.
Its private output is `counted-string-comparison-original-20261010.aot-private.json`;
the independent CPU and title evidence are
`counted-string-comparison-qualified-20261010.aot-private.json` and
`counted-string-comparison-title-startup-20261010.aot-private.json`.
Earlier67-entry qualification and startup at279 retain their source identities.
The first long oracle call exceeded its previous million-instruction fixture
bound; that failure is preserved, and the unchanged original call returns under
an explicit four-million bound. No failed call is counted as passing. ROM and
EEPROM input hashes remain unchanged. No physical Xbox operation, playback or
attract rerun was performed.
