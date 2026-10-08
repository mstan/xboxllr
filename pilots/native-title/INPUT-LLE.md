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

## Next bounded step

Establish the reached device-object/driver lifecycle against the original
kernel, implement its real supported behavior, and rerun original XAPI startup.
Then connect the reached USB/IRQ/transfer path to the existing OHCI/XID model.
Record further dependencies from actual execution. Do not fabricate device
creation, enumeration or transfer completion to pass a guard. The final gate
still requires matching HLE/LLE input effects and actual Blinx interruption,
menu return and attract reentry with the LLE input route selected.
