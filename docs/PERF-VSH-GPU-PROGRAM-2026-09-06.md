# Complete-program GPU vertex shadow

Tracker: `beads-xbox.1.15.11`. Starting revision: `8b4b488`.
The goal remains faithful LLE at 60 FPS with headroom. This checkpoint validates
complete decoded vertex programs against real CPU executions; it does not yet
replace CPU vertex work or triangle preparation in the emulator.

## Ownership and sampling

`XLLE_VSH_CAPTURE=ON` builds an explicitly armed capture hook. Normal builds
compile out its execution counter, callback, and per-vertex work. The command
`--debug-vsh-capture <path> <interval> <count>` samples the first eligible actual
execution and then every interval-th execution, up to the bounded count. The
limit is 16,384 records. Attribute fetch and the existing index cache remain
authoritative. Cache hits bypass execution and are counted separately; they are
never submitted as extra GPU vertex work by this diagnostic.

The initial capture scope is compiled, non-context-writing programs without
relative operands or address/context destinations. Unsafe probes are refused.
The capture deliberately retains CPU-supported opcodes that a GPU candidate
may decline, so unsupported-program coverage remains visible instead of being
removed from the population. No program is selected by fingerprint.

Each private binary record owns all decoded operations, 16 input registers,
192 context registers, seven consumed raw output registers, and the position
after the machine's actual screen/W postprocessing. It also records the actual
execution ordinal and a reporting-only program fingerprint. Operation records
contain integer opcode/operand/store fields, never host pointers or padding.
The reader checks format/version, bounds, increasing ordinals, completion count,
and trailing bytes. `.bin` captures are already ignored and blocked by repository
hygiene; they contain guest material and must remain private.

## CPU and GPU comparison

The offline tool validates every operand, destination, arity, reserved field,
and paired operation before submitting GPU data. It reconstructs the compiled
CPU operations and calls the pinned CPU functions with fully zeroed temporary,
output, and address files. Every consumed word must match the captured CPU
result. Independently recomputed position postprocessing must also match its
capture. This avoids copying the CPU zero-plan optimization into the first GPU
validator.

A separate CPU trace uses the same operand helpers and pinned arithmetic
functions to inspect every fetched and result lane, including masked-out lanes.
For GPU-admitted programs, numerical decline must agree exactly with this
conservative NaN policy. Its final outputs must independently match the actual
compiled executor. Review caught an initial implementation that delegated the
executor check to the trace itself; that self-comparison was removed before
the final validation.

The GL 4.3 compute shader consumes the complete decoded sequence. Both halves
of a paired instruction fetch their operands before either stores; both output
slots and partial masks retain their order. Register aliases are already resolved
in the decoded operand bases. GPU outputs remain private until the complete
record succeeds; declined output records and all unused tail records retain
sentinels, which the reader checks.

The shader supports MOV, MUL, ADD, MAD, DP3, DPH, DP4, DST, MIN, MAX, SLT, SGE,
RCP, RCC, and RSQ. ARL/EXP/LOG/LIT decline as complete programs. Relative access
and context/address writes remain outside this capture scope. All arithmetic
uses the explicit FP32 rounding candidate from
[the numeric probe](PERF-VSH-GPU-NUMERIC-2026-09-06.md). RSQ rounds `sqrtf` before
the separate reciprocal. RCC retains the pinned CPU's literal clamp thresholds
and signed-zero behavior, which differ from final position-W postprocessing.
This remains device-specific evidence, not a portable IEEE arithmetic proof.

The final synthetic program corpus has 10,039 records: 10,036 accepted matches,
two independently checked numerical declines, and one intentional LIT program
decline. Fixed CPU expectations cover paired reads and conflicting writes, two
destinations, output aliases, masks/swizzles/negation, signed-zero comparisons,
DPH/DST, and MAD product overflow before addition. The corpus also includes
10,000 random finite reciprocal/square-root inputs and directed boundary values.
The original explicit-rounding dot corpus still returns 302,635 accepted matches
and five declines. All output/status guards remain intact.

