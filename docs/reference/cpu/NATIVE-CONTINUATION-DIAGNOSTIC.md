# Native continuation diagnostic

This is the first execution-state prerequisite tracked by `beads-xbox.1.131`.
It is a diagnostic compiler profile, **not a Blinx save state**. It does not
reduce campaign boot/load times. Existing UDATA copies and original LoadGame
input prefixes remain save-backed reproduction.

`tools/native_pilot.py --diagnostic-continuations` applies
`tools/native_continuation_lowering.py` after the existing pinned Phobos
translation, hardening, thread checkpoints and interrupt lowering. Default
preparation never imports/applies the continuation pass and emits its existing
fast native bodies. The native-title CMake project explicitly refuses artifacts
containing `pilot_continuation_schema.h`; they need a separate diagnostic host.
The existing donor provenance/license requirements still apply to generated
output and its helper header.

The pass consumes the donor's C output and verifies instruction coverage against
the original bytes. It does not decode instructions into a second execution
implementation. The supported diagnostic subset is integer MOV, PUSH/POP,
CMP/TEST, ADD/SUB, INC/DEC, direct CALL, the donor's captured indirect CALL,
local direct JMP, JE/JNE/JL/JGE, RET and NOP. Tail/indirect jumps, unknown
automatic storage, unsupported donor bodies, external direct service calls and
other opcodes fail preparation before this pass changes its files. Runtime
indirect targets must also belong to the generated continuation schema.

Each activation has a guest function address, generated resume token, callee
address and eight fixed scalar fields: unsigned/signed lazy compare operands,
fallback flags, local EBP, and the donor's saved indirect stack/target. Entry and
backedge labels remain the original generated control flow. Call-resume labels
sit after the original guest return-address push and immediately before the
native call. A restored activation jumps there without repeating argument
preparation. Reentering nested functions rebuilds an ordinary native C call
chain from these records; its original RET paths return through that chain.
No live host C stack is copied, unwound for restore, or placed in a file.

The diagnostic runtime is single-context C11, capped at 32 activations. Loading
requires no active native guest frames. Its versioned little-endian wire has
an SHA-256 generated-source/original-byte/lowering-runtime/site-table identity, function/token records and
an FNV-1a corruption checksum. The checksum is not authentication. The complete
schema and parent/callee chain, including saved indirect target identity, are
validated into temporary data before execution records change. Saving is enabled
only inside the runtime's generated-label callback. CPU/RAM/service
state must be separately staged and validated by a host; this runtime cannot
make an arbitrary host's restore transactional. Fault hooks must terminate.
Donor `RECOMP_ABI_CHECK` native-call probes are rejected at compilation: replay
reconstructs suspended caller activations while CPU registers still describe
the deepest callee. Indirect-call profiling counters are diagnostic telemetry,
not part of this fixture's guest-observable state.

Run the bounded test with:

```powershell
py -3 tests/test_native_continuation.py
```

`XLLE_CPU_DONOR` can select an existing checkout of the pinned CPU donor, `CC`
selects a C compiler, and `XLLE_CROSS_CC` selects clang for the freestanding
AArch64 compile check. Missing local donor/compiler dependencies report a skip;
they are never fetched. The optional CTest registration is enabled with
`XLLE_TEST_NATIVE_CONTINUATIONS=ON`. On this Windows machine the test sets normal
priority and logical CPU affinity 8/10/12/15 before launching children when all
four CPUs are allowed. Other Windows process/job masks use up to four allowed
CPUs. Compiler discovery checks existing absolute paths, then available GCC,
Clang or CC executables; an invalid explicit `CC` is an error. Compiler
invocations are sequential. Other hosts retain their normal process settings.

The authored IA-32 fixture goes through the actual pinned donor
`FunctionTranslator` and the framework's `harden_generated` before this pass.
Root calls middle directly; middle calls leaf indirectly with a guest stack
argument. A disposable capture process exits while three activations are live,
after the leaf's compare and before a branch using its lazy flags. A fresh
process loads the data, resumes the nested callee, follows the original returns,
executes a backward loop and observes a final authored service function.
Original uninstrumented, instrumented uninterrupted and restored runs compare
all fixture CPU fields, full guest RAM hash and entry/service trace.

Tests re-sign malformed records to exercise schema/token/chain validation
instead of relying on checksum failure. A rejected load preserves previously
loaded valid execution records and CPU/RAM. Deliberately changing saved lazy
flags takes the original failure branch; changing the parent's local EBP changes
the result. A freestanding AArch64 compile exercises the portable runtime, not
an ARM execution claim. Outputs/evidence stay under the ignored
`build/native-continuation-diagnostic` directory.

Full native title continuation remains unimplemented. This subset does not
serialize architectural EFLAGS, x87/MMX/SSE state, SEH/nonlocal jumps, dynamic
code, multiple guest threads, host waits/locks or services. Future owners are:

- Generator/lowering: complete live-value records, all generated constructs,
  resume identities and frame/call qualification.
- `runtime.c`, `thread_scheduler.inc`, `dispatcher.inc`: CPU/TLS contexts,
  persistent wait records, thread/DPC quiescence, scheduling and clock state.
- VM/object/media owners: mapping/residency, handle identity, pending I/O,
  file offsets and a private storage snapshot/overlay.
- Graphics owners: drain real GPU work, preserve authoritative surfaces and
  resource contents/bindings, and rebuild host D3D objects.
- Audio owners: playback cursors, owned queues, DSP state and rebuilt voices;
  host output queues remain distinct from guest simulation state.
- Input/clock owners: pending input, deterministic replay phase and explicit
  clock/absolute-time restoration policy.

Only after those owners can provide a qualified state blob and deterministic
advance operation can `RbeSnapVTable` store it or recomp-net use it for rollback.
Those libraries own opaque blobs and episode policy; they cannot supply the
missing Xbox serializer or make this diagnostic a title checkpoint.
