# PROGRESS — pick up here

> Session-pickup document for ANY agent (Claude or Codex) with zero prior context.
> Read this first, then STATUS.md (newest entry first), then docs/ROADMAP.md + docs/DESIGN.md.
> STATUS.md stays the single source of "what is true"; this file is the operational
> quick-start + the precise cursor of where the boot currently stops.

## What this project is

**xboxlle** — an LLE-first, dynamic-engine (NOT static-recomp) faithful original-Xbox
emulator. CPU floor = Unicorn (embedded QEMU x86 core, 2.1.1, via FetchContent). Devices
harvested from xemu, silicon-corrected against a live console. GPL-2.

- Framework repo (git, branch `master`): `F:\Projects\xboxlle\xboxlle`
- Workspace parent (NOT a repo): `F:\Projects\xboxlle\` — holds `refs/` (xemu, nxdk,
  XboxBiosTool, fatx clones) and `machine/` (git-ignored copyrighted dumps: flash, decrypted
  2BL/kernel, eeprom.bin, 8GB FATX xbox-hdd.img with C:\xboxdash.xbe = XBMC softmod).
- Phase: **Arc 1, A4** (boot device → dashboard). A2e complete. See docs/ROADMAP.md.
- Private upstream: `https://github.com/mstan/xboxlle` (`origin`; branch `master`).

## Arc 1 burndown

- **Done:** A0 foundations; A2 live-kernel boot, phase-1 device init, FATX mount, and
  dashboard XBE mapping.
- **Partial / validation debt:** A1's real-hardware probe channel is live, but deterministic
  xemu-TCG cosim remains deferred. A3 uses Unicorn's dynamic translator, but the planned
  page-versioning proof is not complete.
- **In progress:** A4. Storage loads the XBE; PFIFO semaphore DMA and timed APU voice
  completion clear the early AV waits; PGRAPH software notifications plus flip/vblank
  completion finish the boot animation; `xbmc.exe` initializes NVNet and transmits through
  its descriptor engine; guest-programmed scanout can be dumped/presented; and Kelvin
  color/depth/stencil clears, indexed vertex DMA/fetch/assembly, and programmable vertex
  shaders plus the exercised fixed-function transform subset now execute. The first
  indexed and inline vertices now traverse the same programmable/fixed transforms. The
  landed triangle profiles write the dashboard's pitch-Z16 depth prepass, dominant
  untextured swizzled radial-combiner pass, and both witnessed indexed 256x256 textured
  A8R8G8B8/Z24S8 passes, the indexed 1280x480 pitch-textured and
  pitch-projected passes, plus the inline path's pitch A8R8G8B8 projected-texture
  additive-blend pass.
- **Immediate burn-down path:** census and execute the remaining low-frequency draw states
  → plumb host input into the landed XID controller → drive end-to-end dashboard navigation.
- **Not started:** A5 end-to-end retail-dashboard navigation and Arc 2 game work.

## Where the boot stands RIGHT NOW

The full chain works: 2BL (SMC handshake → SHA-1 → RC4 → LZX) → kernel entry 0x80010000 →
HAL/memory/executive init → phase-1 device init (interrupt-driven SMBus, EEPROM at 0x54) →
**FATX HDD mounted over our IDE controller** (13,121 sectors DMA-read, 419 IRQ-14
completions) → **dashboard XBE mapped** (`LOAD symbols: xbmc.exe` + 16 section notifications
via kernel debug service int 0x2D).

**Current frontier:** the AV/display worker now finishes, `xbmc.exe` creates a fourth KTHREAD,
and dashboard user code executes through NIC initialization and its first TX kick. The
dependency chain peeled this session is:

