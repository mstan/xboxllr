# Controller input LLE: active serial family

Tracked in `beads-xbox.1.104`, after calendar slice `.103`. The input family
remains **open**. This document records an actual dependency check, not a
qualified LLE input implementation.

## What runs today

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

## Next bounded step

Qualify the reached page-pinning/DMA ownership boundary against the original
kernel and existing MMU/physical-memory floor. Preserve real page lifetime and
prevent releasing locked storage; do not turn the call into a no-op. Then wire
the reached original USB-driver MMIO/DMA/IRQ traffic to the existing OHCI/XID
implementation. The missing initializer is already recovered; avoid repeating
discovery. The final gate still requires matching HLE/LLE input effects and
actual Blinx interruption, menu return and attract reentry with LLE input.

Bundle page-pinning, DMA ownership and transfer checks where they share setup,
as requested by the owner. Keep separate observed outcomes inside that batch,
then run the title after a coherent dependency chain is implemented.
