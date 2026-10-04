# Native dashboard matrix multiply: retain as an opt-in candidate

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
Retain the narrow opt-in candidate for combined qualification because the
whole hot routine is removed, its caller contract passes, and the route-level
screen is favorable. Do not claim a 30.9% FPS gain from the instruction count.

Private evidence: main `build/r2-matrix-r1-*`, `build/r2-matrix-tests.log`.
No firmware, title bytes, generated private header or captures are committed.
