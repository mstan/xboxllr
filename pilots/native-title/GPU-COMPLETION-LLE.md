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

The next actual-title checkpoint passes the former PCI-ROM stop, PLL/timer
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

Current executable SHA-256:
`31f949897d8d7a7e7d37f6a7482ece46e1732fbc0689332d4ec538728b0b7294`.
Current NV2A DLL SHA-256:
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
