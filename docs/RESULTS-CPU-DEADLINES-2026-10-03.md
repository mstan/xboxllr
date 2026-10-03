# Exact CPU deadlines and the MMX route comparison

Tracking: `beads-xbox.1.30` (scheduler) and `beads-xbox.1.31`
(CMPXCHG8B flags), under the dashboard goal `beads-xbox.1.25.5`.
This investigates the event-digest gap in the
[MMX menu checkpoint](RESULTS-AOT-MMX-2026-10-03.md).

## What differed

The original 22B runs had equal device/DMA totals and ten identical images,
but different cumulative event hashes. These hashes exclude timestamps.
Aligning the always-on rings by event ordinal identified permutations:

- At 12B, three groups contain 54 differing device-event positions. Each group
  has the same address, size, direction and value multiset. GPU and audio
  interrupt servicing changes order. The overlapping DMA window is identical.
- At 22B, the device ring contains 17 such groups, totaling 274 positions.
- A 17.5B DMA dump isolates 20 reordered writes: a GPU semaphore write changes
  position relative to USB writes. The data hashes and address/size multiset
  agree. The later DMA window collected at 22B contains no differences.

These are comparisons of retained overlapping windows, not a byte audit of
every DMA payload since reset. They explain the observed order differences;
they do not justify discarding order from the acceptance check.

The fallback scheduler stopped before a crossing translation block but let
the first block of its next call exceed the remaining budget. Native execution
stopped at individual instructions. The two paths therefore exposed different
device and interrupt boundaries.

## Exact fallback boundaries

`cmake/UnicornDeadlines.cmake` derives a build copy of pinned QEMU `cpu-exec.c`,
preserving its license. Its normalized source SHA-256 is
`99db183f8b049c5530a90fc00aa28eb1c783188929053d2ecd4d62facfe80102`.
The generation callback now reports the first block with a null predecessor.
No block executes with deferred bootstrap retirement.

When a block would cross the deadline, the CPU floor stops before it and
requests a shorter translation using QEMU's existing `CF_COUNT_MASK` mechanism.
That prefix actually executes through the usual block hook and fault machinery.
The request preserves any tighter precise-SMC/watchpoint limit. No global
instruction hook, Unicorn count hook, fabricated retirement or guest-code
change is involved. `tb_deadline_splits` reports these fallback restarts.
Strict native scopes still reject all guest fallback execution.

The first real replay exposed another existing assumption: evicting an
instruction count from the small fast table does not force QEMU to regenerate
the corresponding cached block. More block-length variants made this visible
at clock 708,650. Displaced counts now live in a spill map and are refreshed
when the same identity is regenerated. Unknown counts still stop loudly.

The authored deadline fixture checks every prefix of a 65-instruction loop,
mixed successive budgets, 64 lengths at the same PC, re-entry into the cached
full block, and a fault inside a shortened block. It asserts actual register
effects, exact retirement, fault EIP and the absence of instruction/count hooks.
The machine fixture checks independently calculated 1ms and 2ms deadlines for
both the floor and native path. The original cold-loop and collision cases
were observed failing before their fixes.

## Shared CPU-core defect exposed by finer boundaries

After those fixes, both paths have identical checkpoint streams through 11.4B.
Both then raise a debug exception with EFLAGS `0xffff` at kernel PC
`0x8001590e`. Private RAM records bugcheck `0x1e`, exception status `0x80000004`
and that same PC. The later unknown serial-port access is debugger
initialization, not the origin of the failure.

The pinned CMPXCHG8B helper reads the engine's lazy condition-code operation,
but its translator did not publish the preceding arithmetic operation first.
An ADD operand of `0xffff` could therefore be treated as literal EFLAGS,
including control bits. Publishing `cc_op` before calling the existing helper
fixes that contract; the values are not masked into a plausible result.

