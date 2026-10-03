# NORTH-STAR

> One active goal and one next milestone. Updated by the owner on 2026-10-03.
> This replaces the previous strict-AOT-dashboard-first ordering.

## The project

Build a performant Xbox native-port ecosystem with a faithful, runnable LLE
floor and interchangeable enhanced implementations. Any system or subsystem
may have an HLE/native replacement with compatible observable contracts and
explicit tolerances. Qualified enhanced paths are the intended shipping default;
the real reference path remains selectable globally and per subsystem.

No stubs, fabricated device success or captured-frame substitution. Native
internals may differ: compatible state, outputs, errors, ordering, ownership
and declared numerical/presentation behavior determine correctness. The name
of the repository and retaining our own implementation are not objectives.
Substantial donor reuse or a different foundation is allowed when evidence
shows it gets to this outcome with less work.

## Current goal

Get useful enhanced rendering, faster iteration and a real Blinx launch/playable
slice using the existing hybrid CPU execution path. The dashboard is the first
performance fixture; complete dashboard navigation remains a product requirement
but does not gate game bring-up. JSRF is the next generalization target.

Full portable AOT remains the final CPU goal: prepare native artifacts before
launch and remove dynarec/interpreter dependence from the declared supported
workflows, including reference opt-out. Partial AOT and explicit CPU fallback
are acceptable during development. Report HLE replacement work separately from
AOT guest instructions, dynamic fallback and proven idle skips.

The LLE floor must exist for every claimed interchangeable replacement. Its
current dashboard qualification is bounded; media, exceptions, audio and other
known gaps still need real implementation when reached. An HLE success cannot
stand in for an absent reference contract.

## Next milestone

**A live borrowed enhanced renderer path with an explicit LLE reference opt-out.**
Use the existing owned-packet/resolve interface to establish the first contract.
Borrow xemu or matching xboxrecomp shader/raster/resource implementation rather
than requiring our software rasterizer's exact arithmetic. Compare observable
outputs under declared tolerances, measure guest speed/frame pacing/latency and
keep both execution modes working. Avoid building a general HLE framework first.

Blinx loading is an independent early workstream: after the first renderer
integration checkpoint or a concrete donor blocker, move the pinned media through
the real supported loader/device route and capture the first title execution or
precise failure. Do not wait for full AOT, all dashboard interiors or a completed
renderer transplant. Do not invent media authentication or device readiness.

The detailed order, PSX/Tomba source precedent, subsystem contracts, donor
choices and completion gates live in the
[ecosystem gameplan](docs/GOAL-AOT-DASHBOARD.md). Central tracking:
`beads-xbox.1.25.5`; planning decision `beads-xbox.1.40`.

## Evidence and performance

Target original-speed guest progress and sustained 60 FPS where the software
produces 60 Hz content. Distinct frames, host presents, guest vblanks, simulation
speed, audio and input latency are separate metrics. Preserve a game's native
simulation cadence unless a separately qualified enhancement changes it.

Reference-to-reference regressions can retain exact hashes and instruction
histories. Enhanced-to-reference comparisons use the selected boundary's
observable contract; HLE need not execute the same instruction sequence or have
the same private state. Numerical and visual tolerances are specific and recorded,
not permission for missing objects, wrong control flow or data corruption.
Measure optimizations against the preceding usable configuration at equivalent
logical work, with a quiet host and exposed fallback. Native output remains
required even if an early implementation is slower; an instruction share does
not prove a speed improvement. Historical startup benchmarks remain historical
and do not stand in for live menu/game performance.

## Work boundaries

Work solo. Implementation is paused for the owner's check-in; the gameplan is
the requested planning deliverable. Resume implementation on explicit direction.
ARM hardware setup and authentic MCPX cold boot remain separate later work.
Real-console qualification remains deferred until a concrete blocker requires
it; disclose limits of authored/reference tests and never label them hardware
proof. Preserve upstream attribution and applicable licenses.
