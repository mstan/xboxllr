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

**Planning.** No engine code yet. The plan was pressure-tested over two adversarial rounds
with an external reviewer, then reshaped by maintainer direction (fork/harvest over
from-scratch; arc structure). Awaiting final sign-off before Arc 1 / A0 begins.

## Legal / distribution posture

No copyrighted bytes ever enter this repo. MCPX ROM, flash BIOS, kernel, EEPROM, HDD
images, and game discs are **maintainer-supplied, git-ignored, and hash-gated**, and come
from the maintainer's own console. Harvested code (Unicorn, xemu) is GPL-2; the whole is
GPL-2. Borrowed host libs (SDL3 zlib, etc.) are permissive and compose in.
