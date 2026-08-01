# ROADMAP — xboxlle

Two arcs. Terse by design (the gcnrecomp shape): each milestone has a one-line intent, an
explicit **Deliverable**, and — once closed — a dated **DONE** stamp with the
machine-verifiable evidence that closed it.

**Global gates (every milestone):**
- Fault-stop on anything unimplemented; never a stub or plausible default.
- Every claim backed by a machine-verifiable JSON artifact that regenerates deterministically.
- Validated against **real hardware** (authority) and cross-checked against pinned xemu-TCG
  (reference), to a documented first divergence, behind the four validation gates.
- Crucibles gate subsystem admission: a subsystem's conformance crucible passes **before** the
  milestone that first relies on it.

## Burndown snapshot — 2026-07-31

| Milestone | State | Evidence / remaining gate |
| --- | --- | --- |
| A0 | **DONE** | Unicorn floor, host shell, rings/JSONL/debug server; suite now `ctest` 5/5. |
| A1 | **PARTIAL / DEFERRED** | Real-console agent and hardware probes work; pinned deterministic xemu-TCG cosim is still owed. |
| A2 | **DONE** | Retail 4983 kernel reaches executive/IO init, services IRQs, completes phase-1, mounts FATX, and maps the dashboard XBE. |
| A3 | **PARTIAL** | Unicorn's dynamic translator is the normal tier; page versioning and SMC/decryption invalidation proof remain. |
| A4 | **IN PROGRESS** | Boot animation/XBE launch, PFIFO, NVNet TX DMA, 720x480 scanout/readout, Kelvin clears, indexed fetch/assembly, both exercised vertex-transform modes, and the pitch-Z16 depth-only triangle prepass work. A 6.4B run executes all 48,554,942 indexed vertices and 139,247,115 depth writes fault-free. Color/textured triangle writes remain. Next: the dominant swizzled color pass, then textured draws/inline arrays, OHCI TD/XID input, and NVNet RX/transport. |
| A5 | **NOT STARTED** | Requires A4 dashboard execution and rendering. |
| Arc 2 | **NOT STARTED** | Explicitly out of current scope. |

---

# ARC 1 — the real Xbox dashboard, fully navigable

The platform's first product: boot the real firmware LLE and run `xboxdash.xbe` from the real
HDD image, fully navigable and correct. Mirrors gcnrecomp's GameCube menu / psxrecomp's BIOS
shell. The dashboard UI is a VRML97 scene + a JavaScript VM (intel from the sp00nz reference),
and it exercises nearly every subsystem — which is the point.

## A0 — Foundations
Repo + doctrine; host shell (SDL3: window, input plumbing, audio-out, GPU context);
observability skeleton (always-on rings + TCP debug server + one JSONL schema); harvest
scaffolding. **CPU-floor spike:** evaluate **Unicorn Engine** (embeddable QEMU x86 core +
hooks) vs a from-scratch checked interpreter; pick the floor on fidelity + hookability.
Reference clones (xemu, sp00nz/xboxrecomp) pulled read-only, out of tree.
**Deliverable:** host runs; chosen CPU floor executes instruction fixtures under ring
instrumentation, with **no Xbox devices attached**.

**DONE 2026-07-16:** Unicorn 2.1.1 selected; fixtures, host shell, observability rings,
JSONL/debug server, and the three-test suite pass.

## A1 — Oracle + hardware harness
Pinned xemu-TCG reference behind a determinism gate (pinned commit, TCG-not-KVM, single vCPU,
fixed firmware hashes, virtualized RTC, fixed input; repeated boots reproduce identical epoch
hashes). **nxdk homebrew probe agent on the real console**, emitting the one JSONL schema
(flags/CPUID/MSR/TSC/PCI reset values/SMBus/SMC/NV2A-register/cache probes). Hierarchical
epoch-hash cosim with checkpoint + binary-search.
**Deliverable:** engine, xemu, and real hardware all speak one schema; determinism qualified.
**Gate: no boot-differential work starts until this passes.**

## A2 — LLE boot to a live kernel
Run the real machine, deferring only the crypto:
- **A2a** Reset → deferred-crypto boundary: real MCPX 1BL ROM overlay + real X-codes
  (memory-controller/chipset init) + mode transitions; stop at the RC4 boundary.
- **A2b** Post-crypto 2BL CPU phase: inject decrypted 2BL bytes (tagged manifest); MTRRs,
  self-copy, paging, relocation.
- **A2c** 2BL platform init (**first multi-month section**): PCI config + reset values +
  W1C; MCPX southbridge; NV2A PCI identity + minimal register shell; SMBus + SMC; video
  encoder + thermal; IDE presence/config; RAM-timing latches; LDT; USB PCI presence; ROM
  disable. Each harvested from xemu, rewritten modular, hardware-corrected.