1. spun on APU VP `NV1BA0_PIO_FREE` → fixed (returns 0x80, xemu semantics)
2. spun on PFIFO idle → fixed (RUNOUT/CACHE1 status read EMPTY)
3. blocked on interrupt-driven SMBus to the TV encoder → fixed (CX25871 slave at 0x45)
4. spun polling VGA input-status-1 (PRMCIO 0x3DA) retrace bit → fixed (sub-frame phase)
5. spun at 0x800570d4–0x800570dc waiting for RAM 0x03fb3000 to advance → fixed: the former
   DMA_GET=DMA_PUT sink discarded the kernel's NV097 semaphore releases. PFIFO now parses and
   records the stream, resolves RAMHT/DMA objects in PRAMIN, and writes semaphore values to
   unified guest RAM. Verified: 158,966 methods, 14 releases (5..0x1f), old loop exited.
6. the 0x800637d3–0x800637da APU voice-idle wait → **fixed**. VOICE_ON/OFF now maintain
   guest voice state/lists, the 187.5 Hz APU frame emits SE2FE_IDLE_VOICE, the FE trap and
   IEN/ISTS lifecycle drive IRQ 5, and FECTL release consumes the decoded trap. The retail
   kernel executes its real completion routine before freeing voice 64.
7. the two-buffer throttle at 0x80052f71 → **fixed**. Nonzero NV097 NOPs now raise PGRAPH
   software-method notifications; the kernel schedules the flip, vblank writes
   NV_PGRAPH_INCREMENT, and PFIFO resumes the deferred semaphore. The counters advance only
   through the guest's real ISR path.
8. ACI descriptor completion was implemented and tested, but proved unrelated to the
   throttle. It remains as functional headless PCM output with routed IRQ 6.
9. after 485 completed flips the animation exits; SMC fan readback 0x10 and a standard
   disconnected-bus OHCI floor carry the dashboard into user code. A 6.1B run is fault-free
   at EIP 0x0011f09e with four KTHREADs and OHCI0 operational.
10. dashboard D3D rebuilds PFIFO with PUT temporarily ahead of GET → **fixed**. CACHE1
    pusher enable/mode/channel gates are honored, GET/PUT translate through the selected DMA
    object, and consumed GET is mirrored into USER and PFIFO readback.
11. the first NIC access at 0xFEF00190 → **fixed through initialization**. NVNet now has a
    strict register aperture, PHY/MDIO/link lifecycle, ring-pointer reset, W1C status, and
    IRQ4 aggregation. A 6.35B run completes 13 MDIO reads + one write and stops loudly on
    the first enabled TX descriptor kick at EIP 0x002d74a5.
12. the enabled TX kick → **fixed** with descriptor DMA, multi-fragment assembly, guest
    writeback, ring wrap, IDLE, TX status/IRQ, and an explicit headless packet sink. The
    dashboard's first kick finds an empty ring (zero VALID descriptors), then a 6.4B run
    reaches its budget fault-free in `KiIdleLoop`.
13. first framebuffer readout → **fixed through scanout/presentation**. PCRTC_START, PRMCIO
    CRTC pitch/depth, and PRAMDAC dimensions/format resolve a validated unified-memory
    surface to RGBA; `--frame-dump` writes PPM and `--window` presents it through SDL3. At
    6.4B the guest selects 720x480x32 at `0x03c8c000`, pitch 3072. The frame is black because
    the recorded NV097 clear/draw stream still has no raster side effects.
14. first raster operation → **fixed through Kelvin surface clears**. RAMHT object/class
    binding gates PGRAPH execution; color/zeta DMA contexts, pitch surfaces, masked color,
    Z16, and Z24S8 clear semantics write validated unified-memory rectangles. At 6.4B, 852
    color and 1,968 zeta clears execute fault-free; the final scanout contains the guest's
    uniform RGB(1,0,1) clear. Geometry remains the next boundary.
15. dominant indexed geometry front-end → **fixed through fetch and assembly**. Vertex DMA
    A/B, all 16 array slots, six Xbox formats, `BEGIN_END`, `ARRAY_ELEMENT16/32`, and
    `DRAW_ARRAYS` now validate and decode guest memory. At 6.4B, 436,456 batches submit
    48,554,942 vertices, perform 99,763,024 float-attribute fetches, and assemble 45,997,592
    triangles across triangle/list/strip/fan modes. Both indexed transform modes now execute;
    raster writes are the next boundary, and 485 `INLINE_ARRAY` batches remain recorder-only.
