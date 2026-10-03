# Assessment: making AOT the primary Xbox execution path

Date: 2026-09-25. Source inspected: `c3fbd22`. Tracking: `beads-xbox.1.25.5`.
Scope: source review, analysis of saved private evidence, and a proposed roadmap.
No emulator behavior was changed and no new dashboard timing run was performed.

## Decision summary

Near-total AOT for a pinned dashboard/kernel build is a worthwhile engineering
target, but our existing plan does not yet establish how to reach it. We have a
working integration prototype. Substantial compiler and runtime work remains.

The remaining work is not mainly selecting more blocks for the existing
generator. The captured route contains 23,666 block shapes. Only 896 are fully
supported by the current decoder/parser. Beyond the two already selected loops,
these supported shapes account for only **0.397% of the remaining recorded
instruction work**. This is syntax support, not a prediction of successful native
execution: memory, context and live-code checks can still decline.

The next investment should combine **static program discovery, general integer
control flow, and a runtime that can service native memory accesses**. Floating
point needs an early qualification experiment: blocks containing x87 account for
**30.05% of the instruction work remaining after the two loops** on this route.
That percentage counts entire blocks containing x87, not just their FP operations.
The census does not establish which workload phase executes them.

We can reduce scope by retaining qualified Unicorn/QEMU state, MMU and arithmetic
helpers while bypassing its dynamic code translator for compiled guest code.
Eliminating guest JIT execution and eliminating the Unicorn dependency are
different deliverables. AOT calling a faithful device or arithmetic helper is
still AOT; falling back to `uc_emu_start` is not. Helper reuse needs a reviewed
interface and independent tests, not an assumption that internal helpers are
safe to call directly from generated C.

## What the saved evidence actually measures

Inputs are the existing `aot4-census-code-12b` CSV, blockpack and report. The
report identifies source `c843839a645acf8061091a9f3cfbb8ff992004bf`; this is a
historical instruction census, not a fresh profile of `c3fbd22`. It covers one
boot/dashboard/clock-accept route. Its totals are internally consistent:

| Recorded quantity | Value |
| --- | ---: |
| Guest instruction clock | 12,000,000,001 |
| Instructions attributed to executed fallback blocks | 7,301,356,681 |
| Idle instructions skipped | 4,698,643,320 |
| Native instructions in this census | 0 |
| Distinct `(PC, byte length)` block shapes | 23,666 |
| Block shapes with captured first-executed bytes | 23,666 |
| Captured bytes, including overlaps | 410,286 |
| Distinct virtual byte addresses represented | 381,594 |
| Decoded instruction identities `(PC, bytes)` | 122,971 |
| Observed mnemonic spellings | 188 |
| Entire block shapes supported by current decoder/parser | 896 |
| Recorded instructions attributed to those 896 shapes | 4,734,855,600 |
| Recorded instructions attributed to the two selected loops | 4,724,616,624 |
| Additional syntax-supported shapes beyond those loops | 894 |
| Recorded instructions attributed to those additional shapes | 10,238,976 |
| Recorded instructions remaining after the two loops | 2,576,740,057 |

There are 404 unsupported mnemonic/operand-kind/operand-width signatures in the
decoded capture. That is a rough inventory, not 404 equal implementation tasks:
it combines some distinct prefix/addressing cases and separates some cases that
one general implementation could handle. There are also unsupported forms of
already implemented mnemonics such as MOV, XOR and SUB.

Instruction work is concentrated, even after excluding the two loops:

| Share of the remaining recorded instructions | Number of hottest block shapes |
| --- | ---: |
| 50% | 13 |
| 90% | 244 |
| 95% | 518 |
| 99% | 1,843 |
| 99.9% | 5,816 |

These are blocks, not functions. This table can prioritize investigation. It
cannot establish static completeness, post-boot coverage or host-time savings.
There is no reason to assume that the next highest-count block is useful
interactive work; countdown loops already showed why that assumption fails.

### Method and limitations

The analysis validates the CSV instruction partition using the existing census
reader, requires a captured block for every row, disassembles first-captured
bytes with Capstone in IA-32 mode, and checks current `decode_one` support at
those boundaries. Entire-block support also requires the real `parse_region`
control-flow validation. The scan found no supported instruction-length
disagreement. A separate sequential scan reproduces all 896 supported blocks
and their aggregate count; authored fixtures check calls, stack operations,
floating point, segment overrides, operand-form distinctions and truncation.

Two captured blocks cannot be decoded completely by this Capstone setup. Each
has one charged instruction. Retrying those captures as 16-bit code does not
resolve them. They remain explicit unknowns; the scan does not fabricate an
instruction or silently remove them from the denominator. The blockpack lacks
the CPU-mode/context and version history needed to resolve their cause here.

For every other block, decoded instruction count times entries equals charged
instructions. The two unknown instructions explain the entire aggregate
difference. There are zero recorded unretired instructions in this census.
This supports a diagnostic weighted instruction mix, but cannot prove that
every later execution used the first captured bytes. Counts aggregate code
versions at a PC/size; only the first version is saved. No conflicting bytes
were found among overlapping first captures, which also does not prove absence
of later self-modification.

