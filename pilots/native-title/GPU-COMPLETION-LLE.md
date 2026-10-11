# GPU completion, callbacks and visibility LLE

Family `beads-xbox.1.109`, after qualified filesystem `.108`. Burndown:
**4/9 complete, 5 open**. Original reference execution and the selectable title
transport now exist. The title selection still stops during device initialization;
it is not yet a working renderer or a qualified title completion route.

**Owner reprioritization:** the earlier 2026-10-09 real-console deferral was
superseded when the owner made the Xbox available and requested probing now,
followed by completion of the entire LLE checklist. Read-only checks and benign
volatile writes are authorized without repeated prompts; avoid dangerous work.
The hardware observations below preserve this partial floor and open startup
guard; they do not close dependent integration. Historical next-step entries
describe GPU dependencies. Independent scheduling/audio/graphics work remains
available; no unobserved behavior is waived or counted as qualified.

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

## Original interrupt-driven callback qualification - 2026-10-09

`tools/native_gpu_callback_reference.py` now closes the component fixture's
previous callback-delivery gap. It reuses the qualified display reference's
PIC/original-kernel transport and executes original NTSC `InsertCallback`, kick,
interrupt gate, ISR, DPC, software-method handler and IRET over maintained NV2A.
All original callees run; an authored cdecl consumer records the context,
preceding semaphore DMA value, actual kernel IRQL and PGRAPH puller state.
It neither acknowledges interrupts nor substitutes completion.

Four batches pass in each of three independent device/kernel lifetimes:

| Case | Observed original behavior |
| --- | --- |
| PGRAPH puller held | Neither queued semaphore nor callback completes; releasing the puller raises the real interrupt and delivers both callback types |
| Device interrupt masked | The real trap stays pending with no callback delivery; unmasking permits original dispatch |
| Original disconnect/reconnect | Disconnection masks PIC delivery without completing the pending callback; reconnecting the original interrupt object permits delivery |
| Mixed 70-callback burst | Both types preserve FIFO context order, each callback sees its preceding semaphore value, and later DMA remains held until its callback returns |

Across these batches, **228 actual callbacks** pass (76 per lifetime), including
zero and high-bit DWORD contexts. The original callback executes at IRQL 2
with PGRAPH held, and original return restores all seven interrupted general
registers, ESP, EFLAGS and IRQL. Repeated kick/kernel calls do not replay
callbacks. Original interrupt disconnect and allocation frees succeed; all
nine original `MmQueryStatistics` words return exactly to their initial values.
Recorded entry counters prove 76 InsertCallback/graph/software-method executions
and four original ISR/DPC executions per lifetime.

Private result: `build/blinx/gpu-callback-original.aot-private.json`, plus
`build/blinx/gpu-callback-original.log`; the result retains original routine
hashes, snapshot/XBE identities, MMIO and individual callback records. DLL SHA-256
is `014ba56ff7de60ee8f7639e9e88b706e92ef877ac303c0baf663d37ba070c9e5`.

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_gpu_callback_reference.py --xbe <owned-default.xbe> --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_nv2a_reference.dll --functions build/blinx/disasm/functions.json --output build/blinx/gpu-callback-original.aot-private.json
ctest --test-dir build/blinx/host-gpu-lle -C Release -R '^pilot_graphics_(abi|scheduler)$' --output-on-failure
```

The existing HLE tests pass **2/2 in 3.17s**, retaining actual GPU WRITE
completion/pixels, 70 FIFO callbacks, cdecl context, IRQL 2, reentrant queries,
ordering barriers, fences and teardown. No accelerated implementation change
was required by this comparison. HLE does not reproduce PIC/MMIO mechanics.
Test executable SHA-256:
`dfee8abfc18167b1e427547a68d9e69e654a67d5af4fd3c8ee027c8f9e43882c`.

Scope remains bounded: the miniport is explicitly preowned fixture state,
not successful original device initialization. These tests do not qualify
title startup, arbitrary callback bodies, refill/teardown while work is pending,
or READ-versus-WRITE behavior under asynchronous raster work. The local raster
model completes work synchronously; semaphore ordering is not a silicon latency
measurement. No new console access, xemu runtime comparison, original binary
redistribution or external implementation import occurred. Production source
and binaries are unchanged, so no redundant full attract run is claimed.
The family remains open and the burndown stays **4/9 qualified**.

## Donor decision and remaining work

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` remains the native strategy
reference. Its immediate callback and eventual invented visibility count in
`hle_d3d8.c` are unsuitable. Its optional `nv2a_pb_exec.c` offers decoding ideas
but describes skipped geometry; it cannot establish this correctness floor.
Reuse our maintained NV2A and original command behavior. No donor body was copied.

Finish original device initialization through the selected title route, then
qualify callback dispatch and resource ownership in actual Blinx. The bounded
original interrupt/callback component above does not replace that run.
Visibility consumes real rendered work, so this now has an explicit dependency on the broader
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

## Tile enable correction and hardware-qualified address-layout component

Further inspection of original `sub_0013C3DF` establishes that it writes
`base|1` for color tiles and `base|3` for depth tiles. Our earlier `value&2`
guard mistakenly allowed active color tiles. All three banks now reject both
low flag bits until their memory clients are connected. Tests cover flags
1/2/3 through PFB, PGRAPH and RDI, preserving rejected state and RDI index.
Final actual run `gpu-lle-title-tile-enable-contract` exits 2 after 18.641s at
`FD400900=036BC001`, immediately after the 2,457,600-byte color allocation.
The earlier stop at depth-tile setup was not proof of working color tiling.
Bootstrap/graphics interrupts still execute; there are no rendered frames.

Reuse now supplies a concrete address-layout component:

- `src/machine/nv2a_tile_layout.hpp` adapts the MIT-licensed envytools NV20
  address translation, with its complete notice retained. The scoped Xbox
  layout uses two partition bits, 4-KiB banks and the selected bank XOR.
  This is tile memory addressing, distinct from Morton-swizzled textures.
- The generic envytools IGP setup clears the memory-controller configuration,
  which does not reproduce the Xbox capture. The NV20 two-partition-bit layout
  does; original PFB config `03070003` also selects two partition bits under
  envytools' NV20 field description. This selection was made using the public
  hardware result, not an independent blind/holdout test. It does not establish
  every NV2A memory-controller mode.
