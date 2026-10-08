# Controller input LLE: active serial family

Tracked in `beads-xbox.1.104`, after calendar slice `.103`. The input family
remains **open**. This document records an actual dependency check, not a
qualified LLE input implementation.

## Current checkpoint

The original-input build now delivers a real OHCI/PIC interrupt through the
original kernel to Blinx's original ISR, then reaches its original USB DPC.
The next explicit stop is untranslated `CLI` at `0x00188a9a`. Reports, controller
enumeration and LLE Start/menu/reentry remain unqualified. The native-input
attract milestone remains the production path. See the interrupt checkpoint
below for evidence and the next bounded step.

## Initial boundary (retained history)

The native input path has real host reports, original ABI layouts, handle and
connection lifetimes, and demonstrated Blinx interruption/menu return. The
original eight XAPI bodies are retained by `tools/native_input_binding.py` and
selected with `PILOT_INPUT=ORIGINAL`. Retaining those bodies does not establish
a working LLE route.

The original-input build was exercised against the actual NTSC title after
calendar qualification. It reaches `XInitDevices`, performs real pool allocations
and **stops at ordinal 65, `IoCreateDevice`, before USB initialization**. No
substitute completion or empty device was added. Private evidence:

- `build/blinx/input-original-boundary-20261008/run.aot-private.json`
- `build/blinx/run-input-original-boundary-20261008.log`
- 5.016 seconds, exit 2 from the explicit missing-service guard.
- Executable SHA-256:
  `d054c9779046fb6990c998df326d3a7f9e65e674d768d5c6a7436544337c8569`.

The probe used the calendar-LLE build directory with only `PILOT_INPUT` changed
to `ORIGINAL`. Afterwards it was restored to `NATIVE` and rebuilt. The exact
qualified calendar executable is also preserved locally at
`build/blinx/calendar-qualified-executable/xlle_native_title.exe`; its identity
is recorded in [CALENDAR-LLE.md](CALENDAR-LLE.md).

## Reuse and missing integration