16. programmable vertex transform → **fixed through Cheops execution**. The 136-slot
    program store, 192 float4 constants, load/start pointers, viewport aliases, and execution
    mode feed the pinned NV2A CPU interpreter. At 6.4B, 292,866 programmable batches execute
    35,477,464 vertices with deterministic oPos hash `18b38c709acba547`.
17. fixed-function vertex transform → **fixed for the full exercised subset**. Matrix,
    material, lighting, light-mask, texgen, and texture-matrix state are tracked. All 143,590
    fixed batches use skinning off, no active lights/texgen/texture matrices, and one of two
    implemented lit/unlit color paths. The full 48,554,942-vertex position hash is
    `e7d742624c1f1793`.
18. first triangle raster path → **fixed for the untextured pitch-Z16 depth prepass**.
    Triangle lists/strips/fans use fixed-coordinate top-left coverage, culling, polygon
    offset, clip rejection, and LEQUAL D16 writes through zeta DMA. At 6.4B, 142,842
    batches rasterize 5,243,066 front-facing triangles and write 139,247,115 depth samples
    with hash `9424b7ed8661f321`. Programmable screen postprocessing corrects the combined
    48,554,942-vertex position hash to `b1bdfde660324e2e`; color draws remain the frontier.
19. dominant untextured color profile → **fixed through swizzled A8R8G8B8/Z24S8
    rasterization**. Morton-addressed color/zeta surfaces, perspective-correct smooth
    varyings, the exact radial three-stage combiner, alpha/blend, LEQUAL, and stencil-
    preserving Z24 writes execute. At 6.4B, 155,372 batches cover 4,107,334,135 fragments
    and commit 727,241,984 paired writes with hash `b57945aef12a9289`. Its last target is
    `0x02f8f00c`, not final scanout `0x03c8c000`; textured draws are the visible-frame
    frontier.
20. next high-volume textured color profile → **fixed through two witnessed texture
    fetches and its eight-stage combiner**. Texture DMA A/B, offsets, palettes, borders,
    shadow compare, and final factors are retained in the draw census. Shader `0x10c60`
    disables stage 0, cube-remaps stages 1/2 into one 64x64 or 128x128 swizzled
    X8R8G8B8 image, samples them bilinearly with clamp-to-edge, and forces stage 3 to zero
    through shadow-NEVER. With the interleaved projected sibling also executing, all 2,394
    witnessed batches rasterize 30,198 triangles, perform 1,434,732 samples, and commit
    717,366 paired color/Z24 writes with hash `537c8e788aaf4e83`. This supersedes the
    standalone checkpoint: the sibling's earlier depth writes correctly reject later
    fragments, and the shared combiner now clamps its weighted intermediate exactly.
21. lower-volume projected textured profile → **fixed through three bilinear samples and
    the seven-stage dot-product combiner**. Shader `0x10c61` PROJECT2D-divides stage 0 by
    interpolated W and repeat-samples a 128x128 swizzled A8R8G8B8 image; stages 1/2 retain
    the cube-remapped shared X8 image and stage 3 remains shadow-NEVER. At 6.4B, 204 batches
    rasterize 10,368 triangles, perform 2,044,719 samples, and commit 681,573 paired writes
    with hash `117e65d4ec81492a`. Earlier depth/radial/transform hashes remain unchanged;
    targets `0x0299f394` and `0x029a2980` are offscreen and final scanout stays the clear.
