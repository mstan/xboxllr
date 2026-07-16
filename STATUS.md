# STATUS

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

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
