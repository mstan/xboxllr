# Expanded x87 AOT math and exception qualification

Tracking: `beads-xbox.1.32`, with broader floor contracts in `beads-xbox.1.33`.
This extends the [first production x87 checkpoint](RESULTS-AOT-X87-2026-10-03.md)
under the unchanged [dashboard goal](GOAL-AOT-DASHBOARD.md).

## General operations and real code selection

The compiler and fixed runtime services now additionally support register
`FMUL ST(0),ST(i)`, `FSUBP ST(i),ST(0)` and `FFREE ST(i)`. Multiply reads the
actual register; subtract uses the architectural destination-minus-source
direction before popping; free changes the selected physical tag without
popping or changing register contents. The appended ABI 4 operation numbers
are explicit; unknown operations still decline.

General register/register and register/immediate TEST32 lowering preserves
operands and computes SF/ZF/PF while clearing CF/OF. Architecturally undefined
AF follows the existing generated logical-operation policy and is excluded
from differential comparisons. Memory/prefixed TEST forms remain unsupported.

The five explicitly selected private regions are 84, 736, 71, 17 and 81 bytes,
with 371 supported decoded instructions in their first captured versions.
They include the prior x87 and MMX regions, additional math and real tag cleanup.
All contained identities from the recent capture match the selected original
blockpack bytes. This does not establish every code version or static whole-image
coverage; live byte identity checks remain active.

## Independent checks found additional floor defects

The new fixtures reproduced eleven failed assertions before their corrections:

- FFREE ignored a pending `#MF` and modified FPU state. Its floor translation
  now performs the waiting-instruction check before tag changes.
- Extended denormal and pseudo-denormal arithmetic produced correct numerical
  results but omitted five required DE flags. The existing 80-bit arithmetic
  now publishes DE while respecting higher-priority NaN, invalid and divide-by-zero
  cases. Pseudo-denormals retain their specified normalized value.
- Four memory-multiply cases published a conversion's DE flag before a NaN
  operand could take precedence. FMUL32/64 now combines conversion and arithmetic
  status as one instruction, in both the floor and native services. Standalone
  FLD still has its own load semantics. No operand is flushed or patched.

