# Native CPU bookkeeping assessment, 2026-10-18

Tracked by `beads-xbox.1.100`, branch `perf/blinx-cpu-bookkeeping-20261018`.
The comparison starts at framework `8d3bce3` and private BlinxRecomp `739111c`,
after `/O2` and compact indirect dispatch were already enabled. There is no x86
interpreter fallback in the measured fast gameplay configuration.

## What was measured

Original-input stationary and movement/jump/vacuum windows cover Round 1 Stage 1
and Round 3 Stage 1 (original stage indices 0 and 8). Both start from independent
copies of the same authentic completed QA save. Runs are hidden and muted, use
21:9 adaptive framing at internal 640x480, normal priority and logical CPUs
8/10/12/15. No real Xbox operations or owner-save writes are involved.

CPU milliseconds per **original game update** are process/thread execution time,
not display FPS. Unprofiled windows span 180 updates after 100 warmup updates.
Leased controller actions follow guest-update milestones. Host load and the
game's wall clock remain uncontrolled; matching spawn and input does not imply
a bit-identical world replay. Timing comparisons do not use the IP sampler.

A separate Windows x64 external sampler briefly captures the main thread's RIP
and integer registers, then resumes it before PDB lookup/disassembly. Matching
PDB TLS offsets, TEB/TLS-base reads and actual sampled address calculations
distinguish register fields, guest stack, guest memory, TLS lookup and native
services. Movement collected 2,069 positions over 182 updates at approximately
222 Hz; average suspension was 171 microseconds. These are **native resume-IP
samples, not retired instructions or hardware-cycle shares**. DLL/wait positions
stay in the denominator. Sampling runs are diagnostic, not performance results.

| Cost | Evidence and implication |
| --- | --- |
| GPR/TLS access | Movement includes 46 direct GPR-cell positions and 90 TLS lookup positions. A separate strict recognizer attributes 155 positions to complete 11-instruction GPR address setup/access sequences in the large original routines. Counts overlap; do not add them. Repeated address materialization is real. |
| Flags | Source emits local lazy-operand/result snapshots and signed mirrors. Unoptimized-looking large native bodies retain stack initialization and stores that optimized small routines eliminate. This sample does not isolate a reliable flags-only CPU percentage. Do not delete flag state based on frequency or native execution alone. |
| Guest stack | Eight direct sampled guest-stack memory positions, plus native call/stack work. Guest return slots, original stack arguments, cleanup and nonlocal unwind remain observable. TLS address caching changes none of them. |
| Entry hooks | Roughly 50,000 stationary / 55,000 movement entries per update in the first-stage samples; later-stage samples can be higher. Entry contains a required suspension safe point, history and counters. Six movement positions resolve inside the hook; this does not measure its caller-side optimization barrier. |
| Exit hooks | The header declares an empty exit helper, but an audit of the actual generated C finds **no exit-hook calls**. Removing that helper cannot speed this build. Earlier working notes suggesting one exit call per generated return were incorrect. |
| Native services | Graphics texture/state/resource work and OS/driver waits still occupy substantial samples. CPU-register improvements cannot establish a title-wide presentation rate. |

## Actual machine code

The exact baseline executable is `a7ae3b2be65a568134db1032dc490d0c65b747cd2ca8b44acd4f7b732ecfb98f`.
The compiler is MSVC 19.41, and its generated project/actual command lines contain
`/O2 /Ob2 /fp:precise`; no source pragma disables optimization. Nevertheless,
some large routines have repeated eleven-instruction TLS setup sequences,
redundant self-moves and stack-backed lazy flag temporaries.

| Original routine | Baseline native bytes | Baseline static TLS-index references | Eight-routine address-cache experiment: bytes / references |
| --- | ---: | ---: | ---: |
| `267C0` | 8,581 | 2 | 5,957 / 1 |
| `33E20` | 18,989 | 12 | 12,755 / 2 |
| `37E90` | 17,579 | 1 | 9,815 / 1 |
| `27020` | 47,230 | 1 | 25,801 / 1 |
| `C87A0` | 1,084,680 | 19,257 | 402,221 / 78 |
| `AB4F0` | 4,301 | 1 | 4,326 / 1 |
| `DB460` | 434,210 | 7,735 | 37,960 / 4 |
| `D48E0` (player update) | 1,254,474 | 21,836 | 537,830 / 78 |

