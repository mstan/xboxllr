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
  completion clear the early AV waits; and PGRAPH software notifications plus flip/vblank
  completion finish the boot animation. `xbmc.exe` now has a live user thread.
- **Immediate burn-down path:** continue the running XBE past the OHCI empty-bus floor →
  implement the first required OHCI TD/XID or NIC path → emit the first software-rendered
  framebuffer → extend the recorded method subset into the software rasterizer.
- **Not started:** A5 end-to-end retail-dashboard navigation and Arc 2 game work.

## Where the boot stands RIGHT NOW

The full chain works: 2BL (SMC handshake → SHA-1 → RC4 → LZX) → kernel entry 0x80010000 →
HAL/memory/executive init → phase-1 device init (interrupt-driven SMBus, EEPROM at 0x54) →
**FATX HDD mounted over our IDE controller** (13,121 sectors DMA-read, 419 IRQ-14
completions) → **dashboard XBE mapped** (`LOAD symbols: xbmc.exe` + 16 section notifications
via kernel debug service int 0x2D).

**Current frontier:** the AV/display worker now finishes, `xbmc.exe` creates a fourth KTHREAD,
and dashboard user code executes. The dependency chain peeled this session is:

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

## How to build & run

```sh
export PATH=/c/msys64/mingw64/bin:$PATH        # MSYS2 mingw64 toolchain
cd /f/Projects/xboxlle/xboxlle
# First configure only (CMake 4 needs the policy flag for old deps):
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j
ctest --test-dir build                         # 8/8 (ring, jsonl, nv2a, apu, aci, smc, ohci, spike)

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
  still have no CP/raster side effects, and there is no software rasterizer yet.
- APU has the load-bearing voice-list, VOICE_OFF, timed idle-voice FE trap, notifier, and IRQ
  lifecycle, but still no DSP/audio synthesis. ACI advances real PCM descriptors and IRQs
  headlessly but produces no host audio.
- OHCI implements the standard register/frame/HCCA/IRQ floor with no attached devices.
  Endpoint/TD execution and XID peripherals remain.
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

1. Continue beyond the 6.1B fault-free XBE checkpoint until the next loud dependency.
   Expect OHCI transfer descriptors/XID input, NIC at 0xFEF00000, or another SMC register.
2. Extend OHCI only when exercised: control/bulk TD execution first, then the Xbox XID
   controller protocol and host input mapping.
3. NV2A framebuffer: get the first frame out (PCRTC fb base +
   PRAMDAC; software path). That is the A4 deliverable boundary.
4. Longer term (A4 proper): extend the landed recorder into the required CP/method subset and
   software rasterizer (docs/ROADMAP.md).
5. Keep validating modeled values against the live console; reconfirm xemu-sourced defaults.

## Session hygiene

- Update STATUS.md (newest entry first) each milestone; commit per milestone with the
  existing message style (`git log --oneline`).
- Temp payloads/scripts go in the session scratchpad, not the repo.
