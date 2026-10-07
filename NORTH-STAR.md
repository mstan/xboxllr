# NORTH-STAR

> Updated by the owner on 2026-10-06.
> Complete Blinx's attract sequence first, then JSRF. Continue dashboard performance work.

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

Continuously assess useful changes in all three linked xboxrecomp repositories:
sp00nznet, NoRain211 and phobos665, plus relevant title runtimes. Each relevant
performance spike records what can be borrowed or adapted from them. A stubbed
portion excludes that behavior, not the donor's useful implementations elsewhere.
See the [all-donor assessment](docs/RENDERER-REUSE-2026-10-02.md).

For all Xbox work, prefer established emulator implementations and reference
fixtures before writing new device or service logic. xemu is the primary LLE
device/reference donor; Cxbx-Reloaded supplies useful kernel/D3D contracts and
native implementations, alongside the recomp forks. Borrow complete useful
boundaries when their ownership fits. Validate changed adapters and exercised
caller behavior, and retain known donor limitations; do not independently
rediscover every borrowed algorithm. A pinned external LLE reference runner may
provide evidence while the in-tree reference catches up. Shared implementation
ancestry is not independent corroboration, and stubs remain excluded.

Owner distribution constraint, 2026-10-06: GPL projects may be external research
and execution references, but GPL code must not enter the intended non-GPL
shipping product. A source port, linked runtime or copied generated-code helper
is still incorporated code. Prefer permissively licensed implementations with
verified file provenance, or independently implement the observed contract.
The current reference build already has GPL dependencies; this policy is a
shipping target, not a claim that they have been removed. See
[license scope](LICENSE-SCOPE.md), tracked by `beads-xbox.1.21`.

## Current goal

Complete original NTSC Blinx's full attract sequence, then JSRF's. Work serially:
finish and validate Blinx before beginning further JSRF work. This supersedes
the earlier parallel title-check policy. Use Phobos as the primary current
behavioral reference for native title launch and whole D3D8 services; retain
shared implementations and isolate title/XDK bindings for later JSRF validation.
Continue pursuing original guest speed and content cadence in the animated
stock dashboard with working selection/input; it does not gate title work.
Use enhanced rendering, compatible native/HLE replacements and the existing
hybrid CPU path to get there. Complete dashboard navigation remains a product
requirement; every menu interior is not part of this immediate performance gate.
Blinx supplies the first game workload; JSRF is the subsequent generalization gate.
Success requires real startup/loading/rendering/audio and movies/gameplay where
exercised, input interruption and return to menu, repeatable attract loops
without crashes/hangs/resource growth, and measured original-speed cadence.
The central objective is `beads-xbox.1.62`. The goal controller now contains
this serial Blinx-then-JSRF objective; dashboard performance remains ongoing.

Full portable AOT remains the final CPU goal: prepare native artifacts before
launch and remove dynarec/interpreter dependence from the declared supported
workflows, including reference opt-out. Partial AOT and explicit CPU fallback
are acceptable during development. Report HLE replacement work separately from
AOT guest instructions, dynamic fallback and proven idle skips.

HLE may lead delivery while the maintained LLE reference advances alongside it.
The reference need not be exhaustive or fast: define the larger observable
contract, validate the exercised scope, and record inaccuracies and untested
details. Small gaps do not block a useful HLE implementation. A successful HLE
scene proves that scene's progress, not complete hardware correctness. Missing
required work and fabricated completion remain unacceptable.

## Next milestone

**Original NTSC Blinx's complete repeatable attract sequence at original speed,
then JSRF's. Keep the bounded LLE reference maintained and dashboard speed open.**

The [latest Phobos audit](docs/PHOBOS-DEEP-AUDIT-2026-10-06.md) compares native
title launch with grafting the full machine into the donor. It records actual
Blinx/JSRF symbol coverage, revision mismatches, missing indexed-draw bindings,
and the next bounded work. Shared names and contracts do not make the donor's
unchecked completion or missing-function paths acceptable implementations.

The paragraphs below retain earlier renderer evidence; the latest five trials
and retry conditions are in [the donor results](docs/RESULTS-REMAINING-DONOR-SPIKES-2026-10-04.md).
The first borrowed-renderer checkpoint now exists: xemu-generated pixel shaders
execute live dashboard draws with reference opt-out. Its first timing pair was
slower and whole-menu numerical differences remain unqualified, so it stays
experimental. See [the measured checkpoint](docs/RESULTS-XEMU-PSH-2026-10-03.md).
Decoded-texture and unused-stage qualification now raise donor execution to
5,349 generated draws, with exact reference opt-out regression results. Timing
remains unqualified and whole-scene tolerance open; this is not a 60 FPS win.
Renderer performance/coverage work continues from measured costs; avoid building
a general HLE framework first.

Measure steady animation and selection changes separately from startup. Establish
frame pacing and distinct guest-produced output as well as guest progress;
vblank counts or repeated host presents cannot close this gate. Profile the
measured expensive work and qualify useful renderer/subsystem replacements.
Neither full AOT nor a complete renderer transplant is a prerequisite.

Blinx media/loading now proceeds independently. Retain the pinned launch-input audit
and real loader/device requirements; do not invent authentication or readiness.

The detailed order, PSX/Tomba source precedent, subsystem contracts, donor
choices and completion gates live in the
[ecosystem gameplan](docs/GOAL-AOT-DASHBOARD.md). Central tracking:
`beads-xbox.1.25.5`; active menu gate `beads-xbox.1.17`.

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

Implementation resumed on the owner's 2026-10-03 direction. Work solo by
default; the owner authorized bounded Sol/high assistance for this task, with
independent review of its recommendations.
ARM hardware setup and authentic MCPX cold boot remain separate later work.
Real-console qualification remains deferred until a concrete blocker requires
it; disclose limits of authored/reference tests and never label them hardware
proof. Preserve upstream attribution and applicable licenses.
