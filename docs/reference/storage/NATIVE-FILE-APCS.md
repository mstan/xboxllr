# Native file completion APCs

Owner: beads-xbox.1.141. Native file reads/writes keep their real asynchronous
host operation, IOSB and completion event. An optional APC retains the
requesting native thread's identity and a private execution-handle reference.
The completion worker publishes data/status before queuing a host wake. It
never executes guest code. A qualified alertable native wait releases the guest
CPU permit, observes the host wake, reacquires that permit, and then invokes the
generated callback on the requesting thread and its real guest stack.

The callback frame preserves surrounding general registers, x87/MMX/SIMD state,
host floating environment, FS/SEH and kernel-dispatch bookkeeping. A callback
may call services or terminate its own thread. Queue owners retire before guest
execution, so a nonreturning callback cannot leak its duplicated thread handle.
Thread exit cancels queued callbacks; late completion checks the full identity,
so a retired/reused slot cannot receive another thread's callback. Closing a
guest thread handle does not end its independent execution ownership.

Original kernel232, NtUserIoApcDispatcher, converts an I/O completion into the
game's Win32-style callback arguments. Its actual rule is NT_ERROR severity3,
not signed NT_SUCCESS: warning/informational results retain the byte count and
error zero. An error maps through the maintained NTSTATUS/DOS table and reports
zero bytes. The callback receives the original IOSB/OVERLAPPED guest address.
Direct generated I/O APCs instead receive context, IOSB and reserved zero.

Five original232 executions verify success, information, warning, EOF and invalid
parameter arguments, RET12 and a caller canary. No original callee is replaced.
The authored callback is only an observer. Run with private owned inputs:

```powershell
python tools/native_file_apc_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output <private-output.json>
```

Fourteen combined native file/wait/event/mutant/thread tests pass in 0.27 seconds.
The real-file fixture checks both callback forms, nonalertable deferral, exact
once-only delivery, requesting thread/CPU permit, restored FP/SIMD state, queued
exit cancellation, callback-initiated exit and stable handle counts. Existing
64 async close/reopen drains and event/IOSB/data ordering remain covered.

Scope remains explicit: native-scheduled threads and UserMode alertable native
waits only. KernelMode alertable waits, original KEVENT dispatcher APC delivery,
implicit asynchronous offsets and non-generated callback targets remain guarded.
This does not qualify physical Xbox interrupts/APC timing or a full attract
cycle. Actual JSRF proceeds through its real file loading to the DSP effects
image boundary at 2499 entries; that graph needs separate memory-layout work.
