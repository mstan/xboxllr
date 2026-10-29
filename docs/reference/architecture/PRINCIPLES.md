# Engineering principles

Shared engineering contracts for XboxLLR. Inherits the recomp-family constitution
(`../recomp-template/PRINCIPLES.md`) and specializes it for an IA-32 guest,
secure-booting, GPU-heavy console that we build by **harvesting proven code into a clean,
observable architecture**. Where this file and a platform note disagree, the stricter
reading wins, except where a later owner instruction explicitly changes that policy.
[Architecture direction](NORTH-STAR.md) governs enhanced defaults and donor reuse.
Read this before touching code.

The host is not required by design to use x86. Windows x64 is the validated build
today; portable static/native output is required by NORTH-STAR. Actual ARM hardware
qualification is deferred and must not block the initial x64 native target.

## 0. Founding rationale (why xboxlle exists)

v1 hand-wrote the Xbox kernel as 119 HLE thunks. That is the same setup that failed on PSX
three times: **faked kernel state drifts from real RAM**, and the drift surfaces as
cascading downstream bugs (timing, callback ordering, black framebuffer). The fix is
architectural: keep the real kernel and hardware path independently runnable as
the reference. A complete, interchangeable native service can replace its work
in enhanced mode; invented kernel state or fabricated success remains forbidden.
See `../xboxrecomp/ARCHITECTURE-REVIEW.md` for the historical failure analysis.

## 1. LLE is the floor; qualified enhanced execution can be the shipping default

The target is the real MCPX ROM, real 2BL, and real Microsoft kernel boot chain.
Today we start at an approximate 2BL handoff using a supplied decrypted bootloader;
complete MCPX cold boot remains unimplemented. The real kernel builds its
own dispatcher/thread/object/MM structures in real guest RAM on the reference
path. Enhanced mode may replace kernel exports, devices or title functions at
documented boundaries, with the reference implementation still runnable.

Owner clarification, 2026-10-03: any subsystem can have an interchangeable HLE or
native implementation once its implemented LLE counterpart and observable contract
exist. Internals need not reproduce the hardware mechanism. Inputs, results,
memory/state effects, errors, resource ownership, ordering and guest-visible time
must remain compatible, with documented per-contract numerical/presentation
tolerances. Title-specific replacements require explicit image/profile identity
and qualification. Qualified enhanced paths may ship by default; global and
per-subsystem opt-outs select the reference. "Fake-the-answer" HLE is forbidden.

Owner clarification, 2026-10-07: implement enough runnable LLE to inform the
bounded contract, then adapt HLE and validate the exercised outcomes. Exhaustive
or flawless hardware emulation is not a gate. Record unproven details and
tolerances; expand the reference when an observed mismatch needs it. Neither
instruction-history equality nor matching internal algorithms is required.

No subsystem returns fake device success; a *headless* subsystem still models every
guest-visible register, DMA, status, timing, and interrupt — only host *output* is
suppressed.

Enhancement profiles may change declared behavior, such as camera aspect or
filtering, while preserving unrelated behavior and keeping reference execution
available. Unsupported requests decline before effects. Fallback after a committed
operation requires an explicit resolve/state-transfer boundary. Startup selection
is sufficient initially; arbitrary live hot swapping is not a prerequisite.

## 2. Fast-track, don't shortcut: harvest and borrow, but own the architecture

We do not reinvent proven wheels, and we do not take shortcuts. The difference is deliberate:

- **Reuse** already-solved logic directly or through adapters: CPU foundations,
  NV2A/APU/device models, renderers, shader translators and compiler tooling.
  Substantial upstream components or forks are permitted; rewriting their code is
  not a requirement. Choose xemu, xboxrecomp or another foundation by observed
  integration cost and behavior, preserving each component's applicable license.
  Validate dependencies and replace unimplemented shim behavior with real services.
- **Borrow** the host layer outright (SDL3, Vulkan/OpenGL, format/crypto libs) — it only
  touches the host side and is not faithfulness-critical.
- **Build ourselves** the machine wiring, observability, cosim/hardware-conformance harness,
  the LLE→HLE framework, and per-title packaging.

Build only the missing integration and observable contracts. Prefer reusable
components over repeated custom implementations. The reference floor and enhanced
product can use different algorithms, with explicit qualification at their boundary.

## 3. No stubs. No silent fallbacks. Fault-stop, don't guess.

An enhanced operation that cannot handle its input returns to an implemented
reference path before effects and reports that choice. If neither path implements
the required behavior, the loud-stop rule below applies. A fallback is real work,
never a fabricated successful result.

An unimplemented instruction, MMIO register, kernel-generated code path, or device behavior
is a **loud halt that records the exact address/context**, never a default that returns a
plausible value. "Unknown" is acceptable; guessing is not. Forbidden in artifacts/reports:
"probably", "should work", "looks correct", "seems like." Replace with a proof, a citation,
or the literal token `unknown`.

## 4. Harvested code is a starting point, not authority; hardware is truth

Code we harvest (Unicorn, xemu) is proven but **not** ground truth — xemu knowingly
approximates (it makes the MCPX ROM writable to dodge a real write-back-cache dependency; its
timing is instruction-count-based, not cycle-accurate; it has retail-DRM gaps). We therefore
use hardware to arbitrate faithfulness and correct donor behavior when evidence requires it.
Per owner direction, hardware work is deferred until a concrete blocker needs it;
authored and reference tests support bounded progress and must not be called hardware proof.
Generated/
translated artifacts (decoded blocks, block packs, dispatch tables, traces) are build
outputs — never hand-edited; fix the source and regenerate.