22. sparse inline-array geometry → **fixed through interleaved decode, transform, and
    primitive assembly**. `INLINE_ARRAY` derives component-aligned slot offsets from the
    active array formats, decodes all six supported Xbox formats without DMA, preserves
    disabled/last attribute values, and submits into the existing programmable/fixed
    transform path. At 6.4B, all 485 batches consume 5,820 words into 1,455 vertices and
    2,910 attribute decodes, bringing totals to 436,941 batches, 48,556,397 vertices, and
    45,998,077 triangles with position hash `2824162d794cc5de`. Existing raster hashes and
    final scanout are unchanged. The inline states expose the next profile: pitch
    A8R8G8B8, one projected texture, source-alpha/ONE additive blending, initially ALWAYS
    depth, and no depth writes.
23. inline pitch/additive color profile → **fixed through projected sampling, alpha,
    blending, and read-only depth**. PROJECT2D-divided coordinates bilinearly sample a
    256x256 swizzled A8R8G8B8 texture with clamped U/V, the one-stage combiner multiplies
    texture RGBA by c0, GEQUAL 1 rejects zero alpha, and SRC_ALPHA/ONE ADD saturates into the
    pitch target. The later dashboard state switches the unused P address mode, supplies an
    inert opaque border, and changes depth from ALWAYS to read-only Z24S8 LESS; both pass and
    reject preserve depth and stencil. At 6.4B, 113 batches rasterize 226 triangles and
    43,083,242 fragments, perform 39,818,347 texture samples, pass alpha 38,224,887 times,
    and commit 15,123,585 color-only writes with hash `6363f356e93c6124`. Prior raster,
    transform, and final scanout hashes remain unchanged; the last inline target
    `0x0387fb78` is still offscreen from final scanout `0x03c8c000`.
24. OHCI TD execution and Xbox Controller S → **fixed through enumeration and endpoint
    I/O**. The scheduler walks periodic, control, and bulk ED lists; transfers bounded
    one/two-page TD buffers; retires condition codes/toggles; writes HCCA done heads; and
    raises the standard completion/root-hub interrupts. OHCI0 port 1 exposes a full-speed
    Controller S with standard USB and XID descriptors, control requests, injectable
    20-byte input reports, and 6-byte rumble output. Focused coverage verifies descriptor,
    address/configuration, done-head, IRQ, exact input bytes, and rumble. A fault-free 6.4B
    retail replay performs four control requests across 10 TDs and configures address 1;
    an 8B replay remains fault-free with the same counts and all prior raster/scanout hashes
    unchanged. This dashboard boot window does not yet poll endpoint 2; host input plumbing
    remains.
25. NVNet receive DMA and external packet transport → **fixed through retail ingress**.
    RX checks the enabled/link/DMA gates, applies the witnessed broadcast/multicast/MAC
    filter, consumes one AVAIL descriptor per frame, bounds and writes the guest buffer,
    stores exact length plus flags `0x0011`, advances/wraps the high-half-sized RX ring,
    restores IDLE, and raises RX IRQ `0x02`. An opt-in nonblocking UDP tunnel carries one
    complete Ethernet frame per datagram and honors guest backpressure at a 1 kHz guest-time
    poll. Focused coverage proves DMA, filtering, drops, wrap, IRQ, and a loopback datagram.
    A UDP-backed 6.4B replay admits a queued 42-byte broadcast ARP frame through the retail
    driver's ring (`RX offer/desc/pkt/bytes=1/1/1/42`), clears the IRQ through the guest,
    reaches budget fault-free, and preserves every prior raster/scanout hash.
26. dominant indexed pitch-textured family → **fixed through linear color/Z24 output**.
    Shader `0x10c60` reuses the validated two cube samples and eight-stage combiner with
    per-state stage-6 RGB weights; its enabled PROJECT3D Y16 shadow stage is output-inert
    because texture 3 is not referenced by the combiner. At 6.4B, 121,296 batches
    rasterize 4,464,588 triangles, perform 833,192,036 samples, and commit 416,596,018
    paired pitch color/Z24 writes with hash `78a9682b85e41f92`. Recorder-only batches fall
    from 136,016 to 14,720. Prior independent hashes and final scanout remain unchanged;
    the shared-destination inline hash re-baselines to `610e9157ec10a027` with its write
    count unchanged.
