# STATUS

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

## 2026-07-16 — A2e: kernel phase-1 runs to completion (SMBus completion IRQ)

The kernel now boots fully and runs **phase-1 device init to completion** — the phase-1 thread
terminates normally (KTHREAD scan: 0x80001bf8 State=4). System then idles with no dashboard
(that needs the boot device — next).

Cracked the idle-loop block by walking the kernel scheduler/thread structures (layouts from
nxdk's xboxkrnl.h): the phase-1 thread was blocked in KeWaitForSingleObject on a local
notification event, set up by the HAL's **interrupt-driven SMBus read** (transaction started
with GE_HCYC_EN → protocol 0x1a). Our AMD756 completed synchronously and never raised the
completion IRQ → the ISR never signalled the event. Fix: AMD756 now raises a level-triggered
completion interrupt on **IRQ 11** (silicon: LPC cfg 0x64=0x0b0c → SMBus IRQ 0x0b, SCI IRQ
0x0c), deasserting when the ISR clears status. Wired through the PIC.

- Modelled the SMC registers phase-1 reads, all silicon-confirmed: VER "P11" (reg 0x01, cycles
  0x50 0x31 0x31), INTSTATUS 0 (0x11). SMC writes to output-control regs (LED 0x07/0x08, fan
  0x05/0x06, tray, INTEN) now accept+ignore (xemu-style); POWER (0x02) stays a loud stop.
- Observability added to --run2bl: KTHREAD scan (state/priority), wait-object + kernel-stack
  walk using a new `CpuFloor::translate()` (32-bit page-table walk) to read the 0xD0000000
  kernel-stack mapping.
- **Next (A4):** IDE/ATA controller + attach `machine/xbox-hdd.img` (FATX) so the kernel mounts
  the disk and launches `xboxdash.xbe`; then NV2A rendering to display it. ctest 3/3.

## 2026-07-16 — A2e: the real kernel is ALIVE and services timer interrupts

The Xbox kernel now boots to a live, stable state and takes external interrupts. From reset the
whole chain runs: 2BL (SMC challenge → ROM disable → SHA-1 → RC4 → LZX) → kernel entry
0x80010000 → HAL init (SuperIO probe, PIC/PIT/RTC) → memory manager → executive/GPU init →
**KiIdleLoop with working timer interrupts** (250/251 IRQ0 pulses delivered; PIC ends clean,
irr/isr=0 — the clock ISR runs and EOIs).

- **Interrupt/timer stack** (`pic.*`, `pit.*`, `rtc.*`, `port61.hpp`, `open_bus.hpp`): dual
  i8259 (+ELCR, Xbox IRQ map, elcr_mask 0xFF), i8254 PIT (Xbox 1.125 MHz clock, ch0→IRQ0,
  clocked from retired instructions), mc146818 RTC (century 0x7F, 0x55/0xAA CMOS signature).
- **Manual IRQ delivery** (Unicorn has no native IRQ path): `inject_interrupt()` synthesizes the
  protected-mode interrupt-gate entry (IDT via IDTR, push EFLAGS/CS/EIP, clear IF, load handler
  CS:EIP). Three fixes made it work: (1) **page-table translation** — uc_mem_read/write bypass
  the MMU, so the virtual IDT/stack addresses must be walked to physical (`translate()`; this
  was the silent failure); (2) **IF-window delivery** — a code-hook stop fires the IRQ inside
  the idle loop's `sti;nop;cli` window, not at coarse chunk boundaries; (3) **peek-then-ack** —
  confirm inject succeeds before the PIC ack so a failed delivery can't wedge the line.
- **Silicon-confirmed values folded in**: host-bridge cfg 0x84 = 0x03FFFFFF (64 MB size
  register — the X-codes set this and we inject past them, so the kernel was defaulting to
  128 MB and walking the PFN DB off the end of RAM); SMC AVPACK = 0x06 (composite); 64 MB RAM
  aliasing (address bit 26 undecoded). All read from the live console via the agent.
- **NV2A** undecoded-aperture accesses now return xemu-default 0 + log (the 16 MB aperture is
  legitimately sparse) rather than faulting.

