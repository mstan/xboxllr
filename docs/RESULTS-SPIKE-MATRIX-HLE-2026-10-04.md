# Native dashboard matrix multiply: parked; no substantial speed gain established

Branch `spike/native-matrix-20261004`, parent `54c102a`; tracking
`beads-xbox.1.51`. This is a whole-routine HLE replacement, independent of the
direct-index, state-cache and render-worker experiments.

## Borrowed approach and caller contract

The NDS ecosystem's `docs/mph-math-hle.md` replaces whole math routines through
private byte-pinned entry adapters, with copied inputs and native arithmetic.
Its three routines are not evidence of a large Xbox gain; our own dashboard
profile selects the Xbox routine. phobos's host Rtl memory helpers are another
example of the same whole-function approach. No donor kernel or title stub is
incorporated.

Dashboard `0x000cfff1` is audited as stdcall(out,left,right), 4x4 row-major
matrix multiplication, EAX returns out, RET 12, callee registers preserved.
Both input matrices are copied before writes, including out==left, out==right
and all-three-equal. Guest virtual transfers are page-translated and RAM-bounded.
Native float arithmetic permits small rounding differences from guest x87;
internal FPU history and instruction counts are deliberately not reproduced.
There is no external service, MMIO operation or guest helper inside this body.

`tools/matrix_profile.py` accepts only the audited function SHA-256 and emits a
private header under build/. Installation verifies the actual full function
bytes in the running image. Unknown revisions retain ordinary execution rather
than activating this service. Configure `XLLE_RENDERER=HLE` and
`XLLE_MATRIX_PROFILE=<absolute private header>` to select it. Omit the profile
for ordinary execution; select LLE for the faithful renderer/CPU path. Selection
is at build/startup, with no live exchange of HLE and LLE internal state.

## Validation and measured result

- 80/80 CTest checks pass. 2,000 original-function comparisons execute the
  actual private guest routine under Unicorn, including four alias layouts.
  Finite randomized inputs use an absolute bound of 5e-7 times one plus the
  sum of product magnitudes; return pointer, RET 12, output extent and callee
  registers are checked. Exceptional FP inputs and other title revisions are
  not qualified by this bounded test.
- Real interactive 27B dashboard route completes all 14 events, empty stderr,
  zero presentation failures. Settings and returned menu visually reviewed.
  28 captures per arm, 22 differ: nominal arithmetic and animation phase changes
  are allowed. This is not a full game compatibility claim.
- 941,677 native matrix calls. Final interval native instructions fall from
  91,925,180 to 63,534,052 (30.9%); the hot `d0075` guest region executes zero
  instructions. Fallback 40,306,151 to 39,892,551. Idle fast-forward remains
  functional and increases to 1,396,373,408 instructions.
- All renderer draw/vertex/upload-byte counts match the baseline; resolves
  change from 2,351 to 2,385. No required render work is removed.
- Sequential A/B total frame-window time: 122.1214 to 117.4513 seconds (3.8%
  reduction observed). Last four intervals, 491 vblanks: 24.8007 to 21.3450
  seconds. The final interval alone is flat/slightly worse: 5.8952 to 5.9537.

Both arms are explicitly contaminated by other host processes. These timings
do not establish a reliable percentage speedup or original-speed dashboard.
Park this implementation. Its caller contract passes, but it did not establish
a substantial dashboard elapsed-time improvement. The 30.9% instruction-count
change is not sufficient evidence to retain it as an active performance candidate.

Private evidence: main `build/r2-matrix-r1-*`, `build/r2-matrix-tests.log`.
No firmware, title bytes, generated private header or captures are committed.

Owner disposition, 2026-10-04: **park all experiments from this round**, including
this one. The code and tests remain on this remote branch for reference, with no
runtime integration or ongoing qualification. Reopen only on new evidence that
identifies material host time to remove, not on the guest instruction count alone.

## Why fewer counted instructions need not produce the expected speed gain

The reported 91.9M -> 63.5M counts guest instructions executed by the native CPU
tier. It neither counts host CPU instructions nor measures milliseconds spent
in that tier. The new C++ HLE body and adapter also do not appear as equivalent
retired guest instructions in that counter.

`NativeMatrix::invoke` still uses `UnicornFloor::get_regs` (ten individual
`uc_reg_read` calls), page-translated argument/input/output transfers, and
`redirect_native_service`, which uses `set_regs` (ten `uc_reg_write` calls) and
`uc_emu_stop`. The computation is native, but dispatch and guest state transfer
still use the emulator. This is not a direct native-to-native AOT call.

The final interval retains 39.9M fallback instructions versus 40.3M before;
fallback-to-native handoffs are 4,536,916 versus 4,526,619. Whole-route rendered
draws and vertices are unchanged. Most avoided logical clock units become
fast-forwarded idle, rather than an earlier end of the fixed guest-time interval.
This clock accounting does not itself explain host wall time or impose a host
speed cap.

The logs do not attribute host milliseconds to the original routine, adapter,
handoffs or unrelated work. Adapter overhead could consume savings; other work
could dominate; external load could hide a modest gain. Those are explanations
to distinguish, not measured conclusions. Selecting this hotspot by instruction
count alone was insufficient. The factual result remains: correctness checks
pass, fewer guest instructions execute, and substantial menu speedup is unproven.