27. indexed pitch-projected family → **fixed through three-sample linear output**.
    Shader `0x10c61` PROJECT2D-samples stage 0, cube-remaps stages 1/2, applies dynamic
    stage-5 RGB weights, and writes pitch A8R8G8B8/Z24S8. The later opaque-black border
    is inert under REPEAT addressing and is covered explicitly. At 6.4B, 10,336 batches
    rasterize 1,693,723 triangles, perform 695,540,724 samples, and commit 231,846,908
    paired writes with hash `4ebdb2f4475cd80c`. Recorder-only batches fall from 14,720
    to 4,384. Independent raster/transform/scanout hashes remain unchanged; shared pitch
    hashes re-baseline under the newly interleaved depth writes.

## How to build & run

```sh
export PATH=/c/msys64/mingw64/bin:$PATH        # MSYS2 mingw64 toolchain
cd /f/Projects/xboxlle/xboxlle
# First configure only (CMake 4 needs the policy flag for old deps):
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j
ctest --test-dir build                         # 9/9 (ring, jsonl, nv2a, apu, aci, smc, ohci, nvnet, spike)

cd build
./xlle.exe --boot ../../machine/flash-dump-ff000000.bin \
           --run2bl ../../machine/bios-extract/bldr.bin \
           --hdd ../../machine/xbox-hdd.img \
           --eeprom ../../machine/eeprom.bin \
           --max-insns 500000000
```

External Ethernet is opt-in. Add
`--net-udp-bind <local-port> --net-udp-peer <host> <peer-port>` to carry one complete
Ethernet frame per UDP datagram. This is a raw layer-2 tunnel, not NAT or an IP shim; pair
it with another tunnel endpoint or a bridge that preserves Ethernet frames.

500M insns takes roughly 50–55 seconds with the landed radial and textured software raster
paths. The run
prints rich diagnostics:
KTHREAD scan + every-waiter wait-block/stack walk, per-thread instruction attribution,
non-idle PC ring, last-64 I/O + MMIO rings, IDE/NV2A/IRQ counters, code bytes around final
EIP, and decoded kernel debug services (`[dbg] DbgPrint:`, `[dbg] LOAD symbols:`).

## The bring-up loop (how work proceeds)

Run → the machine fault-stops loudly on the first unmodeled register/port/command (or a
thread visibly spins) → characterize (against the live console when possible, else xemu as
boot witness) → model faithfully → rerun. Never a silent default; every xemu-default-0 or
stand-in is documented in the header of the device that carries it.

Deliberate documented stand-ins currently in place (fine for boot, replaced later in A4):
- NV2A PFIFO is a synchronous recorder with semaphore DMA execution; most recorded methods
  still have no CP/raster side effects. Guest-programmed framebuffer selection, host
  readout/presentation, Kelvin pitch-surface clears, and indexed vertex fetch/primitive
  assembly plus programmable and exercised fixed-function transforms work. The witnessed
  pitch-Z16 depth-only, untextured swizzled radial-color, and both witnessed indexed
  shader-`0x10c60`/`0x10c61` textured triangle profiles rasterize. Inline arrays execute
  through transform/assembly and their pitch/additive color profile writes guest color.
- APU has the load-bearing voice-list, VOICE_OFF, timed idle-voice FE trap, notifier, and IRQ
  lifecycle, but still no DSP/audio synthesis. ACI advances real PCM descriptors and IRQs
  headlessly but produces no host audio.
- OHCI0 attaches a deterministic Controller S on root port 1. Periodic/control/bulk ED
  scheduling, TD buffer transfer/retirement, HCCA done writeback, standard enumeration,
  XID descriptors, injectable input reports, and rumble output work. The retail dashboard
  enumerates and configures it; host gamepad mapping and an observed retail endpoint-2 poll
  remain. OHCI1 remains empty.
