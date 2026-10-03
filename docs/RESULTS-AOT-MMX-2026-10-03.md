# MMX menu execution checkpoint

Tracking: `beads-xbox.1.25.5`. The measured dashboard MMX transfer loop now
executes as generated native code. The initial block-scheduled checkpoint below
measured **26.11% of executed CPU instructions in main-menu idle**, excluding
idle skips. The subsequent [exact-deadline qualification](RESULTS-CPU-DEADLINES-2026-10-03.md)
corrects the CPU reference and input schedule, resolves the observed history gap
and measured **10.02%** with four qualified idle phases. The
[eight-phase follow-up](RESULTS-IDLE-PHASES-2026-10-03.md) preserves the fully
executed route while removing more idle work; the same native menu loop now
accounts for **26.04%** of remaining executed main-menu idle instructions.
Full menu AOT and 60 FPS remain open. Preserve the initial evidence below as
a record of that earlier build.

## CPU floor corrections come first

The pinned Unicorn/QEMU translator failed 21 independently expected assertions
for the MMX operations needed by the captured loop. MOVQ did not set the written
register's x87 exponent to all ones. Memory faults and an invalid MOVNTQ register
encoding changed tags/TOP before completion. Pending x87 exceptions were omitted.

`cmake/UnicornMmx.cmake` derives corrections in the existing hash-verified build
copy of `translate.c`; it preserves the upstream source and license. Successful
MOVQ/MOVNTQ transfers commit MMX state after the memory operation. Register
destinations get the required exponent. Fixed FWAIT helpers handle pending
exceptions. The change is scoped to MOVQ, MOVNTQ and EMMS, not a qualification of
the complete MMX/SSE instruction set.

Expected alias, TOP/tag and exponent behavior comes from Intel SDM Vol.1,
section 9.6.2; pending-exception behavior is specified in Vol.3A, chapter 11.
The full tag image saved to memory is classified from register values; this
differs from the internal valid/empty tags. Sources:
[Intel Vol.1](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366521.pdf),
[Intel Vol.3A](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf).
Real-console qualification remains deferred; a corrected reference is not an
independent hardware measurement.

The floor fixture now passes all physical-register loads, both register-move
directions, unaligned stores, EMMS preservation, invalid/unmapped operands,
pending exceptions and genuine second-page guest #PF cases. A missing physical
bus mapping is distinct from a guest page fault: the page-fault tests install
actual page tables rather than treating a host API mapping error as #PF.

## Native contract

ABI 3 adds fixed MOVQ load/store/register-move, MOVNTQ and EMMS services.
Generated code still carries GPR/EIP/EFLAGS; the engine owns the shared x87/MMX
register file and control/status state. These helpers do no runtime guest-opcode
decoding, instruction execution or TLB refill. No parallel shadow CPU state is
invented. Old generated ABI artifacts fail registration and must be rebuilt.

Every eight-byte memory operand is certified across all pages before any guest
effect. Cold translations, MMIO and unsupported mappings decline. Stores update
the complete certified RAM span and invalidate executable physical aliases.
CR0.EM/TS, missing features and pending x87 exceptions decline before effects;
the CPU floor delivers exceptions when fallback is permitted. Strict validation
refuses the fallback instead. Flags, instruction budgets and code identity remain
checked at each generated instruction.

The loop also needs PREFETCHh. Its four locality hints check SSE availability;
they do not read guest data, service MMIO or fault on an unavailable data address.
Where supported by the host compiler, certified RAM receives a host prefetch.
This implements an architecturally discardable cache hint, not an invented
successful load. The existing CPU model does not model guest cache-cycle timing.
See Intel's [PREFETCHh instruction contract](https://cdrdv2-public.intel.com/789581/325383-sdm-vol-2abcd.pdf).

