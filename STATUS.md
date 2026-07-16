# STATUS

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

## 2026-07-16 — Plan committed; ready to kick off A0

- Repo `xboxlle/xboxlle` initialized and the plan committed. No engine code yet.
- Renamed from `xboxrecomp-v2` → **`xboxlle`** (LLE, not static recomp; avoids the misnomer
  and the collision with the public `sp00nznet/xboxrecomp`; mirrors the `n64lle` vs
  `n64recomp` split).
- Architecture converged with the maintainer over this session and pressure-tested in two
  adversarial review rounds with an external reviewer:
  - **Engine**: faithful LLE floor + opt-in HLE on top. Dynamic CPU engine **harvested** from
    **Unicorn** (QEMU x86 core, GPL-2) — CPU-floor spike in A0 confirms Unicorn vs a
    from-scratch checked interpreter. **No** static x86→C recompilation (that's `n64recomp`'s
    lane; incompatible with LLE + self-decrypting boot).
  - **Harvest, don't fork**: NV2A/APU/devices/boot logic harvested from **xemu** (GPL-2),
    rewritten modular + hardware-validated. Host layer (SDL3/Vulkan) borrowed outright.
    `sp00nz/xboxrecomp` is a supplementary reference (kernel-ABI map + dashboard intel).
  - **Oracle**: real Xbox hardware (nxdk homebrew probe) is the authority; pinned xemu-TCG is
    a fast reference (can share our harvested bugs, so hardware arbitrates).
  - **Boot**: run real MCPX X-codes from reset, defer only the RC4 crypto.
  - **Structure**: Arc 1 = fully navigable real dashboard; Arc 2 = Blinx (`BlinxLLE` repo).
- **Ready to start A0** (foundations + host shell + observability + CPU-floor spike) — needs
  no firmware or hardware.
- **Blocked for A1 onward on:** the Tier-1 machine evidence package + the nxdk homebrew probe
  channel (`docs/INPUTS.md`).

## Next action

Maintainer: (1) confirm go for A0, (2) begin assembling the Tier-1 dumps + homebrew probe
capability. On go, A0 starts: repo scaffolding, SDL3 host shell, ring/TCP/JSONL observability
skeleton, and the Unicorn-vs-interpreter CPU-floor spike.