These counts come from each containing PE unwind range, not dynamic execution
counts. Code size alone is not a performance result.

An isolated compilation of the unchanged original `D48E0` with the experimental
MSVC `/d2OptimizeHugeFunctions` flag reduced its object from 1,782,424 to 528,383
bytes, but increased compilation from 2.9 to 295.4 seconds. This establishes
that full optimization of this body produces materially different code; it
does not establish a gameplay gain. The undocumented flag is not a production
build requirement. A Clang 17 diagnostic compilation also succeeded; changing
the shipping compiler has not been qualified by that compilation.
Microsoft documents huge-function optimization/throughput tradeoffs in
[its code-generation investigation](https://devblogs.microsoft.com/cppblog/improving-code-generation-time-with-cpp-build-insights/)
and [compiler throughput advice](https://devblogs.microsoft.com/cppblog/visual-studio-2017-throughput-improvements-and-advice/).

## Candidate and acceptance gate

The experimental `native_register_addresses.py` caches **addresses of existing TLS objects**
inside explicitly selected original functions. Reads/writes still reach the
same authoritative objects, including when header macros access them. No value
shadowing, delayed writeback, `restrict`, register-layout change, direct assembly
or optional instruction requirement is introduced. Other compilers retain
ordinary portable C. The reference generation simply omits the selection.

The eight-routine experiment (`47b42e7...`) is not accepted. First sequential
comparisons were worse while host load ranged from 37% to 98%. Three alternating
resident pairs at stage 8 then measured median main-thread CPU time of
30.38 -> 31.68 ms/update stationary, and 29.60 -> 27.43 ms/update moving.
That is a regression stationary and an improvement moving, not a general win.
Physical spawn resets and 100 original settling updates were explicit cheats;
they did not reset the entire world. Both processes were frozen between windows.

A second candidate narrowed selection to `C87A0`, `DB460`, `D48E0`, the three
large bodies with pervasive repeated TLS lookup. Smaller already-optimized
routines kept their original source, verified in actual native code by matching
their baseline byte/instruction/TLS-reference counts. This candidate also fails
the performance gate: alternating stage-8 medians were 29.60 -> 31.42 main-thread
CPU ms/update stationary, 27.86 -> 30.38 moving. It is **not accepted**. An initial
three-routine build was excluded before launch because restoring source with
`copy2` preserved timestamps older than experimental objects. The harness now
touches restored units; the actual measured replacement is `6cc3e578...`.

The full-optimization experiment kept every original function source and compiled
only `D48E0` with full MSVC optimization, using the already completed isolated
object. Its native body shrank from 1,254,474 to 371,383 bytes. The remaining
units kept ordinary optimization, the same register header and original entry
hook/suspension logic. That candidate is `654287f1...`.

For direct attribution, separate diagnostic images surrounded the normal and
fully optimized original body with an identical small `QueryThreadCycleTime`
wrapper. COFF instruction-section hashes prove that symbol renaming for these
wrappers did not alter either original body's machine code. Both execute once
per original game update in these windows. Three alternating pairs per case:

| Stage 8 case | Normal/full player cycles per update, median | Normal/full main CPU ms per update, median |
| --- | ---: | ---: |
| Stationary | 1,869,621 / 1,473,212 | 35.16 / 31.77 |
| Movement | 2,103,127 / 1,845,063 | 28.47 / 31.51 |

The routine uses 12–21% fewer cycles, while the total CPU comparison still gives
mixed results. An additional short measurement reads the main thread's cycle
counter with the target frozen at each endpoint. Normal player execution,
**including its callees**, accounts for 1.34% of main-thread cycles stationary
and 1.69% moving. Combining those shares with the observed routine reduction
suggests only roughly **0.2–0.3%** possible main-thread savings from this routine.
This estimate is not all translated CPU/register overhead and is not a measured
title-wide speedup. The five-minute compilation cost is not justified by this
result, so this policy is not integrated into the game preset.

[Microsoft's counter contract](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)
includes user and kernel cycles and warns against converting them to elapsed
time. Ratios here use the same counter on the same host; no assumed GHz
conversion is made. Native position sampling alone had overstated the apparent
importance of this routine, illustrating why it is not a CPU-cycle percentage.

The full-optimization candidate also completed original Round 3 Stage 1 goal,
Results, scoring and save, using explicitly recorded HP/position assists. A fresh
process authenticated that saved payload and reached the selector without guest
state writes or terminal guards. Saved payload SHA256:
`eb4babfa29c2e9f3b083e3cead32fb672de59c1a7f618aaeede31a9fc9031b77`.
The existing 32 native runtime/ABI checks pass. No full-campaign requalification
is claimed.

All three performance experiments are retained privately and **none is promoted**.
The canonical generated manifest and executable are restored byte-for-byte to
`be2bd2cd...` and `a7ae3b2b...`; the register header remains `84554b7c...`.
Restored source timestamps force correct original objects on the next build.
Private BlinxRecomp stays at `739111c` with framework `8d3bce3`. The remaining
investigation is entry-hook/small-function cost and its caller-side barriers;
the large-player result does not settle that question.

Four focused tests compiled uncached/cached authored C at GCC `-O0` and `-O2`
and compare complete results across callbacks, lazy flag snapshots, guest stack,
guest FP control, `longjmp` exits and two actual host threads. They also exercise
macro cleanup, changed-type/qualifier rejection before writes, idempotency and
reapplication after function recovery. The existing 13 extent tests pass.
The prototype applied caching after all bindings and reapplied the recorded
selection during incremental recovery. Because both experiments failed the
performance gate, the generator/recovery/game-profile edits and new test/tool
were removed from production. A complete source/test copy remains under the
ignored evidence directory `rejected-cache-source/`. No game preset selects it.

## Donors and remaining costs

The [previous donor audit](NATIVE-HLE-OPTIMIZATION-2026-10-18.md#what-the-refreshed-donors-offer)
records exact Phobos, sp00nznet and NoRain revisions. Phobos's unified TLS CPU
structure supplies a useful design reference, especially for Mach-O, but this
experiment retains the current binary layout and imports no donor code. Current
hot large routines are title-specific player/geometry logic, not identified
drop-in CRT/HLE replacements. A foreign kernel or name-only ABI replacement
would not solve the observed compiler issue.

| Deferred work | Reason and reopening trigger |
| --- | --- |
| Guest register value promotion across calls | Address caching retains immediate visibility. Value promotion needs explicit callback/wait/exception materialization. Reopen when a narrower block/routine profile shows enough remaining cell traffic to justify it. |
| Large-body optimizer policy / source-level dead-flag elimination | Full MSVC optimization has a substantial measured compile-time cost. Reopen if remaining native profiles still identify these bodies after the narrower cache is assessed; require dataflow proof before dropping flags. |
| Inline entry-hook fast path | Entry frequency is high, but safe suspension and diagnostic behavior must remain. Reopen with direct caller-side timing/barrier evidence; the unused exit helper is not a target. |
| Whole-routine game HLE | No suitable donor replacement was identified for the measured hot title routines. Reopen on a concrete native hotspot with a defined caller, memory and exception contract. |
| Broader x87/flag architecture, full native original kernel and general save states | Existing distinct scope remains open. This host optimization neither proves nor changes those contracts and does not restart exhaustive qualification. |

Private reproduction/evidence: `build/blinx/cpu-bookkeeping-20261018/` contains
immutable identities, launch/profile/benchmark JSON, native listings, compiler
comparison, original source backups and harnesses. Original code, RAM, media
and save payloads remain ignored. No cross-build/netplay compatibility is claimed.
