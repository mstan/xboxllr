# General x87 services and dashboard AOT execution

Tracking: `beads-xbox.1.32`, under the active dashboard goal
`beads-xbox.1.25.5`. Remaining exception/stack qualification is tracked in
`beads-xbox.1.33`. This follows the
[eight-phase idle qualification](RESULTS-IDLE-PHASES-2026-10-03.md).

## Implementation and limits

ABI 4 adds fixed arithmetic/transfer services over the engine-owned 80-bit
SoftFloat state. The compiler lowers FLD32/64, FMUL32/64, FST32/64, FSTP32/64,
FLD ST(i), FXCH ST(i), FADDP ST(i) and FSTP ST(i). These are general decoded
operations, with actual effective addresses and predecoded FOP metadata.
No helper decodes or executes guest instructions, substitutes host-double math,
or recognizes a dashboard function by address to replace its behavior.

The adapter checks the complete cached RAM span before any x87 effects. Stores
retain physical-alias code invalidation. The engine owns TOP, tags, registers,
control/status and environment; memory operands select the actual default
DS/SS segment. Register forms preserve the preceding data pointer. P6-style
last-instruction FIP/FCS/FOP and memory FDP/FDS are retained.

Native eligibility currently requires all six x87 exceptions masked, a defined
precision control, no pending exception, and valid stack sources/push space.
Other contexts, cold memory and MMIO decline before effects and execute on the
CPU floor. FFREE and other unsupported instructions still decline. This is
bounded qualification, not complete x87 support or whole-route AOT.

## Correct the floor before using it as a reference

Independent fixtures first reproduced **19 failures** in the pinned floor:
masked exception bits were missing, defined C1 changes were missing, pending
`#MF` could be ignored, and conversion before a failed store changed FPU state.
Hash-checked build-tree sources now retain SoftFloat results and merge their
status into FPSW, calculate C1 at the active precision/rounding mode, check
pending exceptions, and commit floating-store conversion state only after the
real memory operation succeeds. The translator also records P6 FOP metadata.

The portable exception-merging approach is adapted from pinned xemu/QEMU code;
its separate host-FPU path is not used. Upstream notices and LGPL 2.1-or-later
terms are retained in [the attribution record](../THIRD_PARTY_ATTRIBUTION.md).

Full replays exposed two integration defects beyond the initial small tests:
using the pinned `probe_access` helper on a translated destination page caused
a virtual/physical mapping lookup crash, and newly allocated FOP temporaries
overflowed translation state in long blocks. Both were reproduced in public
fixtures. Floating stores now use the actual store helper's existing guest
page checks and code invalidation, with staged FPU state; metadata generation
uses the translator's existing scratch register. The failed runs are retained
as evidence, not counted as successful qualification.

Remaining floor contracts include newly generated unmasked exceptions,
masked/unmasked stack faults and other arithmetic families. Native refusals
do not establish that those inherited floor paths are correct. They remain
explicit work in `beads-xbox.1.33`.

## Public qualification

The complete Release suite passes **72/72**. The three new x87 tests also pass
in the census build. The checks include:

- Independent masked invalid, denormal, precision, overflow and underflow
  expectations; exact result bits and defined C1 changes.
- Independent positive/negative halfway additions for all three defined
  precisions and all four rounding modes in strict native execution.
- O0/O3 generated fixtures: complete x87 register/tag/control/status/environment
  and integer state versus instruction-scheduled floor execution at every
  instruction boundary; repeated cycles and split-page RAM spans.
- Pending `#MF` before memory access; failed stores preserve complete FPU
  state. Nonidentity paging onto a compiled destination page succeeds, and
  second-page faults preserve every destination byte and FPU state.
- A 512-instruction x87 block retains its final result/environment without
  exhausting translation temporaries.
- Native refusals leave FPU, EIP and destination bytes unchanged. Self-modifying
  stores through physical RAM aliases exit before subsequent compiled code.
  Strict fixtures retain zero CPU fallback and unchanged TCG execution counters.