**Next:** KiIdleLoop does not yet progress to launching the dashboard — the scheduler isn't
switching to the phase-1 init thread (or it's blocked on a boot device we haven't provided:
IDE/HDD). Investigate thread scheduling / DPC delivery / boot-device path. Then A4 (NV2A/APU/
IDE/USB) toward the dashboard. ctest 3/3.

## 2026-07-16 — Oracle validation pass (console live): v1.1 PM config corrected; NV2A straps captured

With `xlle-agent` up (192.168.4.81:4380, kernel 1.0.4983.6), captured ground truth that closed
gaps and fixed a fidelity bug:
- **This board is a v1.1** and PM is decoded differently than I'd modelled. Silicon: LPC
  (00:01.0) cfg **reg 0x10 (BAR) = 0**, **reg 0x84 = 0x8001** (the v1.1 PMBASE alias),
  reg 0x00 = 0x01B210DE. I had reg 0x10 = 0x8001 (the v1.0 BAR path). The 2BL's
  `cmp [cfg 0x10], 0x8001` is a **board-revision detector** (v1.0 BAR-PM vs v1.1 reg-0x84-PM);
  we were forcing the v1.0 path on a v1.1 board. Fixed `pci.cpp` to match silicon → boot now
  takes the v1.1 path and still reaches the same SuperIO stop (16.24M insns), tests 3/3.
- **NV2A straps from silicon** (replacing xemu stubs/defaults): **PSTRAPS 0xFD101000 =
  0x861D4401** (xemu stubs this to 0; the 2BL discards it but the kernel may not), **PFB_CFG0
  0xFD100200 = 0x03070003**. Both now returned by `nv2a.cpp`.
- Agent read path re-confirmed byte-accurate (PMC_BOOT_0 = 0x02A000A3; RAM 0x10000 = "XBEH").
- Console access kept strictly serialized (single command stream; fragile box). Read-only PCI
  probes done via short cli/…/sti EXEC payloads.

## 2026-07-16 — A2c→A2e: 2BL boots end-to-end; the REAL KERNEL runs to HAL init

The I/O fabric turned the 2BL from an 8086-instruction dead-end into a full firmware boot: it
now runs the entire 2BL and **jumps into the real kernel, which executes ~16.24M instructions
of early/HAL init** before stopping at the first unmodelled device. Concretely, the 2BL:
SMC PIC challenge/handshake → MCPX ROM-overlay disable (LPC cfg 0x80) → SHA-1 hash (implicit
pass, real flash mapped) → RC4 decrypt → **LZX decompress of the kernel (~16M insns)** → jump
to kernel entry at 0x80010000. The kernel then read its own PCI identity (LPC rev 0xB2, PM BAR
0x8001), poked ACPI GPIO, and stopped at `out 0x2e` — the **LPC47M157 SuperIO** config probe.

- **CPU floor gained an I/O fabric** (`src/cpu/unicorn_floor.*`, `include/xlle/cpu_floor.hpp`):
  `on_io` via UC_HOOK_INSN (IN/OUT), `map_mmio` via `uc_mmio_map`, and `request_stop` so a
  device can halt the machine loudly. New `IoEvent`; MMIO events reuse `MemEvent`.
- **Device fabric in the machine** (`src/machine/{io_device,mmio_device}.hpp`, `machine.*`):
  port-I/O + MMIO dispatch by range, **loud fault-stop on any unhandled port/addr** (never a
  silent no-op), each access fed to an always-on ring. `FaultFn` lets a device request the
  stop with exact context.
- **Devices modelled** (all harvested from xemu, verified vs source):
  - **AMD756 SMBus** (`smbus.*`) at I/O 0xC000 + **SMC (PIC16LC)** at addr 0x10 — the challenge
    reads 0x1c–0x1f = 0x52/72/ea/46, handshake completes.
  - **PCI config** (`pci.*`): 0xCF8/0xCFC mechanism + the 10 bus-0 functions with real
    identities; LPC PM BAR pre-set to 0x8001 (retail path), SMBus BAR to 0xC001. LPC cfg-0x80
    bit1 = MCPX ROM-overlay disable (no-op: flash always mapped, overlay is M9).
  - **NV2A shell** (`nv2a.*`): 16 MB aperture, engine-block map; PMC_BOOT_0=0x02A000A3 (matches
    silicon), PBUS PCI mirror, PSTRAPS. Value read at PSTRAPS is discarded by the 2BL.
  - **ACPI/PM** (`pm.*`) behind the LPC I/O BAR at 0x8000: PM1/GPE/GPIO + a **monotonic PM
    timer stand-in** (kernel stall/calibration loops progress; real 3.375 MHz cadence TBD).