- NVNet implements the register/PHY/MDIO/link/IRQ floor plus TX and RX descriptor engines.
  An opt-in nonblocking UDP layer-2 tunnel provides external packet transport; without it,
  transmitted packets are explicitly counted and discarded. Higher-level NAT/bridging is
  intentionally outside the device model.
- PIT tick cadence approximated from retired instructions (1 IPC assumption).

## Hardware oracle (live console)

Xbox v1.1, Conexant encoder, softmod XBMC. `xlle-agent v0.2` on `192.168.4.81:4380`.
Driver: `python tools/xbox_agent.py --host 192.168.4.81 {ping|info|read|exec <file.bin>}`.
- EXEC runs a small int(void) payload, returns `RET <eax hex>`. Payload generators from this
  session: `pci_probe.py` (PCI config reads) and `smc_read.py` (SMBus reads) in the session
  scratchpad — regenerate as needed; both validated against known registers.
- STRICTLY one serialized command at a time; NEVER parallel-subagent the console.
- Agent blocks kernel-space reads; MMIO + low RAM fine. Ask the maintainer before assuming
  the console is powered on.

## Key kernel addresses (this image, retail 4983)

- KPCR 0x8003da3c; PrcbData.CurrentThread at +0x28 (phys 0x0003da64, polled by diagnostics).
- Idle thread KTHREAD 0x8003d8f0; phase-1 0x80001bf8 (aka 0xd0008bf8); AV worker 0x80001a98.
- Idle loop 0x8001e3ce; clock ISR 0x80035630; IDT base 0x8003dcc0.
- HAL SMBus interrupt-driven wait ~0x800176e9-0x8001783a; KeStallExecutionProcessor loop
  0x80056ca8-0x80056cb9; resolved AV fence spin 0x800570dc; resolved APU completion spin
  0x800637d3-0x800637da; resolved flip throttle caller 0x80052f71 (counters
  0x80061998/0x80061fe0). First observed dashboard user EIPs include 0x0046671d and
  0x0011f09e.
- KTHREAD offsets (nxdk xboxkrnl.h): State +0x2c, Priority +0x32, KernelStack +0x24,
  WaitBlockList +0x58; KWAIT_BLOCK: Thread +0x08, Object +0x0c, Next +0x10.
  States: 1=Ready 2=Running 4=Terminated 5=Waiting.
- Kernel VAs identity-map 0x80000000→phys 0 (+ 4MB large pages); thread stacks live at
  0xD0000000 (NOT identity — use `CpuFloor::translate()`; uc_mem_read/write are physical).

## Hard rules

- Faithful LLE floor + opt-in HLE. Dynamic engine. No static recomp. No HLE-kernel. No
  stubs. Fault-stop loudly on anything unimplemented (documented xemu-default-0 exceptions
  only). Real hardware is the oracle; xemu is a reference/witness, not truth.
- Most complete option always wins over the faster/surgical shortcut.
- Always-on ring buffers for observability; never arm-then-run or pause/step to observe.
- Copyrighted bytes stay in `machine/` (workspace parent), never committed.
- `.gitignore` gotcha: the rule must stay `/machine/` (root-anchored) or `src/machine/`
  silently vanishes from git.

## Next steps (priority order)

1. Census and execute the remaining low-frequency Kelvin draw states; the indexed/inline
   submission paths and every currently witnessed high-volume raster profile are landed.
2. OHCI TD scheduling and the Xbox XID controller protocol are landed. Plumb host input
   mapping when dashboard navigation begins exercising endpoint 2.
3. Exercise the landed NVNet UDP tunnel against a useful peer/bridge as navigation requires;
   RX descriptor DMA is proven through the retail driver.
4. Longer term (A4 proper): extend the landed recorder into the required CP/method subset and
   software rasterizer (docs/ROADMAP.md).
5. Keep validating modeled values against the live console; reconfirm xemu-sourced defaults.

## Session hygiene

- Update STATUS.md (newest entry first) each milestone; commit per milestone with the
  existing message style (`git log --oneline`).
- Temp payloads/scripts go in the session scratchpad, not the repo.