The block-category totals overlap: one block can contain integer operations,
calls, x87 and memory operations. For example, x87-containing blocks account
for 774,371,690 remaining instructions (30.05%). Weighting the decoded x87
instructions themselves by block entries gives 539,430,555 (20.93%); the latter
is an inference from first captures, not an instruction-by-instruction trace.
Neither percentage is a CPU wall-time fraction.

Installed Capstone package metadata reports **5.0.9**. Its Python
`__version__` reports **5.0.7**, and `cs_version()` returns `(5, 0, 1280)`.
All three identities are recorded rather than assuming they agree.

Private reproducibility artifacts, all under ignored `build/`:

- `assess_aot_coverage_20260925.py`: analysis script.
- `check_aot_assessment_20260925.py`: independent totals/support scan and authored checks.
- `aot-coverage-assessment-20260925.json`: detailed aggregate/shape report.
- `aot-coverage-assessment-20260925.log` and `...-check.log`: results.
- Original inputs: `aot4-census-code-12b.csv`, `.csv.blockpack`, `.report.json`.

Reproduce locally with `py -3 build/assess_aot_coverage_20260925.py`, followed
by `py -3 build/check_aot_assessment_20260925.py`. These one-off assessment
scripts and input artifacts are local evidence, not installed public tooling.
The checked-in document contains aggregate findings, not guest code.

Input SHA-256 identities:

- CSV: `19d34ed1b1fd7bf101b16d1be0db12a7d9764891eeafe1dcc1c22f8d87f9c820`
- Blockpack: `8fabfe6dedf13842448bf63199e836c00bf4aa54d1871d21d3d2a24e49101ccf`

## Architectural gaps, verified in source

| Area | Present restriction | What broad AOT needs |
| --- | --- | --- |
| Discovery | Explicit selected regions from first-executed captures; a small reachable-code decoder | Pinned image inventory, static code/data and control-flow recovery, explicit unresolved targets and separately labelled runtime observations |
| Integer execution | A small collection of operand forms; no CALL/RET/PUSH/POP lowering | General integer/addressing/stack/control-flow support with exact guest stack, flags and exits |
| Region dispatch | Launcher rejects more than 64 regions; region lookup is linear; overlapping registrations are rejected | Indexed entry lookup, scalable generated units, multiple entries and explicit image/version identity |
| Memory | Only `read32`/`write32`; already-resident certified RAM; no native refill or MMIO | Required widths, segment-relative accesses, mapping/refill, precise faults, device access ordering and alias invalidation |
| Extended state | ABI carries only GPRs, EIP, EFLAGS and retirement; Unicorn retains x87/MMX/SSE state | Qualified native FP/SIMD operations and explicit state ownership at all exits |
| System operations | Privileged, interrupt-shadow and unsupported contexts decline | Bounded native helpers for the target's required operations, exact interrupt/event behavior and state publication |
| Fault delivery | Error-code exception classes currently stop with delivery unmodeled | Explicit scope and faithful delivery before workloads requiring these paths can qualify |
| Code checking | Full region-byte comparison at each generated instruction | Scalable, tested identity/invalidation scheme; increasing region size alone makes this check more expensive |
| Evidence | One aggregate route; first code version; no per-phase opcode/target inventory | Static denominator, phase/version-aware discrepancy reporting, complete fallback accounting and native-required tests |

Sources: `tools/aot_compile.py`, `tools/prepare_aot_input.py`,
`src/app/main.cpp` native registration, `src/cpu/aot_executor.{hpp,cpp}`,
`include/xlle/aot_runtime.h`, `src/cpu/unicorn_aot_bridge.c`, and
`src/cpu/unicorn_floor.cpp` census and exception paths.

The current pointer caches are safe only under their existing narrow contract:
generated instructions cannot change MMU/segment state or run devices. Adding
such operations requires revisiting cache invalidation before admission. The
64-region launcher cap is a small change; scalable dispatch, overlapping code
versions and safe invalidation are the larger work behind it.

Pinned QEMU already contains x87 helpers in `qemu/target/i386/fpu_helper.c`.
Some use `GETPC()`, translated-code fault restoration, or memory helpers with
side effects. Their presence is an opportunity to qualify reuse, not evidence
that calling them from AOT is already safe. Retain third-party licensing.
This assessment does not select a new dependency or copy a backend.

## What "100%" must mean

Track three separate results. None can substitute for the other two:

1. **Static inventory coverage:** of the code recovered from each pinned image,
   how much has a qualified translation? List unresolved indirect transfers,
   ambiguous boundaries and unclassified executable ranges separately. Translating
   100% of a partial inventory does not establish whole-program completeness.
2. **Executed native coverage:** count AOT and fallback guest instructions in
   each defined workload phase. Exclude idle skips from the executed-work
   denominator and report them alongside it. Known native helpers must be
   accounted separately from calls that execute guest instructions through JIT.