- The independent expectation is `Image_blit/BlitBeyondWidth.png` from
  [Xbox 1.0 hardware results](https://github.com/abaire/nxdk_pgraph_tests_golden_results/blob/06b041d6a50c060b4974d93617405f86a03a0b8f/results/Image_blit/BlitBeyondWidth.png),
  repository revision `06b041d6a50c060b4974d93617405f86a03a0b8f` (CC0), PNG
  SHA-256 `d5e8cfbd7cd6c8a229236c38fd50c4decafef145391f3f0bdc6c34018051a513`.
  The image visibly records successful tile guard preservation and overdraw
  into the untiled guard, plus the raw tile-swizzled image.
- The corresponding test procedure is `TestBlitPastWidth` in
  `nxdk_pgraph_tests` revision `706a41e61188d3b08e5b1bb2a32b74ad062a883d`.
  It explicitly calls pbkit's historical framebuffer flags incorrect, resolving
  why that sequence differed from the original driver. Its pinned nxdk is
  `e22ab118fe9afbf5a3f51c47d70e9d3a5c516d6d`; source-pattern helper is
  pbkitplusplus `72600fab67a9972fe2d3a1c6937f64e99dfa5290`.
  The capture has no embedded test-executable identity; source/capture pins are
  recorded separately rather than claiming an exact matching historical build.
- `test_nv2a_tile_layout` reproduces the tile write and raw readback with the
  maintained mapper. It also checks both banks, nine pitches, full two-row-group
  byte bijections, byte preservation, invalid configuration and address overflow.
  These properties test the broader algorithm but do not extend hardware
  qualification beyond the captured pitch-2560/bank-1 case.
- `native_gpu_tile_reference.py` verifies the original PNG hash, executes the
  compiled fixture and compares RGBA exactly in two fixed rectangles outside
  the explanatory text. All **140,800 pixels match**, including all 1,024 guard
  pixels. Linear storage fails at **139,568** of those pixels. Expected pixels
  are read from the capture; no expected image is regenerated from our code.

Reproduce with the downloaded, unmodified pinned PNG:

```powershell
cmake --build build/visibility-lle --target test_nv2a_tile_layout
$env:PATH='C:/msys64/mingw64/bin;'+$env:PATH
py -3 tools/native_gpu_tile_reference.py --fixture build/visibility-lle/test_nv2a_tile_layout.exe --golden build/blinx/tile-reference/BlitBeyondWidth.png --output build/blinx/tile-reference/layout-hardware.aot-private.json
```

The mapper is **not yet connected to production GPU/CPU memory clients**;
active tiles remain guarded. The fixture models the observed clipping/copy
sequence; it is not a test of our IMAGE_BLIT command engine. Next connect tile
selection, base/limit clipping and translated reads/writes to actual clients,
keeping PFIFO and physical CPU storage distinct from tiled surface/aperture
access. Then qualify compression and other reached draw/context dependencies.
Do not close this family on a standalone layout comparison.

Validation: software NV2A suite and the new layout CTest pass; OpenGL fixtures
explicitly skip. Nine original query/trap/fence cases again pass across three
lifetimes with the changed DLL; five GPU/device/memory CTests pass in 2.22s.
The tile-enabled guard is shared across all memory banks. DLL SHA-256 is
`ae5dfca388d9b287531762cfe3dff5cb777e5f3af8de0b8d03fdbd4306c8cefc`;
the unchanged title executable remains `4510a9e...32f9`. HLE runtime code and
the game pin are unchanged; no new full HLE attract pass is claimed. No console
was contacted and no GPL implementation was copied. The burndown stays 4/9.

## Tile memory clients: real blits, uncompressed clears and scanout

The maintained mapper now handles actual GPU memory transfers. Original Blinx
passes both color and depth base/limit/pitch setup; final title run
`gpu-lle-title-tile-clients-final` stops at `FD100304=84000000`, the depth tile's
compression configuration, after 9.000s. Both render-surface allocations and
the real bootstrap/ISR remain present. The earlier `gpu-lle-title-tile-clients`
run reached the same boundary before clear/scanout were added. Neither run
renders a frame or qualifies the title's completion/callback family.

Implemented behavior:

- Separate PFB, PGRAPH and RDI tile banks retain programmed state. Base, limit
  and pitch can be written in driver order; actual memory clients validate the
  finished configuration. Unsupported flag bits, active-job reconfiguration,
  invalid pitch/extents and overlapping tiles fail explicitly. The bounded
  supported profile requires matching active banks; differing client views
  remain unqualified. Enabling a tile now changes real memory behavior.
- Image-blit source and destination transfers resolve the selected tile and
  use the adapted layout to access physical backing in contiguous 16-byte runs.
  Unaligned and smaller transfers retain their byte positions. Raw CPU storage
  and PFIFO command fetch remain physical; enabling/disabling a tile does not
  rearrange the backing bytes. Actual bus callbacks retain ownership/protection
  checks. Cross-tile/partly tiled spans outside the tested profile fail.
- `IMAGE_BLIT SRCCOPY` accepts rows wider than destination stride, as the
  hardware test requires. A destination starting inside a tile clips to its
  inclusive limit; an untiled destination can overdraw its nominal rectangle.
  Copy counters advance only after successful memory operations. There is no
  fabricated completion, omitted byte transfer or native renderer substitute.
- Uncompressed color and depth/stencil clears perform translated writes and
  masked read/modify/write. Software scanout reads through the same qualified
  matching-bank layout and applies the existing pixel conversion. Direct
  raw-pointer scanout declines active tiles so callers use translated capture.
- Tiled triangle/texture/vertex draw clients and the native CPU tiled aperture
  remain unimplemented and guarded. Primitive submission and the batching path
  cannot bypass the draw guard. Tiled host-renderer residency remains outside
  this software reference profile. Compression still faults at configuration;
  no tag or compression success is invented.

Qualification:

- `test_nv2a_tile_commands` executes the source pattern through real
  PFIFO/RAMHT, class `0x62` surfaces and class `0x9F` copy commands. It verifies
  the hardware test's preserved tiled guard (`CCCCCCCC`) and overwritten
  untiled guard (`FFC0003F`), removes the tile, and performs the final raw-memory
  readback with another real GPU command. It does not call the layout helper
  directly. Final captured RGBA again matches **all 140,800** pixels in the
  pinned public hardware comparison rectangles, including the entire guard.
- Additional executed-command cases cover tiled-source recovery, inclusive
  last-pixel clipping, bank mismatch with no output/copy completion, denied bus
  writes, both tile bank settings, uncompressed color/Z24S8/stencil clears,
  red-only masked updates across partition/row boundaries, translated scanout
  and rejected raw-pointer/unfinished primitive paths. Clear/scanout checks
  compose existing supported method/pixel semantics with the layout; they are
  not an additional independent hardware golden for bank 0 or every format.
- The layout, command and existing software NV2A CTests pass together in
  1.00s. Existing OpenGL cases explicitly skip. Nine original query/trap/fence
  cases pass across three device lifetimes on the final DLL. Five pilot
  GPU/device/memory checks pass in 6.31s. No native-runtime source changed and
  no new full HLE attract pass is claimed; the stable game pin remains `439a45b`.

Reproduce the actual-command hardware comparison:

```powershell
cmake --build build/visibility-lle --target test_nv2a_tile_commands
$env:PATH='C:/msys64/mingw64/bin;'+$env:PATH
py -3 tools/native_gpu_tile_reference.py --mode commands --fixture build/visibility-lle/test_nv2a_tile_commands.exe --golden build/blinx/tile-reference/BlitBeyondWidth.png --output build/blinx/tile-reference/commands-final-hardware.aot-private.json
```

Final DLL SHA-256:
`67e48157079f0f3d28d2f81b5e6de6cf8100bf68d30b9a79461b16ab320b05f0`.
Title executable remains
`4510a9e07d2817cca90afafdda501fae23bcc9022ed72637c80dd1fe2ea032f9`.
The command reference RGBA hash equals the preceding layout-only result;
the new proof additionally covers actual controller selection and memory I/O.

Next: the reached depth-compression configuration and tag/data contract, using
envytools' MIT `nvhw/comp.c` and public depth-format hardware cases as reuse
candidates, followed by remaining 3D/context/CPU-aperture dependencies. Do not
turn compression into ignored enable bits or promote the 2D/clear cases to
whole-renderer qualification. No hardware contact or GPL implementation copy
occurred. The family remains open and the overall count stays 4/9.

## Z24S8 compression: executable memory effects, donor-qualified codec

Actual original Blinx now passes `FD100304=84000000` and stops at the unbound
kernel export `AvSendTVEncoderOption` (ordinal 2). Final run
`build/blinx/gpu-lle-title-zcomp-final/run.aot-private.json` exits 2 after
9.000s, with 448 original entries. This is still graphics initialization,
before any rendered frame. The first candidate reached the same boundary in
9.015s. No title completion/callback or whole-renderer qualification is claimed.

Maintained implementation:

- `nv2a_zeta_compression.hpp` adapts envytools' MIT NV20 non-MS, little-endian
  Z24S8 decoder. Original Xbox and nxdk tile programming identify the NV20
  register shape, including enable bit 31 and depth-format bit 26. The donor's
  generic chipset `0x2a` selection would choose NV25 fields; it is not used.
  A 4x4 block decodes real 8-byte or 16-byte backing, with signed gradients,
  packed residuals and shared stencil. The 8-byte flag does not mean constant
  depth; its gradients still apply.
- An authored encoder represents uniform-stencil blocks when slopes/residuals
  fit that format. Otherwise it writes all 64 bytes and publishes an
  uncompressed tag. `SET_COMPRESS_ZBUFFER_EN` controls this choice for depth
  clears. The representation policy is not asserted to reproduce the exact
  silicon compressor; logical pixels and real backing/tag effects are tested.
- `nv2a_zeta_memory.inc` connects real clear and tiled read/write clients.
  Tags are keyed by the adapted controller partition/tag address, with the
  configured tag base; they are not cached clear colors or tile-index state.
  Disabling/rebinding a tile preserves tags and bytes. Raw CPU/PFIFO accesses
  still see physical backing. Reads decode its current contents.
- Full 4x4 overwrites initialize actual tag state. Partial/masked writes first
  read and preserve unaffected pixels. Ordinary 2D writes expand the block and
  clear its tag after successful backing writes. Unknown initial tags fail;
  there is no assumed zero-filled tag RAM. Failed backing writes invalidate
  the affected modeled tag and cannot count as successful clears/copies.
- Separate PFB/PGRAPH/RDI compression words now retain supported state and
  must agree at active memory operations. Invalid formats/flags preserve old
  state; invalid RDI writes preserve its index. Reconfiguration during active
  work is guarded. Z16, endian modes, nonzero compression offsets, tag debug
  access, tiled 3D/texture/vertex clients and the CPU tiled aperture remain
  unimplemented or unqualified and guarded.

Independent evidence and its limits:

- `native_gpu_compression_reference.py` builds the unmodified pinned envytools
  `comp.c`, `chipset.c` and generated chipset tables, without replacement
  functions. All **20,130 encoded blocks / 322,080 pixels** match exactly:
  zero, all-ones, all 128 individual bits and 20,000 seeded random payloads.
  It also passes **4,096 authored-encoder blocks** through the original donor
  decoder: 128 eight-byte and 3,968 sixteen-byte blocks recover every input
  pixel. Decoder result SHA-256 is
  `f33b07f195623bac4c31c1cbdfb5ce1639413d0dcabfaa6c95e6a1c2fce88159`;
  original-decoded encoder inputs hash to
  `5c7836cf740250ef36b5756f373de31adb6bf79db1f318122201bfa65481f0a1`.
- This proves adaptation/format equivalence to an external implementation,
  **not NV2A silicon qualification**. The NV20 partition/tag mapping is an
  explicit current hypothesis under the Xbox geometry. Tag capacity/debug
  port, reset contents and exact compressor decisions still need matching
  Xbox evidence. No capacity register or debug-port response is fabricated.
  The existing uncompressed hardware golden cannot establish those details.
- Actual PFIFO commands test compressed clears, 8-to-16-byte updates, raw
  payload modification followed by fresh decoding, masked stencil updates
  requiring 64-byte expansion, ordinary 2D overwrite, disabled compression,
  all 307,200 pixels of a full-surface clear/readback, partition/row boundaries,
  nonzero tag base, tile-index reuse, mismatched banks, unknown tags and denied
  backing writes. Codec fixtures separately check signed/wrapping depth,
  residual extremes and fallback instead of dropped stencil/residual data.
- The command output for the existing Xbox `BlitBeyondWidth` comparison still
  matches all **140,800 hardware pixels**, with the same output RGBA hash and
  139,568 linear negative-control mismatches. Four software/layout/command/
  codec CTests pass in 0.31s; OpenGL cases explicitly skip. The nine original
  query/trap/fence cases pass across three device lifetimes. Five pilot
  GPU/device/memory checks pass in 2.03s.

Reproduce the donor comparison (the script only writes under `build/`):

```powershell
py -3 tools/native_gpu_compression_reference.py --envytools ../refs/envytools-review-20261008 --fixture build/visibility-lle/test_nv2a_zeta_compression.exe --cc C:/msys64/mingw64/bin/gcc.exe --git "C:/Program Files/Git/cmd/git.exe" --output build/blinx/compression-reference/decoder-encoder.aot-private.json
```

Other final manifests: `gpu-completion-zcomp-original.aot-private.json` and
`tile-reference/commands-zcomp-hardware.aot-private.json` under `build/blinx/`.
Final DLL SHA-256 is
`0c9275abd717d5e9687e6470cc64893d93ac3043515a4c22210839aa9698d03f`.
Title executable remains `4510a9e...32f9`. Native HLE runtime source and the
stable game pin `439a45b` are unchanged; no fresh full HLE attract run is
claimed. Attribution identifies the adapted MIT code and authored integration.
No console was contacted and no new GPL implementation body was incorporated.

Next executable dependency: connect the reached original AV kernel service,
then continue the original graphics/context/draw path. Preserve the explicit
Xbox compression qualification debt while collecting the relevant evidence;
do not close the GPU family on donor equivalence or initialization progress.
The overall burndown remains **4/9**.

## Original AV kernel and SMBus control checkpoint

Original `AvSendTVEncoderOption` (ordinal 2) now executes through the owned
kernel transport. Ordinal 3's six-argument `AvSetDisplayMode` ABI is connected
too, but its full device path remains unqualified. The bus supports disjoint
PCI/GPIO/SMBus port ranges and an actual NV2A MMIO mapping for AV registers.
Kernel-driven AV MMIO cannot run FIFO/DMA recursively under the transport lock.
Other engine accesses still stop explicitly.

SMBus reuses `Amd756Smbus`, IRQ11 and the shared PIC. The original kernel owns
ISR/DPC dispatch, status acknowledgment and wait completion. Interrupt delivery
inside original services no longer depends on an attached disk. The fixture
uses the same original worker priority/critical-region setup as the title;
without it, unrelated captured boot work becomes runnable. No ready queues,
events, return values or original callees are patched to complete a transaction.

This audit corrected a real controller defect inherited from the previous
xemu-style behavior: writing zero to global status used to set host completion.
Status now clears only the requested W1C bits, preserving unrelated errors;
HOSTST/ABORT are command strobes. Independent register specification:
[AMD-766 23167B, March 2001, pp84-86](https://dtsheet.com/doc/261442/amd-amd).
MCPX compatibility remains the bounded board-model assumption, not a new
silicon measurement or a qualification of wire timing/collision behavior.

`Cx25871Control` is an authored, guarded control-plane implementation, separate
from the old unrestricted register store. It starts with unknown warm state:
the current kernel RAM capture does not contain encoder register state. Its
supported reads require established standard-readback mode and previously
programmed bytes; unknown reads preserve the data latch and stop qualification.
Only the reached control registers and documented readback setup are admitted.
Analog video/filter output is not implemented by this control-plane work, and
scanout through a programmed encoder explicitly refuses to claim that output.
Reference: [Conexant 100381B, September 2001, sections 2.3/2.4 and tables 2-1/2-5](https://datasheet.octopart.com/CX25871-15-Conexant-datasheet-180221357.pdf).

Qualification retained/added:

- `native_av_reference.py` executes 15 original software queries across three
  independent captured-kernel instances: capabilities, current mode, encoder
  type, table version and repeat query, with output sentinels and stdcall ABI.
  Results belong to the pinned captured configuration, not every Xbox profile.
- The compiled GPU fixture checks these queries, both external field levels,
  six filter settings and both soften settings over three device lifetimes.
  Fixture-only setup programs the control bytes through actual SMBus writes.
  Original AV calls use real IRQ11/ISR/DPC completion; status and pending IRQs
  are checked before further fixture bus operations can acknowledge anything.
  This proves bounded software/control transport, not filtered video pixels.
- `smbus_av` checks real control writes/readback, missing-state rejection,
  actual absent-address NAK, retained errors, individual W1C acknowledgments,
  command readback and no completion/IRQ from a zero status write.
- All nine original query/trap/fence cases still pass with the changed DLL.
  Seven software GPU/SMBus/SMC/DVD tests and eight native-renderer shared
  device/USB/memory/graphics checks pass. OpenGL raster cases still skip.

Private evidence: `build/blinx/av-reference/queries.aot-private.json` and
`build/blinx/gpu-completion-av-original.aot-private.json`. Reproduce AV queries:

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_av_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output build/blinx/av-reference/queries.aot-private.json
```

The actual title reaches the original capability query, then filter setting 5;
it stops at `CX25871 standard readback requires established EN_REG_RD state`,
before rendered frames. Do not install the fixture's synthetic starting register
values in the title. Next reconstruct/capture the encoder's actual original-boot
programming with provenance tied to the RAM capture, then complete its required
device effects. A RAM dump alone is insufficient evidence for device state.
Full display-mode/blanking, context transfer, tiled draw/texture/aperture clients,
and title callback/resource qualification remain open. Burndown stays **4/9**;
the working game framework pin remains `439a45b`.

Final validation: `gpu-lle-title-av-final` exits 2 after 9.000s at that encoder
guard, with 899 entries and no rendered frames. Seven GPU-preset transport/
device/USB/memory tests pass in 2.32s. The native renderer with the changed
shared kernel transport runs `av-shared-io-native-regression` for 190.156s,
recording 166 captures and no terminal guard/fault. Manual capture inspection
shows opening FMV, Demo gameplay, replay Start interruption, title return and
second Demo entry. The harness terminates it at its requested duration; this
is a bounded regression, not new long-loop/audio-timing qualification.
`review.png` records inspected captures 8/50/95/105/130/165 under that private
run directory. No screenshot/game asset is committed.

Final executable SHA-256 (GPU preset):
`e635230df1aa4e38eb69b99435867ed20f3467479e980b0c1806ba0cccbba4da`.
Device DLL:
`278d346645ac507ee6b5cf020ebec7ec2f82166b1579e9888ef4b094ef8e470b`.
Native regression executable:
`2950123bbd7be21aba75f6b53a8bbe22390f103daa578e7eb115b63dbad3dd0c`.
No new hardware contact or xemu runtime differential is claimed. Phobos's AV
option switch was audited but not adopted: several option constants and its
null-result early return disagree with the original kernel. Attribution records
the donor consultation and primary specifications separately.

## Encoder boot provenance and guarded boot configuration

The existing kernel capture is now reproducible from the maintained machine.
At 18,000,223 retired instructions, RAM SHA-256 is exactly
`31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`,
matching `io-boot-18m.ram.bin`. The paired encoder write journal is empty and
the original current-video-mode global is zero. The capture predates software
encoder initialization; it did not merely lose an already programmed device.

`--det-ram-dump` now emits `<ram-path>.encoder.aot-private.json` at the same
stopped-CPU boundary, recording actual byte/word writes to the legacy boot
encoder while captures are pending. The bounded journal stops the machine on
overflow. RAM/sidecar open, write and close failures also stop instead of
printing a successful capture. This is observation, not new qualification of
the legacy encoder model. Register all capture requests before boot.

The next original boot write was `FD400880=0008CFFF` (PGRAPH DEBUG_7), also
programmed by pinned nxdk. The maintained GPU now retains and reads back this
known configuration. Its command behavior remains unqualified: both the
ordinary FIFO and batched route stop before consuming any GPU command under
it. Tests verify preserved semaphore memory/counters and retained pending work,
successful execution after fixture selection of the previously supported mode,
rejection of unknown values, pristine reset, and rejection of live reset.
This does not assert that `0008CFFF` and the title's `0028C3FF` draw alike.

That transport change lets the original boot reach video programming:

| Actual capture boundary | Encoder writes observed | Original published video mode |
| --- | ---: | --- |
| 18,000,223 instructions | 0 | 0 |
| 24,000,010 instructions | 89 | 0 |
| 36,000,406 instructions | 89 | 0 |
| 48,500,352 instructions | 94 | `04010101` |

The later boot attempt stops at 48,534,340 instructions when commands are
submitted under the unqualified boot DEBUG_7 mode. The guard is effective in
actual execution, not just the fixture. No boot-mode draw completion is claimed.
An older executable (`ee1d72f9...e656c`) was used first to locate the sequence;
the final observations above come from the rebuilt maintained source.

This uses the existing approximate `--run2bl` handoff, not an authenticated MCPX
cold boot. Source HDD writes are copy-on-write; provisioned locked-drive
authentication is retained. Other legacy boot devices are not newly qualified.
In particular, Conexant's specification says timing-reset commands clear
themselves. The legacy register store retains their written bit, so a last-write
journal is **not** a valid encoder snapshot. Neither these bytes nor the later
RAM capture are installed in the title. The original 18m title capture and
stable game pin remain unchanged.

Reproduction from this worktree (owned inputs stay private):

```powershell
$env:PATH='C:/msys64/mingw64/bin;'+$env:PATH
& ./build/visibility-lle/xlle.exe --boot ../machine/flash-dump-ff000000.bin --run2bl ../machine/bios-extract/bldr.bin --hdd ../machine/xbox-hdd-stockdash.img --hdd-security-file ../xboxlle/build/aot-20261002/hdd-security.key --eeprom ../machine/eeprom.bin --net-disconnected --reduced-retire-diag --machine-chunk-insns 512 --idle-fast-forward --max-insns 48500512 --det-ram-dump build/blinx/av-reference/boot-debug7-18m.ram.bin 18000000 --det-ram-dump build/blinx/av-reference/boot-debug7-48500k.ram.bin 48500000
```

Private manifest `build/blinx/av-reference/boot-provenance.aot-private.json`
records RAM/journal hashes and the interpretation limits. Capture executable:
`5e7adcb1d65ee510a175ef0c6d83d1f0014027c3dfe3accc6afeb08642a3ab6b`;
GPU DLL: `79a978e96e563379c357a8ce7fed2fdc0802deaf1afb8cf22b4d2d49fe222e88`.
Five focused software GPU/tile/compression/SMBus tests pass (0.63s), all nine
original query/trap/fence cases across three lifetimes pass, and the compiled
GPU transport/AV fixture passes (0.22s). OpenGL cases still skip explicitly.
Actual `gpu-lle-title-encoder-provenance` still exits 2 at the missing encoder
readback-state guard, 884 entries, before frames. No fresh HLE attract claim.

Next: qualify original AV initialization's reached reset/timing/control effects
against the specification and independent references, then connect that real
initialization to the LLE launch route. GPIO/PRAMDAC dependencies must have
their actual effects; do not transplant the legacy register store, replace
original callees, or replay a guessed warm state. Subsequent GPU context/draw
dependencies and the family acceptance criteria remain open. Burndown: **4/9**.

## Encoder software reset and original HAL transport

The maintained encoder now executes bounded software reset (`BA=80`) and
autoconfiguration modes 0/1 using Conexant 100381B sections 1.3.4/1.3.8,
table 2-5 and table C-1's 13.500-MHz register facts. Reset invalidates prior
warm-state knowledge, disables standard readback and loads the configuration
selected by an explicit PAL board strap. Construction still means unknown warm
state; it does not silently perform a reset. The title has no new startup seed.

Autoconfiguration changes register 38 and even registers 76..B4, retaining
unrelated control state. DIV2's two views (38 bit 4 and D4 bit 6) share one
latch. The supported register set now admits the reached timing/control writes,
but does not claim their full output behavior. Alternate clocks/configuration
modes, combined reset commands, monitor sensing and timing reset still stop.
Timing reset is not implemented by clearing its command bit alone.

Known-state masks preserve two specification ambiguities: EACTIVE's default
and CHECK_STAT's clearing behavior. Reset does not turn either ambiguity into
a manufactured byte read. The original driver's explicit subsequent writes
establish the values it needs. Reads with remaining unknown bits fail without
changing the caller's data. Analog/filter/timing output remains unqualified.

Original kernel exports 45/50 (HalReadSMBusValue/HalWriteSMBusValue) are now
available through the compiled GPU dispatch and shared original-kernel
transport. The fixture executes their four-argument ABI, real SMBus accesses,
IRQ11, original ISR and DPC. It checks status acknowledgment, interrupt
retirement, bounded DWORD output and surrounding memory sentinels. Across
three GPU lifetimes, six original-HAL reset/configuration cycles pass, alongside
the prior original AV filter/softening cases. Fixture setup is never installed
as title device state.

Validation:

- `smbus_av`: both PAL straps, three device lifetimes each, two resets each;
  independent split-register totals, retained controls, paired DIV2 writes,
  unknown-state and unsupported-command rejection; pass in 0.21s.
- Five compiled GPU-preset device/USB/GPU/memory checks pass in 2.39s.
- All nine original query/trap/fence cases pass with the changed DLL, across
  three lifetimes; private `gpu-completion-encoder-reset-original.aot-private.json`.
- Native-renderer preset rebuilt; eight shared device/USB/memory/shader/graphics
  tests pass in 5.83s. No new long-attract or A/V qualification is claimed.
- Actual `gpu-lle-title-encoder-reset` exits 2 after 11.016s, 891 entries,
  at the same missing EN_REG_RD startup-state guard, before frames. The passing
  HAL fixture does not close original video initialization or the GPU family.

Final DLL SHA-256:
`b0f74d666eb1e7efa0fbe514a1a4b727c5b9f40cc6bb2c5317eee50fe7d12997`.
GPU title executable:
`c032ba46091038c26209f400a71005aaa15c199c5df55e48325cbfd5b7a260b4`.
Native-renderer executable:
`d091b5081d3f6511c61f333571114a71bd5d7c05d741e3dc4ab1d7012659dc26`.

Next remains original startup initialization with actual timing/GPIO/PRAMDAC
effects, then the remaining GPU execution/context dependencies. No console
contact or new xemu runtime comparison occurred. Family count remains **4/9**;
the game-owned stable framework pin remains `439a45b`.

## AV connector outputs and digital screen blanking

The original AV service's byte OUT instructions now reach a bounded MCPX
output-pin model through the same compiled/original-kernel transport. NVIDIA
MCP-1 0.15 section 5.15 establishes the contiguous GPIO port map: GPIO19 at
80D3, GPIO22 at 80D6 and GPIO24 at 80D8. Xbox hardware specification 1.02
figures 16/55 identifies GPIO22/19 as LVLCNT0/1. Their combinations select
4:3 (00), 16:9 (10), TV internal tuner (11), or an explicitly undefined state
(01). GPIO19 is not an encoder-reset pin. The retail launch schematic's GPIO
table identifies GPIO24 as an unused test point; it retains a real pin level
without changing SCART selection. Sources are linked in the attribution record.

Only static-output low/high commands 04/05 are qualified. Unknown warm state
remains unknown until programming. Unsupported commands, guest readback and
widths stop; observation of the electrical output does not invent whole GPIO
register readback. Analog voltages, rise times, other pins and output modes
are outside this bounded connector contract.

PRMVIO sequencer index 1 now implements the original screen-off sequence:
21 disables picture fetches, 01 restores the existing eight-dot digital
scanout profile. Blanked capture returns black pixels without reading or
resolving a framebuffer or draining GPU work. Direct framebuffer presentation
declines the blanked case so the existing caller uses converted black output.
Sync/vblank and pending command lifetimes continue independently. Other
clock/serializer modes remain guarded. IBM VGA/XGA May 1992 p2-49 supplies
the screen-off semantics; Nouveau v6.12 corroborates the NV register mirrors
and screen-off bit. This is a digital pre-encoder contract, not qualification
of the analog encoder/filter/output route, which remains guarded.

Validation of the final batch:

- Software NV2A and AV-pin/SMBus tests pass, 2/2 in 0.25s. Tests include
  all SCART states across three lifetimes, rejected writes without mutation,
  repeated blank/unblank pixel capture, absent-framebuffer blanking, continued
  vblank IRQs while a real flip/semaphore remains pending, and no scanout drain.
  OpenGL cases remain explicitly skipped in this software build.
- Compiled original AV/transport and mode-entry guard fixtures pass, 2/2 in
  0.19s. Original AvSendTVEncoderOption option 9 performs twelve blank/unblank
  cycles across three device lifetimes, preserving framebuffer bytes and
  restoring actual colored pixels and connector selection.
- Original AvSetDisplayMode drives GPIO19 high and then reaches the explicit
  unmodelled FD6806A0 read. The guard fixture verifies no success return is
  published; it does not claim complete original display initialization.
- All nine original query/trap/fence cases remain passing across three
  lifetimes with this DLL: `gpu-completion-av-pins-original.aot-private.json`.
- Rebuilt native-renderer preset passes eight shared IO/USB/memory/graphics
  checks in 5.81s. No fresh full HLE attract or A/V sign-off is claimed.
- Actual `gpu-lle-title-av-output-pins` exits 2 after 10.016s, 892 entries,
  before frames, at the existing missing EN_REG_RD startup-state guard.
  The original mode-entry fixture is further ahead than the actual title;
  its advancement must not be reported as Blinx startup advancement.

Final DLL: `40c024238766c99b428ab6d69d017874c296b8e03341d8316c32cf444e756e5c`.
GPU executable: `0a0068a5e8e858ea3798b7164dc981a0cbbfe3b41b576d75a480998ae30ce20e`.
Native executable: `234d5f2db3c38d11ba5d5d3d5f1d43c644a9f719773f8a7d8f2233f3cf074d5c`.

Next: identify the original FD6806A0 handshake and qualify encoder timing
and remaining original mode-setting device effects, then establish actual
startup state through that execution. No guessed encoder state is installed.
GPU/title callback/resource and wider rendering acceptance remain open;
burndown stays **4/9**, stable game pin `439a45b`. No hardware contact, new
xemu runtime comparison, donor implementation copy or new HLE AV completion
claim belongs to this batch.

### Next initialization boundary: reference audit

The owned original AvSetDisplayMode polls FD6806A0 until bits 0 and 4 agree,
retains that value, programs the mode, then writes the inverse of the saved
bit 0. It subsequently waits three real vblanks. This suggests a request/ack
transaction, but original caller code alone does not identify the latched
state, acknowledgment event or register reset state. No matching behavior is
asserted or implemented from this hypothesis.

Pinned xemu `pramdac.c` has no case for offset 6A0: its default read is zero
and writes have no effect. That lets the caller's initial poll finish but
does not qualify the transaction. Scoped nxdk, envytools display register,
Cxbx and Phobos AV audits did not supply the missing device semantics.
[MakeMHz's hardware investigation](https://makemhz.com/blogs/news/project-stellar-firmware-update-1-8)
is a further behavioral lead: the public notes/code screenshots describe
display timing work but do not expose this handshake. No private spreadsheet
access, external contact or incorporation of the pictured source occurred.

Keep this guard while qualifying the separately documented Conexant timing
generator/reset and remaining mode-setting controls. If no matching public
hardware evidence resolves the transaction, a bounded hardware probe will
need separate authorization under `.12`; none was performed here.

## Encoder clock-source prerequisite

The next timing/reset dependency now has an explicit maintained clock-rate
contract in `Cx25871Control::clocks`: encoder, input-timing and CLKO pin rates
are separate exact rational values. The supported 13.5-MHz board profile derives
them from PLL_INT/FRACT and divisor 6/10, crystal bypass, an explicitly supplied
external CLKI, the 3:2 encoder divider, asynchronous input and DIV2. HALF_CLKO
divides only the output pin; EN_OUT and external-clock selection determine
whether that pin is driven. Unknown warm controls or a missing required CLKI
source fail without overwriting the observation. No nominal game FPS is used.

Software reset establishes the documented disabled output/clock-control bits;
unrelated caption/status bits remain unknown. C4 only accepts EN_OUT here.
MODE2X, external PLL reference, other crystal/power controls and combined
bypass/external pin gating remain unqualified. The rate query describes settled
frequencies, not PLL lock time or a generated waveform. It does not yet drive
the GPU, FIELD GPIO, encoder pixel/counter timeline or analog output. Timing
reset stays guarded; no command is admitted by merely clearing its bit.

Independent reference: Conexant 100381B sections 1.3.6/1.3.6.1 and table 2-5.
Tests check independent rational evaluations, divider isolation, missing source,
output gating and reset across six device lifetimes. Software NV2A/SMBus tests
pass 2/2 (0.29s). Original HAL/IRQ transport and mode-entry guard tests pass
2/2 (0.21s); six original-HAL cycles verify CLKO enable/division and changed
fractional PLL values. Existing original query/trap/fence cases remain 9/9
across three lifetimes. Native-renderer shared regression checks pass 8/8
(5.84s); the native binary remains unchanged.

Actual `gpu-lle-title-encoder-clocks` remains exit 2 after 10.015s, 898 entries,
at missing EN_REG_RD startup state, before frames. Manifest:
`build/blinx/gpu-lle-title-encoder-clocks/run.aot-private.json`.
Original GPU cases: `gpu-completion-encoder-clocks-original.aot-private.json`.
DLL: `cbab0e16962f3dbca74c10154455a49e8400bffba2a1ee89a011ce1401885042`.
GPU executable: `af4cab4652bb6e6f72087d0d60bfac1209323df0594f92b493f9a3a3579bb0c6`.

Next remains actual timing-generator progression/reset and FIELD behavior,
then original mode-setting/handshake and coherent title startup. Specification
sections 1.3.4 and 1.3.31 require odd-field counter origin after timing reset,
with slave HSYNC/VSYNC synchronization and a two-CLKI FIELD transition delay.
Sections 1.3.26/27 additionally require the subcarrier accumulator and its
four/eight-field reset policy (unless disabled), not merely a stored reset bit.
No new hardware access, donor bodies, full attract claim or family closure;
burndown remains **4/9**, stable game pin `439a45b`.

## Running encoder timing, reset and FIELD transport

The maintained `Cx25871Timing` now consumes explicit common-clock events for
the bounded synchronous progressive-input/interlaced-output profile. Timing
reset establishes real counter and subcarrier origins before its strobe
self-clears. Full-frame output coordinates retain the half-line even-field
phase. The DTO advances with the programmed increment and resets every four
NTSC/eight PAL fields unless DIS_SCRST is set. FIELD follows the documented
odd/even sequence, two-CLKI delay, FIELDI polarity and EN_OUT gating.

Slave mode requires actual, aligned HSYNC/VSYNC events. Missing, duplicate,
wrong-phase and inconsistent syncs fail; they are not inferred from nominal
host frames. Software reset invalidates timing knowledge rather than pretending
to establish a counter origin. Once programmed timing has been selected in a
device lifetime, the old nominal FIELD injection cannot regain authority,
including after reset. A GPIO read of an unknown or undriven FIELD pin fails
without manufacturing a caller output. Batched master clock events retain an
intervening active level in the MCPX latch even if the final level is low.

The source is authored from Conexant 100381B sections 1.3.4, 1.3.26/27 and
1.3.31. The settled rate query validates clock controls, but the timing engine
advances in explicitly supplied clock ticks: no wall-time conversion or actual
NV2A clock/sync provider is connected to the title yet. Supported slave phases
are aligned; arbitrary startup phase/resynchronization, asynchronous/fractional
timing, PLL settling, external reset-pin behavior, pixel filtering and analog
output remain open. Changing relevant timing controls invalidates this bounded
model until reset; that is a qualification limit, not a claim that real silicon
stops its counters on every such write.

Validation:

- Software NV2A/SMBus suites pass 2/2 in 0.26s; twelve PAL/NTSC master/slave
  lifetimes exercise counter/half-line phase, full color-frame reset periods,
  polarity, output gating, repeated resets and rejected syncs/clock overflow.
  SDL/OpenGL cases remain explicitly skipped in this software build.
- Original HAL/IRQ transport and mode-entry guard checks pass 2/2 in 0.30s.
  The additional original timing fixture passes in 0.10s across six lifetimes:
  real HAL writes, original AV FIELD reads, both sides of the two-clock
  transition, phase/reset/polarity/gating and batched GPIO latching. Five
  lifetimes tear down cleanly; the sixth deliberately rejects an undriven FIELD
  read and exits without further mutations of the terminally faulted machine.
- All nine original query/trap/fence cases pass across three lifetimes with
  this DLL: `gpu-completion-encoder-timing-original.aot-private.json`.
- Rebuilt native-renderer preset passes eight shared IO/USB/memory/graphics
  checks in 6.56s. No fresh full attract or audio/video sign-off is claimed.
- Actual `gpu-lle-title-encoder-timing` exits 2 after 10.000s, 895 entries,
  before frames, at the existing missing EN_REG_RD startup-state guard.
  The clock/sync fixture does not establish original title initialization.

DLL: `014ba56ff7de60ee8f7639e9e88b706e92ef877ac303c0baf663d37ba070c9e5`.
GPU executable: `43f3dbd21d2eef1ccd027e36eee47cdb5358c4be8550b44ee652d402aef75f32`.
Native executable: `b7abf1a26dc8873d6ba7e2c34e86ecd0d5a5473fa2bcb0fdc0571883d0348670`.

Next: original mode-setting controls and coherent NV2A-to-encoder clock/sync
events, the unresolved FD6806A0 transaction, then actual title startup and
remaining graphics/context/callback/resource qualification. No new hardware
contact, xemu runtime comparison or donor implementation body was used.
Family count remains **4/9**; stable game pin `439a45b` is unchanged.

### Board clock/sync connection: resolved topology, remaining implementation

[Xbox Hardware Design Specification 1.02](https://consolemods.org/wiki/images/a/ae/Xbox_Hardware_Design_Specification_1.02.pdf)
sections 2.4.1.5 and 2.8.1 explicitly identify the signal directions: the
encoder supplies PIXCLK to the GPU, while the GPU supplies HSYNC/VSYNC back
to the encoder. Consequently the next board connection must use the encoder's
enabled CLKO rate as the external pixel source and derive sync events from
the programmed GPU display generator. Core-clock/VPLL arithmetic or a fixed
60-Hz FIELD toggle cannot establish this route.

The existing private original-boot write journal narrows the generator to
investigate. At 24 million instructions, it records FP_HTOTAL=00000307 and
FP_VTOTAL=0000020C, corresponding to 776 clocks by 525 lines. These match the
encoder input totals in the later paired encoder journal. The VGA CRTC bytes
instead describe a 728-clock horizontal total. Do not feed a conventional VGA
counter directly into the encoder on the assumption that both totals match.
This is evidence of original programming and a routing question, not proof of
the model's hardware behavior or permission to install journal state.

Pinned envytools `rnndb/display/nv3_pramdac.xml` identifies the FP total/sync
registers, PLL_CONTROL's TV_PCLK bit and FP clock power-down. The original
routine programs PLL_CONTROL=10020000 and FP_TG_CONTROL=10100111, later changes
FP_DEBUG_0, then resets encoder timing. Active versus programmed timing,
the unknown FD6806A0 commit/acknowledgment transaction, clock gating and the
phase of the first real sync after reset must therefore be qualified together.
The current aligned-sync fixture must not silently reset GPU phase to make
this sequence pass. Next is that bounded digital output/control family; analog
waveform generation is not established by these register facts.

No hardware was contacted. The probe repository was inspected only; its
current-task authorization requirements remain in force if a silicon experiment
is eventually needed. No new probe code or protocol was deployed or added.

### Prepared silicon observation for FD6806A0

Follow-up `beads-xbox.1.111` now has a concrete, offline-tested experiment in
[`xboxlle-probe` commit a22bb74](https://github.com/mstan/xboxlle-probe/blob/a22bb74/docs/AV-HANDSHAKE.md),
branch `feat/av-handshake-probe-20261009`. The armed command reads one fixed
register, requires a stable idle precondition, writes the original caller's
request encoding once and records actual read values/timing brackets. It stops
on unexpected state, 50 ms or 100,000 post-write polls, and consumes its single
attempt even on failure. There is no automatic retry or restoring write.

All 17 offline tests pass, including the actual C control loop with mocked
MMIO/clock callbacks and loopback client tests. The pinned-nxdk XBE builds;
SHA-256 `421405527c8e1d80ed70a7da04d682b3637fcebd7cd9518f8d0f29b317b4cc96`.
No hardware connection, deployment or execution occurred. The owner has been
asked for the exact target and current-task authorization required by that
repository's AGENTS.md, or may run the prepared experiment manually.

This first observation can establish sampled request/acknowledgment behavior
and elapsed-time bounds, not reset defaults, timing-bank contents or the exact
sync edge that causes acknowledgment. Keep the LLE guard until evidence supports
the reached transaction; do not install immediate success or a captured idle
value. Known digital timing/control work can continue offline while the hardware
question is pending. Blinx runtime and stable game pin are unchanged, **4/9**.

### Digital sync generator component while hardware observation is pending

`src/machine/nv2a_fp_timing.hpp` now provides the bounded output-counter and
signal component needed downstream of an established active FP mode. Given
active horizontal/vertical timing and an explicit counter origin, pixel clocks
advance horizontal/vertical position and complete-frame count, and produce
HSYNC, VSYNC and display-enable transitions with independently selected
polarities. Valid-region cropping, line/frame wrap and batched clock advances
have real effects. Zero clock edges preserve state; invalid configuration or
clock overflow fails before mutation. Disabled electrical outputs, wrapping
sync intervals and unsupported controls are rejected rather than inferred.

The board must establish the active mode and counter origin. This API does
not choose a reset phase, reset an encoder to align clocks, perform live HLE
switching, activate programmed guest writes or acknowledge FD6806A0. It is an
isolated component and **is not yet connected to the GPU/title route**. The
existing programmable/active-bank and clock-source guards must remain until
their actual effects are qualified. FP_CRTC upstream triggers, pixel scaling,
serialization and physical pixel-clock selection/division are separate work.

Nouveau v6.12 `dfp.c` and `nvreg.h`, plus pinned envytools, corroborate count
encoding and polarity. The original boot FP profile is one exercised input;
it is not a hardware golden. An independent dense pixel-by-pixel traversal
checks every sparse output edge across three profiles and all eight polarity
combinations (24 lifetimes), including the reached centered-output profile,
nonzero initial phase, clipped valid regions, immediate boundaries, overflow
and rejected configurations preserving prior state. This validates the component
algorithm under that stated model, not the unknown NV2A activation event.

Validation: `nv2a_fp_timing`, `nv2a` and `smbus_av` pass 3/3 in 0.33s. The final
test-only extension exercising the centered profile passes in 0.05s. Existing
software-suite OpenGL skips remain. The title EXE and reference DLL hashes are
unchanged from the encoder-timing checkpoint above; no redundant title run or
new original-mode/attract success is claimed for this unconnected component.

Next remains active FP control/clock routing and the first encoder sync phase,
with the FD6806A0 question tracked in `.111`. Hardware authorization is still
pending; no console contact or additional family closure occurred. **4/9**.

## Tiled 3D surface clients - 2026-10-09

The maintained software renderer now uses `ram_tile_transfer` for 3D color
and depth surfaces. This reuses the attributed envytools layout and existing
Z24S8 decoding/tag lifetime; it does not invent another tile mapper or rasterizer.
Tiled draws select the existing strict generic software raster path, with
translated reads/writes and no raw surface pointers, resident host renderer or
worker queue. Invalid tile-bank configuration and unsupported draws remain
explicit failures. Texture and vertex reads that touch any tile, including a
span crossing into one, remain guarded until those client contracts are qualified.
Physical PFIFO fetches, report DMA and CPU backing retain their own addresses.

Eight cases combine both color-bank selectors, available/unavailable direct RAM
pointers, and uncompressed/previously compressed Z24S8 depth. Real PFIFO commands
clear, draw three overlapping rectangles, publish visibility, and blit both
planes out for inspection. Independent screen-space formulas check every pixel
and depth DWORD in eight 640x480 color/depth pairs (**4,915,200 values**), including
tile-row/partition boundaries, occlusion, red-only masks, disabled depth writes,
preserved stencil and allocation guards. All cases count **696** actual passing
samples. An intentionally active tile contains the FIFO and report storage;
their bytes still travel physically. A direct-pointer observer confirms no
tiled surface bypass. Existing compressed blocks are read correctly and writes
expand them with valid tag updates; this does not qualify hardware ZROP
compression decisions or compressed physical-byte equivalence.

The failure cases exposed a separate completion bug: END discarded a failed
vertex fetch, allowing GET_REPORT to publish the previous count. A recorded
negative test fails on that behavior. END now invalidates visibility after
failed inline decoding or batch submission; both rejected vertex and texture
cases leave the report sentinel and RAM write count untouched. Generic-path
errors retain their original diagnostic instead of being overwritten by a
second unsupported-draw message.

Validation on the final device library:

- `nv2a_tile_layout`, `nv2a_tile_commands`, `nv2a_zeta_compression`, and `nv2a`:
  **4/4 pass in 0.44s**. Existing software-build OpenGL skips remain explicit.
- Pinned Xbox blit golden: **140,800 pixels match exactly**, with 139,568
  mismatches under the linear negative control. This rechecks the existing 2D
  hardware evidence; it is not a newly captured 3D reference.
- Original query/trap/fence fixture: all nine cases across three lifetimes pass.
  Original interrupt/callback fixture: all 228 callbacks across three lifetimes
  pass. Their result files are `gpu-completion-tiled-surfaces.aot-private.json`
  and `gpu-callback-tiled-surfaces.aot-private.json` under `build/blinx/`.
- Shared GPU transport, original AV guard, encoder timing and native graphics
  ABI/scheduler: **5/5 pass in 3.34s**.
- Actual selected LLE Blinx launch: **9.921s**, explicit exit 2 at
  `CX25871 standard readback requires established EN_REG_RD state`, before
  frames. No timeout, new title-rendering result or full native attract run is
  claimed. Evidence: `build/blinx/gpu-lle-title-tiled-surfaces/run.aot-private.json`
  and `build/blinx/run-gpu-lle-title-tiled-surfaces.log`.

Library SHA-256:
`099401b86303ff6947eeb496e856c2bd820d6e132be59f9881969d66f622f726`.
Title executable remains
`43f3dbd21d2eef1ccd027e36eee47cdb5358c4be8550b44ee652d402aef75f32`.
The independent analytic cases qualify the bounded client integration, not
every NV2A format or a complete scene. Original startup/FP handshake, object
context transfer, tiled texture/vertex/CPU-aperture clients and title resource
lifetime remain open. No hardware contact or new external implementation body
was added. Stable Blinx pin remains `439a45b`; family count remains **4/9**.

## Tiled color textures and matching native sampling - 2026-10-09

Pitch-linear A8R8G8B8/X8R8G8B8 texture reads now consume the existing translated
surface memory. Admission requires matching PFB/PGRAPH/RDI banks, a single-level
2D color format, DMA A or B, an uncompressed tile and a read wholly within that
tile. The sampler receives logical bytes from the same maintained mapper used
by the 2D and 3D clients; it never bypasses that mapping through a direct pointer.
This composes the existing memory and sampler models. The mirror programming
and format fields have external source evidence; **there is no new Xbox tiled
texture golden or hardware measurement**. Exact texture-client silicon behavior
and broader formats remain part of the open graphics qualification.

The new tests exposed two generic sampler errors, now corrected for linear
images regardless of tiling:

- Raw image reads retained guest row padding, but setup replaced their pitch
  with `width * 4`. Sampling therefore read the wrong row after the first one.
  Only decoded DXT/YUV output now adopts a tightly packed pitch.
- X8R8G8B8 incorrectly sampled the stored X byte as alpha. Its private sampling
  snapshot now supplies alpha one; guest memory is unchanged.

`test_nv2a_tile_commands` runs 32 configurations: tiled/untiled, both tile-bank
selectors, direct RAM access present/absent, DMA A/B, and ARGB/XRGB. Real blits
populate a padded image; the texture client samples an offset subimage across
tile boundaries; a 3D draw changes that image; a second texture draw must see
the change. Independent coordinate formulas check both images, alpha and the
untouched background. A held puller withholds the final report until actual
sampling completes, yielding 4,608 samples. Seven additional rejection cases
cover Morton, DXT, cube, mip-chain, depth, partial-entry and compressed storage;
each leaves output/report untouched. The prior partial-exit case also passes.
Initial wrong-row evidence is retained in `build/blinx/tiled-texture-before.log`.

The Phobos-backed HLE receives an equivalent caller-visible test in
`graphics_feedback_backend_test.inc`: padded 37x19 ARGB/XRGB uploads, actual GPU
sampling, then GPU writes followed by sampling again. It passes across all three
device lifetimes. Existing native row repacking and native XRGB image format
already satisfy this contract; no HLE production change or physical tiling is
needed. This validates the shared color/stride/update outcome without requiring
matching internal operations.

Validation:

- Software NV2A/tile/compression suites: **4/4, 0.62s**. Final test-only rejection
  additions: tile-command suite passes again in **0.23s**. Existing OpenGL skips
  remain explicit in the software build.
- Original query/trap/fence: nine cases in each of three lifetimes; original
  IRQ/DPC callback fixture: **228 callbacks**, no errors. Results are
  `gpu-completion-tiled-textures.aot-private.json` and
  `gpu-callback-tiled-textures.aot-private.json` under `build/blinx/`.
- Existing Xbox 2D blit golden: **140,800 exact pixels**, with 139,568 mismatches
  for the linear negative control. This is still 2D evidence, not texture proof.
- Native graphics ABI/scheduler and GPU transport/AV/encoder checks: **5/5,
  3.15s**. Extended native backend test: **1/1, 1.03s**, three new contract passes.
- Actual LLE Blinx launch: **9.531s**, exit 2, no timeout, same
  `CX25871 standard readback requires established EN_REG_RD state` guard before
  frames. Evidence: `build/blinx/gpu-lle-title-tiled-textures/run.aot-private.json`.

Reference DLL SHA-256:
`2e4310fef468ff272341a96076be75e1bbbacf06678c68330ae0c6e256b8b767`.
Native backend test SHA-256:
`9d03cfb8b4c9a47b846b4c99a4b9b6453d3e87642504ec0203bad1f3054144c9`.
The title executable and stable game pin are unchanged. Startup/FP handshake,
context transfer, remaining texture layouts, vertex/CPU aperture clients and
actual-title resource lifetime stay open. Texel-center comparisons do not claim
complete point/bilinear/mipmap behavior or asynchronous texture-cache timing.
No console access occurred; the family count remains **4/9**.

## Encoder reset between incoming sync edges - 2026-10-09

Conexant 100381B sections 1.3.4, 1.3.14/15 and 1.3.31 establish that a slave
timing reset need not align with the GPU's incoming sync. The first actual
HSYNC/VSYNC combination selects even FIELD; the second resynchronizes odd.
For progressive input, VSYNC can precede the associated HSYNC. FIELD changes
two CLKI cycles after that horizontal edge. The maintained timing component now
accepts this sequence without resetting the GPU or inventing missing edges.

The first nonaligned frame leaves transient analog raster phase unknown; the
second establishes the odd raster origin. Automatic color-frame phase after
nonaligned resynchronization remains unknown until explicit timing reset.
DIS_SCRST retains a qualified free-running subcarrier accumulator. Observation
flags distinguish these cases; the exported full-timing query rejects unknown
phase before changing any caller output. Original FIELD queries still execute
against their independently established digital signal.

Validation on the final reference DLL:

- Software NV2A, FP timing, tile commands and SMBus/encoder suites: **4/4,
  0.52s**. Encoder tests cover 48 PAL/NTSC, color-reset, first-edge and VSYNC-lead
  configurations, each with two resets (96 lifetimes). Six additional runs
  connect the real FP timing component to the encoder across three explicit
  GPU origins, resetting only the encoder. Active-mode fixture inputs do not
  establish guest register activation or the missing handshake.
- Native GPU transport, AV guard, original HAL/FIELD timing, unknown-phase
  output preservation and existing HLE graphics ABI/scheduler: **6/6, 3.03s**.
  Three slave-device lifetimes run the shifted reset through original kernel
  calls, retaining the existing master/polarity/high-impedance cases.
- Original query/trap/fence: nine cases across three lifetimes; original IRQ/DPC
  callback fixture: 228 callbacks across three lifetimes, no errors. Private
  results: `build/blinx/gpu-{completion,callback}-encoder-phase.aot-private.json`.
- Actual LLE Blinx: **9.297s**, exit 2, no timeout; unchanged
  `CX25871 standard readback requires established EN_REG_RD state` stop before
  frames. Manifest: `build/blinx/gpu-lle-title-encoder-phase/run.aot-private.json`.

Reference DLL SHA-256:
`4d6425e456173442e346fa02893837e6bc1f121bfd34696a704cf495639171ed`.
Native contract executable SHA-256:
`3bb801ce2c4c8372a2df7c487542b773b40a006ae367cf268af39331532a66a5`.
The title executable and stable HLE game pin `439a45b` are unchanged. No HLE
production change, full analog waveform claim or new hardware observation.

Cromwell revision `86f547387184d1001d377cce97d3756c8acf91cc` was inspected as a
reference for GPU programming followed by encoder setup and TIMING_RST. It
does not supply the missing FD6806A0 semantics; no GPL implementation body was
copied. Attribution records the exact files. Startup state, timing activation,
board clock routing, context transfer and remaining GPU clients stay open.
At this offline checkpoint the prepared hardware observation had not run;
the later authorized result is recorded below.
The family count remains **4/9**.

## First authorized AV handshake observation - 2026-10-09

After the verified recovery scope and separate XBE upload described in the
[burndown](../../docs/BLINX-BURNDOWN.md), the owner manually launched the
reviewed v0.4.0 probe. An earlier identity check saw the legacy v0.2 program
and stopped before ARM; no experiment was sent in that attempt. The corrected
session passed the exact new-agent identity and fixed CPU/NV2A read-only probes,
then issued the previously authorized armed `AV_HANDSHAKE` once.

| Observation | Result |
| --- | --- |
| Initial DWORD FD6806A0, two reads | Stable `0x11` |
| Request written | `(~initial)&1 = 0`, exactly one DWORD write |
| First post-write sample | `0x00`; original caller's bits-0/4 equality holds for the new request |
| Pending state sampled | None |
| Total reads / writes | 3 / 1 |
| Performance-counter frequency | 3,375,000 Hz |
| Instrumented completion bracket | 0 to 7.111111 microseconds from write-begin to completed acknowledged sample |
| Retry / restoring write | Neither |

This establishes one sampled transaction in the already active nxdk display
mode. The upper bound includes counter calls, MMIO reads and probe overhead;
it does not identify the acknowledgment edge. A pending interval could have
ended before the first post-write read. The observation establishes neither
instantaneous completion nor a particular horizontal/vertical sync dependency.
It also does not establish reset defaults, the opposite request direction,
or which timing-bank writes are activated. The startup mode was selected by
nxdk before this observation; its observed idle state is not an emulator reset
specification.

Raw console context, kernel details, timing records and identifiers remain
private with the G: recovery evidence. No EEPROM, flash, boot configuration or
disk-layout write occurred. Keep the existing LLE guard and `.111` / `.109`
open until the reached original-mode contract has enough evidence and passes
implementation validation. HLE draw/cache/upload behavior is unchanged;
graphics continues independently and the service-family count remains **4/9**.

## Display-bank and board-context observations - 2026-10-10

The owner separately approved the prepared 23-read batch, then authorized
read-only checks and benign writes without further prompts while advising
against dangerous operations. The existing v0.4.0 agent passed identity checks
for each of three new, separately preserved private sessions. No new XBE,
launch, AV transaction, GPIO output, audio command or storage operation occurred.

The first batch completed all **23 DWORD reads** with zero MMIO writes and
stable AV endpoints `0`. Its selected read bank had READ_PROG set. Observed
display context:

| Register/context | Observed value |
| --- | --- |
| NVPLL / MPLL / VPLL | `00011C01` / `00007702` / `0003C20D` |
| GENERAL_CONTROL | `00100030` |
| FP_TG_CONTROL / FP_DEBUG_0 | `10100111` / `21101100` |
| Horizontal display end / total / CRTC | 639 / 935 / 599 |
| Horizontal sync start / end | 755 / 819 |
| Horizontal valid start / end | 0 / 639 |
| Vertical display end / total / CRTC | 479 / 524 / 479 |
| Vertical sync start / end | 498 / 500 |
| Vertical valid start / end | 0 / 479 |
| FP_DITHER | `0` |

A second fixed batch completed five reads with zero MMIO or I/O writes:
AV endpoints remained `0`, PLL_CONTROL was `100A0000`, GPIO15 control was
`44`, and the audio FE fence control was `1`. These are current-state
observations, not reset defaults. GPIO15's byte is not an inferred
input/output or flash-write truth table.

The third experiment temporarily cleared only FD680848's documented READ_PROG
bit, verified that selector value, read horizontal/vertical totals, restored
READ_PROG while preserving fresh other control bits, and verified actual
control readback `10100111`. It returned **935 / 524** again, or **936 clocks
by 525 lines** under the documented count-minus-one encoding. The two
selected banks agreed on these two fields in this already active nxdk mode.
It performed six MMIO reads and exactly two writes to the single read-selector
control; no display timing or enable bit changed. The interpretation as
programmed/active banks uses the pinned register definitions, not an independent
electrical waveform measurement. Other active fields were not read.

New host tools and exact payload review are in `xboxlle-probe`'s
[`BOARD-CONTEXT.md`](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/BOARD-CONTEXT.md)
and [`FP-BANKS.md`](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/FP-BANKS.md).
All **42 offline probe tests** pass, including actual x86 execution checking
approved MMIO/port effects, selector restoration, callee state, failed selector
readback, bad timing widths, refused ARM and no retry after host failures.
Raw results and timestamps remain private on the recovery drive.

The hardware specification 1.02 section 2.6.5 assigns GPIO15 to development
flash programming and leaves it unconnected in the retail design. Combined
with MCP-1's base+C0+pin register map, this narrows the scheduler's 80CF
dependency without experimenting with flash-related output. The owner's
actual board wiring is not established by that design specification.

Neither equal totals nor the earlier fast AV acknowledgment proves the reset
state, opposite request direction, commit event, first-sync phase, PLL lock
delay or coherent original startup. FE control `1` does not establish audio
completion ordering. Keep `.111`, `.109`, `.112` and `.115` open; the LLE AV
guard remains. This checkpoint changes no shipping HLE path or family count.

## Reverse AV and valid-window observations - 2026-10-10

Four distinct follow-ups completed under the owner's standing read-only and
benign-write authorization, using the existing v0.4.0 ARM/EXEC route. The
original AV_HANDSHAKE one-attempt latch remains consumed and unchanged. No
new XBE, launch, audio playback, GPIO, clock/sync/total/enable or storage write
occurred. Every session completed successfully without a retry.

| Experiment | Actual observation | Device stores |
| --- | --- | ---: |
| Reverse request from stable AV `0` | Request `1`; first post-write read `1` (pending), second `11` (acknowledged). Phase remains `11` | 1 |
| Reversible one-pixel HVALID_START | Programmed `1` read back; alternate start remained `0` before request and immediately after ack. Both AV directions sampled pending; four combined post-write AV reads. Restored programmed/alternate start `0`, selector `10100111`, AV `11` | 12 |
| Same field with measured waits | Same result after at least 50 ms following **each** ack, including restoration. Actual kernel counter frequency 3,375,000 Hz; waits require 168,750 ticks and have finite call budgets | 12 |
| Read-only mode context | Ten DWORD reads: DEBUG_1=`0`, DEBUG_2=`0`, DEBUG_0=`21101100`, horizontal programmed start/end/display `0/639/639`; selector and AV endpoints match the restored state | 0 |

Each successful field experiment uses eight read-selector stores, two
one-pixel field/restoration stores and two planned AV phase stores. These
are separate successful transactions; no recovery or retry follows a failed
operation. The measured-wait variant validates the actual counter exports and
frequency before any selector or crop store. Fresh selector words are checked
before restoration; unexpected values cannot become whole-word writes.

The reverse result establishes the opposite request direction and a sampled
pending state. Poll counts do not measure elapsed hardware latency. The window
experiments reject the assumption that acknowledgment necessarily publishes
this field in the current mode, even within the measured 50 ms window. They
do not establish later activation, other fields, reset state, electrical edges
or original-title startup. The nonzero-word override hypothesis is absent in
the subsequent snapshot; the exact NV2A mode/automatic-window rule remains
unknown. Sequential samples do not atomically exclude other kernel writers.

Tooling, closed payload sources and limits are recorded in
[AV-REVERSE.md](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/AV-REVERSE.md)
and [FP-ACTIVATION.md](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/FP-ACTIVATION.md).
The factual extra register offsets come from
[Linux v6.12 nvreg.h](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/nvreg.h);
[DFP setup](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/dfp.c)
motivates the scaling/override context reads, without proving NV2A behavior.

All **76 offline probe tests pass in 46.868 s**, CPU-only at Idle on one logical
processor. Actual compiled bytecode is tested with independent bank policies,
pending transactions, delayed restoration, invalid PE/frequency, backwards and
stopped clocks, unexpected selectors and finite failure limits. Current source
reproduces the exact observed payload hashes: reverse 204 bytes, immediate
window 712 bytes and timed window 1516 bytes. The extracted shared compiler and
PE32 resolver also preserve both prior FE payloads byte-for-byte (888/968 bytes).
Raw evidence and source/payload hashes remain private on G. `.111` and startup
stay open; no guessed commit rule is added, HLE rendering performance is
unchanged, and the service-family count stays **4/9**.

## Original-thread GPU bus and device work - 2026-10-10

The actual original scheduling CPU now calls the same maintained NV2A MMIO
owner as compiled accesses. The earlier AV-only adapter restriction existed
because the CPU held a nonrecursive transport lock while GPU bus DMA attempted
to acquire it again. A same-thread, callback-scoped borrow now permits only
synchronous RAM transfers through the existing full extent/PFN/PTE/protection
and pin checks. Other host threads still acquire the lock. Retained USB leases,
recursive original services/IRQ/VM/CPU execution, and binding changes inside
device work are rejected before mutation. No original CPU reentry is permitted.

A sole idle-bound device-work callback samples the existing external monotonic
GPU clock between actual CPU slices. MMIO uses the same clock/device owner;
register reads do not invent time. Existing nominal display precision and
unqualified AV/mode/register behavior retain their guards. Remove the work
callback before destroying the GPU. This route is compiled only into the LLE
renderer; the default performant HLE receives no additional device execution.

Three actual original CPU/GPU lifetimes create real original contiguous PFNs,
program PBUS/PRAMIN/PFIFO, hold and release PGRAPH, and observe genuine semaphore
DMA through the same RAM owner. Nine original threads retain exact complete
caller RAM, original thread/handle baselines and cleanup. Negative controls
reject cross-page read-only writes before their first byte, retained USB leases,
CPU reentry and active-owner replacement. Stale DMA after original free fails.
The console clock remains alive across replacement GPU lifetimes.

Five focused GPU checks pass in0.453s. The coherent CPU/GPU/DMA integration
passes **74/74 in15.406s**. Its fresh optional DMA lifecycle and existing GP graph
checks also validate the independently qualified19-entry APU batch. One muted
original startup after that pass runs7.828s, passes FD001804 and actual installed
HDD verification, then guards **HalGetInterruptVector44**. Full original startup,
composed rendering/callbacks and the larger family remain open; the burndown is
still **4/9**. No physical console operation or attract/playback run occurred.

Private evidence: `gpu-thread-cpu-focused-final-20261010.aot-private.json`,
`gpu-audio-integration-qualified-20261010.aot-private.json`, and
`gpu-audio-final-title-startup-20261010.aot-private.json`. The maintained GPU DLL
stays `9fdec5189881eabf77d00a52a8442ee57f5f2fe8199d7c22335594ee28cedf95`.
The rejected second console-clock initialization, missing-target configuration,
and old synchronous-DMA GP fixture failures remain preserved; no failed result
is counted as a pass. The earlier7.922s startup is a dependency observation;
the final source/binary-bound startup is the7.828s result above.

## Shared HAL, PCI and original interrupt ownership - 2026-10-10

Shared C/raw-thread device hooks now retain the original kernel's HAL, PCI,
interrupt, device/pool and pin effects on the existing RAM/IRQ owners. The
interrupt-vector guard rejects out-of-scope initialization before object writes.
Three GPU lifetimes use fifteen actual threads; six execute full/partial PCI
reads through original HAL instructions and the same maintained GPU bus.
The real device and pin lifetimes, duplicate-unlock failure and existing C/USB
contracts pass in the **76/76 coherent batch in14.750s**.

One muted original Blinx startup after that pass reaches ISR13B210 at IRQL23
and acknowledges its real GPU interrupt. The compiled ISR's DPC queue call then
hits `native dispatcher unavailable with original scheduling selected` after
8.297s. This establishes original interrupt delivery, not composed DPC/rendering
qualification. The next dependency is compiled callback routing to the already
maintained original kernel service floor. Full startup, rendering and callback
lifecycle remain required; family count remains **4/9**.

Private reports: `thread-device-qualified-20261010.aot-private.json` and
`thread-device-title-startup-20261010.aot-private.json`. All43 source inputs and
nine binaries match; original ROM/EEPROM hashes remain unchanged. The GPU DLL
retains the prior identity. No physical console or attract/audio run occurred.

## Compiled callbacks and bounded service frames - 2026-10-11

Compiled original ISR/DPC kernel calls now execute genuine original queue,
event, timer and IRQL services through the maintained C owner. The native
dispatcher remains forbidden under original scheduling. Shared raw-thread
arities additionally connect actual event reset/set. Four separately guarded
C-service frames permit the observed interrupt during an original ownership
call without overlapping bridge/GDT or original thread/DPC stacks.

The final batch passes **78/78 in15.969s**. Its dedicated original-scheduling
callback executable checks real queue/remove/cancel ordering, event state,
fastcall, caller context and unsupported-frame/handoff guards; existing device,
GPU, DMA and mixed native checks pass. The original muted title then queues real
GPU work at IRQL23, executes original DPC13BBC0 at IRQL2 and returns from actual
KeSetEvent145. Its depth3 queue/event calls return through separate frames.
After8.375s the next stop is raw AV export2. Full AV/startup/rendering and the
larger gate remain open at **4/9**; default HLE gains no callback/device work.

Private reports: `callback-service-frames-qualified-20261011.aot-private.json`
and `callback-service-frames-title-startup-20261011.aot-private.json`. Their49
sources, ten binaries and unchanged ROM/EEPROM inputs match. Prior mixed-target
coverage failures and the real two-frame capacity stop remain separate evidence.
No physical Xbox, playback or repeated attract run was performed.

## Raw AV and encoder services - 2026-10-11

The already maintained original AV2/3 and SMBus45/50 services now accept raw
original-thread CALL frames. Three device/thread lifetimes check36 original
returns, independently established AV query values, output DWORD canaries,
encoder reset/control and actual SMBus IRQ completion. A separate six-argument
original-thread mode call reaches the genuine FD6806A0 guard without publishing
success or changing its caller result. The combined batch passes80/80 in15.500s.

One muted actual startup passes the former raw AV2 stop. After7.875s it stops
while GPU ISR13B210 uses an unpublished original kernel stack at80041BD4.
Original interrupt-stack ownership/publication is next; cold mode, full title
rendering and parent qualification remain unproven. The initial test correctly
triggered the encoder standard-read guard before EN_REG_RD programming; only
fixture ordering changed. No fabricated hardware result, physical Xbox
operation, playback or attract rerun was introduced. Default HLE is unchanged.

Private reports: `thread-av-corrected-qualified-20261011.aot-private.json` and
`thread-av-title-startup-20261011.aot-private.json`, with exact source/binary
identities and unchanged ROM/EEPROM inputs. Family count remains4/9.