Authored machine tests run every prefix of an MMX copy/EMMS loop in a declared
warm strict scope with zero fallback instructions, runs or translation-block
entries. They check shared extended state against the corrected floor, independent
qword results/exponents, flags, cross-page/unaligned accesses, the second page
being cold, disabled FPU/pending exception refusal, and changed code through a
physical alias. Generated optimized and O0 variants pass. All four prefetch
localities preserve state for warm and unavailable addresses. The full public
Release suite passes **66/66**; final fixture refinements also pass.

## Actual dashboard execution

The private input selects the measured 81-byte region beginning at `0x00148c50`
from the existing first-code pack, whose SHA-256 is
`bfc370a97b205323735809d036d3812070a8d9b16fcac0a6a36ac2aa2936dfd6`.
All 24 instructions are supported by general lowering rules. Runtime byte guards
verify the live code on each instruction; the first-code capture alone is not
treated as proof of version stability. No guest-derived bytes enter public source.

The corrected-floor, private O2 and private O0 builds complete the existing
22B `stockdash-settings.txt` route with OpenGL and provisioned HDD security.
Use the [phase command](RESULTS-DASHBOARD-PHASES-2026-10-02.md), adding
`--aot-native` and the private input prepared with `tools/prepare_aot_input.py`.
The private workflow describes configuration and publication guards.

O0 and O2 have identical native/fallback/idle counters, phase clocks, all ten
saved images, device/DMA digests and pending-IRQ boundary count. They retire
351,997,592 native instructions, 8,247,988,724 fallback instructions and
13,400,013,696 idle skips, totaling 22,000,000,012. Code-TLB and entry-context
declines are zero; 258,886 memory declines remain. These runs are instrumented
correctness/coverage evidence, not timing comparisons.

| Interval | Native instructions | Native share of executed CPU instructions |
| --- | ---: | ---: |
| Main-menu idle | 27,681,792 | 26.1145% |
| Select Xbox Live | 27,270,788 | 25.8003% |
| Select Settings | 27,663,694 | 25.8159% |
| Enter Settings | 32,273,513 | 12.6960% |
| Settings idle | 9,783,662 | 15.9063% |
| Return to main menu | 55,438,309 | 24.6733% |

These intervals use the same controller-report checkpoints as the earlier
baseline. Native and corrected-floor captures differ by at most three guest
clock units and are **10/10 byte-identical**, including the final frame.
NV2A method/draw totals, controller reports, IDE traffic, IRQ0 pulse count and
delivered interrupt count agree. The displayed screens were established in the
earlier phase audit; matching these images does not qualify unvisited workflows.

## Original equivalence gap (resolved for the tested route)

Device and DMA event totals agree, but their cumulative digests do not:

| Measure | Corrected floor | Native O0/O2 |
| --- | --- | --- |
| Device events | 17,940,679 | 17,940,679 |
| Device digest | `207fdc54269006c1` | `ca6232d46abe3141` |
| DMA writes | 2,054,354 | 2,054,354 |
| DMA digest | `324fef031d1c3a01` | `b18bdc96f9bee5e9` |
| Pending IRQ boundaries | 24,804 | 24,815 |

These hashes exclude event timestamps. The subsequent ring comparison found
reordered interrupt servicing and DMA writes. Exact fallback deadlines and a
separately reproduced CMPXCHG8B flag correction resolve the observed discrepancy:
floor/O0/O2 now have identical 270-row histories and ten images on the visually
verified 27B Settings route. See the linked qualification for counts, refreshed
phase coverage and retained failed attempts. This is not complete machine or
hardware equivalence. Whole-menu strict execution still fails outside the one
compiled region. Production x87 arithmetic and remaining kernel/memory operations
are subsequent coverage work.

Private artifacts are under `build/aot-20261003/`: `mmx-native*`, `mmx-floor*`,
`mmx-o0*`, `menu-mmx.aot-private.json`, and diagnostic `mmx-det-*`/event-ring dumps.
The ordinary Release configuration retains private input empty and census OFF.
Source changes are uncommitted.
