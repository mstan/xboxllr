# General scalar AOT across dashboard functions

Tracking: `beads-xbox.1.35`, under the active
[dashboard goal](GOAL-AOT-DASHBOARD.md). This extends the
[kernel integer checkpoint](RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md).

## Implementation and independent qualification

The main-idle census identifies shifts, extension/narrow reads, immediate stores
and conditional branches among the remaining blockers. General lowering now
implements SHL/SHR/SAR on every 32-bit register with one, immediate or CL counts;
all 16 short/near Jcc conditions; MOVZX from byte/word registers and qualified
RAM; and C7 immediate moves to registers or qualified RAM. Other widths, prefixes
and memory arithmetic remain explicit unsupported exits. No helper executes
guest code.

ABI **5** adds an exact one- or two-byte read callback. It certifies the entire
access before reading and never widens it. Narrow reads do not populate or
consume the separate four-byte read cache. Existing RAM ownership, cached
translation, segment, permission and code-invalidation contracts remain active.

Shift counts use five bits; zero counts leave all flags intact. Defined result,
CF, one-bit OF and SZP values have independent expectations. Nonzero AF and
multi-bit OF are architecturally undefined; their deterministic choices match
the pinned CPU floor and are not physical Xbox measurements.

Instruction references: Intel's
[volume 2A, Jcc](https://cdrdv2-public.intel.com/812383/253666-sdm-vol-2a.pdf)
and [volume 2B, MOVZX and shifts](https://cdrdv2-public.intel.com/782151/253667-sdm-vol-2b.pdf).

Public Release tests pass **74/74**. Generated scalar C is checked at O0 and O3;
the census build passes both scalar and both native-required machine tests.
The compiler's 30 Python tests also pass after preserving narrow-memory address
details in generated manifests.

- **73,728 CL cases** cover all byte counts, destinations, three operations,
  six operands and two flag states. Another **2,880** immediate/implicit cases
  cover zero, one, large and masked-zero counts. Independent bit placement and
  selected floor comparisons check results, flags and old-CL aliasing.
- **4,096 branch cases** compare every arithmetic-flag combination, both
  displacement widths and both directions with an independent truth table and
  the floor. Another **2,048** exercise generated branches with the taken target
  beyond CS and a valid fallthrough: taken branches decline before retirement.
- MOVZX checks all source/destination register pairs, including high-byte and
  destination/address aliases. Memory checks compare exact access widths,
  addresses, order and results; SIB/negative displacements, unaligned stores,
  write refusal, pending events and self-modifying code are included.
- A real-machine strict scope compares **121 successive instruction boundaries**
  and **25 narrow reads** with the floor. Native retirement advances; fallback
  runs/instructions and all CPU execution-run/translated-block counters stay
  unchanged. Control flags remain preserved.
- Nonidentity paging covers the last byte and word before an absent page,
  a split word with an absent/contiguous/noncontiguous next page, and a narrow
  read followed by a four-byte read at the same address. Strict declines preserve
  state and page tables without hidden execution. A separate floor machine
  reads valid noncontiguous memory and reports precise missing-page faults.
  Error-code exception delivery remains an existing explicit unsupported
  contract, now tracked in `beads-xbox.1.36`; these tests do not claim a guest
  page-fault handler runs.

## Real-code selection and route qualification

Qualified input `build/aot-20261003/menu-scalar.aot-private.json` contains **246
regions**, **8,122 guest bytes** and **2,833 decoded instructions**. It preserves
the ten prior regions and selects 236 additional nonoverlapping captured blocks.
Candidates must decode completely within the supported subset and match the
weighted block identities in the latest capture. Rank uses contained fallback
retirement across the six measured phases, with a 75,000 minimum and a 256-new-
region cap; 236 qualify. There are zero weighted identity conflicts.

This expands both new instruction coverage and selection of already supported
code. Whole-block census weights are not opcode costs or promised native
retirement: runtime context, memory and live-byte guards still decide admission.
The captures do not establish a complete static program inventory.

All source bytes come from the pinned original
`build/aot-20261002/dashboard-phase.csv.blockpack`, SHA-256
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`.
Input SHA-256:
`a19d16d71a6bfb594b0301d7dbe80ef89735422555c6bf65efa6c7d97474fc44`.
The exact selection policy and identities are retained in
`scalar-selection.aot-private.json` alongside that input.

The **27B floor/native O2/native O0 comparison passes**. All three complete the
14-event Settings-entry/return route. All **270 checkpoint rows and ten images
are byte-identical**, including the prior kernel-integer floor. IRQ delivery,
TSC reads and complete device/DMA history endpoints agree. Every reported native
adapter counter is identical at O0 and O2.

Native execution rises by **435,903,290** instructions to **1,815,586,408** over
the full route. Actual fallback is **7,191,748,216** instructions, with unchanged
**17,992,665,376** qualified idle skips. The census retains **52,144 identities**,
zero overflow and six reconciled phase intervals.

| Phase | Native instructions | Fallback instructions | Native share of executed work |
| --- | ---: | ---: | ---: |
| Main-menu idle | 74,767,141 | 31,244,829 | 70.53% |
| Select Live | 73,735,170 | 31,964,385 | 69.76% |
| Select Settings | 74,793,894 | 32,374,908 | 69.79% |
| Enter Settings | 194,306,000 | 58,975,039 | 76.72% |
| Settings idle | 40,016,408 | 21,352,957 | 65.21% |
| Return to main | 158,293,686 | 66,847,265 | 70.31% |

Shares exclude proven idle skips and include all fallback. Main-menu idle rises
from **47.92% to 70.53%**; Settings entry rises from **61.32% to 76.72%**.
Remaining hot blocks include LEAVE, NEG, memory arithmetic/comparisons, string
operations, unimplemented x87 forms and privileged kernel/interrupt paths.

This is a coverage checkpoint, with a material performance concern: total
native attempts rise from 11,924,114 to **94,298,273**, and handoffs rise from
3,899,713 to **48,856,092**. Two selected regions execute zero native instructions:
`0x8001e3cf` is rejected 14,856,588 times for its interrupt shadow, and
`0x000cfff1` has 944,804 code-translation declines. These are real refusals,
never counted as native work. Follow-up must measure the preceding native
baseline and address admission/dispatch overhead without relaxing guards.
No speedup is inferred from the instruction share.

## Reproduction and limits

Retained executables are
`xlle-scalar-{o2,o0}.exe`; replay prefixes are `scalar-{floor,native,o0}` under
`build/aot-20261003/`. The workload uses the provisioned locked HDD, retained
OpenGL renderer and `tests/scripts/stockdash-settings-exact.txt`.

Executable SHA-256:

- O2: `ea695e074b5e42b8c7b8e342dff367eccbefe3070ba409f2a2c020fe441658b5`.
- O0: `f01278ff19958bb459ca87c90b838fbaa15c35739013ccdf8c7082c4e42854b0`.

The common checkpoint SHA-256 remains
`9deb0f87d7e608e5f02ceb37eae56bdb6329458e78af56b20bc0462c524c0521`.
Complete evidence is retained in `scalar-validation.aot-private.json` and six
`scalar-<phase>.aot-private.json` summaries. Public tests are recorded in
`scalar-final-tests.log`; census tests in `scalar-census-tests.log` and
`scalar-census-required-tests.log`. Source integration is recorded in the
owning Beads issue; private code, captures and binaries remain excluded.

Full AOT, required menu interiors, host audio and original-speed 60 FPS remain
open. Concurrent instrumented replays are correctness evidence only.