The arithmetic remains portable SoftFloat with existing upstream attribution.
Instruction and exception references are Intel SDM
[volume 2A](https://cdrdv2-public.intel.com/812383/253666-sdm-vol-2a.pdf)
(FFREE, FMUL, FSUBP) and
[volume 1](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf)
(sections 8.2.2, 8.5 and 8.3.12).

One integration attempt applied the memory-arithmetic source replacement to
register arithmetic too. Independent floor/native tests caught it. The anchor
is now line-specific, and every x87 translator replacement requires a unique
match as well as the pinned source hash. That failed build is retained outside
the source tree and is not accepted evidence.

Remaining memory ADD/SUB/DIV/compare conversion-priority contracts need their
own audit. Newly generated unmasked exceptions and stack fault behavior remain
open under `beads-xbox.1.33`. Native eligibility still refuses unqualified
unmasked controls, pending exceptions and invalid stack contexts before effects.
Those refusals are actual fallback, not an implementation of the missing path.

## Qualification

The final public Release suite passes **72/72**, including the expanded tests.
The x87 floor and both native machine tests pass **3/3** in the census build.

- Every TOP and register index is exercised, including FFREE on empty registers
  and FSUBP targeting ST(0). Full state matches the floor; independent values,
  subtraction direction, tags and pop count are checked.
- Positive/negative halfway add, subtract and multiply cases cover every defined
  precision and rounding mode, with independently calculated result bits and C1.
- Extended denormals, pseudo-denormals, infinities, signaling/quiet NaNs and
  unsupported encodings have independent result/status expectations. Memory
  multiply tests check joint exception priority and NaN selection.
- A repeated load/multiply/subtract/store/free cycle is checked at every
  instruction boundary, all precision/rounding settings and split RAM spans.
- Strict native refusals preserve FPU state and EIP without hidden TCG execution.
  Previous memory-fault, alias/code invalidation and deadline checks remain.
- TEST has independent flag/no-write expectations, randomized differential
  cases, all register-pair decoder checks and malformed/unsupported-form checks.

The final corrected **27B floor/O0/O2 dashboard comparison passes**. All three
runs complete the 14-event Settings-entry/return script. Their entire **270-row
checkpoint streams and ten images are byte-identical**, including comparison
with the previously qualified x87 floor. IRQ counts, guest TSC reads and complete
device/DMA history endpoints agree. Native O0/O2 have identical instruction
partitions and every reported adapter counter.

The native builds execute **872,134,242** guest instructions through AOT, with
**8,135,200,382** actual fallback instructions and **17,992,665,376** qualified
idle skips. This moves another **170,378,215** instructions into native execution
relative to the two-region checkpoint, with idle skips unchanged. The final
census contains **56,589 identities**, zero overflow and six reconciled intervals.

| Phase | Native instructions | Fallback instructions | Native share of executed work |
| --- | ---: | ---: | ---: |
| Main-menu idle | 50,801,772 | 55,210,198 | 47.92% |
| Select Live | 50,097,915 | 55,601,640 | 47.40% |
| Select Settings | 50,838,190 | 56,330,612 | 47.44% |
| Enter Settings | 74,278,221 | 179,002,818 | 29.33% |
| Settings idle | 25,638,296 | 35,731,069 | 41.78% |
| Return to main | 106,132,710 | 119,008,241 | 47.14% |

Shares exclude qualified idle skips; all actually executed fallback remains in
the denominator. Main-menu idle rises from **39.33% to 47.92%**. These are dynamic
coverage measurements for the observed route, not static whole-image coverage
or elapsed-time improvements. Earlier candidate runs predate the final exception
corrections and are retained separately, not substituted for these results.

## Private reproduction

The input is `build/aot-20261003/menu-x87-expanded.aot-private.json`, prepared
from `build/aot-20261002/dashboard-phase.csv.blockpack`, SHA-256
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`, selecting
`0x000cf57d:84`, `0x000cf5ea:736`, `0x000d0075:71`, `0x000d00bc:17` and
`0x00148c50:81`. Generated private C is 157,415 bytes at this checkpoint.
No captured code or generated private executable belongs in source publication.

Use the same locked provisioned HDD, OpenGL baseline, eight-phase idle handling,
`tests/scripts/stockdash-settings-exact.txt` and 27-billion-instruction endpoint.
Final replay artifacts use `x87-expanded-final-*`; earlier candidates use
`x87-expanded-*`. Public build/test logs use `x87-expand-corrected-*`, and
census tests use `x87-expand-final-census-tests.log` under `build/aot-20261003/`.
The complete comparison is saved in
`x87-expanded-final-validation.aot-private.json`; six interval summaries use
`x87-expanded-final-<phase>.aot-private.json` in the same directory.

Retained executable SHA-256:

- O2, `xlle-x87-expanded-final-o2.exe`:
  `3a27cf8dfa78f8213de3124ec810e491f792909117b3382a1adc3d5abeaf5b3d`.
- O0, `xlle-x87-expanded-final-o0.exe`:
  `3ed5fc498ce15a5c98db17f644cdadba0253be7d9b59942878166037c519afda`.

The private input SHA-256 is
`e4196b7c0909e268df6e60b118eab2ec63f8ab7607ff81c4154c4144e4a91b62`.
The common checkpoint stream SHA-256 is
`9deb0f87d7e608e5f02ceb37eae56bdb6329458e78af56b20bc0462c524c0521`.

Full menu AOT, required menu interiors, UI audio, complete x87/kernel/memory
qualification and sustained original-speed 60 FPS remain open. The working
OpenGL renderer remains the baseline, with qualified donor reuse preferred.
Concurrent instrumented replays cannot support a timing claim. Source changes
remain uncommitted.
