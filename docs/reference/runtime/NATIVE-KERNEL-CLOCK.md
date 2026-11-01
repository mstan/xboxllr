# Native coherent kernel clock

Tracking: `beads-xbox.1.137`. Build policy `PILOT_KERNEL_CLOCK=NATIVE` explicitly
selects `native-monotonic-v1`; ORIGINAL remains the stable default. Native clock
ownership requires native scheduling and IO/device/USB OFF. No existing audio
reference/device timeline branch is replaced by this module.

## Source contract and distinct rates

Owned flat kernel SHA256
`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`
(version4983) supplies the independently reviewed service facts:

| Export | Original behavior | Native projection |
|---|---|---|
|156 KeTickCount|volatile DWORD; ISR increments once per10000interrupt-time100ns units|floor(elapsed100ns/10000), modulo2^32|
|126 KeQueryPerformanceCounter|64-bit extended PM hardware counter, port8008|floor(elapsed100ns*3375000/10000000)|
|127 KeQueryPerformanceFrequency|EDX:EAX=3375000 (`0x337F98`)|same immutable rate|
|Native RDTSC|separate existing CPU counter policy|floor(elapsed100ns*733333333/10000000)|

126/127 have no arguments and return64bits in EDX:EAX with RET0 cleanup. PM
performance-counter frequency is **not** CPU/TSC frequency. Original ISR code
at80035647 adds0x2710 to interrupt time and at8003568D increments the exported
DWORD. This verifies1ms units without importing a guest hardware IRQ loop.

The native model shares `dispatch_clock_origin` and `dispatch_monotonic` with
the existing timer dispatcher. It uses elapsed time from that one immutable
epoch, with100ns source quantization and checked quotient/remainder scaling.
Out-of-order samples cannot move time backwards. Reads never invent progress;
repeated reads with an unchanged source return unchanged counter/tick values.
DWORD wrap does not reset the underlying64-bit epoch or publisher deadline.
The selected native5838 compatibility policy is independent of the observed4983
source version; this is no claim of5838 binary or silicon timing provenance.

## Integration and publication

Root calls `dispatch_ensure()` before `kernel_clock_initialize(tick_va,epoch)`
and outside the clock owner's locks. The supplied aligned DWORD must already
have mapped native backing (currently a distinct export page+9000). A zero
address permits counter-only ownership without a guest tick export. The model
must be initialized before admitting counter APIs or native TSC execution.

A small independent host thread samples the shared source at1ms boundaries
using an optional high-resolution waitable timer, with normal waitable-timer
fallback. It recomputes elapsed time on wake; it never catch-up-spins or acquires
a guest CPU lease. It publishes only a naturally aligned atomic DWORD into guest
storage. Generated MEM32 reads are already volatile, so an ordinary single guest
thread can busy-poll156 without entering another API, scheduler safe point or
DPC. The timer dispatcher itself can execute a guest DPC without starving this
publisher. No callbacks, guest IRQs, guest timers, APCs or scheduler completions
are fabricated by publication. Counter observations also use the same owner
and may refresh elapsed tick publication without incrementing time artificially.

Initialization creates transport first, starts the publisher, and waits for its
first successful timer arm before marking APIs available. Creation/arming/wait
failures guard; an already-running publisher failure stops rather than silently
freezing156. Public APIs reject before initialization or after shutdown. The
epoch cannot be changed or reinitialized after use.

`kernel_clock_dispatch` recognizes only126/127 and consumes the return address
(ESP+=4), preserving other guest registers. `kernel_clock_tsc_counter` is for
the existing native RDTSC branch only. Existing audio reference/device counter
branches retain their own qualified ownership; selecting this profile does not
certify their cross-clock phase or netplay compatibility.

`kernel_clock_shutdown` signals stop, joins the publisher and only then detaches
its guest address/closes handles. Root must first retire other guest users that
could request counter reads, and call shutdown before freeing guest backing.
Lifecycle serialization makes repeated shutdown safe and prevents close/join
races between shutdown callers. The publisher cannot join itself.

## Deliberately unadmitted services

99 KeDelayExecutionThread includes wait-mode/alertability/APC and thread-state
semantics.238 NtYieldExecution selects actual ready threads and reports
STATUS_NO_YIELD_PERFORMED when none is eligible. Neither is admitted as a host
Sleep/SwitchToThread success substitute. Their existing guards remain until a
reached caller and native scheduler contract justify separate implementation.

## Validation and limits

The authored C fixture passes MSVC C11/O2/W4/WX and GCC C11/O2 with
Wall/Wextra/Werror. It checks exact PM/TSC unit/fraction values,64-bit return
carry, unchanged-source reads, monotonic sampling, DWORD wrap, overflow guards,
ABI cleanup, independent real busy-poll progress with no guest API/safe points,
and no post-join writes. Isolated cases check thread/timer startup failure,
invalid/future epoch/backing, before-init/after-stop API calls and immutable
reinitialization. No speaker output or game process is involved.

Host wake-up granularity may delay publication; elapsed values catch up on the
next observation. This profile models a common native monotonic timeline, not
physical IRQ latency, oscillator phase, tick/read atomic ordering against real
Xbox interrupts, original timer/APC scheduling, or cross-host determinism. Reopen
for a caller requiring those relationships, another timebase owner, explicit
wall-clock changes, alertable delays/yields, or a netplay profile. No successful
dashboard boot/performance claim follows from these clock component tests.