3. **Product performance:** actual guest progress, distinct presented frames,
   latency, and native-versus-native total cost. High instruction coverage does
   not prove fast code or remove GPU/device costs.

The proposed first product scope is one pinned kernel/dashboard pair with boot,
idle animation, navigation, settings, and available memory/music workflows.
Declare unavailable workflows and asset requirements explicitly. State whether
boot is inside the native-required scope; beginning after boot must never be
reported as whole-machine 100%. The current injected-2BL boundary remains its
own documented limitation.

Use **95%, then 99% of executed non-idle instructions per defined phase** as
proposed coverage milestones, not predicted outcomes or completion criteria
for static recovery. Native-required runs additionally fail on any guest JIT
execution in their declared scope and identify the first unsupported case.
An indirect call can dispatch to a precompiled target at runtime and still be
AOT; encountering a target with no compiled body must remain visible.

Unknown/dynamically changed code may continue to use explicit fallback. The
goal is to investigate and minimize that list, not call it solved by renaming
fallback as a helper. Removing Unicorn entirely is outside this coverage goal.

## Proposed implementation work packages

This is an assessment of scope, not authorization to resume implementation.
Effort labels express complexity and uncertainty, not a calendar estimate.

| Package | Work and exit evidence | Assessment |
| --- | --- | --- |
| C0: define and measure the program | Image/build identities; executable-image inventory; initial static recovery with gaps; per-phase and per-version observations; strict fallback counters. Reconcile the two undecodable captures. | Medium; essential before trustworthy percentage claims |
| C1: general integer control flow | Qualified decoder/operand normalization; common widths, flags, stack, CALL/RET, conditional and indirect transfers. Execute authored multi-function programs with exact interrupted/faulted state and then useful real regions. | Large, but much of the expansion becomes systematic after the control-flow contract is settled |
| C2: scale artifacts and dispatch | Remove the launch cap; index entries; manage code-image identities and multiple entries; bound generated-unit/build cost. Validate stale/overwritten code and cross-unit branches. | Medium; correctness becomes harder around overlapping/reloaded code |
| C3: native memory and machine services | Qualified widths/segments, RAM misses/refills, ordered MMIO and port I/O, required system instructions, exact fault/event integration. Demonstrate execution without using JIT to warm each missing mapping. | Large and correctness-sensitive |
| C4: x87/SIMD | First qualify a small x87 load/arithmetic/store/state sequence at O0/O2, including exceptions and interrupted exits. Establish whether faithful existing helpers can be reused, then expand required FP/SIMD forms. | Large and currently one of the highest uncertainties |
| C5: efficient generated programs | Direct internal edges, local state where safe, flag and guard optimization, scalable invalidation. Preserve observability and measure against prior native code. | Large iterative work; full coverage must not remain permanently slow |
| C6: close coverage gaps | Translate the static inventory, investigate remaining targets/versions and every native-required failure. Exercise the workload matrix, including infrequent and error paths. | Open-ended tail; breadth depends on evidence gathered in C0 |

C0 starts first. A bounded C1 vertical slice and C4 helper qualification should
follow early, because they answer whether the bulk can proceed mechanically.
C2 is needed before wide generation; C3 gates admission of general memory and
machine operations. C5 proceeds alongside coverage growth with separate
correctness and timing evidence. C6 closes a scoped product, not all Xbox titles.
No speedup or week count is inferred from instruction totals.

### The next bounded package

1. Create the static image/entry/edge/gap inventory and explicitly reconcile
   runtime observations against it; do not use a trace to define all possible code.
2. Obtain useful per-phase work and fallback-reason evidence. Choose an integer
   multi-function path and an x87 path from that evidence.
3. Qualify the minimum general CALL/RET and memory interface needed by the first
   path, plus a separate small x87 helper experiment. Preserve portable output.
4. Run native-required probes, differential checks and full dashboard regression.
   Record helper, fallback, discovery and code-generation blockers explicitly.
5. Re-estimate C1/C3/C4 from these concrete results before scheduling the larger
   rollout. Runtime timing claims still require a quiet host and the existing
   interleaved acceptance methodology.

The assessment itself is complete. No C0--C6 implementation is claimed here.

## Architectural guidance from psxrecomp

The local sibling inspected was `F:/Projects/psxrecomp/psxrecomp`, source
`93a93c8e07c647f1414d759d370ce390b818de7a`. Its `docs/ARCHITECTURE.md` describes
separate offline compilation and hardware runtime projects. Its
`docs/FUNCTION_DISCOVERY.md` defines image-derived function/edge/gap artifacts
and explicitly distinguishes proved, heuristic and runtime-derived evidence.
These are useful contracts to adopt. This review did not qualify its runtime
or transfer its MIPS decoder/semantics to x86.

Our first-executed blockpack is useful evidence and a preparation input for the
existing prototype. It is insufficient as the sole authority for static program
coverage. Borrow the separation of discovery, generation, artifact identity and
runtime qualification; keep unproven targets visible rather than manufacturing
functions from every executable-section byte.
