# Display clock and vblank LLE

Active serial family: `beads-xbox.1.105`, following bounded input `.104`.
**The family remains open.** Eight of nine family gates remain open. The
original-driver component below works; its build-selected native-title route
and complete wait/present integration are still being connected.

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
replaced. Separate component cases cover callback removal and actual PFIFO flip
configuration, software-method swap submission, PCRTC_START and flip-read effects.

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

Next: connect the original ISR/DPC/wait/callback route to the maintained native
title build, feed real native GPU completion through the display boundary,
qualify blocking waits and queue/lifetime edges, and validate actual Blinx on
that selection. Keep native rendering/audio/input selections independent.
Do not close this family based only on the component fixture or HLE regression.
