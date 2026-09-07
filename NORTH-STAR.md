# NORTH-STAR

> One active goal. One next milestone. Nothing else lives here.
> On a pivot, edit THIS file — never leave the old goal in a roadmap and the new one in a
> status log. If this file and reality disagree, that is a bug to fix before writing code.

## The project, in one sentence

Build a **faithful Xbox foundation for portable native ports**, with opt-in ultrawide,
higher-resolution, and higher-frame-rate enhancements. The first validation workload
is the original Xbox dashboard. The existing LLE runtime is the reference foundation;
portable static/native output is a required deliverable. A synthetic AOT target now
exists; the dashboard continues to use Unicorn until machine integration is qualified.

## The two arcs

- **Arc 1 (now): the real Xbox dashboard, fully navigable.** Boot the real firmware LLE,
  launch and run `xboxdash.xbe` from the real HDD image, and make the whole dashboard work
  end-to-end — settings, clock, memory manager, music — cosim/hardware-validated.
- **Arc 2 (after Arc 1): Blinx: The Time Sweeper**, in its own `BlinxLLE` repo, on the
  proven platform, with opt-in HLE fast paths and Blinx-specific enhancements.

## The ONE current goal

**Build the portable static/native path using Arc 1 / A4 as the faithful baseline.**
Owner clarification, 2026-09-06: psxrecomp is the architectural model, and native
artifacts are required. The initial pipeline is guest IA-32 to portable generated C
to host machine code, with the existing device runtime and guarded execution fallback.
The first host is Windows x64. ARM hardware setup is deferred; inexpensive portability
checks may inform the design but are not proof of an ARM port. See the
[comparison and implementation milestones](docs/COMPARISON-XEMU-NATIVIZATION-2026-09-06.md).

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

Execute the [AOT burndown plan](docs/PLAN-AOT-BURNDOWN-2026-09-06.md), tracked under
`beads-xbox.1.25`. The first landing is the synthetic generated-code target; the
later machine adapter and real dashboard integration remain separate gates.

**Deliver the first reproducible generated-code target on the existing x64 host.**
Define the artifact manifest, runtime ABI, complete state-preservation contract, and
guarded dispatcher; translate a bounded region to portable C and compile it before
execution. Validate state, memory, faults, and event ordering against Unicorn, including
safe rejection of changed or unsupported code. Synthetic fixtures establish plumbing;
a representative dashboard region is the integration milestone. Neither a JIT cache nor
a launcher that delegates all execution to Unicorn satisfies native output.

Provide explicit, versioned replacement points for future title functions and runtime
rendering/timing changes, disabled by default. Implementing 21:9 or higher-rate simulation
is later work. Actual ARM hardware and a matched xemu benchmark are not prerequisites
for this first native target. Runtime implementation choices remain measurable; the
requirement to produce portable native artifacts is settled.

**AOT is the intended primary execution path.** Progressively move most supported
guest execution into code compiled before launch. Unicorn remains the fallback and
comparison engine for uncovered or invalidated code. Measure the share of guest
instructions executed through AOT versus fallback per workload phase, plus fallback
time, reasons, and transition cost. Emitting many functions alone is not coverage.
The first bounded target is an integration step, not completion of this AOT goal.

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

- Full game bring-up, including Blinx (that is Arc 2).
- Replacing the current 2BL injection boundary with authentic MCPX cold boot (a later
  faithfulness pass).
- HLE and visual/timing enhancement implementation; establish their interfaces now,
  keep the faithful default, and implement opt-in behavior later.
- ARM hardware setup and platform qualification. Cheap cross-target or simulated
  checks are optional diagnostics and must be labeled as such.
- A full replacement CPU or full-title native port in the first milestone. Build the
  static path incrementally with fallback; native output itself is required.