Decoder tests cover supported encodings, all register indices, FOP values,
DS/SS defaults, malformed input and explicitly rejected prefixes/forms.

## Dashboard comparison

The updated floor and O0/O2 native builds complete the same **27B** route.
Their entire **270-row checkpoint streams** are byte-identical to each other
and the preceding qualified floor. All ten same-clock images are byte-identical
across those runs, retaining the previously inspected Settings interior and
main-menu return. Both native builds also have identical complete instruction
partitions and adapter counters, including 451,309 memory declines.

The endpoint remains 36,796 PIT pulses, 46,341 delivered interrupts, 28,481
pending interrupt boundaries and 14,342 guest TSC reads. The complete device
history has 18,059,827 events with digest `bb5d546fb1f0bc08`; DMA has 2,476,411
writes with digest `041fbd7997416182`.

The optimized build retires **263,882,564 additional instructions** through
the new 71-byte x87 region. The existing MMX region still retires 437,873,463.
Total native is 701,756,027; fallback is 8,305,578,597; idle skipped is unchanged
at 17,992,665,376. These sum exactly to 27 billion.

The census retains **57,227 block identities, zero overflow**, and all six
intervals reconcile. CPU translation boundaries can change when helper work
changes; the number of observed block identities is not static coverage.

| Interval | Native instructions | Fallback instructions | Idle skipped | Native / executed |
| --- | ---: | ---: | ---: | ---: |
| Main-menu idle | 41,697,852 | 64,314,118 | 744,654,696 | 39.3332% |
| Select Xbox Live | 41,121,395 | 64,578,160 | 744,967,112 | 38.9040% |
| Select Settings | 41,729,348 | 65,439,454 | 743,497,864 | 38.9380% |
| Enter Settings | 57,738,953 | 195,542,086 | 1,330,718,960 | 22.7964% |
| Settings idle | 19,409,936 | 41,959,429 | 671,963,968 | 31.6281% |
| Return to main menu | 86,276,969 | 138,863,982 | 1,358,859,048 | 38.3213% |

Main-idle native coverage rises from 26.0422% to **39.3332%** through additional
compiled guest execution, with the same idle skips and denominator. Cold-memory
declines and unsupported instructions remain in fallback. Broader x87 math,
stack-tag cleanup, integer widths and kernel/memory operations remain work.

## Reproduction and remaining gates

Use the preceding 27B route command, locked provisioned HDD, OpenGL renderer,
eight-phase idle optimization and `tests/scripts/stockdash-settings-exact.txt`.
The private two-region input is `build/aot-20261003/menu-x87.aot-private.json`,
prepared from the original first-executed blockpack, with the x87 identity
also checked against the recent capture. Generated code and executables remain
private. The selections are `0x000d0075:71` and `0x00148c50:81`, from
`build/aot-20261002/dashboard-phase.csv.blockpack` (SHA-256
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`).
Successful proof files use the `x87-qualified-*` prefix; failed
integration runs use `x87-two-*` and `x87-fixed-*`.

`x87-qualified-validation.aot-private.json` records the four compared runs,
all image hashes, checkpoint hash, endpoint/adapter counters, input identity,
binary hashes and six reconciled phase summaries. The retained executables are:

- `xlle-x87-qualified-o2.exe`: SHA-256
  `737d37cebc90f88bd540f2b07387e0eb037ce644c8292693a77c9a291dd8aec2`.
- `xlle-x87-qualified-o0.exe`: SHA-256
  `268b16304acc3b9fb23556900c87b19977935b4d7c29de9867928d44988a3d17`.

These instrumented replays qualify the observed images and histories, not
every byte of machine state, complete hardware equivalence or performance.
The renderer remains the working OpenGL implementation, with qualified donor
reuse still preferred. Full menu AOT, required menu interiors, UI audio and
quiet-host original-speed 60 FPS remain open. Source changes are uncommitted.