Final standalone probe SHA-256:
`1581d2a9a7ff2cf655a00d6b8ed6f4e97943afe94cc2395d86351c1200b88690`.

## Real dashboard evidence

The capture build's executable SHA-256 is
`427f1c8710bbed44142a0c5805b13fccc66e049bf34e21541aa26fa9587e0c6f`.
Both runs use an interval of 32,768 actual executions and a budget of 4,096.
Every actual VSH execution in these runs is eligible; neither run reaches the
budget. Measured device: RTX 3080 Ti, GL 4.3 / NVIDIA 616.86.

| Replay extent | Actual VSH executions | Index-cache hits | Samples | Accepted GPU matches | Numeric declines | Program declines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 B | 38,337,230 | 75,499,247 | 1,170 | 1,164 | 6 | 0 |
| 16 B | 85,933,966 | 207,381,159 | 2,623 | 2,617 | 6 | 0 |

Accepted records have zero differing raw or postprocessed-position words and
intact guards. The six declined records have NaNs in the CPU consumed outputs.
The dominant resident-primary program has 23 operations, including RSQ/RCC and
two paired steps; all 554 samples at 12 B and all 1,999 at 16 B match. These are
sampled executions, not proof of every possible input or full original-hardware
behavior. The 12 B capture is an exact prefix of the 16 B capture apart from the
completion footer, so the two sample counts must not be summed as independent
coverage.

The instrumented 12 B replay matches all 28 PPM checkpoints and checked guest
state/timeline fields against the preserved normal `22c2a3a3...` build. The 16 B
capture uses 500 M checkpoints; its older reference uses 250 M checkpoints. A
strict whole-series comparison initially rejected the different populations.
All 40 candidate PPM checkpoints match the corresponding reference checkpoints,
the checked cumulative guest state matches, and all 32 candidate timeline rows
match after summing reference vblank deltas over each candidate interval. These
are fidelity checks only; no cross-build or instrumented timing claim follows.

Private evidence is under `build/vsh-program-shadow/`: captures, replay reports,
GPU logs, and the strict/aligned fidelity results. Published source contains
synthetic fixtures and generic execution code, not guest program captures.

After restoring both options to OFF, the normal executable is
`2b49c35b88640854fde55aa29e5a7c8b51891bca8856b25b2c1c51902386cc8b`.
All 40 CTest tests pass. Its separate 12 B fidelity replay matches all 28 PPMs
and checked guest state/timeline against the preserved `22c2a3a3...` baseline.
The normal binary refuses the capture flag before creating a file. The
publication-tree scan covers 181 files with zero Gitleaks findings.

## Reproduce and continue

Use the README's native toolchain setup, then:

```powershell
cmake -S . -B build-release -DXLLE_VSH_CAPTURE=ON -DXLLE_VSH_GPU_NUMERIC_PROBE=ON
cmake --build build-release --target xlle xlle_vsh_dot_probe test_vsh_capture
.\build-release\test_vsh_capture.exe
.\build-release\xlle_vsh_dot_probe.exe --program-self-test
.\tools\run_stockdash_replay.ps1 -Label vsh-capture-local -ExtraXlleArgs @('--debug-vsh-capture','build/vsh-capture-local.bin','32768','4096')
.\build-release\xlle_vsh_dot_probe.exe --program-capture build/vsh-capture-local.bin
```

Tool exits: 0 means all records match; 2 means invalid capture, CPU revalidation,
setup, or guard failure; 3 means GPU output differs; 4 means accepted records
match but some records decline. Always retain the counts with the exit code.
Restore both CMake options to OFF for normal builds.

The follow-up in `PERF-VSH-GPU-BATCH-2026-09-06.md` now retains complete batch
ownership and validates a GPU triangle dispatch consuming GPU VSH output.
A production implementation must retain complete batch ownership and the original
reference/index-cache mapping, then move triangle preparation with the GPU VSH
consumer. It must preserve finite-position checks, signed W classification,
area/cull rules, and destination ordering, including a defined decline before
destination writes. The current readback is a validator and is not a production
presentation or draw path. A complete production candidate still needs the
same-session interleaved A/B gate and interactive distinct-frame/latency checks.
