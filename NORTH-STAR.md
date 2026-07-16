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

**Arc 1 / A0 — Foundations.** Stand up the repo + doctrine, the host shell (SDL3), and the
observability skeleton (always-on rings + TCP debug server + one JSONL schema), and run the
**CPU-floor spike**: evaluate **Unicorn Engine** (embeddable QEMU x86 core with
instrumentation hooks) as the CPU floor, with a from-scratch checked interpreter as the
fallback. Deliverable: the host runs, the chosen CPU floor executes instruction fixtures
under ring instrumentation, with **no Xbox devices attached.**

## The ONE next milestone

**Arc 1 / A1 — Oracle + hardware harness.** A pinned `xemu`-TCG reference behind a
determinism gate, and an **nxdk homebrew probe agent on the real console**, all emitting the
one JSONL schema — so the engine, xemu, and real hardware are directly comparable before any
boot-differential work begins.

## Not-yet (explicitly deferred, do not drift into these)

- Blinx or any game (that is Arc 2).
- Authentic retail secure boot / MCPX cold-boot crypto (a later faithfulness pass; Arc 1
  uses the P0 development-parity BIOS).
- HLE fast paths and enhancements (opt-in, and only after the faithful floor is proven).
- Static x86→C export / "native port" artifacts (not this project — see `n64lle` vs
  `n64recomp`).
