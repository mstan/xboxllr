# Original NT mutant and mixed wait reference

Tracking: `beads-xbox.1.137`. `tools/native_mutant_reference.py` executes the
unchanged original kernel object manager through the existing captured RAM
reference. The flat live kernel and captured input are SHA-bound. No native
runtime, handle registry, generated title or hardware is modified by this tool.

Confirmed ABI: NtCreateMutant192 takes three arguments (RET12),
NtQueryMutant213 and NtReleaseMutant221 take two each (RET8), and
NtWaitForMultipleObjectsEx235 takes six (RET24). The desktop multiwait has
a different argument count; do not project its ABI onto Xbox callers.

Four original mutant lifetimes cover InitialOwner 0, 1, 100 and 101 hex.
Only the low byte determines initial ownership. Available count is 1; an
initially owned or newly acquired mutant has count 0; recursive acquisition
decrements it below zero. Zero-poll single waits233/234 acquire recursively.
Release returns the previous signed count through its optional four-byte
output and increments the count toward 1. NULL PreviousCount succeeds.

Query213 writes exactly **six** bytes: signed count at 0, OwnedByCaller at 4,
AbandonedState at 5. The two bytes of trailing struct padding are untouched,
as are surrounding canaries. These original same-thread cases report no
abandonment. Closing and querying/waiting/releasing the stale handle returns
invalid handle `C0000008`; closing the last owned handle is separately observed
without claiming any competing waiter's abandonment behavior.

Cross-type operations return object type mismatch `C0000024`, retaining all
output bytes: query-mutant/release-mutant on an event, and query-event/set-event
on a mutant. This does not imply that valid mixed *waitable* objects are wrong
types for a wait.

Seven original mixed event/mutant zero-poll multiwait235 cases establish:

- WaitAny selects index0 and consumes a signaled synchronization event without
  acquiring the later available mutant; the next call returns index1 and
  acquires that mutant, including recursive acquisition on the following call.
- WaitAll times out without partial acquisition when the event is unavailable,
  including without a recursive mutant decrement when the caller already owns it.
  When both are available, it consumes the event and acquires the mutant.
- An invalid second handle is rejected before consuming a signaled first event.

All these use UserMode1, nonalertable waits and a real zero timeout, with RET
cleanup/nonvolatile-register checks. No blocked wait, competing thread,
alertable delivery or thread-handle transport is qualified by these cases.

Releasing an unowned available mutant reaches the original exception path
with exception record code `C0000046` and untouched PreviousCount. The captured
software floor then stops on unmodeled interrupt41 (`int29`); the whole SEH
return is not execution-qualified. The public returned status `C0000046` is
nevertheless source-grounded: the active scope table selects an unconditional
EXECUTE_HANDLER1 filter which saves ExceptionRecord.Code. Its handler drops
the referenced object, resets the try level, loads that code and jumps to the
normal RET8 epilogue. The API catches this exception internally, so reproducing
this public error does not require delivering an exception to the guest caller.
The receipt hashes the exact scope table, filter, handler and interrupt stub,
preserving the distinction between observed raise and source-derived return.

Read-only ambient source lookahead finds a necessary multiwait path:
constructor62DE0 starts worker62C80/62C90; the worker calls original
6D796/6D6B2 and ordinal235 for WaitAny over its exit and buffer events,
then over exit event plus mutant. The requested timeout is infinite and
Alertable is false. This is source prediction, not an observed reached235 or
blocked-wakeup qualification. Conditional teardown634A0 calls230 through6D66B
only when its buffer object exists, signaling the exit event while waiting on
the worker thread. No230 component execution or implementation is claimed.
Exact original title bodies and caller arguments are retained privately in
`ambient-mutant-wait-lookahead.aot-private.json`.

Reproduce the component batch from the framework checkout:

```powershell
py -3 tools/native_mutant_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --kernel F:/Projects/xboxlle/xboxlle/build/kernel-live.bin --output build/native-profile-checks-20261019/original-mutant-contract.aot-private.json
```

The reference follows the existing event/timer tool's captured-thread priority
setup. All object and output mutations occur in its independent software
capture copy. It does not replace callees, skip caller branches or synthesize
kernel completions. The private receipt retains exact source/export hashes,
statuses, query/padding and release observations, and the SEH limitation.
