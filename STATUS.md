# STATUS

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

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
