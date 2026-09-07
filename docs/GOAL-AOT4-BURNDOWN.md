# AOT-4 burndown goal

Prerequisite: complete and integrate [AOT-3](GOAL-AOT3-BURNDOWN.md), including its
dashboard regression and validation record. Track AOT-4 under central Beads issue
`beads-xbox.1.25.4`, within epic `beads-xbox.1.25`.

## Objective

Run a representative, measured dashboard region through generated portable native
code in the real machine. Preserve faithful LLE behavior and Unicorn fallback.
The broader target remains predominantly AOT execution and 60 FPS with headroom;
this stage must report its actual contribution without assuming a speedup.

## Work and acceptance

1. Perform the bounded upstream xboxrecomp frontend qualification already required
   by [the plan](PLAN-AOT-BURNDOWN-2026-09-06.md) and
   [DOAXBV audit](AUDIT-DOAXBV-RE-2026-09-06.md). Pin the revision and licenses.
   Compare its output against independent expectations and Unicorn using our
   state, memory, fault, event, and exit ABI. Record an adopt/adapt/reject decision
   before substantial decoder expansion. Borrow only compatible, qualified work.
2. Profile the current dashboard workload and select a hot, bounded region whose
   code identity and entry points can be established from private user inputs.
   Explain why the selected region represents useful work, including its observed
   frequency, instruction share, and boundary costs.
3. Provide a reproducible private-input preparation and rebuild workflow. Validate
   input/manifest identities, generated ABI, code ranges, and live-code coherence.
   Keep proprietary inputs and generated proprietary code outside tracked files.
4. Integrate the region with AOT-3's real-machine dispatcher. Exercise native and
   fallback continuation, state ownership, memory permissions/aliases, precise
   faults, interrupt/deadline exits, and deterministic time. Unsupported cases must
   decline safely and remain visible in coverage reports.
5. Pass the established dashboard regression route and meaningful differential
   tests at optimized and unoptimized generated-C settings. Report real AOT
   retirement share, fallback reasons/time/transitions, native boundary overhead,
   and total workload time. Use quiet, same-session interleaved A/B with at least
   three legs per arm; apply the existing minimum-and-median performance method.
   Use uninstrumented builds for headline timing and separate attribution runs.
   Account for the measured AOT-3 floor cost (2.88% median / 3.54% minimum wall
   regression); measure against that corrected baseline without undoing its
   validated fault, clock, interrupt-shadow, or alias-coherence behavior.
6. Keep default execution faithful. Explicit modification profiles remain opt-in;
   preserve separate guest adapters, service models, and host backends. Widescreen,
   higher resolution/frame rates, ARM hardware setup, and real-console access are
   not acceptance requirements for this region.
7. Document reproducible commands, measured results, remaining limits, and the
   ranked AOT-5 coverage plan. Commit validated work on the working branch,
   integrate and push under existing authorization, and update/close Beads only
   after the requested outcome is validated.

## Execution instruction

Burn down this goal autonomously, using one GPT-5.5 High helper at a time with no
children and independent adversarial review. Preserve the instruction-clock and
private-asset rules. Never use Unicorn timeout or switch one engine between
positive-count and zero-count execution. Record disproved assumptions. Continue
until acceptance passes or a concrete blocker requires an owner decision; do not
substitute another comparison document for real dashboard native execution.

## Goal to paste back

> Burn down `docs/GOAL-AOT4-BURNDOWN.md` from the current repository state,
> preserving completed work. Qualify reusable upstream work, then execute a
> measured dashboard region through portable AOT with faithful LLE behavior and
> Unicorn fallback. Verify correctness, private-asset protection, actual native
> coverage, boundary costs, and total performance with the documented A/B gates.
> Use one GPT-5.5 High helper at a time, with no children, and independently
> challenge its findings. Commit and push validated work under my existing
> authorization. Continue until this milestone passes acceptance or a concrete
> blocker requires my decision. Finish with measured results and ranked next
> steps toward predominantly AOT execution and 60 FPS with headroom; do not claim
> that completing this milestone achieves the broader performance target.
