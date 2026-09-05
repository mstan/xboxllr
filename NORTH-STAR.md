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
guest-owned NV2A output is visible through SDL3/OpenGL. The bottleneck is still speed: sustain
at least 10 presented fps during stock-dashboard navigation, keeping the software/LLE fallback.

**The metric is defined, not vibes** (`docs/PERF-METHODOLOGY.md`). Presented fps = NV2A flip
write-increments per second of host wall. It is NOT `effective_fps`, which is 60 x guest speed
and may be reported but never gated on. The retention gate is the **total 12 B replay wall,
median AND min, both required to improve by 5 %**, from interleaved same-session legs on a quiet
host. Acceptance is **behavioural faithfulness, not byte-exactness**: same screens reached, same
input and interrupt timeline within tolerance, no artifacts, no faults. Byte-locks stay as the
cheapest regression detector, and hardware truth outranks any golden.

## The ONE next milestone

**Sustain >=10 presented fps in a LIVE SDL navigation session.** The bounded-replay half is
**met**: on the shipping binary at the shipping defaults the canonical 12 B replay is 58.5-59.4 s
at **11.7 presented flips/s** across nine legs of three interleaved sets, reproducing the golden
final frame SHA `36e87a97...`; the single-threaded software-raster reference is 147.5 s and
4.71 flips/s. What remains unmeasured is the interactive path, which carries a 33 ms present
throttle and a scalar scanout that the replay never exercises. Completion means a live SDL run
holds the target with no host-rendered dashboard substitute, replacement kernel, synthetic USB
poll, or copyrighted asset checked into the repo.

After that, the CPU tier: `tcg_exec` is down 5.4x (77.3 -> 14.3 s) but Unicorn is the next floor,
and the owner decision is to replace it if it must be replaced (WHPX/Hyper-V versus a custom
dynamic recompiler).

## Not-yet (explicitly deferred, do not drift into these)

- Blinx or any game (that is Arc 2).
- Replacing the current 2BL injection boundary with authentic MCPX cold boot (a later
  faithfulness pass).
- HLE fast paths and enhancements (opt-in, and only after the faithful floor is proven).
- Static x86→C export / "native port" artifacts (not this project — see `n64lle` vs
  `n64recomp`).
