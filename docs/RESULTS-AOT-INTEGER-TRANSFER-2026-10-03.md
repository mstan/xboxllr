# Bulk integer state at the native execution boundary

Tracking: `beads-xbox.1.43`. Source baseline: `82d33f3`, under the active
[60 FPS dashboard goal](GOAL-AOT-DASHBOARD.md). This checkpoint qualifies
correctness of an **opt-in** CPU optimization. Performance remains unqualified;
the existing public register API remains the default.

## Measured reason and implementation

The [quiet main-menu profile](RESULTS-HOST-SAMPLER-WINDOWS-2026-10-03.md)
identified register reads/writes and native admission as CPU costs. The current
27B route performs **111,104,829 native invocations**, including **10,016,646**
in its final 25.5B-27B window. Each invocation previously transferred integer
state through ten individual public register reads and ten writes.

`--aot-bulk-registers` transfers eight general registers, EIP and EFLAGS through
the existing private bridge to the pinned Unicorn core. `--no-aot-bulk-registers`
selects the public API. The bulk path uses `cpu_compute_eflags` and
`cpu_load_eflags`, preserving lazy arithmetic flags and DF/system flags. Writes
retain the public API's 32-bit register assignment and EIP exit-request behavior.
The bridge refuses missing, non-x86, non-32-bit or currently executing engines.
It runs only after the existing native admission checks.

Extended registers, hidden CPU state, memory translation, live-code checks,
instruction budgets and event boundaries retain their implementations. An
unexpected writeback-context change stops execution explicitly. Generated ABI
**5**, private code selection and guest instruction coverage are unchanged.
There are no new HLE services or stubs.

All three xboxrecomp register templates were examined for this spike. Their
direct scalar state is useful precedent, while their global/TLS lifetimes,
flags and calling convention do not implement our engine-owned CPU contract.
The adaptation uses the pinned core's state and flag helpers, with no donor CPU
shim imported. sp00nznet `1409a7d` and NoRain211 `7adaf21` remain unchanged;
phobos advanced to `7884426`, a fog-coordinate shader fix retained as a future
renderer candidate. See the [donor follow-up](RENDERER-REUSE-2026-10-02.md#cpu-boundary-follow-up-2026-10-03).

## Correctness evidence

All **79 Release tests pass** in both the private-input qualification build and
the restored ordinary build with private input cleared. The four focused
integer-transfer/extended-state tests also pass with generated fixtures compiled
at O0 and O3.

The new real-machine differential test compares floor, public API and bulk
execution at all 32 instruction prefixes of carry and no-carry cases. Fallback
ADD creates lazy flags; native INC/JNC/DEC preserves and consumes carry; fallback
PUSHFD/ADC observes native flags before returning to native execution. It checks
every integer register, EIP/EFLAGS, all mapped RAM and retirement/handoff counts,
with independent expectations for branch choice, stack restoration and carry.
Both native variants must actually retire instructions. The existing extended
state test now exercises both transfer modes across native/fallback transitions.

The same immutable executable completes both **27B** dashboard routes using the
reference OpenGL renderer and the provisioned locked HDD with COW writes:

- All **270 checkpoint rows** agree exactly, including the prior committed
  reference route.
- All **28 captures** agree byte for byte: 18 periodic captures and ten route/
  final captures covering clock acceptance, selection, Settings entry and return.
- All **19 AOT summaries, 5,434 region rows, 8,306 exit rows and 19 idle/device
  census rows** agree exactly.
- Both finish the 14-event input script with endpoint-2 count **3,018**, aggregate
  count **3,019**, and empty stderr.

Both retire **1,964,141,698 native instructions**, **7,043,192,926 fallback
instructions** and **17,992,665,376 qualified idle skips**. This is unchanged
execution coverage, not full AOT. Final image SHA-256:
`4044cbd73dec3e8afbb5c69d3ba75cab0e436597f9e6bedc320812ac4a8ee671`.

## Performance decision and retained evidence

The two functional runs had healthy observers but detected **455 / 901 foreign
process identities**. Both are marked `timing_usable=false`. Their final windows
contain 123 guest vblanks over **22.068 / 19.507 seconds** (API / bulk); these
contaminated observations cannot establish a gain or displayed FPS. The window
contains only about two seconds of guest time, not the sustained menu gate.

A separate same-executable, interleaved three-pair timing attempt used
`-RequireQuiescence`. Its first leg was **not launched** because unrelated
compiler/build processes were present. No quiet pair exists from that attempt.
Keep bulk transfer opt-in until repeated clean measurements justify promotion.
Do not relax host observation or infer performance from unchanged output.

Private evidence remains under `build/menu60-integer-20261003/`:
`tests-r1.log`, `public-tests.log`, `focused-r1.log`, `control-validation.json`,
`route-validation.json` and `quiet-r2-harness.log`. Route artifacts use prefixes
`build/menu60-integer-r1-{api,bulk}`; the rejected timing preflight uses
`build/menu60-integer-r2-quiet-leg00-A-r1`. Their argv and observer records retain
the exact inputs and process identities.

The retained private executable is `xlle-integer-r1.exe`, SHA-256
`1a3e9259b11873b092735353fafc4449b756fe0a41768d688ab3e8e1a645023c`.
It uses the existing 286-region memory-integer artifact. No private guest bytes
or generated artifacts are committed. For a future quiet timing comparison, use
that same executable in both arms, the 27B Settings route, 1.5B frame spacing,
the final image hash above, and the two opposing register-transfer switches.

Sustained original-speed 60 FPS, distinct frames, input latency, full AOT and
broader dashboard/audio qualification remain open. Menu performance stays ahead
of Blinx. The next performance decision needs a clean native-versus-native
comparison; renderer submission, vertex work and qualified service replacement
remain separate candidates from the measured profile.
