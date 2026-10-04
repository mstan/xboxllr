# AOT-5 burndown: efficient portable native execution

**Sequencing update, 2026-10-04:** exploratory Blinx launch may proceed before
dashboard original-speed qualification. Dashboard slowness remains open;
parked candidates are retained for later title-workload comparisons. The
[current gameplan](GOAL-AOT-DASHBOARD.md) supersedes older ordering below.

**Work-order update, 2026-10-03:** the
[current ecosystem gameplan](GOAL-AOT-DASHBOARD.md) governs priorities.
Deliver useful enhanced rendering and a real game path with the existing hybrid
CPU before requiring strict AOT completion. Qualified enhanced/HLE subsystems
may ship by default with real LLE opt-outs. The AOT work below remains required
for eventual removal of dynarec; its older ordering is superseded.

Updated 2026-10-03. Owner: central Beads `beads-xbox.1.25.5`, under
`beads-xbox.1.25`. The owner has authorized implementation, with no stubs and
LLE as the required behavioral foundation. Work solo. Allow no competing builds
or workers during timing runs. Initial inventory, integer/call and x87-helper
evidence is recorded in [the foundations checkpoint](RESULTS-AOT-FOUNDATIONS-2026-10-02.md).

## Objective

**Owner pivot, 2026-10-02:** the [faithful AOT dashboard at original-speed 60 FPS](GOAL-AOT-DASHBOARD.md)
is primary. Maximize qualified renderer reuse. Blinx boot is the stretch goal;
JSRF remains later. The dashboard scope includes the real kernel and LLE devices.

Make the portable static/native path more efficient and move useful dashboard
work into it. Static/native is a required deliverable. Compare each optimization
with the preceding native implementation at equivalent guest work. Unicorn is
the correctness reference and fallback; beating its speed is not an acceptance
requirement. Faithful LLE and the broader 60 FPS with headroom target remain.

## Starting point

AOT-1 through AOT-4 are validated and integrated. The starting source commit is
`82d0875`; the [AOT-4 results](RESULTS-AOT4-2026-09-07.md) record 49 passing tests,
O0/O2 reference replays, private-input safeguards and actual dashboard native
execution. Those are prior validation results, not fresh timing measurements.

The two selected regions retire 38.9% of the full replay instruction clock, mostly
in startup/delay loops. This does not establish steady dashboard coverage.
The ordinary build has private input disabled. The saved private native executable
and preparation workflow remain available locally; generated guest code stays
outside Git. Follow [the private workflow](AOT-PRIVATE-WORKFLOW.md) to rebuild.

## Burndown board

The observer implementation and tests are validated; its controlled overhead
measurement and clean native attribution remain open. See
[the first iteration evidence](RESULTS-AOT5-OBSERVER-2026-09-11.md).
The correctness replay also exposed native/fallback partition variation despite
matching images and device/DMA digests. A controlled fixture proves that host-time
TLB sizing can change native eligibility. The
[deterministic cache policy](RESULTS-AOT5-TLB-2026-09-11.md) removes that host-time
input. Full O0/O2 native work and CPU-census records now match, with zero code-TLB
declines on both reference replays. Clean attribution and timing remain open.
Rows measure deliverables,
not equal amounts of effort; they are not a calendar or percentage estimate.

| ID | Status | Work | Evidence required to finish |
| --- | --- | --- | --- |
| 5.1 | Implemented and tested; controlled cost pending | Detect competing work during replay | A controlled foreign process starts and ends inside a leg and still marks it contaminated; owned emulator excluded; observer failure visible; monitor cost and detection limits recorded |
| 5.2 | Clean full-route and bounded post-boot leaf attribution qualified; exact script-phase alignment remains explicit | Profile the current native path and inspect generated host code | Current binary/source identities; clean separate attribution; generated-body, boundary and fallback costs; native/idle/fallback partitions by phase; ranked bottleneck hypotheses |
| 5.3 | Planned | Improve generated control flow and state handling | Direct block edges and local guest values where justified; exact state on every observable exit; inspect host output; measure each retained transformation against native baseline |
| 5.4 | Planned | Reduce repeated flag and guard work | Independent tests of defined flags and every affected exit boundary; code/event changes through callbacks remain visible; each proposed hoist has an explicit validity/invalidation argument |
| 5.5 | Deterministic cache policy validated; further memory work open | Improve certified memory access and avoid unnecessary fallback attempts | Preserve permissions, aliases, self-modifying code and precise faults; diagnose actual fetch-TLB declines; measure callback/invalidation and handoff costs separately where practical |
| 5.6 | Foundations implemented; product coverage open | Expand useful post-boot native coverage | Define guest-anchored animation/navigation windows; rank uncovered work; qualify and execute additional useful regions; report coverage separately from startup and idle skips |
| 5.7 | Repeated per landing | Validate, document and integrate | Relevant differential tests, O0/O2 generated-C checks, full dashboard regression and suite for runtime changes, native-versus-native A/B, private-material checks, commit/push and Beads evidence |

Work through 5.1 and 5.2 first. Their findings determine the order of 5.3 through
5.5; the list does not claim that every proposed optimization will help. Prepare
the post-boot workload in 5.6 early enough to avoid optimizing only delay loops.
Apply 5.7 to each useful landing instead of accumulating an unreviewed rewrite.

### Qualified native efficiency checkpoint (2026-10-03)