- **A2d** Kernel production & handoff: RC4 decrypt, LZX decompress, relocation/fixups,
  destructive-cleanup transitions in the trace contract, final CPU state at kernel entry.
- **A2e** Kernel init by observable transition: entry/early-HAL → memory manager (its own
  structures in real RAM, **no shadow state**) → interrupts/timers → dispatcher + first
  context switch → executive/IO init.
**Deliverable:** the real kernel reaches executive/IO-init with its own structures in real
guest RAM; cosim/hardware-clean through each transition.

**DONE 2026-07-16 (execution deliverable):** the retail kernel reaches executive/IO init,
services timer/device interrupts, completes phase-1 device initialization, mounts the FATX
HDD, and maps the dashboard XBE. The cross-engine A1 validation gate remains recorded debt.

## A3 — Fast tier online
Page-versioned dynamic recompilation running as the normal execution tier (Unicorn's
translator + our block-versioning/hook layer, or our own JIT if the spike chose the
interpreter), proven byte-exact vs the truth tier; SMC/decrypted-page invalidation exercised.
**Deliverable:** boot runs at usable speed; block versioning proven under real decryption/SMC.

## A4 — Dashboard's hardware: NV2A, APU, storage, input
Harvest from xemu, software-reference-first:
- **NV2A**: pushbuffer/PFIFO **recorder ring first**, then implement the CP/method subset the
  dashboard emits; software rasterizer = byte-exact reference; hardware-accelerated backend
  (SDL3/Vulkan) validated by frame-hash parity; `AvSetDisplayMode` + framebuffer.
- **APU**: headless-correct (all guest-visible registers/DMA/status/timing/IRQ; host output
  silenced — not a fake-success shim) + host audio-out.
- **Storage**: IDE/HDD + **FATX**, DVD, RTC.
- **Input**: USB (OHCI) + Xbox controller protocol.
**Deliverable:** the kernel can load and start `xboxdash.xbe`; boot animation + first
dashboard frame VRAM byte-compares vs hardware/xemu.

## A5 — Dashboard end-to-end  ← **ARC 1 finish line**
The real dashboard runs, fully navigable: settings, clock, memory manager, music player; the
VRML97 + JS-VM UI engine driven correctly; **save/load to HDD/MU byte-exact** (the milestone
that exposes any faked kernel state).
**Deliverable:** navigate the real Xbox dashboard end-to-end, cosim/hardware-validated. The
platform is proven faithful across the board before any game.

---

# ARC 2 — Blinx: The Time Sweeper

In a sibling `BlinxLLE` repo that pins the framework; takes BIOS + ISO → faithful, enhanced
Blinx. Same shape as Arc 1: the real kernel loads and runs an XBE — just the *second* one.

## B0 — Launch
The real kernel loads and starts `blinx.xbe`; incremental bring-up (`xbe-00..0n` style
staging where useful); fault-stop on anything unimplemented.
**Deliverable:** Blinx boots through CRT/init on the real kernel.

## B1 — Faithful to first playable
Extend NV2A/APU/input coverage as Blinx actually exercises them, each hardware-validated.
**Deliverable:** Blinx reaches gameplay, faithful, cosim/hardware-validated.

## B2 — HLE opt-in for performance
On the proven faithful floor, add opt-in HLE fast paths (kernel scheduler, direct-to-host-GPU
pushbuffer path, etc.), each validated against the faithful path + hardware.
**Deliverable:** playable performance with faithful-floor fallback intact.

## B3 — Blinx-specific enhancements  ← **ARC 2 finish line**
Opt-in widescreen, higher internal resolution, frame-rate — the faithful core preserved.
**Deliverable:** faithful, enhanced Blinx from BIOS + ISO.

---

## Cross-cutting (standing infrastructure, not milestones)
- **Crucibles gate admission**: paging/exceptions/split-page → before A2b; interrupt-shadow/
  PIC-PIT → before A2c/A2e; x87/MMX/SSE → before the trace that first hits each; PCI
  config-space → before A2c; NV2A method classes → before A4.
- **Hardware oracle from A0/A1** (nxdk probe agent) — the authority for cache/timing/
  retail-security/undefined-CPU behavior.
- **Always-on rings + one JSONL schema** across engine, xemu bridge, hardware probe.
- **Three firmware profiles** P0/P1/P2 (DESIGN §8).
- **Authentic retail secure boot** (MCPX secret ROM → 2BL RC4 → kernel, P2 profile,
  hardware-validated): a later faithfulness pass, off the Arc-1 critical path.
- **Reference resources** (out of tree): xemu (primary harvest), sp00nz/xboxrecomp (kernel-ABI
  map + dashboard/RenderWare intel), Cxbx-Reloaded (NV2A/kernel semantics reference).
