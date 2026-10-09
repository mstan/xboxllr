# GPU completion, callbacks and visibility LLE

Active family `beads-xbox.1.109`, after qualified filesystem `.108`. Burndown:
**4/9 complete, 5 open**. Original reference execution and the selectable title
transport now exist. The title selection still stops during device initialization;
it is not yet a working renderer or a qualified title completion route.

## Executed floor

`tools/native_gpu_completion_reference.py` binds original NTSC XDK execution
and the maintained NV2A PFIFO/software rasterizer to the same 64 MiB RAM, with
real MMIO, report DMA, semaphores and traps. Preowned device/pushbuffer/report
storage bounds the fast paths; no allocator or kick result is substituted.
Entry addresses come from title analysis; End's call identifies the kick.
Private device-layout offsets are isolated in this hash-bound probe.

Nine distinct cases pass across three independent device lifetimes:

| Contract | Actual execution |
| --- | --- |
| Original Begin/End/kick/Get | Real triangle rasterization gives 400 visible, 0 occluded, 200 partially occluded samples; empty index reuse gives 0 |
| Pending reports | The actual PGRAPH puller gate holds draw/report work; original Get returns incomplete with untouched outputs. Reenabling it performs actual report DMA |
| Result ownership | Indexed report reuse, repeated Get and optional timestamp outputs; real color/depth writes accompany sample counts |
| Original READ/WRITE packets | InsertCallback and kick raise traps 6/7 with actual routine/context; a later marker cannot execute before acknowledgement |
| Original fence waits | InsertFence submits real semaphore writes 2/4/6. BlockOnFence stays in its original polling loop while the puller is held, returns after DMA, then returns immediately on repeat; stack/nonvolatile registers survive |

PFIFO can prefetch through PUT while PGRAPH is blocked. DMA_GET does not prove
draw completion. The callback cases assert trap/later-method ordering, not CPU
ISR delivery. The query fixture now executes full End/kick, which the earlier
standalone oracle only reviewed. It is not a new hardware or xemu run.

Original kick exposed missing PFB write-buffer control at `0xFD100410`. The
model now publishes CPU memory writes with a host fence before returning
flush-complete. Its CPU/DMA callbacks already commit synchronously to coherent
RAM; there is no deferred write queue to discard. This neither signals draw
completion nor releases a blocked PGRAPH puller. Unqualified controls/sizes stop.
Pinned xemu supplies flush-bit/status behavior; no GPL implementation body is
copied. See THIRD_PARTY_ATTRIBUTION.md.

```powershell
cmake --build build/visibility-lle --target xlle_nv2a_reference test_nv2a --parallel 4
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_gpu_completion_reference.py --xbe <owned-default.xbe> --symbols <default_xdk_symbols.json> --functions <disasm/functions.json> --library build/visibility-lle/libxlle_nv2a_reference.dll --dependencies C:/msys64/mingw64/bin --output build/blinx/gpu-completion-original.aot-private.json
build/visibility-lle/test_nv2a.exe
```

XBE SHA-256:
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`.
Reference library SHA-256:
`9d2d989ec66add3cff8f63614cde5ad2d515dfb2fa287769b2bad84b5657943d`.
The private result retains function hashes, MMIO and all lifetimes. The NV2A
suite passes; SDL-dependent OpenGL cases explicitly skip in this software build.

## Donor decision and remaining work

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` remains the native strategy
reference. Its immediate callback and eventual invented visibility count in
`hle_d3d8.c` are unsuitable. Its optional `nv2a_pb_exec.c` offers decoding ideas
but describes skipped geometry; it cannot establish this correctness floor.
Reuse our maintained NV2A and original command behavior. No donor body was copied.

Finish original device initialization through the selected title route, then
qualify actual callback dispatch and resource ownership. Visibility consumes
real rendered work, so this now has an explicit dependency on the broader
graphics family. The whole original renderer runs once; native query counts
cannot be relabeled as LLE evidence.

