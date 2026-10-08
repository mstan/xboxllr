# Display clock and vblank LLE

Active serial family: `beads-xbox.1.105`, following bounded input `.104`.
**The family remains open.** Eight of nine family gates remain open. The
original-driver component and build-selected native-title route below work.
Full attract regression and integrated queue/lifetime qualification remain open.

## Executable component and findings

`tools/native_vblank_reference.py` executes the owned NTSC title's original
miniport vblank and software-method swap handlers with original kernel event,
interrupt and DPC services. `tools/nv2a_reference.cpp` exposes the maintained
`Nv2a` and `DualPic` through a private C ABI. It contains transport, not a second
GPU implementation. Unknown device accesses stop the qualification.

The fixture executes original event/DPC/interrupt initialization and connection,
then asserts real NV2A IRQ3. The modeled PIC acknowledges it; the original kernel
gate calls the original ISR, which queues its original DPC. That DPC acknowledges
PCRTC, signals the original event and invokes an authored recording callback.
Original interrupt return restores stack, flags and IRQL. No original callee is
replaced. Component cases cover callback removal, PFIFO flip configuration,
actual NOP packet/software-method IRQ submission, PCRTC_START and flip-read
effects. Three independently authored BGRA32 surfaces in original allocated RAM
qualify scanout pixels and extended VGA pitch bits. A stalled FIFO leaves its
pending software method incomplete while original display events/callbacks
continue through IRQ3 and the original ISR/DPC.

The caller-visible callback is cdecl with three DWORDs: vblank count, completed
swap count and flags. Actual original observations establish:

| Sequence | Original callback flags |
| --- | --- |
| No submitted swap | 0 |
| Paced flip retires on this vblank | 1 (`SWAP_DONE`) |
| Next expected swap is missed, including after the prior queue has drained | 2 (`SWAP_MISSED`); its expected marker advances |
| Immediate flip retires before the next vblank | 0 on that next vblank; completed swap count has advanced |

The HLE clock previously used only currently pending presents for missed flags
and carried immediate retirement into the next vblank's done flags. Both are
corrected, with expected values supplied by original execution. Native GPU
completion/queue timing still needs the later integrated comparison; this small
correction does not certify all present scheduling behavior.

The fixture supplies its encoder field pin as an external component input.
Its read is stable and does not advance time. The existing full-system PM model,
like the inspected xemu/Cxbx implementations, toggles that pin per read; that is
not hardware timing evidence. Authentic field phase/mode selection remains an
explicit question. No whole-console PM or dashboard change is included here.

## Reproduce and evidence

Build the private DLL in an existing full-system `XLLE_RENDERER=LLE` build:

```powershell
cmake --build build/visibility-lle --target xlle_nv2a_reference
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_vblank_reference.py --xbe <owned-NTSC-default.xbe> --snapshot <owned-boot-RAM.bin> --library build/visibility-lle/libxlle_nv2a_reference.dll --output <private-observations.json>
```

The script validates XBE SHA-256
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`
and initialized kernel capture SHA-256
`31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`.
It relocates imports to actual original exports. Original addresses remain
within this revision-qualified tool, not a generic service implementation.

Private evidence: `build/blinx/display-vblank-reference.aot-private.json`.
Native qualification: **40/40 tests (20.05s)** and actual
`display-flags-attract-20261008`, **190.188s**, 19 captures, no guards.
Inspected captures 8/12/17 show Demo play, title return after replayed Start,
and Demo play reentry. The harness terminated at its deadline (exit 1).
Executable SHA-256:
`4587c252eb3691416d39a7017d84b5d28ffab84c94f4422efe49caa93e52222c`.
This is a behavior regression, not a new cadence or endurance claim.

## Reference provenance and remaining acceptance

Original owned XDK4831/kernel execution provides software-contract evidence.
Our maintained NV2A/PIC provide the executable device component; the CPU engine
is Unicorn, shared with other floor fixtures. They are not independent hardware
or CPU oracles. Pinned xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/nv2a/pcrtc.c`, `nv2a.c` and `hw/xbox/acpi_xbox.c` were inspected for
register/IRQ/GPIO behavior. No xemu runtime differential or new hardware capture
is claimed, and no new GPL/LGPL implementation body was copied.
The existing private reference target inherits the repository's dependency
scope and is excluded from default builds and installation.

