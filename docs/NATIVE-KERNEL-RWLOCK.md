# Native ERWLOCK service

The new `kernel_rwlock.inc` implements exports 12, 13, 18 and 28 using host
synchronization. Include it after scheduler helpers, and dispatch these ordinals
before the generic kernel switch. Each recognized VOID/NTAPI call consumes the
return address and one argument (ESP += 8); integer registers/flags are retained.

The independently authored state machine follows the owned kernel implementation
at 800159A8/800159CC/80015F43/800159FC, checked against the local nxdk ERWLOCK
layout. No emulator implementation is copied or linked. ERWLOCK occupies 52 bytes:
LockCount, WritersWaitingCount, ReadersWaitingCount, ReadersEntryCount at offsets
0/4/8/12; a synchronization event at 16 and semaphore at 32 (limit at 48).
Initialization sets count -1, zero counters and empty dispatcher headers/lists.

Every acquisition increments LockCount. A shared acquirer joins active readers
only if no writer is queued. The last reader grants one writer. An exclusive
release grants the entire waiting reader cohort before another writer. Host
auto-reset events and counting semaphores implement those grants; generic host
SRW fairness is not substituted for the guest algorithm. State publication and
queue/grant decisions share a short host lock. A waiting caller releases that
lock before scheduler_block_begin, performs a real wait, then reacquires guest
CPU ownership with scheduler_block_end before completing the acquisition.

This bounded service owns the object exclusively through these four exports.
The first four guest words remain observable and external mutation is rejected.
Embedded dispatcher objects are opaque host queue transports, not independent
native dispatcher objects: **integration must use
kernel_rwlock_dispatcher_object(va) to reject independent wait/signal/reset calls
on their addresses**, including multi-object waits. Their transient signal and
wait-list fields are not a reconstructed guest scheduler queue. Such external
operations require a shared dispatcher ownership adapter before admission.
Reserved header bytes are not interpreted as hardware provenance.

Exclusive recursion, upgrades, recursive shared acquisition behind a queued
writer, nonowner release, unknown objects, live reinitialization, address wrap,
misalignment and capacity exhaustion are guarded. Shared recursion with no queued
writer follows the original counter path. Tables are bounded to 256 locks and
64 simultaneously recorded owners per lock; inactive owner entries are reused.
Idle reinitialization is supported; there is no invented lock-delete export.
Ownership uses a nonzero monotonic process-lifetime token cached in native TLS,
not the recyclable Windows thread ID. Token exhaustion guards before reuse.
An exiting thread's unreleased lock remains owned by its old token: a new
thread lifetime cannot inherit it even if the OS recycles the same thread ID.
Both MSVC and GNU C TLS declarations are supported. There is no automatic
unlock on thread retirement; callers must release their locks before exiting.

The authored Windows fixture checks exact layout/adjacent bytes and stack ABI,
shared recursion, actual writer/reader contention, writer preference, reader
cohort handoff and blocked CPU-token release. Fatal guards run in isolated child
processes, including simulated thread-ID reuse for shared/exclusive owners and
token exhaustion. Idle owner slots remain reusable. GCC C11/O2 with
Wall/Wextra/Werror passes on the current x64 Windows
host. These tests do not establish Xbox scheduling/timing equivalence, cross-host
netplay qualification, or successful dashboard execution. No hardware was probed.