Then qualify pending/error/order/reuse/teardown and run Blinx. Preowned fast
paths do not qualify allocation/refill, arbitrary IRQ latency or all resource
busy cases. Existing display-clock timestamp resolution/epoch tolerances remain.
These bounded geometry cases do not establish full-scene raster correctness.

## Whole-renderer integration checkpoint

`PILOT_RENDERER=LLE` keeps the preserved original AOT D3D bodies and connects
their actual MMIO accesses to the maintained NV2A DLL. `gpu_lle.inc` supplies
the shared original allocator/pinned-page DMA bus, PIC IRQ3 connection and
elapsed clock; `mmio_fault.c` decodes actual compiled load/store faults. The
title does not link the native D3D backend in this selection. The independent
contract-test executable can contain both consumers, selecting one per test.
The previously qualified native-rendered display floor is a different build
selection and cannot be combined with this whole original renderer.

The original driver owns lifetime across queued GPU work. DMA validates live
owned contiguous ranges or originally pinned pages at access time; it does not
take USB's controller-reset leases. This alone cannot detect every stale
reference after address reuse: original resource-busy/fence and teardown
qualification remains necessary.

Implemented prerequisite device behavior:

- PBUS command mirror preserves the original bus-master bit write/read. Other
  command controls stop. External PCI arbitration/decode is not qualified by
  this shared-RAM bus.
- PFB CSTATUS reports the actual modeled retail capacity, 64 MiB.
- PTIMER has a programmable integer clock ratio, 56-bit counter, low-counter
  alarm with wrap/crossing detection, pending/enable masks and W1C acknowledgement
  through PMC bit 20. Reads do not advance time. Tests use explicit source edges;
  machine/title clocks supply actual elapsed edges. The fixed reset oscillator
  comes from pinned xemu. PLL changes, global engine reset/enable, reset epoch,
  ratio-change sub-tick phase and title-observed timing remain unqualified.
  Existing visibility-report timestamp approximation remains separately scoped;
  this checkpoint does not qualify its relation to the new timer counter.

