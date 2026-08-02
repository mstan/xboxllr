# xboxlle

A **faithful, LLE-first low-level emulation platform for the original Xbox** (2001,
IA-32 "Coppermine" Pentium III + NV2A). It boots the **real** MCPX boot ROM, 2BL, and
Microsoft kernel — no hand-written kernel, no HLE shell — and its first product is the
**real Xbox dashboard, fully navigable**, running on that real firmware.

It belongs to the recomp family (nesrecomp / psxrecomp / gbarecomp / gcnrecomp /
n64lle) and inherits its doctrine: **LLE-first, run the real firmware, validate against
ground truth, always-on ring buffers, no faked state, no stubs, artifacts are evidence
not authority.** Like its closest sibling **`n64lle`**, it takes the *low-level-emulation*
name rather than "recomp" on purpose: the Xbox guest is x86-on-x86-64, so this is a
**dynamic execution platform** (a CPU floor + JIT + harvested device models), **not** a
guest→C static recompiler. That distinction is why it is `xboxlle`, not `xboxrecomp`.

It is a from-scratch successor to `../xboxrecomp` (v1), whose HLE kernel (119 hand-written
thunks) failed the same way the PSX predecessors did: faked kernel state drifted from
real RAM → "thousands of swaps, black framebuffer, never a title screen." v1's own
`ARCHITECTURE-REVIEW.md` is this project's founding rationale.

## Strategy in one paragraph

Don't reinvent proven wheels; don't take shortcuts either. **Harvest** the hard,
already-solved parts and re-express them in our own modular, observable, hardware-correctable
architecture: the CPU from **Unicorn Engine** (embeddable QEMU x86 core, GPL-2), the
**NV2A / APU / device models** from **xemu** (GPL-2, rewritten modular + validated against
real hardware). **Borrow** the host layer outright (SDL3, Vulkan/OpenGL — pure presentation,
not faithfulness-critical). **Build ourselves** the parts that are the actual value: the
deterministic machine wiring, the observability/cosim/hardware-conformance harness, the
LLE-floor → opt-in-HLE framework, and the per-title packaging. Real **Xbox hardware** (via
a homebrew probe agent) is the ground-truth oracle; xemu is a reference we harvest from and
check against.

## The two arcs

- **Arc 1 — the real Xbox dashboard, fully navigable and functional**, booted LLE. The
  platform's first product; no game involved. (Mirrors gcnrecomp's GameCube main menu and
  psxrecomp's BIOS shell.)
- **Arc 2 — Blinx: The Time Sweeper**, the first game, on the proven Arc-1 platform, as its
  own per-title repo (`BlinxLLE`).

## Where to read

| Question | File |
|---|---|
| What is this, in 10 lines? | this file |
| The ONE current goal + next milestone | `NORTH-STAR.md` |
| How we work (the law) | `PRINCIPLES.md` |
| The architecture, cold-readable | `docs/DESIGN.md` |
| The arc/milestone ladder | `docs/ROADMAP.md` |
| What is true *right now* | `STATUS.md` |
| What the maintainer must supply (BIOS/dumps/hardware) | `docs/INPUTS.md` |

## Status

**Arc 1 / A4 — real firmware is producing visible LLE output.** The retail boot chain
reaches the dashboard XBE through the modeled CPU and Xbox devices. The guest's PFIFO/PGRAPH
stream now rasterizes into unified RAM, the retail kernel's flip queue drives PCRTC, and
SDL3 presents the resulting scanout. A deterministic 500M-instruction capture visibly shows
the early green Xbox boot-orb glow on the guest's 640x480 framebuffer—without a replacement
kernel, D3D interception, or host-rendered substitute.

The hardware floor is not complete: remaining low-frequency NV2A states, later boot/dashboard
frames, host input plumbing, timing refinements, and broader device accuracy are still A4
work. See `STATUS.md` for exact evidence and `PROGRESS.md` for the current burndown.

## Legal / distribution posture

No copyrighted bytes ever enter this repo. MCPX ROM, flash BIOS, kernel, EEPROM, HDD
images, and game discs are **maintainer-supplied, git-ignored, and hash-gated**, and come
from the maintainer's own console. Harvested code (Unicorn, xemu) is GPL-2; the whole is
GPL-2. Borrowed host libs (SDL3 zlib, etc.) are permissive and compose in.
