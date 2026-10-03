# General integer AOT for measured dashboard kernel work

Tracking: `beads-xbox.1.34`, under the active
[dashboard goal](GOAL-AOT-DASHBOARD.md). This extends the
[five-region x87 checkpoint](RESULTS-AOT-X87-EXPANSION-2026-10-03.md).

## Selection and implementation

The previous Settings-entry census attributes 72,971,262 retired fallback
instructions to five kernel block identities whose missing lowering consists
of ROL/ROR, register OR and BSWAP. This is whole-block attribution to captured
first-code identities, not an opcode cost measurement or a static recovery claim.

General portable-C lowering now supports:

- ROL/ROR on every 32-bit register, with implicit count one, immediate byte
  counts and CL. Counts are masked to five bits before shifting. Zero counts
  preserve all flags, and ECX destinations use the old CL value.
- OR32 in both register-encoding directions and the accumulator/group-1
  immediate forms, including sign-extended byte immediates.
- BSWAP on every 32-bit register, preserving flags.

The rotate helper defines CF and the one-bit OF result, preserving SF/ZF/AF/PF
and control flags. For nonzero counts greater than one, architecturally undefined
OF follows the pinned floor's deterministic result-bit policy. That policy is
tested against the floor and is not claimed as a physical Xbox measurement.
OR retains the existing logical helper's explicit undefined-AF policy; comparisons
mask AF where it is architecturally undefined. Unsupported memory, width and
prefix forms remain explicit exits. No helper executes guest code.

Instruction reference: Intel
[Software Developer's Manual, volume 2](https://cdrdv2-public.intel.com/774492/325383-sdm-vol-2abcd.pdf),
ROL/ROR, OR and BSWAP entries. Floor policy was also checked in the pinned
Unicorn `gen_rot_rm_T1` and `gen_rot_rm_im` implementation.

## Independent and differential qualification

The public Release suite passes **72/72**, with generated C tested at O0 and O3.
The census build's native-required machine tests pass **2/2**.

- 49,152 CL cases cover both directions, every destination register, all 256
  byte counts, six operand patterns and two initial flag states. Expected
  results use an independent mapping of individual input bits. This includes
  destination/count aliasing and all masked-zero counts.
- 240 immediate/implicit-count cases cover zero, one, larger counts and count
  wraparound. Defined flags have independent expectations; floor comparisons
  additionally check the selected undefined-OF policy.
- 96 BSWAP cases check authored byte-order expectations, untouched registers,
  all flags and absence of memory effects. OR has authored results/flags and
  immediate sign-extension expectations plus randomized differential cases.
- The mixed arithmetic cycle is checked at every instruction boundary across
  three iterations, including exact budgets and pending-event refusal.
- A real-machine strict scope compares 121 successive boundaries with the
  floor. Native retirement advances by exactly 121, while all fallback counters,
  translated-block entries and CPU execution-run counters remain unchanged.
  DF, IOPL and ID preservation are included.
- Decoder tests cover both register directions, every immediate byte and
  malformed/unsupported forms. An older fallback fixture now uses unsupported
  NEG because BSWAP is implemented; it still verifies native/fallback/native
  continuation. The separate unregistered-BSWAP machine test remains intact.

The **27B reference/native O0/native O2 dashboard comparison passes**. All three
complete the 14-event Settings-entry/return script. Their complete **270-row
checkpoint streams and all ten images are byte-identical**, also matching the
previous expanded-x87 floor. IRQ counts, TSC reads and complete device/DMA
history endpoints match. Native O0/O2 have identical instruction partitions
and every reported adapter counter.

The native builds execute **1,379,683,118** instructions through AOT, with
**7,627,651,506** actual fallback instructions and **17,992,665,376** qualified
idle skips. This moves **507,548,876** instructions from fallback into native
execution relative to the five-region checkpoint. Idle skips are unchanged.
The census retains **54,522 identities**, zero overflow and six reconciled
intervals.

| Phase | Native instructions | Fallback instructions | Native share of executed work |
| --- | ---: | ---: | ---: |
| Main-menu idle | 50,801,772 | 55,210,198 | 47.92% |
| Select Live | 50,097,915 | 55,601,640 | 47.40% |
| Select Settings | 50,838,190 | 56,330,612 | 47.44% |
| Enter Settings | 155,304,902 | 97,976,137 | 61.32% |
| Settings idle | 25,638,296 | 35,731,069 | 41.78% |
| Return to main | 107,941,458 | 117,199,493 | 47.94% |

Shares exclude qualified idle skips and include all actual fallback. Settings
entry rises from **29.33% to 61.32%**; main-menu idle remains **47.92%**. The
kernel selection primarily advances loading/transition coverage, not idle
rendering. Remaining main-idle gaps include shifts, extension/narrow transfers,
branches, LEAVE and memory/system operations. Some captured regions are already
syntactically supported but not yet selected or qualified for native execution.

## Private reproduction and limits

`build/aot-20261003/menu-kernel-integer.aot-private.json` selects the prior five
regions plus `0x80035cf7:15`, `0x80035d1a:51`, `0x80035d4d:1117`,
`0x800361aa:1192` and `0x80036652:485`. All ten are from the original
`build/aot-20261002/dashboard-phase.csv.blockpack`, SHA-256
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`.
The input contains 3,849 guest bytes and 1,408 decoded supported instructions.
All 1,230 contained kernel identities in the latest capture match the selected
original bytes. Runtime live-byte guards remain active; capture matching is not
proof of all future code versions. Previously qualified native-only regions can
have no fallback identities in the refreshed pack.

Retained builds and replay artifacts use `xlle-kernel-integer-{o0,o2}.exe` and
`kernel-integer-{floor,native,o0}` under `build/aot-20261003/`. Public test logs
are `integer-full-tests.log`; census checks are `integer-census-tests.log`.
The route uses the real locked, provisioned HDD, the retained OpenGL renderer,
eight-phase idle handling and `tests/scripts/stockdash-settings-exact.txt`.
Private inputs, code and binaries remain excluded from source publication.

The complete private comparison is `kernel-integer-validation.aot-private.json`,
with six `kernel-integer-<phase>.aot-private.json` interval summaries.
Retained executable SHA-256:

- O2, `xlle-kernel-integer-o2.exe`:
  `aa6d0c2b2b2d16cc19a4f090624b3d0ec35cf27e3994ecf701a5d9456aa9b1e6`.
- O0, `xlle-kernel-integer-o0.exe`:
  `bd41b8375e3c52da4883552b895559d303d5f9ddcc2849adca7488480e326ce3`.

The private input SHA-256 is
`61af5b018808903eada4304cafdc7d7f31b7077e39919f0345327e5bd78c14c9`.
The common checkpoint SHA-256 is
`9deb0f87d7e608e5f02ceb37eae56bdb6329458e78af56b20bc0462c524c0521`.

Full AOT, cold/device memory and system operations, remaining x87 contracts,
required menu interiors, host audio and sustained original-speed 60 FPS remain
open. Renderer reuse remains the policy. Concurrent instrumented replay times
are not performance evidence. Source integration is tracked in `beads-xbox.1.34`
on `perf/measurement-first-2026-09`; earlier checkpoint documents describe the
uncommitted state at their respective validation milestones.
