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

## Where the boot stands RIGHT NOW

The full chain works: 2BL (SMC handshake → SHA-1 → RC4 → LZX) → kernel entry 0x80010000 →
HAL/memory/executive init → phase-1 device init (interrupt-driven SMBus, EEPROM at 0x54) →
**FATX HDD mounted over our IDE controller** (13,121 sectors DMA-read, 419 IRQ-14
completions) → **dashboard XBE mapped** (`LOAD symbols: xbmc.exe` + 16 section notifications
via kernel debug service int 0x2D).

**The current blocker (next thing to fix):** the kernel's AV worker thread (KTHREAD
0x80001a98) never finishes display-mode bring-up, so phase-1 (0x80001bf8, waiting on it)
never starts the XBE's entry threads (KTHREAD count stays 3: phase-1, AV worker, idle).
The worker's wait has been peeled one layer at a time this session:

1. spun on APU VP `NV1BA0_PIO_FREE` → fixed (returns 0x80, xemu semantics)
2. spun on PFIFO idle → fixed (RUNOUT/CACHE1 status read EMPTY)
3. blocked on interrupt-driven SMBus to the TV encoder → fixed (CX25871 slave at 0x45)
4. spun polling VGA input-status-1 (PRMCIO 0x3DA) retrace bit → fixed (sub-frame phase)
5. **NOW: spins at EIP 0x800570dc (eax=2, ecx=3 at budget end), 1.47B insns in the worker.**
   Not yet characterized. First move next session: run the boot (command below), look at
   `[2bl] code @...` around 0x000570dc, the `last 64 MMIO ops` ring, and the every-waiter
   wait-block walk to see what register/event it polls. It is almost certainly more NV2A
   display-path state (PCRTC/PRAMDAC/PRMCIO mode-set readback the shell returns 0 for).

## How to build & run

```sh
export PATH=/c/msys64/mingw64/bin:$PATH        # MSYS2 mingw64 toolchain
cd /f/Projects/xboxlle/xboxlle
# First configure only (CMake 4 needs the policy flag for old deps):
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j
ctest --test-dir build                         # 3/3 (ring, jsonl, spike)

cd build
./xlle.exe --boot ../../machine/flash-dump-ff000000.bin \
           --run2bl ../../machine/bios-extract/bldr.bin \
           --hdd ../../machine/xbox-hdd.img \
           --eeprom ../../machine/eeprom.bin \
           --max-insns 1500000000
```

~1.5B insns ≈ 2 sim-seconds ≈ 10 min wall. The run prints rich diagnostics at the end:
KTHREAD scan + every-waiter wait-block/stack walk, per-thread instruction attribution,
non-idle PC ring, last-64 I/O + MMIO rings, IDE/NV2A/IRQ counters, code bytes around final
EIP, and decoded kernel debug services (`[dbg] DbgPrint:`, `[dbg] LOAD symbols:`).

## The bring-up loop (how work proceeds)

Run → the machine fault-stops loudly on the first unmodeled register/port/command (or a
thread visibly spins) → characterize (against the live console when possible, else xemu as
boot witness) → model faithfully → rerun. Never a silent default; every xemu-default-0 or
stand-in is documented in the header of the device that carries it.

Deliberate documented stand-ins currently in place (fine for boot, replaced later in A4):
- NV2A **pushbuffer sink**: USER DMA_GET tracks DMA_PUT; commands are discarded. The A4
  finish line replaces this with a pushbuffer recorder + software rasterizer.
- APU/ACI are register-level shells (no DSP, no audio engine); streams RUN is logged.
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
  0x80056ca8-0x80056cb9; current AV spin 0x800570dc.
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

1. **Characterize the AV-worker spin at 0x800570dc** and model what it polls (bring-up loop).
   Expect a short chain of NV2A display-path registers; each is a small nv2a.cpp addition.
2. Repeat until phase-1 creates the XBE entry threads (KTHREAD count > 3; watch for user-space
   EIPs and new `[dbg]` traffic). XBMC will then start exercising kernel APIs broadly —
   expect new device faults (USB/OHCI at 0xFED00000/0xFED08000, NIC 0xFEF00000, more SMC).
3. NV2A framebuffer: once something writes pixels, get the first frame out (PCRTC fb base +
   PRAMDAC; software path). That is the A4 deliverable boundary.
4. Longer term (A4 proper): replace the pushbuffer sink with the recorder ring + software
   rasterizer (docs/ROADMAP.md).
5. Keep validating modeled values against the live console; reconfirm xemu-sourced defaults.

## Session hygiene

- Update STATUS.md (newest entry first) each milestone; commit per milestone with the
  existing message style (`git log --oneline`).
- Temp payloads/scripts go in the session scratchpad, not the repo.
