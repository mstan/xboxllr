# Native title reference pilot

`beads-xbox.1.61`, 2026-10-06. This standalone experiment uses Phobos
`63f18f0334fbd8c00313c4bb565accf00627a4fd` discovery, lifting and CPU helper header.
It is outside the normal xboxlle build. **The generated executable is a private
GPL-dependent reference artifact, not the intended non-GPL shipping product.**
See [license scope](../../LICENSE-SCOPE.md). There is no install/package target.

## Observed result

The original NTSC Blinx XDK 4831 and JSRF XDK 4134 both execute their own XBE
entries, create real host-backed title threads, and run early XAPI startup.
Both stop explicitly at kernel ordinal 149, `KeSetTimer`. Blinx reaches 10
function entries on its title thread; JSRF reaches 9. These counts are startup
diagnostics, not game coverage or performance measurements. Neither has reached
CRT heap initialization, graphics, movies, a menu or a level in this pilot.

The native implementations reached so far cover thread creation, thread-handle
closure, recursive critical sections, DPC/timer structure initialization,
EEPROM user-settings queries and NTSTATUS-to-DOS-error conversion. Timer
initialization stores the object; **arming a timer stops until actual timed
delivery is implemented**. No callbacks, draws or completions are discarded.

Four contract tests pass: concurrent threads with separate registers/TLS and
recursive exclusion, handle closure/errors and EEPROM buffer/checksum behavior;
plus explicit instruction, indirect-call and kernel-call failure paths. Passing
the other title's XBE to the Blinx artifact is rejected before guest execution.
The main LLE build's runtime source and build selection are untouched.

## Reuse and scope

- Phobos supplies the actual x86 discovery/lowering and register ABI. Both
  titles used their own entries and discoveries, with no donor address seeds.
  Each generated set had 14 unresolved direct targets. Their success-return
  bodies are replaced by terminal unsupported faults. Unknown indirect calls,
  unsupported instructions and missing services also stop.
- Phobos's host-lock mapping and kernel bridge signatures inform the adapter.
  Its kernel/device runtime is not linked. Cxbx `585c49a` supplies Xbox
  `RTL_CRITICAL_SECTION`, `KDPC`, `KTIMER` and EEPROM field contracts. For example,
  the Xbox critical section is 28 bytes; the timer header size is ten DWORDs.
  Host Windows structs are not written over guest structs.
- Only preload XBE sections are committed, with payload copy and zero-fill;
  Blinx's 42 deferred sections and JSRF's two are not loaded ahead of time.
  Shared boundary pages can already be committed. Deferred section lifecycle
  remains unsupported and is not claimed complete.
- Each created thread receives its own stack, TLS template/zero-fill, KPCR,
  thread identity and CPU register state. Diagnostics are thread-local too.
  The bootstrap may close its thread handle and return while the title runs.
- The exploratory launch environment reports retail kernel 1.0.5838.1 and
  NV2A A1/MCPX B1 identity. This does not certify every export of that kernel.
  User settings come from the supplied 256-byte EEPROM with checksum handling.
  Encrypted/factory queries and other data exports remain unsupported.
- Host allocations are bounded by the pilot's tables, but retail physical RAM
  accounting, general virtual allocation, scheduling/IRQL and exceptions are
  not yet qualified. Donor double-backed x87 and other CPU limits remain.
  PUSHFD/POPFD stop because arithmetic flags are not exported/imported by the
  current donor helpers. This is not a faithful replacement for the CPU floor.
- Compilation disables host optimization for short bring-up cycles. No FPS,
  full AOT coverage, or D3D8 HLE performance claim follows from this result.

The next shared execution dependency is real timer/DPC delivery. Use existing
emulator behavior and tests to define it, keeping GPL implementations external.
The inspected MIT sp00nznet code is a candidate shipping implementation source
after file provenance checks. D3D8 remains the performance target; first-scene
bindings still need Blinx's local indexed draws and JSRF's older GetBackBuffer
out-pointer ABI. A complete reference machine is not a prerequisite to those
HLE implementations, and dashboard speed does not gate title work.

## Reproduce privately

From the repository root, using your pinned donor checkout and owned inputs:

```powershell
py -3 tools/native_pilot.py --donor <phobos-checkout> --xbe <default.xbe> --output <repo>/build/blinx --data-sections 'MDL*,MAP[0-9]*'
cmake -S pilots/native-title -B build/blinx/host -G 'Visual Studio 17 2022' -A x64 -DPILOT_GENERATED=<repo>/build/blinx/gen
cmake --build build/blinx/host --config Release --parallel 4
ctest --test-dir build/blinx/host -C Release --output-on-failure
build/blinx/host/Release/xlle_native_title.exe <default.xbe> <eeprom.bin>
```

Use a separate `build/jsrf` directory and omit `--data-sections` for JSRF.
Generated code, game bytes and logs stay ignored under `build/`. Each executable
checks the complete XBE SHA-256 before loading. The output directory also pins
its image identity so two `default.xbe` inputs cannot silently share discovery.
The data-section exclusions are an explicit Blinx analysis hypothesis, not a
runtime patch or a universal rule. Unknown transfers remain visible failures.

Local evidence: `build/blinx/run-final.log`, `build/jsrf/run-final.log`, the final
build/test logs and each `generation.aot-private.json`. No source-share writes
or game patches were needed. This pilot never executes the donor's fake DVD
authentication or unconditional GPU acknowledgement paths.