The independent fixture has 32 cases: LOCK/no LOCK, comparison success/failure,
split/unsplit execution and four arithmetic flag patterns, with unaligned
qwords. Sixteen unsplit cases failed before the correction; all pass afterward.
Expected flags follow Intel's specification that CMPXCHG8B changes ZF while
preserving the other arithmetic flags. See
[Intel SDM Vol.2, CMPXCHG8B/CMPXCHG16B](https://cdrdv2-public.intel.com/789581/325383-sdm-vol-2abcd.pdf).
The fixture also checks both register pairs and all eight memory bytes.
This qualifies the tested 32-bit CMPXCHG8B path, not the complete atomic ISA.

The public Release suite passes **68/68**. The corrected floor and native paths
complete the old 22B schedule with identical 220-row checkpoint streams and ten
same-clock images. The raw retained device and DMA rings are byte-identical too.
Visual inspection is essential: that old input schedule now presses A before
the clock screen accepts input. Its labels do not describe the reached screens;
it never enters Settings. It is not the accepted navigation route below.

## Qualified navigation route

[`stockdash-settings-exact.txt`](../tests/scripts/stockdash-settings-exact.txt)
waits until guest controller report 855 to accept the ready clock screen, holds
A for 20 reports and retains explicit neutral intervals. This is ordinary input
to the guest. No guest state, code or device acknowledgement is changed.

Floor, native O2 and native O0 complete **27,000,000,000** clock units. Images
were inspected: main menu at 18.035B, Settings interior at 22.171B, and return
to the main menu with Settings selected at 24.488B. All ten saved image pairs
have identical capture clocks and bytes. All **270 checkpoint rows** are
byte-identical across the three runs, including EIP, slice count and the
execution/device/DMA digests and event counts.

| Endpoint | All three runs |
| --- | --- |
| Slice digest | `2125fd53c22f7527` |
| Device events / digest | 18,059,827 / `bb5d546fb1f0bc08` |
| DMA writes / digest | 2,476,411 / `041fbd7997416182` |
| Delivered interrupts | 46,341 |
| Pending IRQ boundaries | 28,481 |

Both native variants retire 437,873,463 instructions natively, 12,863,727,577
through the floor, and skip 13,698,398,960 proven idle instructions. These sum
to the exact final clock. All native adapter counters agree between O0/O2.
The observed history discrepancy is resolved for this route. This is not an
independent hardware qualification, a whole-RAM equality proof, full-menu AOT
or a 60 FPS result.

At this checkpoint the finer boundaries exposed additional idle-loop entry
phases; only the previously qualified four phases were eligible for fast-forward.
Other phases executed normally. The [eight-phase follow-up](RESULTS-IDLE-PHASES-2026-10-03.md)
qualifies the remaining phases against a fully executed route. This checkpoint's coverage denominator differs
from the old block-scheduled run, so its 26.11% figure is historical.

## Census integrity

Additional block lengths also filled the diagnostic census's bounded probe
windows. Its validator rejects the overflowed phase inventories; those files
are not used for coverage percentages. A 12-address collision fixture reproduced
the loss (only eight first-code records survived). The recorder now keeps spill
rows, including their first-code bytes, and emits all rows in a stable order.
The fixture reconciles all 24 executed instructions and all 12 code records.
It passes in the census build in addition to the ordinary 68-test suite.
The final run retains **57,301 block identities with zero overflow**; both the
full-run partition and all six intervals reconcile. Its 270-row checkpoint
stream, all native adapter counters and all ten same-clock images are identical
to the already-qualified O2 run. The diagnostic repair preserves observed guest
behavior.

| Corrected interval | Native instructions | Share of executed CPU instructions |
| --- | ---: | ---: |
| Main-menu idle | 27,607,827 | 10.0215% |
| Select Xbox Live | 27,209,434 | 9.6587% |
| Select Settings | 27,612,463 | 9.9251% |
| Enter Settings | 32,084,455 | 5.8491% |
| Settings idle | 9,780,736 | 4.5990% |
| Return to main menu | 55,503,712 | 10.2056% |

Idle skips are excluded from the denominator. Idle instructions that actually
execute remain included; removing those would inflate coverage. These counts
measure instruction coverage, not host time or progress toward the complete
dashboard feature set.

## Reproduction and limits

Use the MMX checkpoint's provisioned-HDD command with the new
`tests/scripts/stockdash-settings-exact.txt` script and `--max-insns 27000000000`.
Floor and native O2 use the same private executable, selecting
`--no-aot-native` or `--aot-native`; native O0 changes only generated-code
optimization. Add
`--det-log <private-path> 100000000` for cumulative checkpoints; the continuously
recorded device and DMA rings can be dumped with the existing CLI options.

Private artifacts in `build/aot-20261003/` retain the initial `mmx-ring*`
comparisons, the failed `mmx-exact-*` and `mmx-exact2-*` attempts, the failure RAM
capture, the corrected `mmx-exact3-*` replay, the qualified `mmx-route-*`
navigation comparisons and the final `mmx-census*` inventory refresh. The
prefix/collision/CMPXCHG before/after logs are retained there too. These
artifacts remain private; no guest code or credentials enter tracked files.
The ordinary Release configuration has private input empty and census OFF.
These instrumented runs are correctness evidence; quiet-host performance,
real-console qualification and the full dashboard AOT gate remain open.
Source changes are uncommitted.