| Part | Existing implementation/reference | Remaining work |
| --- | --- | --- |
| Original XAPI software | Hash-bound owned XDK4831 bodies; all eight retained | Execute initialization and driver dependencies instead of treating the build flag as proof |
| USB hardware and Controller S | Maintained `src/machine/ohci.cpp/.hpp`, register/TD/done-head/IRQ/XID tests | Connect real original-driver traffic, guest DMA and interrupt delivery to this native-title route |
| Kernel device/driver objects | Original owned kernel and existing full-system LLE; Cxbx object lifecycle as a behavioral reference | Implement a real supported device-object lifecycle and driver dispatch boundary; qualify layout, ownership, errors and destruction |
| Native host mapping | Existing attributed sp00nznet mapping and local XAPI adapter; Phobos/NoRain design references | Keep compatible reports, state transitions, packet semantics and lifetime when compared with the functioning LLE route |

Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`
`src/kernel/kernel_io.c` explicitly labels its device/IRP services as stubs and
constructs an `XBOX_FAKE_DEVICE`. That implementation is rejected for this
driver path. Its working games bypass these drivers through whole XDK HLE;
that does not provide the missing low-level device behavior here.

Cxbx `EmuKrnlIo.cpp` identifies ReactOS provenance. It may inform the behavioral
questions; no implementation is copied into the intended non-GPL product.
Prefer the owner's original kernel and the project's existing OHCI/XID model
for executable evidence. Existing synthetic-hub fixtures are explicitly test
plumbing, not captured retail-controller identity.

## Device-object dependency implemented - 2026-10-08

`io_device_lle.c` now executes the original kernel's `IoCreateDevice` and
`IoDeleteDevice`, including the real allocator, object manager, name insertion,
handle closure, queue initialization and destruction. No original callee is
replaced. This maintained implementation is selected with `PILOT_IO_DEVICE=LLE`
and connected to the actual title's kernel imports. It is a bounded component
floor; the complete input family remains open.

The original kernel initializes a 0x48-byte device body, rounds the extension
allocation to eight bytes, preserves the requested unrounded `Size`, and leaves
extension bytes zero. Queue links and the storage-device event point into the
real allocation. Cxbx's extra extension back-pointer is not the original
behavior. The actual original XDK caller writes that pointer itself.

Caller buffers are shared with the interpreter. Device allocation pages are
published to native guest memory and reconciled at the synchronous service
boundary, so caller-written extensions/reference counts remain visible to the
original kernel. Native pool addresses move to the disjoint E0000000 range in
this build; original device objects retain their D0000000 namespace. This is
ordinary component memory transport, not live HLE/LLE switching or duplicate
execution. Original objects and native file/event handles are not yet a unified
I/O-manager namespace.

### Bootstrap and evidence

The late `aot4-ram.bin` capture was rejected for title initialization: it already
contained memory-unit names and correctly returned a name-collision error.
No names were removed and no success result was substituted. An earlier state
was captured by running the existing full-system LLE from the owned boot images,
with the real locked-HDD path, copy-on-write storage and networking disconnected.
At 18,000,223 retired instructions the kernel allocator and namespace are
initialized, IRQL is zero, and the title's memory-unit names are absent.

- Capture: `build/blinx/io-boot-18m.ram.bin`, SHA-256
  `31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`.
- Capturing executable: `../xboxlle/build-release/xlle.exe`, SHA-256
  `ee1d72f9ef83276d5f74fddab8d3b6a4fbdfec0914fe03cf6026b4edf3ee656c`.
  This identifies the existing binary; a matching source revision is not asserted.
- Capture log: `build/blinx/io-boot-capture.log`; page directory physical F000.
- Original-code reference: `tools/native_io_device_reference.py`, private output
  `build/blinx/io-device-fresh-reference.aot-private.json`.
- Integrated checks: `io_device_contract_test.inc` covers zero/nonzero
  extensions, flags, queue/event layout, duplicate-name and missing-parent
  errors, caller writes across calls, an output pointer inside a published
  extension, pending deletion with a retained reference,
  128 deletion/name-reuse cycles and 32 simultaneous device objects. The pending
  case clears the caller-owned reference and calls deletion again; it does not
  qualify every driver reference-release routine.
- Guard checks prove unsupported extension sizes and unresolved driver delete
  callbacks terminate explicitly. Original code is never replaced by a callback
  success result. The reference and backend share Unicorn; their agreement is
  not an independent CPU-engine qualification.

The device build requires the existing pinned calendar LLE engine configuration
and its notices. Configure it in a separate build or select it in the private
calendar build:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' -S pilots/native-title -B build/blinx/host-calendar-lle -DPILOT_INPUT=ORIGINAL -DPILOT_IO_DEVICE=LLE -DPILOT_IO_DEVICE_SNAPSHOT=F:/Projects/xboxlle/wt-native-title-pilot/build/blinx/io-boot-18m.ram.bin
& 'C:/Program Files/CMake/bin/cmake.exe' --build build/blinx/host-calendar-lle --config Release
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/blinx/host-calendar-lle -C Release -R '^pilot_io_device_lle' --output-on-failure
$env:PYTHONPATH='F:/Projects/xboxlle/wt-native-title-pilot/build/reference-python'
py -3 tools/native_io_device_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output build/blinx/io-device-fresh-reference.aot-private.json
```

The ordinary pilot XBE/EEPROM/media/state command now executes this backend for
real title calls. The capture is checked at configuration and runtime, remains
private, and is not installed or committed. The implementation does not boot
the kernel anew for each launch. Its supported scope is the reviewed initialized
state, device extensions up to 368 bytes, and destruction without a driver
delete callback. Unmodelled port/MMIO/interrupt execution stops. Larger
allocations, arbitrary physical aliases and coupled driver/interrupt execution
need separate qualification.

### Actual original-input progress

The fresh capture creates all three requested named memory-unit device objects
with success, real extensions and real queue/event state. The next unresolved
entry was an original XPP initializer at 00183045. Its complete 104-byte body
through `ret 4` is now translated with pinned Phobos `63f18f0`, without a native
substitute or changed return value. `tools/native_function_extent.py --new-entry`
retains hash-bound discovery evidence, rejects overlapping bodies and replays
the entry during full regeneration. Revision addresses stay in private binding
records, not shared runtime services.

Final original-input evidence:
`build/blinx/input-device-final-original-20261008/run.aot-private.json` and
`build/blinx/run-input-device-final-original-20261008.log`.
The exact executable is preserved at
`build/blinx/input-device-qualified-executable/xlle_native_title.exe`.
Its SHA-256 is
`149eed1f4b3a13c9565da142653ecd094c531b1e423d24b2b713f5d32b14cc41`.
It runs 5.015 seconds, creates the three objects and an 8192-byte contiguous
buffer, then stops explicitly at **ordinal 175, `MmLockUnlockBufferPages`**.
No USB transfer or input delivery is claimed from this run.

### Working-attract regression

