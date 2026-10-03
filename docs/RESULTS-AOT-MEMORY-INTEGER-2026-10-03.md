# Memory-read integer AOT and function epilogues

Tracking: `beads-xbox.1.39`, under the active
[dashboard goal](GOAL-AOT-DASHBOARD.md). Source baseline: `64298f5`, following
the [scalar qualification](RESULTS-AOT-SCALAR-2026-10-03.md) and
[exact membership improvement](RESULTS-AOT-MEMBERSHIP-2026-10-03.md).

## Implementation and independent qualification

General lowering now implements 32-bit register NEG/NOT and LEAVE, plus
32-bit memory-source ADD/SUB/XOR/AND/OR, both register/memory CMP directions,
register/memory TEST, and immediate memory CMP/TEST. Effective addresses use
the old registers, including destination/base aliases and signed displacements.
Immediate CMP correctly sign-extends its byte form. Comparisons preserve all
registers. NOT and LEAVE preserve flags.

LEAVE certifies and reads the saved frame before committing ESP and EBP. Every
new read uses the existing four-byte RAM callback; ABI **5**, runtime helpers,
translation/permission checks and code/event guards are unchanged. Refused
reads have no partial register, flag or retirement effects. Memory-destination
arithmetic, other widths and prefixed forms remain explicit unsupported exits.
There are no title-specific operations or helpers that execute guest code.

Intel's [instruction reference, volume 2A](https://cdrdv2-public.intel.com/858446/253666-088-sdm-vol-2a.pdf)
specifies the LEAVE stack/frame transition and unchanged flags. Logical AF is
architecturally undefined. The existing native logic helper preserves it while
the pinned CPU floor clears it. Independent expectations check that native
policy; differential logical checks exclude only AF and continue tracking it
as undefined through flag-preserving instructions until arithmetic defines it.
This is an explicit qualification limit, not a physical Xbox flag measurement.

All **76 public Release tests pass**. Generated scalar C passes at O0 and O3;
the compiler has **31 passing Python tests**. Existing negative tests now use
the still-unsupported NEG AL, retaining their fallback and inventory assertions.
Both scalar and both native-required tests also pass in the census build (4/4).

- **576 unary cases** cover all registers, arithmetic boundaries and three flag
  states, with independent expectations and reference execution.
- **81,920 memory-read cases** cover eight operations, all destination registers,
  ten address forms, operand boundaries and incoming flags. Selected reference
  comparisons check exact access order/width and every defined flag. Every
  encoding also tests a refused read with unchanged architectural state.
- **240 immediate memory cases** check operand direction, sign extension,
  arithmetic/logical flags and single four-byte reads.
- LEAVE covers aligned, unaligned and split-page addresses, saved-frame values,
  and refusal before state changes. Read/LEAVE sequences test zero budget,
  pending events and callback-induced code changes at the next boundary.
- A strict real-machine loop compares **140 successive instruction boundaries**
  and **110 RAM reads** with the floor. Native retirement advances while
  fallback runs/instructions and CPU execution/translated-block counters remain
  unchanged.
- **Twelve paged machine cases** cover CMP and LEAVE with a last DWORD before
  an absent page, a read-only page, absent/contiguous/noncontiguous split pages,
  and cold data translations. Strict refusals preserve state and page tables
  without hidden CPU execution. Separate floor execution handles valid cold or
  noncontiguous accesses and reports precise missing-page faults. The existing
  error-code exception delivery gap remains `beads-xbox.1.36`; these checks do
  not establish execution of a guest page-fault handler.
- A separate high-address LEAVE reads its saved frame at `0xfffffffc` through
  real paging, wraps the final ESP to zero, matches the floor and retires
  strictly natively.

## Real-code selection

Private input `build/aot-20261003/menu-memory-integer.aot-private.json` contains
**286 regions, 8,967 guest bytes and 3,111 decoded instructions**. All 246 prior
regions are preserved exactly. The 40 additions contain 845 bytes and 278
decoded instructions. Selection requires fully supported, nonoverlapping
blocks whose bytes match the latest capture. Ranking uses aggregate fallback
retirement in the six measured phases, a 75,000 minimum and a 256-addition cap.
Two weighted captured identity conflicts are excluded before support filtering;
they are not claimed as otherwise eligible additions.

