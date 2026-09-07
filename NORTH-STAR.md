# NORTH-STAR

> One active goal. One next milestone. Nothing else lives here.
> On a pivot, edit THIS file — never leave the old goal in a roadmap and the new one in a
> status log. If this file and reality disagree, that is a bug to fix before writing code.

## The project, in one sentence

Build a **faithful Xbox foundation for portable native ports**, with opt-in ultrawide,
higher-resolution, and higher-frame-rate enhancements. The first validation workload
is the original Xbox dashboard. The existing LLE runtime is the reference foundation;
portable native output is a goal, not an implemented capability.

## The two arcs

- **Arc 1 (now): the real Xbox dashboard, fully navigable.** Boot the real firmware LLE,
  launch and run `xboxdash.xbe` from the real HDD image, and make the whole dashboard work
  end-to-end — settings, clock, memory manager, music — cosim/hardware-validated.
- **Arc 2 (after Arc 1): Blinx: The Time Sweeper**, in its own `BlinxLLE` repo, on the
  proven platform, with opt-in HLE fast paths and Blinx-specific enhancements.

## The ONE current goal

**Qualify a faithful, portable execution architecture using Arc 1 / A4 as the baseline.**
Owner clarification, 2026-09-06: the intended product follows psxrecomp's portable
native-port direction. Merely making another hardware emulator faster does not complete
that goal. See the [comparison and decision gates](docs/COMPARISON-XEMU-NATIVIZATION-2026-09-06.md).

The real firmware and
kernel boot, FATX loads the stock dashboard image from the maintainer-supplied HDD image, and
guest-owned NV2A output is visible through SDL3/OpenGL. **Owner correction, 2026-09-06:
the target is faithful LLE at 60 FPS with headroom**, including steady dashboard animation
and navigation. Ten FPS is an obsolete intermediate threshold, not completion. Opt-in HLE
must match the LLE floor; it does not replace the requirement to optimize that floor.

**The metrics are distinct** (`docs/PERF-METHODOLOGY.md` and
`docs/PERF-PRESENT-2026-09-06.md`). Successful SDL present calls, guest flip writes, guest
vblanks, and guest speed have separate counters. None alone proves smooth 60 FPS: repeated
images and discarded submissions must not be counted as newly displayed guest frames.
`effective_fps` is 60 x guest speed, not a measured display rate. The retention gate is the **total 12 B replay wall,
median AND min, both required to improve by 5 %**, from interleaved same-session legs on a quiet
host. Acceptance is **behavioural faithfulness, not byte-exactness**: same screens reached, same
input and interrupt timeline within tolerance, no artifacts, no faults. Byte-locks stay as the
cheapest regression detector, and hardware truth outranks any golden.

## The ONE next milestone

**Qualify the native-execution proposal before a broad rewrite.** Establish a matched
xemu reference, specify complete CPU-state and event handoff, and prove a representative
guarded native region on x86-64 and actual ARM64. Then demonstrate one bounded enhancement
seam and decide whether xboxlle or xemu is the better device runtime. A tiny arithmetic
translation or an ARM cross-build alone does not pass. The comparison document defines
the staged evidence required; none of these proposed native capabilities is implemented.

Preserve the dashboard and performance baselines during this investigation.
Stage g is validated; the old replay's 11.7 figure
counted guest flip writes in a headless run; it did not prove interactive presentation.
Stage g removes scanout conversion and the fixed 33 ms throttle, but its measured
18.76 successful SDL present calls/s is not the target.

The plan must cover steady-state dashboard and scripted navigation at guest-anchored
windows, guest speed, distinct displayed frames, and latency tails. Headroom needs an
explicit threshold; 20% (72 FPS capacity, 13.89 ms/frame) is the proposed planning value,
pending the owner's preference. Cold boot remains a separate regression workload.
Keep the existing 12 B gate alongside these product metrics so faster presentation cannot
hide slower emulation.

No subsystem is assumed optimized. Previous negative measurements remain evidence about
their tested workload and configuration, not proofs of global optimality. Re-open a rejected
approach only with a changed prerequisite or evidence that its prior bound was invalid.
The WHPX MMIO-exit rejection does not prove tuned TCG, device work elimination, GPU residency,
or other faithful approaches exhausted. See
[`PERF-CAMPAIGN-2026-09-06.md`](docs/PERF-CAMPAIGN-2026-09-06.md) for ranked experiments.

## Not-yet (explicitly deferred, do not drift into these)

- Full game bring-up, including Blinx (that is Arc 2); any qualification scene must
  stay bounded and must not silently become a full-title project.
- Replacing the current 2BL injection boundary with authentic MCPX cold boot (a later
  faithfulness pass).
- Broad HLE and enhancement implementation beyond the bounded qualification scene
  (opt-in, checked against the faithful floor).
- A full replacement CPU/compiler or full-title native port before the architecture
  qualification passes. Static x86-to-portable-code exploration is now in scope;
  the previous blanket exclusion is superseded by the owner's clarified goal.