After restoring `PILOT_INPUT=NATIVE` (calendar and device LLE remain selected),
all 40 native CTests passed in 17.15 seconds. An earlier full run had one
transient failure at the existing exact-10ms event-wait timing assertion; five
focused repeats and the final combined run passed. That observation remains
recorded in `.104` for the scheduling family; no timing tolerance was silently
relaxed. After the title runs, review added the page-reuse case and fixed output
slots inside published extensions; the focused device/guard batch passes both
cases. The title uses its native stack output slot, covered by the same batch.
Nine extent/entry recovery tests pass, including rejection of
overlapping entries and safe retirement of standalone units on full regeneration.

Actual regression: `build/blinx/input-device-native-regression-20261008/`,
190.187 seconds, 19 captures, no terminal guards. Captures 9/12/16 show Demo play,
the title menu after the replayed Start interruption, and Demo play reentry.
Executable SHA-256:
`11687dccf15d5fd93dda69f9c98a5b2e2a157ebd565a550385c2f3cf7c67e32e`.
The harness terminated its child at the requested deadline (exit 1), not a crash.
This checks preserved attract behavior and native-input interruption, not LLE
input delivery, a new endurance sign-off or original-speed qualification.

## Page ownership and OHCI transport checkpoint - 2026-10-08

`PILOT_USB=LLE` (default `OFF`) requires the device LLE configuration. It uses
original contiguous allocation/release, page locking, physical translation,
protection/persistence, interrupt vector lookup and interrupt-object
initialization. Original routines and callees remain unchanged. Interrupt
connection/delivery is not implied by object initialization.

Original execution corrected the planning assumption: buffer locking skips the
direct physical range but increments PFN lock counts for ordinary mappings.
The reference exercises both, including boundary crossings and nested lock/unlock.
Phobos's unconditional no-op and fake interrupt connection are rejected.
Actual DMA storage lifetime remains required separately from page-lock counts.

Qualification caught a real integration error: the native allocator's address
was absent from the captured kernel PTEs, so original physical translation
returned zero. The USB LLE build now gives allocation and translation the same
original-kernel owner. Actual pages are published to the native caller; caller
and DMA writes are reconciled at service boundaries. Release removes real PTEs
and native mappings. The ordinary accelerated build retains its native allocator.

`usb_lle.cpp` directly reuses our maintained `src/machine/ohci.cpp` and XID model.
Real DMA uses owned storage, retained until controller reset discards schedules.
Unowned DMA and premature freeing stop explicitly. No donor USB body is copied.

Generated C still accesses guest memory directly. `mmio_fault.c` decodes only
actual x64 dword MOV instructions addressing the OHCI window, verifies address
and direction, performs one hardware-model transaction, and preserves flags and
unrelated registers. It neither maps dummy register RAM nor skips unsupported
instructions. Other instructions/access sizes stop. This slower transport is
confined to the selected LLE build; native input does not pay its overhead.

Qualification:

- `tools/native_input_dependencies_reference.py` executes original allocation,
  translation, pinning and interrupt setup. Private observations are in
  `build/blinx/input-dependencies-reference.aot-private.json`.
- `pilot_usb_lle` checks compiled MMIO and a real GET_DESCRIPTOR transfer:
  setup/ED/TD reads, descriptor/TD/done-head writes and reset/release. This is
  transport evidence, not input-report or CPU interrupt delivery.
- DMA guards reject unowned access/premature release. Original physical-memory
  checks cover alignment/bounds, translation, protection, persistence and release,
  without assuming native allocator placement or zeroed uninitialized storage.
- Decoder checks cover extended registers, SIB/displacements, immediates,
  RIP-relative instruction length, flags and rejection without device access.
- All 43 CTests pass in the original-input/USB LLE build (18.27 seconds).
  The native-input/USB-OFF configuration also passes its full 40 tests (17.07s).
- Actual probe `build/blinx/input-usb-final-20261008/` reaches the shared OHCI
  model and stops at **ordinal 129, `KeRaiseIrqlToDpcLevel`**. Its executable is
  preserved in `build/blinx/input-usb-qualified-executable/`. This is startup
  progress beyond ordinal 175, not an attract run with LLE input.
  Probe duration: 5.000s, explicit guard exit 2; executable SHA-256
  `9b8feaef40aa60371926c7030a69511b7fb2662b7ea883378554ede606540251`.

Configure `-DPILOT_INPUT=ORIGINAL -DPILOT_USB=LLE` with the existing calendar and
device options. `-DPILOT_INPUT=NATIVE -DPILOT_USB=OFF` retains accelerated input
and allocation. The Python reference and component share Unicorn; original code
provides independent software expectations, not an independent CPU engine.

