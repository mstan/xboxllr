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

**Arc 1 / A4 — running dashboard with visible output and input.** The real firmware and
kernel boot, FATX loads `xbmc.exe`, the boot animation completes through guest-driven NV2A
flip/vblank interrupts, and dashboard user code now executes. Finish the device paths the
dashboard actually exercises, produce the first faithful software-rendered framebuffer, and
make the dashboard controllable without replacing guest kernel or application logic.

## The ONE next milestone

**Sustain the dashboard XBE past hardware initialization and show its first frame.** Continue
from the 6.1B fault-free user-code checkpoint, implement the first required OHCI TD/XID or
NIC dependency, and convert the recorded NV2A stream into a visible framebuffer. Completion
means a dashboard-produced frame is observable and the XBE remains alive under input-device
enumeration.

## Not-yet (explicitly deferred, do not drift into these)

- Blinx or any game (that is Arc 2).
- Replacing the current 2BL injection boundary with authentic MCPX cold boot (a later
  faithfulness pass).
- HLE fast paths and enhancements (opt-in, and only after the faithful floor is proven).
- Static x86→C export / "native port" artifacts (not this project — see `n64lle` vs
  `n64recomp`).
