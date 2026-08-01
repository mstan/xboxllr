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
  triangle raster profile writes the dashboard's pitch-Z16 depth prepass.
- **Immediate burn-down path:** execute the dominant untextured swizzled
  A8R8G8B8/Z24S8 radial-combiner pass → add the exercised textured passes and
  `INLINE_ARRAY` →
  extend OHCI into TD execution and XID input → add NVNet RX and external transport.
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

500M insns takes roughly 25–30 seconds on the current host. The run prints rich diagnostics:
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
  pitch-Z16 depth-only triangle profile rasterizes; color/textured draws and inline arrays
  do not yet.
- APU has the load-bearing voice-list, VOICE_OFF, timed idle-voice FE trap, notifier, and IRQ
  lifecycle, but still no DSP/audio synthesis. ACI advances real PCM descriptors and IRQs
  headlessly but produces no host audio.
- OHCI implements the standard register/frame/HCCA/IRQ floor with no attached devices.
  Endpoint/TD execution and XID peripherals remain.
- NVNet implements the register/PHY/MDIO/link/IRQ floor and TX descriptor engine. With no
  host callback, transmitted packets are explicitly counted and discarded. RX descriptor
  DMA and external packet transport remain.
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

1. The indexed Kelvin paths and pitch-Z16 depth prepass are landed. Next execute the
   dominant untextured swizzled A8R8G8B8/Z24S8 radial-combiner pass using the shared
   coverage/depth foundation, then add the exercised textured passes and sparse
   `INLINE_ARRAY` path.
2. Extend OHCI: control/bulk TD execution first, then the Xbox XID controller protocol and
   host input mapping.
3. Add NVNet RX descriptor DMA and external packet transport when the guest or navigation
   path requires them.
4. Longer term (A4 proper): extend the landed recorder into the required CP/method subset and
   software rasterizer (docs/ROADMAP.md).
5. Keep validating modeled values against the live console; reconfirm xemu-sourced defaults.

## Session hygiene

- Update STATUS.md (newest entry first) each milestone; commit per milestone with the
  existing message style (`git log --oneline`).
- Temp payloads/scripts go in the session scratchpad, not the repo.