Limits: DMA requires original contiguous allocations; arbitrary native VM/pool
aliases and read-only DMA destinations remain unqualified. The captured allocator
is not a complete shared MMU for all native services. Device time advances on
register access here; independent ticking, IRQ delivery and original XID reports
remain unfinished. Unsupported driver callbacks still stop explicitly.

## Interrupt/clock checkpoint - 2026-10-08

The USB LLE build now executes original connect/disconnect, current/raise/lower
IRQL and stall services (98/100/103/129/151/160/161), plus the original ordered
shutdown-notification registration (47). No callee is replaced. Registration
qualification establishes insertion/order/removal; it does not claim a working
console shutdown or execution of its registered shutdown callbacks.

The existing `DualPic` joins OHCI. Its bounded bootstrap uses the qualified
kernel's mask/ELCR shadows and Xbox vector/cascade configuration; it is not a
capture of every boot peripheral. USB is the only connected interrupt source.
Actual original port instructions update the PIC. A real asserted vector enters
the connected original interrupt thunk, which owns IRQL deferral, pending-vector
replay through `INT`, EOI and `IRET`. The registered ISR's existing generated body
runs through a generic native ABI bridge; no title ISR address is hardcoded in
the shared runtime. Nested original services preserve the suspended CPU context
and use a separate stack. Other driver callbacks still stop.

Host clock ticks request guest checkpoints, or lease the otherwise idle guest
CPU. Device DMA and ISR execution never overlap another guest CPU. QPC-derived
milliseconds advance OHCI even when the guest is blocked. Ordinary/DPC preemption
waits below IRQL2; hardware IRQ25 can interrupt it. The original kernel decides
whether a hardware request must be deferred. This is bounded USB delivery, not
complete platform interrupt, CLI/STI, timer, nested-IRQ or cycle-timing fidelity.

The test batch exposed and corrected two integration errors:

- The DPC worker cleared the inserted flag before acquiring the guest CPU.
  Dequeue now happens after acquisition, preserving duplicate rejection while
  IRQL blocks execution. This correction also applies to the native build.
- Sharing the interpreter's ABI stack with native callbacks initially charged
  host bridge storage against Xbox RAM. Explicit bridge/clock ranges outside
  guest allocators now retain their existing host-overhead classification.
  Real title/physical allocations still consume the modeled RAM budget.

Qualification:

- `tools/native_input_interrupt_reference.py` executes original connect,
  duplicate rejection, high-IRQL deferral, lowering/software-vector replay,
  ISR fixture, EOI/IRET, disconnect and shutdown-registration order/removal.
  Private observations: `build/blinx/input-interrupt-reference.aot-private.json`.
  The reference records PIC writes and reflects real mask-register writes; the
  separate C fixture supplies the actual OHCI/PIC device model and native bridge.
- `pilot_usb_irq` exercises real port-change assertion, hardware acknowledgement,
  both direct and deferred ISR entry, a nested original service, original EOI,
  deferred native DPC, duplicate rejection and disconnect masking.
- `pilot_usb_irq_clock` delivers ISR/DPC work while every guest thread is blocked,
  without a caller performing further register reads or service calls.
- All **45/45 CTests pass** in the original-input/USB LLE configuration (17.46s).
  Retained logs include the initial accounting failures and corrected full pass.
- Actual final probe `build/blinx/input-irq-final-20261008/`, **5.016s**, explicit
  exit2: `[USB LLE ISR] call=1 entry=001889d4 irql=25`, original DPC `001890a2`,
  then `CLI` guard at `00188a9a`. Executable preserved in
  `build/blinx/input-irq-qualified-executable/`; SHA-256
  `2ad6c817f9535f28d29f24c2b70b20ca5a1374df9b4dc179f9cb00d66d5114ac`.

The Python and native bridges share Unicorn: original software expectations are
independent of the native scheduler/ABI adapter, not an independent CPU engine.
Stall execution retains original calibrated-loop code within the instruction
budget; no cycle-accurate wall-time guarantee is claimed. Arbitrary pool/VM DMA
aliases, full USB enumeration/reports and input interruption are still open.

## Next bounded step

Qualify and implement interrupt-flag operations reached by the original USB DPC
(`CLI`/`STI`, and flags preservation if subsequently exercised), including their
CPU eligibility and interrupt-shadow consequences. Audit the adjacent original
timer/device accesses as a coherent driver batch. Reuse original CPU/hardware
behavior and donor lowering where valid; never discard the instruction or
invent its data. Then validate actual original-input progress again.

Closure still requires matching HLE/LLE input effects and actual Blinx Start
interruption, menu return and attract reentry with LLE input. This transport
checkpoint does not close the family. Bundle related checks and retain one
actual title validation per coherent dependency batch.