- **Value policy for NV2A/PM** (documented in the headers): registers the boot reads are
  modelled with faithful values (hardware capture where available, else xemu); unmodelled
  registers inside a real block return xemu's default (0) **and are logged** (a debt to replace
  with hardware truth via the oracle); aperture holes fault loudly. xemu boots the same retail
  2BL with these defaults, so they are boot-correct witnesses.
- **Verified:** `ctest` still 3/3 (~0.1 s, no regression). `xlle --boot <flash> --run2bl <bldr>
  --max-insns 200000000` reaches kernel HAL init in ~16.24M insns, final eip 0x800357b8,
  fault-stop at `out 0x2e` (SuperIO).
- **Open / next (A2e):** LPC47M157 SuperIO (config-port device), then the functional
  interrupt/timer stack the kernel needs — **i8259 PIC (0x20/0xA0), i8254 PIT (0x40), mc146818
  RTC (0x70)** and interrupt delivery into Unicorn. Also to confirm against the oracle when the
  console is next on: PM BAR = 0x8001 for this v1.1 board (v1.1 also has a PMBASE alias at LPC
  cfg 0x84), and the real PSTRAPS / PM-timer rate.

## 2026-07-16 — FATX HDD image built + verified; xemu still deferred

- **`machine/xbox-hdd.img` (8GB FATX) BUILT and independently VERIFIED** (pyfatx): C: has
  `xboxdash.xbe` + `xboxdashdata` + Audio/Xbox.xtf; E: has TDATA/UDATA. A xemu-loadable HDD
  image with the real dashboard + saves. (Built via a subagent whose text reports were
  unreliable, but the filesystem check confirms the real content.)
- **xemu**: WSL build hangs in meson configure (deferred). Docker route attempted — Docker
  Desktop won't reach "running" without maintainer interaction (first-run prompt/login), so
  the container build is paused on the maintainer. Not critical (xemu boot-validation needs
  MCPX anyway).
- **A1/A2 inputs now in hand**: EEPROM ✅, validated flash ✅, **HDD image ✅**, hardware
  profile ✅. Only real remaining gate: MCPX (deferred; authentic-cold-boot only).

## 2026-07-16 — Flash VALIDATED; HDD pulled; NV2A expanded; xemu blocked

