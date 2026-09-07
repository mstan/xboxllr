# AOT-3 burndown goal

Use the [AOT implementation plan](PLAN-AOT-BURNDOWN-2026-09-06.md) and its
[DOAXBV audit amendment](AUDIT-DOAXBV-RE-2026-09-06.md).
Track implementation under central Beads issue `beads-xbox.1.25.3`, within the
AOT epic `beads-xbox.1.25`.

## Objective

Complete AOT-3: integrate generated native code with the actual Xbox machine
runtime, including reliable transitions to and from Unicorn.

The larger objective remains predominantly AOT execution, portability, faithful
behavior, and **60 FPS with headroom**. Do not assume AOT alone guarantees that
performance. This goal completes the machine adapter; AOT-4 and AOT-5 cover
representative dashboard execution and expanded coverage.

## Requirements

- Preserve architectural CPU state, memory access behavior, faults, interrupts,
  instruction accounting, and deterministic guest timing. Make ownership of
  XMM/x87 state across generated functions and fallback transitions explicit.
- Validate code changes and address aliases so stale generated code cannot
  execute. Unsupported execution contexts must safely decline before effects.
- Implement the versioned, identity-checked, profile-controlled replacement
  interface. Test disabled hooks and explicitly enabled synthetic replacements,
  including ineligible and unsupported replacement exits.
- Preserve faithful LLE as the default. Keep service replacements and future
  enhancements behind explicit interfaces. Separate guest register/stack
  adapters, independently callable service models, and host backends.
  Implementing D3D8 replacements is not required to complete AOT-3.
- Use one GPT-5.5 High subagent at a time for bounded work, with no children.
  Independently challenge and validate its output.
- Claim the owning central Beads issue before implementation. Record meaningful
  findings, validation, and disproved assumptions there. Amend the plan when
  evidence warrants a change.
- Never use Unicorn timeout or switch the same Unicorn instance between
  positive instruction-count execution and zero-count execution.
- Run relevant differential tests and dashboard regression checks. Report
  actual execution coverage and overhead; use interleaved same-session A/B
  measurements for performance claims, with the existing minimum-and-median
  acceptance method and uninstrumented headline results.
- Preserve third-party licenses and private-input protections. Do not publish
  proprietary Xbox inputs or generated proprietary code. Real-console access
  and an ARM hardware setup are outside this goal.
- Commit validated changes on the working branch, integrate into master, and
  push under the owner's existing authorization. Preserve unrelated work.

## Completion evidence

- Generated execution runs through the real machine adapter, with validated
  native-to-Unicorn-to-native continuation and correct state ownership.
- Tests exercise supported memory accesses, safe declines, fault/event/deadline
  exits, code invalidation through aliases, and hook eligibility. Compare defined
  architectural behavior with independent expectations and Unicorn.
- Required regression checks pass, including the established dashboard route.
  Record exact configurations, outcomes, and any remaining limitations. A passing
  synthetic fixture alone does not complete machine integration.
- The implementation and validation record are committed and integrated; Beads
  reflects the result. Close AOT-3 only when its acceptance requirements pass.

## Execution instruction

Burn this goal down autonomously until AOT-3 is validated or a concrete blocker
requires an owner decision. Finish with evidence, remaining limitations, and the
next executable goal for AOT-4. Do not substitute another broad comparison for
the implementation or weaken correctness checks to increase coverage.

Before substantial decoder expansion in AOT-4, retain the planned bounded
qualification of upstream xboxrecomp against our state, memory, and exit ABI.
