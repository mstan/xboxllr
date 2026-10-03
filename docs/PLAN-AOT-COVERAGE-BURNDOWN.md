# Burndown: predominantly static Xbox execution

Created 2026-09-25; activated 2026-10-02 by the owner. Central Beads:
`beads-xbox.1.25.5`. Status: **implementation in progress**. Work solo.
The owner authorized implementation and reference clones; real-console access
remains deferred until a concrete blocker requires it. No stubs are admissible.

This turns the [coverage assessment](ASSESSMENT-STATIC-COVERAGE-2026-09-25.md)
into deliverables. It expands AOT-5; it does not replace the existing
[efficiency and validation board](GOAL-AOT5-BURNDOWN.md).

## Target and boundaries

Compile the pinned Xbox kernel/dashboard code into portable host code before
launch, preserving the faithful hardware runtime. Make this the primary
execution path. Keep unsupported cases explicit and keep Unicorn available as
the correctness reference and fallback. Retaining qualified CPU/MMU/arithmetic
helpers does not require retaining JIT guest execution for supported code.

The owner pivoted on 2026-10-02: the [faithful AOT dashboard](GOAL-AOT-DASHBOARD.md)
is the primary product, using qualified renderer reuse to target original-speed
60 FPS. Blinx boot is the stretch goal and Jet Set Radio Future remains later.
The first strict AOT qualification is a declared post-boot dashboard animation
and navigation scope including kernel code; whole-boot AOT is a
separate gate. C0 must name input revisions and workload boundaries. Boot,
initialization, gameplay, transitions, audio and save/reload remain distinct
entries; unavailable workflows get explicit blockers. ARM hardware qualification
is later. HLE requires an implemented LLE counterpart and proof of the complete
guest-visible contract, beyond ABI compatibility. Public tooling must not contain
private guest code or console secrets. The first actual game launch can use the
existing CPU floor: see [Blinx's launch dependencies](BLINX-BURNDOWN.md).

Three coverage milestones have distinct meanings:

| Milestone | Required result |
| --- | --- |
| 95% executed AOT | At least 95% of actually executed guest instructions use precompiled code in **each required workload phase**, with idle skips and HLE replacements reported separately |
| 99% executed AOT | Same per-phase accounting at 99%; every remaining fallback family has an owner, reason, frequency and next action |
| 100% within a declared test scope | Native-required mode completes the full declared workflows with no guest instruction interpreter/JIT and no runtime guest-code compilation; all scheduled state/output checks pass |

All three also report static inventory coverage: qualified recovered instructions
versus untranslated ones, unresolved transfers, ambiguous code/data and unknown
image ranges. A complete translation of a partial inventory is not proof that
every possible software path is covered. The 100% result must name its tested
scope; a post-boot result must never be labelled whole-boot coverage.

The 60 FPS with headroom objective is a separate product gate. Coverage alone
does not meet it. Native optimizations compare against the preceding native
implementation, at equivalent work; beating Unicorn is not the acceptance goal.

## Board

C0, C1, C2 and the bounded C4 helper experiment are in progress. No C0-C6 stage is
closed by the initial qualification. See the [dated evidence and limits](RESULTS-AOT-FOUNDATIONS-2026-10-02.md).
Existing AOT capabilities, tests and cache fixes are inputs, not work to repeat.

The [menu phase checkpoint](RESULTS-DASHBOARD-PHASES-2026-10-02.md) adds nine
cumulative snapshots, six reconciled intervals and qualified general LEA
lowering. The previous startup regions have zero coverage in those intervals.
Measured MMX/x87 and kernel gaps drive the next C3/C4 work; code-version-aware
attribution remains open. No whole stage closes from this checkpoint.

The [exact-deadline follow-up](RESULTS-CPU-DEADLINES-2026-10-03.md) resolves the
MMX route's observed event-order gap and qualifies a revised 27B input route.
Floor/O0/O2 histories and images agree. Its repaired census reconciles all six
intervals. The [eight-phase qualification](RESULTS-IDLE-PHASES-2026-10-03.md)
preserves the fully executed dashboard's images and device/DMA histories while
removing another 4.294B idle instructions from execution. Current native
main-menu idle coverage at that checkpoint is 26.04%, versus 10.02% with four phases; native menu
work is unchanged. Executed idle work stays included, and proven skips are
reported separately. Required x87 and kernel/memory operations remain explicit
work. No whole coverage stage closes from the idle optimization.

The [general x87 checkpoint](RESULTS-AOT-X87-2026-10-03.md) adds fixed 80-bit
arithmetic/transfer services and a real dashboard math region. Main-idle native
coverage at that checkpoint is **39.33%**, with the same idle skips; all six intervals reconcile.
Updated floor/native O0/O2 retain the previous 27B history and images, with
identical native counters. Public tests pass 72/72. Unqualified exception/stack cases and
memory accesses explicitly remain floor work. This advances C4 without closing
it or the full-route C6 gate.

The [expanded x87 checkpoint](RESULTS-AOT-X87-EXPANSION-2026-10-03.md) qualifies
general register multiply/subtract/free and TEST32, with independently reproduced
floor exception corrections. Five private regions now yield **47.92% main-idle
native coverage**, with unchanged idle skips. Final floor/native O0/O2 retain
all 270 checkpoints and ten images across the 27B route. All six phase intervals
reconcile; public tests remain 72/72. Kernel integer operations and cold/device
memory remain substantial gaps; no whole stage closes from this expansion.

The [kernel integer checkpoint](RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md) adds
general ROL/ROR32, OR32 and BSWAP with count/flag/alias and strict machine tests.
Five additional regions move 507,548,876 instructions into native execution.
Settings-entry native coverage rises from 29.33% to **61.32%**; main-idle stays
**47.92%**. Floor/native O0/O2 preserve all 270 checkpoints and ten images over
27B; public tests pass 72/72. Shifts, extension/narrow transfers, branches,
LEAVE and memory/system operations remain measured gaps. C1/C3/C6 remain open.

The [scalar checkpoint](RESULTS-AOT-SCALAR-2026-10-03.md) adds general shifts,
all short/near conditions, MOVZX8/16 and immediate stores. ABI 5 certifies exact
narrow reads; independent and strict machine tests pass. **246 regions** yield
**70.53%** main-idle native execution and **76.72%** at Settings entry. All 270
checkpoints and ten images match across floor/O0/O2 and the previous reference;
public tests pass **74/74**. Wider selection also creates substantially more
native/fallback handoffs, including repeated zero-retirement admission attempts.
C5 must qualify and reduce that cost; the coverage gain is not a speed claim.
Error-code CPU exception delivery is an existing open LLE gap tracked in
`beads-xbox.1.36`. No whole C0-C6 stage closes from this checkpoint.

The [exact membership checkpoint](RESULTS-AOT-MEMBERSHIP-2026-10-03.md) advances
C5 with a qualified general lookup improvement: **17.39% median / 17.04% minimum**
less total 12B startup wall time in six quiet native-versus-native legs. Full
27B checkpoints, images and native counters are unchanged. This improves lookup
cost without changing coverage or handoffs; whole-menu performance remains open.

The [memory integer checkpoint](RESULTS-AOT-MEMORY-INTEGER-2026-10-03.md)
advances C1/C3/C6 with general memory-read ALU/comparisons, register NEG/NOT and
LEAVE. The 286-region input preserves its 246 predecessors; main-idle native
execution reaches **77.60%**, Settings entry **82.08%**. Full 27B floor/O0/O2
histories and ten images match, with identical native counters. Public tests
pass 76/76 and census scalar/native-required tests 4/4. Attempts/handoffs increase;
efficiency remains a separate gate. Interrupt control, strings, byte-width
operations, x87/SSE and translation refusals remain. No whole stage closes.

| ID | Deliverable | Depends on | Difficulty | Gate to close |
| --- | --- | --- | --- | --- |
| C0 | Trustworthy program and workload inventory | Existing capture/tools | Medium | Pinned images, initial static graph with explicit gaps, phase/version evidence, reconciled counters and a tested native-required detector |
| C1 | General integer execution across functions | C0; minimum C3 interfaces as needed | Large, increasingly systematic | Useful real multi-function code executes through qualified integer/control-flow lowering; exact state and deliberate fallback cases pass |
| C2 | Scalable artifact generation and dispatch | C0; C1 entry/exit contract | Medium | More than 64 entries/regions work; indexed dispatch, build partitioning and code-image identity tests pass |
| C3 | Native memory and machine operations | C0; shared C1 interface design | Large | Required memory widths/segments, cold mappings, device accesses and system operations execute with precise state and no hidden guest execution in helpers |
| C4 | Qualified x87/SIMD execution | C0; minimum C3 interfaces | Large, high uncertainty | Extended-state ownership and required operations pass independent and differential checks, including interrupted/faulted execution |
| C5 | Efficient generated programs | C1 onward; clean attribution | Large, iterative | Retained general transformations have faithful exits and measured native-versus-native gains; costs and rejected experiments recorded |
| C6 | Coverage closure and usable native build | C0-C4; C5 continues | Tail depends on unresolved gaps | 95%, 99%, then native-required workload gates; reproducible private build; static gaps and product performance reported honestly |

These are dependency stages, not equal-size tickets or a percentage-complete
chart. Track completed checks and evidence inside each stage. C4 begins with an
early small qualification, rather than waiting until all integer work is done.
C5 continues during expansion so high coverage does not become a permanently
slow endpoint. Quiet-host timing is required for performance claims; it does
not block independent correctness or static-discovery work.

## C0: establish what must be compiled

- [ ] Pin the kernel/dashboard/input identities and define the workload matrix,
  including the exact boot boundary and reproducible guest-anchored phases.
- [ ] Produce an image manifest: mapped executable ranges, loading/relocation
  information where known, entry points, and code-image identities.
- [ ] Produce a first static instruction/control-flow inventory. Keep proven
  entries, heuristic candidates, unclassified ranges and unresolved indirect
  targets separate. Never sweep executable bytes into invented functions.
- [ ] Reconcile observed PCs against this inventory. Runtime discoveries become
  explicit discrepancies to investigate, not silent proof of static completeness.
- [ ] Extend evidence collection to identify phase, execution mode, code version
  and fallback reason. Reconcile the two undecodable one-instruction captures.
- [ ] Add native-required validation: fail at the first disallowed guest
  execution fallback, including hidden fallback inside a proposed helper.
  Test a deliberate fallback, successful native execution and a permitted fixed
  arithmetic/device helper. Scope any boot exclusion explicitly.
- [ ] Report counters with
  `guest clock = native executed + fallback executed + idle skipped`
  for the faithful profile. Report any replacement/HLE work separately.

**Close C0 when:** another developer can reproduce the inventory and phase
report, unresolved work remains visible, all totals reconcile, and the strict
detector provably catches fallback. Complete static recovery is not required
to close this first inventory stage; unresolved recovery stays open in C6.

## C1: make ordinary functions work

- [ ] Qualify a pinned decoder/operand normalization approach. Capstone is the
  existing decode-only candidate; existing xboxrecomp backend rejection remains
  evidence. Do not import its incompatible state/memory assumptions wholesale.
- [ ] Implement common integer widths, addressing, arithmetic, comparisons,
  branches and stack forms through general lowering rules. Derive ordering from
  the inventory rather than a flat opcode checklist.
- [x] Implement bounded 32-bit near CALL/RET with the actual guest return address and stack.
  Preserve EFLAGS and exact state at faults, interrupts, budgets and fallback.
- [ ] Support indirect transfer to compiled entries through checked dispatch;
  a missing or invalidated target must be visible and fail safely.
- [ ] Exercise authored multi-function programs, including a callee changing
  flags, stack-access failures, callbacks/indirect targets and interruption
  inside a function. Validate O0 and O2 generated output.
- [x] Translate and execute an identified useful real path beyond the two
  countdown loops. Record actual native instructions and remaining exits.
  Evidence: isolated Blinx indexed lookup/call path, ten instructions, O0/O2,
  all prefix states and RAM equal, zero fallback in an explicitly warmed scope.
  This closes the narrow path check, not the entire C1 stage or game startup.

**Close C1 when:** the selected real path and its independently authored
boundary cases pass. This gate proves general machinery; completing every
integer form encountered across all workflows remains part of C6.

## C2: scale without weakening identity checks

- [x] Remove the 64-region launcher cap for default selection/registration.
  All generated regions are selected by default; the explicit legacy 64-bit mask
  remains a diagnostic selector for indices 0..63. A 257-region launcher probe
  validates default-all, bit-63-only and zero selections.
- [x] Index address lookup while preserving registration/statistics order.
  Production-generated O0/O2 tests execute a shuffled 257-region branch cycle
  with zero strict-scope fallback and reject stale code after a guest-byte change.
- [ ] Partition generated output into bounded build units and add arbitrary
  explicit subset selection beyond the legacy diagnostic mask.
- [ ] Represent multiple entry points and overlapping/reloaded code identities
  explicitly. A stale body must never run simply because its address matches.
- [ ] Test cross-unit branches, alias writes, reloads and registration failures.
- [ ] Record generated size, build time, dispatch cost and validation cost. Do
  not make whole-program region size amplify a full-byte check per instruction.

**Close C2 when:** a generated corpus exceeding the previous limit registers,
executes and invalidates correctly, with reproducible artifact identities and
no hard-coded fixes to emitted guest source.

## C3: make hardware access possible from native code

- [ ] Define memory outcomes and state ownership for all widths/segments the
  target requires. Preserve partial instruction progress where the architecture
  requires it; do not assume every instruction is an all-or-nothing transaction.
- [ ] Service cold translations and permission checks without executing guest
  instructions through Unicorn merely to warm a mapping.
- [ ] Route MMIO and port I/O through faithful device interfaces with correct
  ordering. A declined/retried access must not duplicate device side effects.
- [ ] Qualify required control-register, segment, clock and interrupt operations.
  Revisit invocation-local pointer caches whenever new operations can invalidate
  their assumptions.
- [ ] Support or explicitly block required fault-delivery paths. A stopped
  unmodeled exception cannot count as successful coverage.
- [ ] Validate cross-page accesses, aliases, permission changes, self-modifying
  code, synchronous callbacks and exact event deadlines.

**Close C3 when:** selected cold-memory and device/system paths pass native-required
tests, with precise guest state and faithful effects. Fixed helpers may retain
qualified Unicorn/QEMU internals; helpers that secretly execute guest instructions
must count as fallback.

## C4: qualify floating point early, then expand

The [2026-10-03 MMX checkpoint](RESULTS-AOT-MMX-2026-10-03.md) adds engine-owned
MMX state services, full-width transfers and strict generated machine tests.
The real menu loop executes natively, and the subsequent exact-deadline work
resolved its event-digest gap. The [x87 checkpoint](RESULTS-AOT-X87-2026-10-03.md)
adds production 80-bit services and a qualified real math region. Broader
operation, exception/stack and memory coverage remain open; C4 is incomplete.

- [ ] Define x87/MMX/SSE state ownership across generated code, helpers, interrupts
  and fallback. Include the control/status state, not only register values.
- [x] Qualify a small x87 load/arithmetic/store path with independent expected
  results, O0/O2 generated code, interruption and exceptional inputs.
  The masked, valid-stack RAM path passes strict native O0/optimized fixtures
  and independent status/rounding tests. Unmasked/stack contracts remain tracked
  separately; this bounded result does not qualify every x87 state.
- [x] Determine whether pinned arithmetic helpers can safely be reused outside
  a translated block. Review `GETPC()`/fault-restoration and memory assumptions.
  Preserve third-party licenses and identity pins.
  Selected arithmetic/register primitives have no guest execution. Native
  stores certify RAM before conversion; faulting floor stores retain their real
  memory path and stage FPU effects. Other helpers still need qualification.
- [ ] Expand the required x87/SIMD forms and qualify rounding, flags, exception
  behavior and memory access. Host C floating-point behavior is not an assumed
  substitute for the guest semantics.
- [ ] Execute a measured real FP path under native-required validation.

**First C4 stopping point:** a demonstrated helper/state design, or a specific
failed design with the next concrete implementation approach. A rejected shortcut
does not complete the required FP capability. Close the stage after the required
real path and its semantic/exit checks pass.

## C5: make generated native code efficient

- [ ] Finish clean attribution and qualify measurement-observer cost. Separate
  generated bodies, state/flag handling, guards, memory helpers and transitions.
- [ ] Qualify direct internal edges, preserving existing guards and exact exits.
- [ ] Keep guest values local where legal; materialize precise observable state
  before callbacks, faults, events, code changes and external exits.
- [ ] Qualify flag/guard work reductions and scalable code invalidation, with
  explicit validity arguments and independent tests.
- [ ] Measure retained candidates against preceding native code: same-session
  interleaved 3+3 or more, quiet host, noperf headline, total 12 B wall minimum
  and median, existing 5% gate. Retain rejected results as evidence.
- [ ] Report useful post-boot throughput, distinct presented frames and latency
  alongside the boot replay. Document the headroom threshold before claiming it.

Capability expansion can temporarily cost performance; record it as a capability
change with an optimization path, not as a timing PASS. Optimizations require
their own measured evidence. Neither these checks nor a 99% coverage count alone
closes the broader 60 FPS/headroom objective.

## C6: burn down every remaining gap

- [ ] Generate qualified static code for the recovered inventory, including
  infrequent paths. Track unsupported forms, unknown transfers, image versions,
  runtime/context limitations and semantic defects as distinct categories.
- [ ] Reach the 95% per-phase gate; rank all remaining fallback families.
- [ ] Reach the 99% per-phase gate; preserve a complete residual-case ledger.
- [ ] Complete native-required workflows and state/output comparisons. Keep
  strict failure detection active; do not hide rare fallback to improve a number.
- [ ] Revisit unresolved static ranges and error paths. Publish what remains
  unknown separately from the successful test-scoped 100% result.
- [ ] Reproduce the build from owner-provided inputs with portable tooling and
  guest compilation complete before launch. Keep generated code/executables
  containing private inputs outside source publication.
- [ ] Record actual native efficiency and remaining GPU/device costs; update
  the next product-performance work package from measured evidence.

## Recommended first implementation package

The next authorized implementation cycle should deliver **C0, one bounded C1
path, and the initial C4 qualification**. This answers the largest uncertainties
without attempting all seven stages in one unchecked rewrite.

1. Land the C0 inventory, phase evidence and strict fallback detector.
2. Choose one useful multi-function integer path and one FP path from that
   evidence. Require real execution; compilation alone is insufficient.
3. Implement the smallest general CALL/RET and memory support needed by the
   integer path, plus the separate x87 state/helper qualification.
4. Validate each increment and preserve the prior native binary for comparison.
   Run relevant differential/O0/O2 checks and the dashboard regression for
   runtime changes. Apply the established private-material checks.
5. Update the board with actual blockers, dependency changes and a revised
   estimate for the broader C1/C3/C4 rollout. Record commit/branch identities
   when integration is authorized and performed.

If a bounded qualification fails, capture the cause and continue useful
independent work within the authorized package. Do not silently redefine the
goal to omit FP, memory or static output. Escalate only a genuine owner decision
or external dependency; ordinary implementation choices need no check-in.

### Suggested future instruction

> Begin the first package in `docs/PLAN-AOT-COVERAGE-BURNDOWN.md`: complete C0,
> execute one useful real multi-function integer path through general AOT
> CALL/RET and the required memory support, and qualify a small native x87 path.
> Work solo. Preserve faithful LLE, portable static output, precise state,
> explicit fallback and private-input protections. Use the existing differential
> and dashboard checks; claim performance only from clean interleaved evidence.
> Update the board and Beads with results and the revised remaining work. Stop at
> that validated checkpoint or a concrete blocking owner decision. Do not expand
> into the entire C1--C6 rollout or push changes without authorization.

This quoted instruction is a proposal for the owner to issue later, not current
authorization. The assessment and this plan are complete; implementation is paused.
