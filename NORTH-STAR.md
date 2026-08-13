# NORTH-STAR

> One active goal. One next milestone. Nothing else lives here.
> On a pivot, edit THIS file — never leave the old goal in a roadmap and the new one in a
> status log. If this file and reality disagree, that is a bug to fix before writing code.

## The project, in one sentence

Build a **faithful, LLE-first Xbox platform** — booting the real MCPX → 2BL → Microsoft
kernel — whose first product is the **completely navigable, functional Xbox dashboard**,
proven against real hardware, with a game (Blinx) as the second arc.

## The two arcs

- **Arc 1 (now): the real Xbox dashboard, fully navigable.** Boot the real firmware LLE,
  launch and run `xboxdash.xbe` from the real HDD image, and make the whole dashboard work
  end-to-end — settings, clock, memory manager, music — cosim/hardware-validated.
- **Arc 2 (after Arc 1): Blinx: The Time Sweeper**, in its own `BlinxLLE` repo, on the
  proven platform, with opt-in HLE fast paths and Blinx-specific enhancements.

## The ONE current goal

**Arc 1 / A4 - make the stock dashboard interactively navigable.** The real firmware and
kernel boot, FATX loads the stock dashboard image from the maintainer-supplied HDD image, and
guest-owned NV2A output is visible through SDL3/OpenGL. The current bottleneck is speed: reach
at least 10 presented fps during stock-dashboard navigation while preserving exact replay
hashes and the software/LLE fallback.

## The ONE next milestone

**Reach an exact, demonstrable stock-dashboard run at >=10 presented fps.** The best retained
opt-in path is still below target, so the next milestone is a validated performance step that
removes real dashboard render/surface work without accepting image-hash drift. Completion means
a bounded stock-dashboard replay and a live/tentative SDL run sustain the target with no
host-rendered dashboard substitute, replacement kernel, synthetic USB poll, or copyrighted asset
checked into the repo.

## Not-yet (explicitly deferred, do not drift into these)

- Blinx or any game (that is Arc 2).
- Replacing the current 2BL injection boundary with authentic MCPX cold boot (a later
  faithfulness pass).
- HLE fast paths and enhancements (opt-in, and only after the faithful floor is proven).
- Static x86→C export / "native port" artifacts (not this project — see `n64lle` vs
  `n64recomp`).
