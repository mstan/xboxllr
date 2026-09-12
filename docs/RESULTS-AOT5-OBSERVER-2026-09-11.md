# AOT-5 first iteration: trustworthy replay observation

This landing implements the in-run host observer from checkpoint 5.1. It does
not claim an emulator speedup or complete AOT-5. Controlled monitor-overhead
measurement and clean native attribution are still required before choosing a
runtime optimization on measured evidence.

## What changed

The old harness inspected competing processes only before and after a replay.
A compiler could run entirely between those checks and leave an apparently
clean report. The new observer samples throughout the actual emulator lifetime,
records its health and actual gaps, and keeps all observed foreign identities.

Only the exact harness and launched emulator are owned. Expected resident PIDs
are explicitly supplied and pinned to creation time once per session. Same-name
processes and descendants receive no blanket exemption. Failed or incomplete
observation cannot satisfy required quiescence. Whole-pair retry excludes both
completed legs on contamination or unusable observation, never on timing.
Preflight refusal stops the invocation and leaves its evidence on disk.

The process launcher now owns the exact child handle, drains both output pipes
concurrently and preserves argument quoting, working directory and raw output.
Both report tools enforce current observation proof in every mode that accepts
`--require-quiescence`. Historical reports remain readable but cannot supply
new quiet-host timing proof. See [the protocol](PERF-METHODOLOGY.md#2b-in-run-process-observation-aot-5)
for sampling and resident-exclusion limits.

## Validation

- CTest: **52/52 passed**, including three newly registered replay tests.
- An authored executable starts a same-name child for 1.2 seconds during its
  own run. The observer detects it although both endpoint snapshots are empty.
- Actual subprocess tests cover owned-process exclusion, unrelated preexisting
  work, explicit residents, stale creation-time exclusions, observer startup
  failure, empty/quoted/backslash arguments, exit codes, working directory,
  and simultaneous 1 MiB stdout/stderr output.
- The integrated harness rejects a contaminated `-RequireQuiescence -NoParse`
  leg and archives the evidence. Pair tests preserve ordering, whole-pair
  exclusion, attempt limits and propagation of correctness failures.
- Python CLI tests reject missing, historical, malformed, failed, degraded,
  uncovered and contaminated evidence, while accepting healthy current proof.
- The full 12 B reference-config native dashboard replay passed its golden:
  29 executed assertions passed; two binary/Git identity entries were
  informational skips, as intended. Final framebuffer SHA remained
  `36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`,
  stderr was empty and all 24 recorded profile hashes matched. Ten profile
  families were unexercised; this is not exhaustive rendering coverage.
  Evidence: `build/aot5-observer-reference-r1.report.json`.
- All **28 saved images** are byte-identical to `aot4-cache-ref-o2`, including
  scripted-input checkpoints. Device and DMA digests also match that reference.
  Comparison: `build/aot5-observer-reference-r1-images.txt`.

That correctness replay was intentionally allowed to run on the busy host.
The observer recorded 2,537 samples, a maximum gap of 319.32 ms, no observer
errors and 402 distinct foreign name/PID entries. It correctly marked the run
contaminated and unusable for timing. Cumulative enumeration work was 35,345.83 ms
over 323,076.00 ms of observed lifetime. These busy-host diagnostic figures
are recorded to expose instrument cost, not to claim a quiet baseline or
low overhead. Default cadence remains provisional until its cost experiment.

### Work-partition variation found during validation

The canonical golden and images pass, but the native/fallback partition and
slice digest differ from the historical O2 reference:

| Counter | Historical O2 reference | New busy-host reference |
| --- | ---: | ---: |
| Native instructions | 4,668,212,388 | 4,601,119,188 |
| Fallback instructions | 2,633,144,301 | 2,700,237,501 |
| Idle skipped instructions | 4,698,643,312 | 4,698,643,312 |
| Code-TLB declines | 14,101,059 | 30,874,359 |
| Slice digest | `dc48a597d4bd403b` | `b49881894c3041fa` |

The archived final-binary native timing legs also had the historical instruction
partition. Equal final images alone therefore do not establish equal native work
for the next optimization comparison. Preserve this variation instead of
repinning or describing the new run as fully deterministic.

A concrete lead exists in the local Unicorn 2.1.1 source:
`qemu/accel/tcg/cputlb.c` resizes its direct-mapped TLB using a **100 ms host-time
window**, fed by `get_clock_realtime()`. Native entry in
`src/cpu/aot_executor.cpp` requires a code pointer certified through the current
TLB by `src/cpu/unicorn_aot_bridge.c`. Missing residency declines to fallback.
Host-dependent cache policy can therefore affect native eligibility. This is a
source-supported mechanism, not yet a controlled demonstration that it caused
this run's exact difference. Isolate cache sizing/residency and repeat workload
partitions before accepting a generated-code speed comparison. Do not erase the
old evidence or change guest-visible behavior to make a digest match.

The sampler's cumulative enumeration wall time is recorded as `sample_work_ms`.
It is not an estimate of emulator slowdown. A quiet controlled overhead
experiment remains open; a busy-host run cannot close that requirement.

## Native baseline and next experiment

The saved native baseline remains the AOT-4 final O2 executable:

```text
SHA-256 7c7478d340881adfa50b3174932f83a3dc2da28c2d735bd67ce589581c2b0cc9
```

The source baseline is `69f58b8`; this landing changes harnesses, reports, tests
and documentation, with no emulator or generated-code semantics change. Private
host disassembly is retained locally in `build/aot5-baseline-*-host.asm`.

Static inspection confirms that the generated regions repeatedly return to
the same guard and EIP dispatch after each guest instruction. The host output
contains repeated code-identity, context, event and budget checks, guest-state
stores, and software flag computation. The stack region also makes indirect
memory callbacks. This is structural evidence, not a runtime cost ranking.

After resolving the work-partition variation, the first compiler candidate to
qualify after attribution is direct internal block
edges with the current guards and operation bodies preserved. It can remove
repeated EIP selection without starting with lazy flags or weakened memory
coherence. Every budget, event, code-change, fault and external exit must still
expose exact guest state and retirement. Completion must retain its current
priority over an exhausted budget. Callback-visible code/event changes require
the next guard before executing further guest instructions.

Later hypotheses are local guest-state values, flags materialized at observable
boundaries, fewer failed native entry attempts and certified memory access.
These need separate validity arguments and native-versus-native measurements.
Neither static inspection nor the old contaminated attribution provides a
speedup estimate.

## Open measurement work

`build/aot5-current-attribution-r1.quiescence.json` records a refused launch:
foreign GCC, CMake, Ninja and Python work was active. No attribution was collected
from that attempt. These processes belong to other work and were not stopped.

When the host is quiet, identify any expected idle resident processes, then run:

```powershell
./tools/run_stockdash_replay.ps1 -Label aot5-current-attribution-r2 `
  -BaselineExe ../private-audit-20260907/aot4-final-native-o2/xlle.exe `
  -ExtraXlleArgs @('--aot-native') -PerfAccounting -RequireQuiescence `
  -ResidentProcessIds $ResidentIds
```

`$ResidentIds` must contain only explicitly identified resident process IDs;
leave it empty if none are exempt. The saved binary path is local and private.
A fresh checkout must prepare its own inputs through the
[private workflow](AOT-PRIVATE-WORKFLOW.md).

Finish the controlled monitor-cost experiment, collect clean current-native
attribution, report native/fallback/idle work by guest phase, then qualify the
first generated-code experiment. Any speedup claim still requires quiet,
same-session interleaved 3+3 or more, total 12 B minimum **and** median, and the
5% gate against the preceding native implementation. No new native performance
claim has been made by this landing.