## 5. Scope by trace, not by the manual

Target exactly the Pentium III machine profile the reference uses and exactly the
instructions/modes/quirks the MCPX + chosen BIOS/kernel + software actually exercise.
Fault-stop on everything else. Don't pre-build "all of IA-32", the full dashboard's every
corner before the dashboard reaches it, or SSE2+/obscure behavior before a trace needs it.
(The harvested CPU core already covers most of the ISA; our job is to *scope, instrument, and
hardware-validate* it, not to re-derive it.)

## 6. Correctness contracts (mostly provided by the harvested core; we own their validation)

The harvested CPU core (Unicorn/QEMU) already implements software x87 (SoftFloat), lazy/
correct flags, restartable faults, `REP` micro-iteration, and the like. Our responsibility is
to **not assume they are perfect** and to pin them down where the Xbox is sensitive:

- **x87 / SSE / MMX** results at the edges (80-bit precision, denormals, rounding) validated
  against hardware; fault-stop on anything the core doesn't model rather than accept a wrong
  answer.
- **Flag definedness/provenance**: where software reads an architecturally-undefined flag,
  emit an always-on `undefined_flag_read` event and settle the real value on hardware — never
  globally canonicalize.
- **Retirement / micro-iteration boundaries** (restartable faults, split-page/unaligned/MMIO
  operands, interrupt shadows, `LOCK`) must be observable to the cosim; if the harvested
  core hides a boundary the Xbox exposes, that is a finding to fix, not paper over.
- **RDTSC / timing**: a deterministic injected clock for cosim vs a modeled Xbox clock for
  hardware-faithful runs — two profiles, never one tuned to match the other.

## 7. Ground truth: always-on rings, queried after the fact

Every observable (retired PC/block ring, MMIO/PIO ring, IRQ/device-event ring, dirty-page
ring, per-frame state ring, undefined-flag-read ring) is an **always-on ring buffer**
recording continuously from reset, in Release too. Probes **query** the ring for the window
of interest. Never `arm trace; run; dump`. Never pause/step two systems to synchronize —
free-run and diff the rings. If needed data isn't captured, *extend the ring*.

## 8. Oracles: real hardware is authority; xemu is a reference we also harvest

Because we harvest xemu's code, xemu is **not** an independent oracle for the subsystems we
took from it (shared code = shared bugs). The true independent oracle is **real Xbox
hardware**, reached by a homebrew probe agent emitting our JSONL schema — authoritative for
cache quirks, retail security, timing, and undefined/model-specific CPU behavior, and a
first-class tier from A0/A1. **Pinned xemu-TCG** is still valuable as a fast reference oracle
for control flow, device sequencing, and regression — used with the explicit knowledge that
it can share our harvested bugs, so hardware arbitrates. Cxbx-Reloaded can also
inform service implementations under the same LLE/enhanced contract rules; its
kernel HLE does not itself qualify or disqualify an implementation. Never infer
correctness from two implementations agreeing.

## 9. Determinism is a per-subsystem property, requalified continuously

The instruction-level method below applies to equivalent reference/native CPU
paths. Enhanced/HLE paths can omit guest instruction sequences; compare their
observable contract at logical boundaries with documented tolerances. Keep guest
time and ordering explicit and count replaced work separately from AOT retirement.

Single deterministic guest-cycle clock; devices on event deadlines; no free-running host
threads. Cosim is **hierarchical**: run an epoch (~1K–16K retired instructions), compare
architectural + control + dirty-page + MMIO/IRQ event hashes + final EIP + instruction count;
on mismatch restore the previous checkpoint and binary-search to the first diverging
instruction/micro-iteration. "Halt at first divergence" is the contract; per-instruction
round-trips are not. Every reference trace records the exact xemu build identity + firmware
hashes. Gates before any cosim result is believed: A-vs-A = 0, B-vs-B = 0, injected-fault
halts at the right place naming the right subsystem, hash-vs-byte audit.

## 10. Crucibles gate subsystem admission

A subsystem (paging, exception priority, split-page access, interrupt shadow, PIC/PIT, x87/
MMX/SSE, a given NV2A method class, …) gets a focused conformance crucible that must pass
**before** the milestone that first relies on it, with the evidence source and limits
identified. Hardware-only uncertainties remain explicit and use the owner-directed
hardware trigger above. Crucibles come
early and block; they are not a late regression afterthought.

## 11. Repo/doc discipline: one question, one file

`docs/reference/architecture/NORTH-STAR.md` = the one goal + next milestone. `STATUS.md` = the only place "what is true
now" lives. `docs/reference/architecture/PRINCIPLES.md` = how we work. `docs/ROADMAP.md` = the arcs/ladder.
`docs/reference/architecture/DESIGN.md` = the architecture. No split-brain between a roadmap and a status log — that
churn is exactly what cost v1.

## 12. Long-horizon (non-binding): the platform is a modding substrate

A faithful, fully-observable execution platform is the ideal substrate for future analysis
and modding (runtime hooks/patches now; hybrid dynamic-driven lifting later; LLM-assisted
semantic understanding eventually). We do not build that now, but we keep the **IR expressive
and the rings comprehensive** so the door stays open at near-zero cost. You can only
confidently mod what you can faithfully run and observe.

## 13. Windows shell hazard

Host is Windows + PowerShell / MSBuild. Never let an unquoted path reach ShellExecute; beware
MSBuild timestamp-vs-content staleness. (See family PRINCIPLES.)