## Selectable title route

`PILOT_DISPLAY=LLE` retains the native renderer but selects the original
miniport IRQ3/ISR/DPC, vblank callback and complete original wait/registration
bodies. Actual D3D11 readback waits for rendered pixels; three original-kernel
contiguous allocations provide owned BGRA32 scanout storage. Real PFIFO NOP
packets enter the original driver's swap queue. Its completion count retires
presents; no host timer or completed-value assignment substitutes for a flip.
The host window still uses native presentation. Diagnostic `scanout-*.ppm`
captures come independently through maintained NV2A's configured PCRTC/VGA
scanout reader. This is the display transport floor, not the later LLE draw
and shader family.

The existing original-kernel interrupt bridge and PIC are shared with USB;
the DLL's private component PIC is inactive in this title configuration.
Native or original controller input can be selected separately. The display
build currently requires `PILOT_USB=LLE` for this bus/interrupt floor. DMA is
restricted to actual owned display allocations, retained until device shutdown.
One guest CPU consumes staging pixels, submits packets and executes original
driver work. No parallel HLE display clock delivers duplicate callbacks.

Configure an existing private pilot build with:

```powershell
cmake -S pilots/native-title -B <pilot-build> -DPILOT_DISPLAY=LLE -DPILOT_USB=LLE -DPILOT_DISPLAY_LIBRARY=<absolute-libxlle_nv2a_reference.dll> -DPILOT_DISPLAY_DEPENDENCIES=<reference-DLL-runtime-directory>
```

`native_display_binding.py` generates hash-bound original entry/layout metadata
alongside the D3D binding. The default remains `PILOT_DISPLAY=NATIVE`.
The DLL is the existing full maintained NV2A implementation, not a simplified
replacement. Byte/dword MMIO retains transaction width; unknown accesses stop.
The exercised same-geometry Reset changes between interval-one and immediate
presentation, drains real pending flips, and sends the original buffer-setup
helper's independently qualified flip-index packet. Geometry/scanout-mode
changes remain an explicit unqualified guard. Encoder field parity
advances with the display epoch and is stable on repeated reads; authentic
field phase/polarity remains outside the bounded claim.

The first title integration exposed native data exports occupying FD000000,
masking NV2A PMC_ENABLE reads with ordinary RAM. They now live outside hardware
apertures and allocation pools at E7000000; display creation rejects any remaining aperture collision.
After that fix, actual original waits return and original callbacks/flips run.
The 210-second `display-lle-attract-20261008` capture has no terminal guards,
but remains in the animated title screen: **it is not an attract pass**.
Nine matching native/scanout frame pairs have identical RGB file hashes.
Its interaction with original input is under investigation before acceptance.
The combined native/USB component suite passes **46/46 (19.90s)**; the display
floor itself is exercised by the original-driver fixture and actual title run,
not by relabeling native display unit tests as LLE tests.

With native input selected independently, `display-lle-reset-20261008` runs
**210.172s**, with 21 native and 21 actual modeled scanout captures and no guards.
Inspected native captures 8/10/16 show Demo play, title return after Start,
and Demo play reentry. Same-geometry interval resets execute in both directions.
Executable SHA-256:
`d966dccdda4b59ec31cb4a86ce541d07e700eb6436d6e64f7be5c45722c33b54`.
Reference DLL SHA-256:
`1b5f1605e95b78288de460fdac9f87d7a0a588a2c2948b560d20800fd63b101f`.
This proves an actual selectable display route; it is not an LLE performance
or attract endurance claim. Original-input plus original-display selection
has not passed the same regression yet.

`PILOT_DISPLAY_QUALIFICATION=ON` is a separate test build. After actual device
creation it runs an authored API consumer, then exits successfully without
continuing the title. Three complete device lifetimes each execute original
waits, paced and immediate flips, independently expected solid-color scanout
pixels, the actual interval Reset, IRQ disconnect and owned-DMA teardown.
`display-lle-lifetime-20261008` completed all three cycles with exit 0.
Keep this option OFF for production and attract validation.

Remaining: investigate the combined original-input/display sequence regression,
final matching native regression, and game-profile integration.
Do not close the family based only on a component fixture or a running menu.