Ran tasks 1-3 (console access serialized). Results:
- **Flash BIOS VALIDATED** (structural, high confidence): offline analysis + local spot-check
  confirm `machine/flash-dump-ff000000.bin` is a genuine, complete, correctly-ordered 256KB
  retail-4983 flash — NV2A-ptr header (0xFF000009, LSB bit), MCPX table @0x70, 2BL @0x39E00,
  FBL @0x3D400, x86 GDT + reset jmp @0x3FFF0, all at documented offsets. **This is our
  `flash.bin`** (only open item: hash cross-check vs a reference DB that 403'd bots).
- **HDD files PULLED**: `C:` + `E:\TDATA` + `E:\UDATA` via FTP → `machine/hdd/` (2895 files,
  1020MB, 0 errors); MS dashboard (`xboxdash.xbe` + `xboxdashdata`) captured. NEXT: pack into
  a FATX HDD image.
- **NV2A expanded snapshot**: PCI `10DE:02A0`, rev A3, ~233MHz core, framebuffer @0x03EB4000,
  engine/PLL/PFIFO registers (`machine/probes/hw-2026-07-16.md`). Agent stayed healthy.
- **xemu build BLOCKED**: needs WSL sudo `apt` deps (meson/ninja/glib/pixman/SDL2/…). Not
  urgent — full boot-validation is gated on MCPX anyway (deferred).
- Agent handed back to MediaCenter (DASH); relaunch `xlle-agent` for future hardware work
  (v0.3 with HDD file-read planned so no MediaCenter/agent swap is needed).

## 2026-07-16 — Agent v0.2 (crash-guarded); flash dumped; NV2A characterized

- **v0.1 hung the console** on a read of the top-of-memory MCPX region (no fault guard);
  required a maintainer power-cycle. **v0.2** guards reads (MmIsAddressValid + MMIO/flash
  range whitelist, top-512B excluded), writes limited to RAM/NV2A. Verified: the fatal
  address now returns ERR, agent survives. Auto-redeployed on reboot; relaunched by maintainer.
- **Flash BIOS dumped by the agent itself** (256KB @0xFF000000 → `machine/flash-dump-ff000000.bin`,
  sha1 02798c98…) — so `flash.bin` won't need to come from the maintainer. **Pending
  validation** vs a known-good retail-4983 image / xemu boot before use (careful posture).
- **NV2A characterized from silicon**: chip rev A3 (`PMC_BOOT_0=0x02A000A3`); GPU core clock
  **≈233 MHz** (NVPLL decode — matches known spec); engine-enable / PFB / PTIMER / PLL
  values captured (`machine/probes/hw-2026-07-16.md`).
- **MCPX secret ROM confirmed not readable** from a running console (masked after boot) —
  stays a known per-revision image, deferred (only needed for authentic cold boot).

## 2026-07-16 — HARDWARE ORACLE LIVE: agent running, real CPU characterized

The "hook Claude to a real Xbox" endgame is working. `xlle-agent` launched on the console;
PC drives it hands-off over tcp/4380. Verified end-to-end:
- `PING`/`INFO` OK; read `kernel 1.0.4983.6` from live memory (matches EEPROM); read the
  agent's own XBE header (`XBEH`) out of guest RAM over the network.
- **Arbitrary code execution proven**: pushed x86 payloads that ran on the real CPU (a
  constant-return, then live `CPUID`).
- **CPU characterized from silicon** (see `machine/hardware.json` `cpu_measured`):
  GenuineIntel, sig **0x068A** = family 6/model 8/stepping 10 (PIII Coppermine); features
  fpu/tsc/msr/cx8/cmov/mmx/fxsr/**sse**; **no SSE2/SSE3** — confirms DESIGN §1 from hardware.
- **Flag-provenance cross-checked vs silicon**: `xor` → defined bits match, AF=0; `mul` →
  CF/OF defined match, undefined ZF/PF/SF/AF captured. The CPU floor's policy is consistent
  with real hardware; the oracle resolves exactly our undefined-flag questions.

This is the independent ground-truth oracle (PRINCIPLES §8) the whole plan leans on — now
operational. Launch is one manual step per session (this build's XBMC HTTP API can't
RunXBE); everything after is hands-off.

## 2026-07-16 — Hardware-control agent built & deployed (ready to launch)

- **nxdk toolchain working** in WSL Ubuntu (clang-18 + `llvm-*-18` symlinked to unversioned
  names, no sudo; build via `../build-nxdk.sh` + `MSYS_NO_PATHCONV=1`). hello sample + agent
  both build clean to `bin/default.xbe`.
- **`xlle-agent` v0.1 built and deployed** to the console at `E:\Apps\xlle-agent\default.xbe`
  (487 KB). TCP control server (tcp/4380): PING/INFO, guest-memory R/W, EXEC arbitrary
  payload, XBE launch, DASH-to-dashboard. Driver: `tools/xbox_agent.py`.
- **Not yet launched** — launching takes over the console screen; awaiting maintainer's go
  (they launch `xlle-agent` from MediaCenter, or I attempt XBMC HTTP `ExecBuiltIn(RunXBE)`).
  Once up, PC connects to `:4380` and drives it hands-off — the hardware-oracle tier is live.

## 2026-07-16 — Console onboarded over FTP; identified; EEPROM secured

- **Console found and reachable:** `192.168.4.81` on the LAN (XBFileZilla / XBMC). FTP
  read/write authorized by maintainer (no deletes).
- **Identified: Xbox v1.1**, kernel **1.0.4983.6**, 64 MB, NTSC / XBE region 01 (NA),
  Seagate ST310014A HDD, Samsung SDG-605B DVD, genuine MS MAC OUI `00-0D-3A`. Softmod
  (MediaCenter via `evoxdash.xbe`) — runs stock **retail** BIOS.
- **Firmware pinned:** v1.1 board → **MCPX v1.0** (matches xemu's recommended profile).
- **Secured into `machine/`** (git-ignored): `eeprom.bin` (256B, sha1
  eb2a1e82…; keys redacted from any output), the ConfigMagic info text, `hardware.json`
  (provenance-tagged), and the original MS dashboard exe `hdd/C/xboxdash.xbe` (Arc 1 target).
- **Still needed for A1/A2:**
  - `mcpx.bin` (standard **MCPX v1.0** — per-revision, not per-console) and a BIOS image
    (retail 4983 and/or an xemu-compatible one like Complex 4627). ConfigMagic can't dump
    BIOS; needs a flash tool or a standard image (maintainer owns the firmware).
  - A bootable HDD image: recommend selective FTP rebuild (`C:` + `E:\TDATA` + `E:\UDATA`)
    into a FATX image, vs. a physical raw dump.
  - nxdk homebrew probe channel (hardware oracle).

## 2026-07-16 — Arc 1 / A0 (Foundations) COMPLETE and verified

The A0 deliverable is met: the host runs and the CPU floor executes IA-32 instruction
fixtures under ring instrumentation, with **no Xbox devices attached**. Builds and runs with
no copyrighted firmware.

- **Repo layout:** `xboxlle/xboxlle` (framework), workspace parent `xboxlle/` holds reference
  clones out of tree.
- **Toolchain:** MSYS2 MinGW64 gcc 15.2 + Ninja + CMake; deps via FetchContent (Unicorn
  x86-only, SDL3). See `docs/BUILDING.md`.
- **CPU floor = Unicorn (confirmed).** The x86-32 fixture spike passes **7/7**
  (`mov`/`add`/`sub`/`xor`/`inc`/two-insn/mem-store), including flag-provenance handling
  (AF correctly excluded after `xor`). The from-scratch interpreter fallback was **not**
  needed — Unicorn is the floor. (`src/cpu/unicorn_floor.*`, `src/spike/*`.)
- **Observability skeleton works end-to-end.** Always-on rings (`include/xlle/ring.hpp`),
  JSONL emitter (`jsonl.hpp`), TCP debug server on 4370 (`src/obs/*`). Verified over the
  socket: `ping→pong`, `rings`, `query retired/mem` return JSONL windows (the `mov_store_mem`
  write showed up as `{addr:0x2000,size:4,value:0xdeadbeef}`).
- **Host shell (SDL3) works.** `xlle --window` opens/closes a window cleanly; SDL_gpu
  (Vulkan/D3D12) backends are available for the future presentation path. SDL3.dll is copied
  next to the exe.
- **Tests:** `ctest` 3/3 (ring, jsonl, spike-acceptance).
- **References cloned out of tree** (`../refs/`): **xemu** (full NV2A source — A4/A5 harvest)
  and **sp00nz/xboxrecomp** (kernel-ABI map — A2/A3 reference).

### Known A0-scope notes (honest, not blockers)
- `seq` is per-CpuFloor; in the spike each fixture uses a fresh floor so `seq` restarts. The
  real machine has one floor, so `seq` will be globally monotonic there.
- Instruction hook counts *dispatch*, not true architectural *retirement* (PRINCIPLES §6);
  precise retirement/restartable-fault boundaries are a later contract validated on hardware.
- x87/MMX/SSE state is not yet in `X86Regs` — added when a trace first needs it
  (PRINCIPLES §5).

## Blocked on (for A1 / A2)

The Tier-1 machine evidence package + the nxdk homebrew probe channel (`docs/INPUTS.md`):
`mcpx.bin` (v1.0), an xemu-bootable P0 `flash.bin`, `eeprom.bin`, `hardware.json`, `hdd.raw`,
and homebrew execution on the console. Everything after A0 needs real hardware/firmware to
have an oracle.

## Next action

A1 — Oracle + hardware harness: stand up the pinned xemu-TCG reference behind a determinism
gate and the nxdk probe agent, all on the one JSONL schema. Begins when the Tier-1 package +
probe channel are available.
