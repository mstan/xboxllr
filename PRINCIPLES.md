# PRINCIPLES

The law for xboxlle. Inherits the recomp-family constitution
(`../recomp-template/PRINCIPLES.md`) and specializes it for an IA-32 guest,
secure-booting, GPU-heavy console that we build by **harvesting proven code into a clean,
observable architecture**. Where this file and a platform note disagree, the stricter
reading wins. Read this before touching code.

The host is not required by design to use x86. Windows x64 is the validated build
today; portable native execution is the qualification goal recorded in NORTH-STAR.

## 0. Founding rationale (why xboxlle exists)

v1 hand-wrote the Xbox kernel as 119 HLE thunks. That is the same setup that failed on PSX
three times: **faked kernel state drifts from real RAM**, and the drift surfaces as
cascading downstream bugs (timing, callback ordering, black framebuffer). The fix is
architectural, not disciplinary: **run the real kernel as real code so the cheap-but-wrong
fix is impossible to write.** See `../xboxrecomp/ARCHITECTURE-REVIEW.md`.

## 1. LLE is the baseline; HLE is an opt-in optimization on top, never the starting point

The target is the real MCPX ROM, real 2BL, and real Microsoft kernel boot chain.
Today we start at an approximate 2BL handoff using a supplied decrypted bootloader;
complete MCPX cold boot remains unimplemented. The real kernel builds its
own dispatcher/thread/object/MM structures in real guest RAM. We do **not** implement kernel
exports — the real kernel already implements them; we implement the CPU and hardware
faithfully enough that the real loader resolves imports itself.

HLE is permitted only as a *later*, *opt-in* replacement of ONE subsystem, only when: (a) it
sits on a proven, faithful LLE path, (b) it is general (not per-title hackery), (c) it
operates on real guest state via a documented hardware mechanism, and (d) it is continuously
validated against the faithful path and against hardware. "Fake-the-answer" HLE is forbidden.
No subsystem returns fake device success; a *headless* subsystem still models every
guest-visible register, DMA, status, timing, and interrupt — only host *output* is
suppressed.

Opt-in enhancement profiles are separate from equivalent HLE optimizations. They may
change declared behavior, such as a title's camera aspect ratio, while preserving
unrelated behavior and keeping the faithful default available.

## 2. Fast-track, don't shortcut: harvest and borrow, but own the architecture

We do not reinvent proven wheels, and we do not take shortcuts. The difference is deliberate:

- **Harvest** the hard, already-solved logic and re-express it in our own **modular,
  observable, hardware-correctable** form: the CPU from Unicorn (QEMU x86 core, GPL-2), the
  NV2A/APU/device models from xemu (GPL-2). Harvesting means reading and rewriting into our
  interfaces — not forking a foreign tree we then spelunk in, and not inheriting its
  approximations uncorrected.
- **Borrow** the host layer outright (SDL3, Vulkan/OpenGL, format/crypto libs) — it only
  touches the host side and is not faithfulness-critical.
- **Build ourselves** the machine wiring, observability, cosim/hardware-conformance harness,
  the LLE→HLE framework, and per-title packaging.

Rule of thumb: *borrow* what only gets our result onto the host; *harvest-and-validate* what
decides what the Xbox produces; *build* what observes, validates, and orchestrates.

## 3. No stubs. No silent fallbacks. Fault-stop, don't guess.

An unimplemented instruction, MMIO register, kernel-generated code path, or device behavior
is a **loud halt that records the exact address/context**, never a default that returns a
plausible value. "Unknown" is acceptable; guessing is not. Forbidden in artifacts/reports:
"probably", "should work", "looks correct", "seems like." Replace with a proof, a citation,
or the literal token `unknown`.

## 4. Harvested code is a starting point, not authority; hardware is truth

Code we harvest (Unicorn, xemu) is proven but **not** ground truth — xemu knowingly
approximates (it makes the MCPX ROM writable to dodge a real write-back-cache dependency; its
timing is instruction-count-based, not cycle-accurate; it has retail-DRM gaps). We therefore
**validate every harvested subsystem against real Xbox hardware** and correct it. Generated/
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
it can share our harvested bugs, so hardware arbitrates. Cxbx-Reloaded is reference-only (it
HLEs the kernel). Never infer correctness from two implementations agreeing.

## 9. Determinism is a per-subsystem property, requalified continuously

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
**before** the milestone that first relies on it — validated against hardware. Crucibles come
early and block; they are not a late regression afterthought.

## 11. Repo/doc discipline: one question, one file

`NORTH-STAR.md` = the one goal + next milestone. `STATUS.md` = the only place "what is true
now" lives. `PRINCIPLES.md` = how we work. `docs/ROADMAP.md` = the arcs/ladder.
`docs/DESIGN.md` = the architecture. No split-brain between a roadmap and a status log — that
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
