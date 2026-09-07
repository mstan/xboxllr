# AOT implementation plan and burndown

Owner decision: build a psxrecomp-style portable native pipeline. AOT is the intended
primary executor; Unicorn remains the fallback and comparison engine. This is an
implementation plan, not a request for another architecture decision. Work tracking
and live status belong to central Beads under `beads-xbox.1.25`.

## Deliverable and boundaries

Produce reproducible **IA-32 bytes → portable C → host executable** artifacts. Start
on the existing Windows x64 toolchain. Establish explicit runtime and modification
interfaces so later title enhancements do not require replacing the architecture.
The faithful dashboard and 60 FPS with headroom remain the product target.

The first landing is a synthetic generated-code target with meaningful semantics
and fallback tests. It does not claim a nativized dashboard, a general x86 lifter,
or a performance win. Dashboard integration follows it. Actual ARM setup, xemu
benchmark qualification, hardware-console access, and enhancement implementation
are outside this first landing and do not block it.

## Implementation decisions

- **Frontend:** an explicit, fail-closed IA-32 subset decoder for the first artifact.
  Decode only declared/reachable entry points. Unsupported instructions terminate
  a translated path. Reject truncated supported encodings and overlapping instruction
  boundaries. Do not guess what follows an unsupported instruction; additional
  entries must be supplied explicitly. Evaluate a general decoder when coverage
  expansion warrants it; the initial subset is not a general disassembler.
- **Artifacts:** generated C11, declarations, and a JSON manifest with ABI version,
  input identity, code ranges, instruction boundaries and control-flow edges. No
  runtime opcode decoder is hidden in the generated target. Generator output lives
  in the build tree; private Xbox input and generated proprietary code stay private.
- **ABI v1:** explicit fixed-width registers, EIP, EFLAGS, instruction count and
  exit reason; bounded flat 32-bit test context, not full CPU interchange. Untouched
  extended state remains with the fallback. Unsupported context exits before effects.
- **Memory:** checked, all-or-nothing 32-bit RAM callbacks; no embedded guest-as-host
  pointers. A callback declining an operation has no guest-visible side effects.
  MMIO and paging/segment faults are explicit exits for the first subset. The later
  machine adapter must implement actual guest access rules before integration.
- **Coherence:** exact live-code validation before each generated instruction in
  the prototype, including after a write. This is intentionally conservative and
  expensive. Later page-generation guards require independent invalidation tests.
- **Events:** bounded instruction budget and synchronous pending-event exit in the
  prototype. Real scheduler/interrupt integration is a separate stage. No Unicorn
  timeout; never mix positive-count and zero-count execution on one engine instance.

## Burndown order and acceptance

| Stage | Work | Required evidence |
|---|---|---|
| AOT-1 | Generator, manifest, ABI and CMake-generated C target | Deterministic artifacts; generated code compiles as C; malformed/truncated/overlapping input rejects; unknown code safely declines; generated instructions actually execute |
| AOT-2 | Differential and independent synthetic validation | Arithmetic boundaries, defined flags, carry preservation, high/low byte registers, taken/untaken branches, RAM ordering, budget/event exits, changed code and unsupported-context checks; explicit native → Unicorn → native continuation |
| AOT-3 | Full-machine adapter and guarded modification interface | Correct state ownership, paging/segment/MMIO semantics, precise exit/IRQ/deadline behavior, code invalidation across aliases; disabled hooks preserve default behavior; enabled synthetic hooks require matching identity/profile |
| AOT-4 | Representative dashboard region | Private preparation and rebuild; existing dashboard regression route preserved; real AOT instruction share, fallback reasons/time/transitions, and total workload time reported |
| AOT-5 | Expand predominant AOT coverage and optimize | Ranked hot regions, safe coverage growth, interleaved same-session measurements including boundary costs; faithful baseline and separate enhanced profiles preserved |

Stages are sequential for acceptance, but independent generator and runtime work
may overlap with one GPT-5.5 High helper. The root agent reviews and tests helper
output adversarially. No helper children. A task closes only after its evidence
passes; synthetic success does not close the parent feature.

## Validation details

Use the existing fixture corpus's independently derived state expectations and
defined-flag masks. Compare generated execution with fresh Unicorn instances under
identical initial state and mapped RAM. Add deterministic randomized initial values
and arithmetic boundaries. Exclude architecturally undefined flags from equality,
but verify flags that instructions preserve, such as carry across INC/DEC.

The fallback test must show native execution before and after a deliberately
unsupported instruction, with the same final defined state as all-Unicorn execution.
This synthetic adapter is not the full machine adapter. A code-write test must
prove stale generated instructions cannot run later in the same invocation.

Build the generated target at normal release optimization and an unoptimized setting
using the available compiler; compare semantics. These are cheap compiler checks,
not an ARM port or a performance benchmark. Run relevant CTest suites and the full
existing suite before landing runtime/build changes. Do not invent speed claims
from test duration. Apply the existing interleaved minimum-and-median methodology
only when proposing measured performance improvements.

## Landing and continuation

First-landing evidence: AOT-1 (`beads-xbox.1.25.1`) and AOT-2
(`beads-xbox.1.25.2`) implement 28 synthetic artifacts, nine independent fixture
expectations, 432 differential cases per optimization level, code/context/event/
budget/memory exits and a native/Unicorn/native continuation. The full CTest suite
passes 45/45; the expanded AOT suites also pass after the final coverage additions.
The Python generator suite contains 14 tests. No performance claim follows from
these test durations. No native code is linked into the dashboard executable yet.

AOT-3 through AOT-5 are `beads-xbox.1.25.3` through `.5` and remain outstanding.
The tests deliberately use `independent_regions: true` to bundle distinct programs
at the same guest base. Each compiled function is bound to its own descriptor and
expected bytes. This is not an ambiguous whole-address-space dispatcher.

Reproduce the first landing from the configured Windows build (see the README for
tool installation). Substitute your installed CMake directory if needed:

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-release --target test_aot_optimized test_aot_unoptimized
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build-release -R '^aot_' --output-on-failure
```

CMake runs `tools/aot_compile.py` on the synthetic fixture JSON and writes C,
declarations and a manifest to `build-release/generated/aot/`. Both optimized and
unoptimized C are compiled under strict C11 warnings. The manifest identifies
generator, input and region bytes by SHA-256 and records ABI/context requirements.
Inspecting these files does not require any Xbox dumps. CI additionally compiles
the generated C on its Linux host; that check is not an ARM runtime qualification.

Keep production execution unchanged until the machine adapter is qualified. Land
the synthetic target and tests on the performance branch, fast-forward master and
push under existing owner authorization after checks pass. Preserve third-party
licenses and the existing combined-executable release restriction. No new dependency
is required for AOT-1. Record failures and disproved assumptions in Beads.

After the first landing, start AOT-3. Do not replace the required AOT deliverable with
another broad comparison, nor weaken fallback/coherence checks to increase coverage.
Consult [NORTH-STAR](../NORTH-STAR.md) and the
[architecture comparison](COMPARISON-XEMU-NATIVIZATION-2026-09-06.md) for product context.