The [exact membership index](RESULTS-AOT-MEMBERSHIP-2026-10-03.md) qualifies
the first retained lookup improvement for the 246-region scalar input. Full
27B O0/O2 preserve all 270 checkpoints, ten images and native counters; all
74 public tests pass. Six quiet interleaved native-versus-native legs reduce
total 12B wall time by **17.39% median / 17.04% minimum**, passing the 5% gate.
The previous tree path remains selectable. This advances 5.5 and 5.7; it does
not close post-boot performance or the broader compiler/runtime work. The first
host profile was contaminated and remains excluded from attribution acceptance.
A subsequent clean 27B candidate profile verifies all 101,633 leaf samples and
separate post-boot interval costs. Native bodies and renderer work dominate those
intervals. The [sampler follow-up](RESULTS-HOST-SAMPLER-WINDOWS-2026-10-03.md)
resolves the clamped long tail with recorded bounds and 18 separately qualified
windows across 27B. Fixed windows can straddle scripted phases; observer cost
qualification remains open. Public tests now pass 76/76.

### Near-total coverage assessment (2026-09-25)

The [static-coverage assessment](ASSESSMENT-STATIC-COVERAGE-2026-09-25.md)
adds a measured gap inventory and proposed C0--C6 work packages. Only 896 of
23,666 historical captured block shapes are fully supported by the current
decoder/parser. Beyond the two selected loops, those shapes account for only
0.397% of the remaining recorded instruction work; this is syntax support, not
runtime eligibility. Blocks containing x87 account for 30.05% of that remaining
work, counting the entire block. Phase attribution and complete static recovery
remain unknown. The launcher cap, memory/extended-state ABI, fault handling and
code-identity costs also need explicit work before wide native coverage.

The next scope should establish static inventory and per-phase discrepancies,
then qualify a general integer CALL/RET/memory path and a bounded x87 helper
path. Instruction coverage, static recovery and product performance are separate
acceptance measures. That assessment did not itself resume implementation;
the owner's 2026-10-02 instruction now authorizes it.

The [active coverage burndown](PLAN-AOT-COVERAGE-BURNDOWN.md) turns C0--C6
into checklists and completion gates, with a first package covering C0, one
general integer path and initial x87 qualification. C0/C1 and the bounded C4
experiment are in progress; none of those stages is closed. Dashboard completion
precedes the Blinx stretch goal and JSRF. Blinx real-kernel launch and removal of inherited device
shortcuts are tracked in [the separate launch burndown](BLINX-BURNDOWN.md).

## First bounded work package

Fix the before/after-only process census in `tools/run_stockdash_replay.ps1`.
Extend the existing `tests/test_replay_quiescence.ps1` with a process that is
absent at both endpoints but runs during the leg. Include the compiler families
actually used on this host, explicit resident-process handling, and a record of
monitor health. Use an event or sampled observer with documented limits; a
sampled observer must not claim to catch arbitrarily short processes.

Record observer mode, lifetime, actual observation gaps or event health, and
errors in the evidence. A configured polling interval is not a guarantee of
detection. Identify the owned leg and observer explicitly, accounting for PID
reuse; do not exclude an entire process tree that could conceal competing work.
With required quiescence, an unavailable or failed monitor makes a timing leg
unusable. Test a real mid-leg contaminant, an unrelated same-name process beside
the owned leg, and monitor failure; census stubs alone do not prove detection.

Keep all attempted legs and exclude runs based on contamination evidence, never
their timing. Do not stop other projects' processes. Measure observer overhead
and use equivalent monitoring in both A/B arms. After this lands, collect clean
current-native attribution and choose the first compiler/runtime experiment.
Older before/after-only records remain historical evidence, not substitutes for
new monitored acceptance measurements.

## Rules for compiler and runtime changes

- Keep the conservative generated path available for comparison while qualifying
  optimized output. Prefer general decoded transformations over patches tied to
  a particular guest address or dashboard snapshot.
- Preserve exact EIP, registers, defined flags and retirement at faults, events,
  budgets, code changes and fallback exits. A flag unused by the next instruction
  can still be observed by an interrupt. A read-only span can still invoke a
  callback that changes code or signals an event.
- Preserve permissions and invalidation across physical aliases, native writes,
  host/DMA writes and mapping changes. Unsupported cases decline without effects.
  Never use Unicorn timeout or mix positive-count and zero-count execution in one
  engine. Keep private assets and generated guest code out of source publication.
- Use the [measurement methodology](PERF-METHODOLOGY.md): uninstrumented headline
  timings, quiet same-session interleaved 3+3 or more, total 12 B wall minimum and
  median, and the existing 5% gate for a speedup claim. Separately report startup,
  post-boot work, native coverage, guest speed, successful presents and latency.
  Preserve negative results; an AOT capability does not need to beat Unicorn.

## Stopping points and completion

The first stopping point is validated contamination detection plus a usable
current-native profile. The next is a qualified, measured improvement to native
execution, with a native baseline preserved and the next experiment ranked.
Neither stopping point closes all of AOT-5.

For this AOT-5 cycle to finish, deliver useful post-boot coverage growth and
measured native-efficiency improvement, resolve or record the compiler/runtime
experiments with evidence, and complete the validation/integration row. A set of
rejected experiments alone does not establish the requested improvement. Record
remaining work toward predominantly AOT execution and the product frame-rate
target; do not claim global optimality or 60 FPS from this checklist alone.

## Goal for the first iteration

> Start the AOT-5 burndown in `docs/GOAL-AOT5-BURNDOWN.md` from the current repo
> state. Use subagents for bounded independent work where appropriate, with
> independent review and a quiet host for timing. First fix and validate in-run
> contamination detection, then
> obtain clean attribution of the current native path. Use that evidence to
> implement and validate the first useful native-code optimization, comparing it
> against the previous native implementation. Preserve faithful LLE behavior,
> precise fallback, portability and private-asset protections. Record rejected
> hypotheses, commit and push validated work under my existing authorization,
> and leave the board and Beads ready for the next iteration. Static/native is
> required; beating Unicorn is not the benchmark.