Independent timer expectations come from the
[envytools PTIMER description](https://envytools.readthedocs.io/en/latest/hw/bus/ptimer.html).
No implementation body was copied. xemu PBUS/PFB behavior is a source reference,
not a new differential run or hardware measurement.

Validation on the checkpoint build:

- `test_nv2a` passes, including timer fractional carry, stopped/restarted count,
  wrap, masked/later-enabled alarm, crossed deadline, acknowledgement and fresh
  device isolation. SDL/OpenGL cases explicitly skip in this software build.
- `pilot_gpu_lle_transport` passes in 0.12s over three independent devices.
  Actual compiled MMIO submits a PFIFO semaphore into original-kernel storage;
  a held PGRAPH leaves output unchanged, release writes the expected value.
  Read-only, released and out-of-range DMA is rejected. This fixture drains its
  work and has no registered ISR; it does not prove general title teardown.
- All nine original query/callback-trap/fence cases pass against the new DLL.
- Four actual original NTSC Blinx launches expose successive initialization
  boundaries. The latest passes PBUS command, timer interrupt disable and PFB
  capacity, then **stops at the unsupported write `FD001830=0`**, the PCI
  expansion-ROM configuration mirror. No game frame or attract pass is claimed.
  Next handle the original device-initialization control family from independent
  behavior; do not turn unknown writes into success to reach a later address.

Private evidence: `build/blinx/gpu-lle-title-capacity/run.aot-private.json`,
`build/blinx/run-gpu-lle-title-capacity.log`,
`build/blinx/host-gpu-lle/Testing/Temporary/LastTest.log`,
`build/blinx/gpu-lle-nv2a-tests.log`, and the original-reference JSON above.
The runtime logs the loaded DLL hash because the executable alone does not
identify a dynamically loaded device implementation.

Title executable SHA-256:
`fe6f24982e7b1fa6d1566f052b3669e89970b6543fc4316e1e2a0e44194443dd`.
NV2A DLL SHA-256:
`a1e4070f403bcd10d4d282a0730c3a8f3333875124dfa245063111b324de4311`.

The private build `build/blinx/host-gpu-lle` uses the qualified files/USB/memory/
calendar configuration, with `PILOT_RENDERER=LLE`, `PILOT_DISPLAY=NATIVE`
(the separate display adapter is off), `PILOT_GPU_LIBRARY` pointing to the
maintained DLL and `PILOT_GPU_DEPENDENCIES=C:/msys64/mingw64/bin`.

```powershell
cmake --build build/blinx/host-gpu-lle --config Release --target xlle_native_title pilot_contract_test --parallel 4
ctest --test-dir build/blinx/host-gpu-lle -C Release -R '^pilot_gpu_lle_transport$' --output-on-failure
$env:XLLE_PILOT_EXECUTABLE="$PWD/build/blinx/host-gpu-lle/Release/xlle_native_title.exe"
$env:XLLE_PILOT_GUEST_SCHEDULER='1'
$env:XLLE_PILOT_SKIP_STACKS='1'
$env:XLLE_PILOT_VISIBLE='0'
$env:XLLE_PILOT_MEDIA_ROOT="$PWD/build/blinx/media-local"
py -3 build/blinx/run_extended.py gpu-lle-title-next 40
```

The established game repo remains pinned to completed-files `439a45b`; this
unfinished renderer is isolated on `feat/blinx-gpu-completion-lle-20261008`.

## Initialization controls and original GPU reservation

The `74b6277` actual-title checkpoint passes the former PCI-ROM stop, PLL/timer
setup, GPU instance reservation, PRAMIN clearing, inactive overlay configuration,
context-table setup and initial FIFO configuration. It now stops at the real
**GPIO byte write to port `80C0`**. The following original routine uses
`HalReadWritePCISpace` (ordinal 46); GPIO/PCI bus integration is the next coherent
dependency. No LLE frame or attract completion is claimed; the family stays open.

Maintained changes and scope:

- Disabled PCI ROM-address selection and latency configuration retain real
  register state; enabling ROM decode requires a real ROM provider and stops.
- PLL reads support actual byte/word/dword extraction. The pinned xemu boot
  coefficients and nxdk crystal formula yield the existing 233333324-Hz core
  source. Memory-clock zero, CFG1 zero and ROM_VERSION zero belong to the bounded
  xemu-compatible board profile, not new silicon measurements. Changing RAM/NVM
  geometry stops. These assumptions still need independent silicon evidence if
  a title-visible outcome depends on them.
- PMC represents the component's existing ready engines, not a cold-reset mask.
  Pristine PGRAPH disable clears modeled control/interrupt state while preserving
  PRAMIN; resets after command submission or of other engines stop. Context-table
  configuration is maintained; context loading/switching is not yet qualified.
- PVIDEO retains both banks' inactive configuration and supports idle stop/W1C.
  Overlay activation stops until actual overlay pixels/scanout are implemented.
  PCRTC retains the bounded HSYNC address-latch profile; existing display phase
  limits remain. This does not claim general video-mode or overlay support.
- `MmClaimGpuInstanceMemory` executes the original kernel routine. It returns
  the original top/padding, releases actual surplus pages, and handles repeat
  and query calls. Growth/empty claims stop before mutating the original state.
  Revision-specific reservation metadata lives with the hash-bound capture
  configuration. PRAMIN/physical-alias equivalence remains unqualified; the
  current NV2A instance store follows the separate-memory reference model.
- Verified `OUT DX,AL` lowering calls the actual machine port interface without
  changing guest registers. PIC byte writes are tested; unsupported GPIO writes
  stop. Instruction bytes and generated statement both must match, with no
  title-address special case. Generated private manifests advance to version 3.

References: nxdk/pbkit `29638d0b001f179b73c3513489af10ddc2986216` for control
names, initialization order and PLL decoding; the previously pinned xemu
PBUS/PFB/PRAMDAC/PVIDEO behavior; original owned kernel execution for reservation
effects; [envytools PMC documentation](https://envytools.readthedocs.io/en/latest/hw/bus/pmc.html)
for engine reset semantics. xemu's blanket ignored accesses, context WRITE_OUT
placeholder and read-driven GPIO toggling were not adopted. No new hardware or
xemu differential run is asserted.

Validation:

- `test_nv2a` passes with initialization/control cases; software-build OpenGL
  cases explicitly skip. All nine original query/trap/fence cases still pass.
- `tools/native_gpu_instance_reference.py` passes nine query/shrink/repeat cases
  across three fresh kernel contexts, checking original protection `0x204 -> 0`
  on released pages and preservation of the retained window.
- Five integration checks pass in 2.52s: GPU transport (including original
  reservation effects and real PIC writes), original device service/guards,
  memory contract and memory statistics. Eight CPU-lowering tests pass.
- Actual title evidence is `build/blinx/gpu-lle-title-port-boundary/` and
  `build/blinx/run-gpu-lle-title-port-boundary.log`. Exit 2 is the explicit
  unsupported GPIO transaction, before rendering. Earlier iteration logs use
  suffixes `controls`, `instance`, `video-config` and `context-table`.

Executable SHA-256 at `74b6277`:
`31f949897d8d7a7e7d37f6a7482ece46e1732fbc0689332d4ec538728b0b7294`.
NV2A DLL SHA-256 at `74b6277`:
`27dc45af8fa393f44d90ea77c54d772ee2ed2502a936c9a0206f6d89889630b5`.
Private additional evidence: `build/blinx/gpu-instance-original.aot-private.json`,
`build/blinx/gpu-completion-init-original.aot-private.json`,
`build/blinx/gpu-lle-init-nv2a-tests.log`, and the build's CTest last log.

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_gpu_instance_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output build/blinx/gpu-instance-original.aot-private.json
py -3 -m unittest discover -s tests -p test_native_interrupt_lowering.py
ctest --test-dir build/blinx/host-gpu-lle -C Release -R '^pilot_(gpu_lle_transport|io_device_lle|io_device_lle_guards|memory_contract|memory_statistics)$' --output-on-failure
```

## GPIO, original PCI transport and timer epoch checkpoint

Actual Blinx now passes the `80C0` byte write, both original
`HalReadWritePCISpace` calls, and subsequent timer counter initialization.
The current stop is **`FD400080=0`, PGRAPH_DEBUG_0**, during the next original
engine-reset/setup routine (`sub_0013FDF0`). No LLE frame has been produced.
Next work is the coherent PGRAPH initialization/context family, followed by
actual callback and resource-lifetime execution; `.109` stays open.

- `McpxFieldInput` implements a stateful encoder input: control readback,
  raw pin level, polarity, latch selection and W1C latch status. The external
  field clock drives it; repeated reads cannot advance time. Output, alternate
  function and debounce modes stop. Original XDK writes `1` and samples bit 5;
  xemu supplies the Xbox field-pin wiring. AMD's **AMD-766 Peripheral Bus
  Controller Data Sheet, 23167B (March 2001), p82** supplies the independently
  documented compatible control layout. **Applying that layout to MCPX is a
  board-profile inference**, supported by these observations, not a silicon
  measurement. Full control/latch readback correspondence and phase remain a
  hardware-evidence question if observable behavior depends on them. The older
  full-machine `XboxPm` read-driven field/timer shortcuts are not used here and
  are not newly qualified by this work. SCI/SMI and other GPIO pins are outside
  this component's scope.
- The maintained `PciHost` now accepts device-owned config functions. NV2A at
  `01:00.0` shares its PBUS register state with CF8/CFC access; no second config
  store is synchronized after the fact. Identity fields remain read-only.
  Byte/word/dword selection, absent functions and disabled config cycles retain
  their bus behavior. Invalid widths and unimplemented GPU config controls stop.
  The AGP request-depth field is retained and respected by the synchronous
  unified-RAM bus, which has at most one outstanding request. External link,
  transfer-rate and sideband reconfiguration are guarded, not declared modeled.
- Ordinal 46 executes the owned original kernel, including its IN/OUT
  instructions, caller-buffer effects and preserved-register/stack/IF checks.
  The interpreter port adapter reaches the same PCI device as compiled code.
  Configuration callbacks do not tick the GPU or reenter locked kernel/DMA work.
- PTIMER low/high writes update the actual 56-bit counter while retaining the
  other half and source-clock progress. Reserved bits are masked, future ticks
  advance from the new epoch, and alarm equality remains observable. Envytools
  supplies counter/alarm wire facts; Nouveau's `nv04_timer_time` independently
  demonstrates counter programming. Subcycle write/compare timing remains
  outside this bounded model; ignoring counter writes was not adopted.

Validation on the final NV2A DLL:

- Software NV2A suite passes, including GPIO state/guards, PCI mirror coherence,
  read-only identity, absent devices and timer rebase/rollover/alarm cases.
  OpenGL cases explicitly skip in this software build.
- All nine original query/trap/fence cases pass across three device lifetimes:
  `build/blinx/gpu-completion-platform-original.aot-private.json`.
- Five focused GPU/device/memory CTests pass in 2.89s. GPU transport includes
  fifteen actual original PCI calls across three device lifetimes, byte-buffer
  sentinels, real bus readback and the existing held/released semaphore tests.
- The native-renderer title and contract executable also rebuild successfully.
  Eight focused native graphics/device/memory checks pass in 8.99s. This
  checkpoint does not claim a fresh complete native attract run; the preserved
  game milestone remains the previously qualified pinned build.
- Actual title run `build/blinx/gpu-lle-title-platform-timer/` exits 2 at the
  explicit PGRAPH guard after 11.172s. This is initialization progress, not
  rendering or attract qualification. The earlier `gpu-lle-title-platform`
  run established the timer-write dependency after GPIO/PCI succeeded.

Executable SHA-256:
`5911978b4617210426b60dc0f516ca780080d9833b8bd90f1d37a5a70ff2c642`.
NV2A DLL SHA-256:
`847aa2216e5b4d2c3b0d18433d8fb42f4663e9bdd1aabad7e6af09634cc8c7d5`.
Build/test logs use `gpu-lle-platform*`; the reference manifest records the
same final DLL hash. No hardware contact or new xemu differential run occurred.
The game pin remains at completed-files `439a45b`.

## PGRAPH setup, context admission and FIFO state checkpoint

Actual original Blinx passes the former `FD400080` guard, the complete initial
PGRAPH setup routine, FIFO channel save/restore initialization and graphics
object creation. The final 10.000s run stops on the original CPU write to
`80000000`, before rendering. Symbolization identifies `sub_00139D90` at host
RVA `09b355cd`: the original writes a jump to physical page zero, executes
`WBINVD`, then starts the DMA pushbuffer and waits for GET to reach its head.
nxdk `pbkit.c` has the same bootstrap sequence. This is a missing reserved-page
publication/cache-ordering dependency, not evidence of a corrupted allocation
or permission to substitute a successful GET value.
The hash-bound original boot capture independently maps `80000000` to physical
zero with write permission (PDE `03ff0067`, PTE `00000663`). Runtime admission
must still verify the current translation and retain actual shared ownership.

Implemented in maintained device code:

- PGRAPH startup mode readback and pristine subengine reset; separate PFB,
  PGRAPH and RDI `EA` RAM configuration banks; indexed access advances only
  after a valid transfer. Active tiles, compression, other internal-memory
  banks and live reset remain guarded. Accepted pipeline settings select the
  existing coherent software pipeline. Register readback and successful
  startup are **not** full qualification of every undocumented debug bit's
  silicon effect; that remains part of the graphics-family audit. Original
  and nxdk DEBUG7 settings differ, and arbitrary variants are not accepted.
- Context validity/channel matching now controls real command admission.
  A mismatch raises PGRAPH's context interrupt and retains the command; IRQ
  acknowledgement alone cannot execute it. Device disable and FIFO hold retain
  pending work. The existing batching path declines when original context
  management is active so it cannot bypass these checks. Actual context
  READ_IN/WRITE_OUT remains unimplemented and guarded; no xemu placeholder was
  promoted into an implementation.
- Engine status reflects active dispatch, stalls and outstanding draw jobs.
  A prefetched FIFO can coexist with an idle engine; neither DMA_GET nor
  STATUS=0 proves completion of a queued semaphore. This distinction lets the
  original driver disable the puller and wait for idle before changing context.
- CACHE1 parser state, subroutine state, reference counter, DMA pointers and
  engine selection now expose live channel state. Restoring partial parser
  state changes actual subsequent execution. `REF_CNT` waits for older draw
  jobs before publishing its value, also visible through the channel's USER
  register. RAMHT graphics-engine routing is validated. PFIFO semaphore
  acquisition remains inactive and rejects programming/activation; this is
  distinct from the already implemented NV097 semaphore DMA route.
- Native fault diagnostics now record module base/RVA and guest registers,
  allowing the next original instruction to be identified without guessing
  from an absolute ASLR address.

Validation/evidence:

- Software `test_nv2a` passes, including separate RAM banks/reset, invalid
  accesses, context hold/IRQ/retry, correct-channel release, actual semaphore
  DMA, ordered reference publication, partial-parser restore and independent
  channel values with batching on/off. OpenGL cases explicitly skip.
- Nine original query/trap/fence cases pass across three fresh device
  lifetimes; `gpu-completion-pgraph-final-original.aot-private.json` records
  the final loaded device hash. Five GPU/device/memory integration tests pass.
- Native-renderer title and test builds pass, along with eight relevant
  graphics/device/memory checks in 6.20s. No fresh complete native attract run
  is claimed for these reference/control-plane and diagnostic changes.
- The optional historical `test_dispatch_table_guards.py` has four stale
  catalogue assertions (126 expected handlers versus 132 present). Running it
  against unchanged parent `2d17832` reproduces the same four failures; the
  runnable NV2A tests pass. No assertion was loosened to mask the discrepancy.
  Tracked separately in `beads-xbox.1.110`.
- Actual launches: `gpu-lle-title-pgraph-setup` first reached CACHE1_REF;
  `gpu-lle-title-pgraph-fifo` reached the physical-zero bootstrap;
  `gpu-lle-title-object-fault` localized it; final
  `gpu-lle-title-bootstrap-boundary` reproduces it with the final DLL. All
  manifests/logs are under private `build/blinx/`; exit 2 is an explicit
  incomplete route, not an attract pass. `gpu-lle-pgraph-*` logs retain checks.

Final executable SHA-256:
`602adf4dc27f46d3c740d527bb9ace69e042195440f15a909a3c4210d0c1970d`.
Final NV2A DLL SHA-256:
`6e60de973c87c413b4a15af729ceafacf5c9b2855c3d36351a9e3001b6dd7690`.

Next: expose the original reserved bootstrap page through the existing shared
CPU/kernel/DMA backing, verify its original translation and ownership, lower
`WBINVD` to the coherent bus's actual publication/invalidation contract, and
let the real PFIFO read and follow the jump. Then address actual graphics
context loading and exercised draw/callback dependencies. Do not allocate a
different scratch page, skip the write/flush, return a fabricated GET, or close
the GPU family on these initialization results. The burndown stays 4/9 closed;
the game remains on its completed-files pin. No hardware contact, new xemu
runtime comparison or new donor/GPL implementation body is claimed.

## Physical-zero bootstrap and coherent cache publication checkpoint

The original DMA bootstrap now executes. Final actual title run
`build/blinx/gpu-lle-title-real-bootstrap/run.aot-private.json` exits 2 after
11.047s at `FD400910=03590003`: the first reached active render-surface tile
configuration. Before that stop the original driver follows its physical-zero
jump, services its real graphics ISR and allocates 2,457,600-byte and
1,228,800-byte render-surface storage. FIFO GET/PUT both reach `03914000`.
There are still no rendered frames on this original-GPU route. This advances
initialization; it does not close the completion or graphics family.

Implemented contract:

- The console owns physical page zero independently of each GPU lifetime.
  Admission verifies the original writable PDE/PTE mapping of `80000000` to
  physical zero and publishes its actual captured contents through the existing
  shared CPU/kernel/DMA backing. It is not a new allocator result or substitute
  scratch page. GPU recreation retains its bytes; allocation-free calls cannot
  retire it. The original kernel still controls other low physical pages.
- DMA resolves the same published page and checks its current translation,
  bounds and host protection. Access does not expose unrelated captured RAM.
  CPU writes become actual FIFO input; device writes are visible to the CPU.
- Hash-verified original `WBINVD` is lowered to a host publication fence for
  the coherent bus. This bus has no deferred emulated CPU cache contents to
  write back or invalidate. The operation orders prior stores before subsequent
  device access and does not wait for GPU completion. This is a bounded cache
  model, not a claim about cache timing, arbitrary privilege levels or modified
  executable AOT translation. Other opcodes/prefixed variants remain rejected.

Qualification:

- The transport test passes across three GPU lifetimes with an actual FIFO jump
  from physical zero, original `MmGetPhysicalAddress`, shared CPU/device writes,
  protected/out-of-range/released DMA rejection and retained console contents.
  A held graphics engine leaves the semaphore pending until work really runs.
- Four other GPU-build device/memory checks pass in 2.30s; the transport test
  passes in 0.14s. Ten interrupt/cache lowering tests pass, including exact
  opcode verification, preserved following code and rejection of `INVD` and
  prefixed variants. No generated title body was manually patched.
- Both title builds succeed. Eight native-renderer device/memory/graphics
  checks pass in 5.68s. No fresh complete HLE attract run is claimed: the
  preserved game build remains pinned to qualified-files `439a45b`.
- Executable SHA-256:
  `4510a9e07d2817cca90afafdda501fae23bcc9022ed72637c80dd1fe2ea032f9`.
  Device DLL remains
  `6e60de973c87c413b4a15af729ceafacf5c9b2855c3d36351a9e3001b6dd7690`;
  no NV2A device-model source changed in this checkpoint. The previous nine
  original query/trap/fence cases therefore remain evidence for that same DLL.

Next dependency is the **tile memory-client contract**, before further context
and draw qualification. nxdk `pb_assign_tile` programs separate PFB, PGRAPH and
RDI banks with base/limit/pitch and optional compression. The current xemu PFB
model stores those registers and uses linear host rendering; this is not proof
of LLE tiled-memory behavior. Envytools `nvhw/tile.c` and `hwtest/nv10_tile.cc`
provide useful translation/test mechanisms, but its generic IGP enable/bank-bit
programming differs from Xbox pbkit's sequence and cannot be assumed equivalent.
XboxDev's `nv2a-trace/Texture.py` uses the `F0000000` aperture for linear surface
readback; Cxbx calls that aperture tiled/write-combined. Establish the actual
client/address distinction before adopting either representation. A successful
register write alone must not silently enable identity-addressed rendering.
No tile guard was relaxed, new hardware contacted, or GPL implementation body
copied during this investigation. Family count remains 4/9 qualified.