Source bytes remain the pinned original
`build/aot-20261002/dashboard-phase.csv.blockpack`, SHA-256
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`.
Input SHA-256:
`e664910fa235669d314dfd9b44a618d58997e7bd4f3036cf34199f2902c0e276`.
The exact selection and rejected identities are retained in
`memory-integer-selection.aot-private.json`. Captured block weights are neither
opcode costs nor promised native retirement; live admission still decides.

## Route qualification

The **27B floor/native O0/native O2 comparison passes**. All three complete the
14-event Settings-entry/return route with all **270 checkpoint rows and ten
images byte-identical**, also matching the previous scalar floor. Device/DMA
history endpoints, IRQ delivery and guest TSC reads agree. Every reported native
adapter counter is identical at O0 and O2.

Full-route native execution reaches **1,964,141,698 instructions**, an increase
of **148,555,290**. Actual fallback is **7,043,192,926**, with unchanged
**17,992,665,376** qualified idle skips. The census retains **51,646 identities**,
zero overflow and six reconciled phase intervals.

| Phase | Native instructions | Fallback instructions | Native share of executed work |
| --- | ---: | ---: | ---: |
| Main-menu idle | 82,268,085 | 23,743,885 | 77.60% |
| Select Live | 81,199,474 | 24,500,081 | 76.82% |
| Select Settings | 82,347,425 | 24,821,377 | 76.84% |
| Enter Settings | 207,885,902 | 45,395,137 | 82.08% |
| Settings idle | 45,257,033 | 16,112,332 | 73.75% |
| Return to main | 174,098,081 | 51,042,870 | 77.33% |

Shares include all actual fallback and exclude proven idle skips. Main-menu
idle rises from **70.53% to 77.60%**; Settings entry rises from **76.72% to
82.08%**. These are measured execution shares, not static inventory completion.

Native attempts rise from 94,298,273 to **127,849,585** and handoffs from
48,856,092 to **55,944,419**. Memory declines rise from 613,860 to **713,860**;
context declines remain **14,856,588**. Wider coverage still needs independent
native-versus-native efficiency measurement. These correctness runs are not
quiet timing legs, and no speedup is inferred from them.

The remaining hot main-idle blocks include CLI/STI and interrupt-shadow
admission, REP/string transfers, byte-width operations, additional x87 forms,
scalar SSE and code-translation refusals. Whole-block weights identify where
work remains; they do not assign all that cost to one unsupported instruction.

## Reproduction and limits

Retained executables are `xlle-memory-integer-{o2,o0}.exe`; replay prefixes are
`memory-integer-{floor,native,o0}` under `build/aot-20261003`. Runs use the same
pinned BIOS/loader/EEPROM, provisioned locked HDD, COW writes, disconnected
network, OpenGL renderer, idle policy and
`tests/scripts/stockdash-settings-exact.txt` as the scalar reference. The native
O2 run enables the CPU block census. All use a 27B limit, 4096-instruction chunks,
100M checkpoint spacing and a 768 MiB host-memory limit.

Checkpoint SHA-256:
`9deb0f87d7e608e5f02ceb37eae56bdb6329458e78af56b20bc0462c524c0521`.
Final image SHA-256:
`4044cbd73dec3e8afbb5c69d3ba75cab0e436597f9e6bedc320812ac4a8ee671`.
Private `memory-integer-validation.aot-private.json` records executable/input
identities, every image digest, endpoint counters and phase partitions.
Six `memory-integer-<phase>.aot-private.json` files retain the interval evidence.
Public test results are in `memory-integer-public-tests-r2.log`; census tests
are in `memory-integer-census-tests.log`. Source integration is recorded in the
tracking issue and private validation artifact.

The ordinary `build-release` has private input cleared and census disabled.
No private guest material is committed. No renderer implementation changes in
this milestone; retain OpenGL and prioritize qualified donor reuse. Full AOT,
required menu interiors, UI audio, distinct frames, input latency and sustained
original-speed 60 FPS remain open. No whole dashboard or coverage gate closes.
