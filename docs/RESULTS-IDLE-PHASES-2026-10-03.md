# Exact idle-loop phases and cached-memory qualification

Tracking: `beads-xbox.1.4.7`, under the active dashboard goal
`beads-xbox.1.25.5`. This follows the
[exact-deadline CPU correction](RESULTS-CPU-DEADLINES-2026-10-03.md).

## Problem and implementation

Exact instruction deadlines expose all eight instruction boundaries in the
pinned kernel idle loop. The original fast-forward recognized four. The corrected
main-menu census attributed roughly 174 million executed instructions in one
850.7-million-clock interval to idle work left outside that qualification.

All eight phases now qualify for whole iterations. Each iteration returns to
the same PC, fixed-point flags and hidden interrupt shadow. IF is set at phases
1, 2 and 3; phase 1 retains the STI shadow, so interrupts can actually arrive
at phases 2 and 3. No register or flag fixup is synthesized. The remaining
zero through seven instructions execute normally. Skips remain bounded by the
run budget and the next device deadline, with no pending interrupt allowed.

The old physical page-table reads were also insufficient qualification:
changing a PTE without invalidation need not change the CPU's cached mapping.
Code fetches and both idle data reads now require already-cached RAM access
with the actual permission and host backing. Each page of an unaligned span
is checked. The check never fills a TLB, sets an accessed bit or reads MMIO.
Unqualified access returns to real guest execution.

The CPU additionally certifies flat ring-0 mode, CS limits, the expected shadow,
and absence of debug, diagnostic-observer, stop and unsupported hidden state.
The shared native context guard now also checks enabled DR7 breakpoint bits:
writing DR7 through the register API did not populate QEMU's breakpoint list,
so inspecting only that list missed this state. No hidden state is altered
to make either optimization eligible. Eight phase counters and a context-rejection
counter expose the result.

## Behavioral qualification

`tests/test_idle_fast_forward.cpp` uses real floor execution and independently
calculated phase, clock and interrupt expectations. It checks:

- 256 combinations of executed/skipped arms, ordinary/split-page data, eight
  entry phases and eight residual instruction counts; full integer flags and
  untouched registers, RAM, STI shadow and a subsequent guest RDTSC agree.
- Memory and instruction observers configured before translation receive all
  effects; pending stops, changed flags, queued work and changed loop code
  prevent skipping. Missing shadow, RF and enabled DR7 state are rejected.
- A changed PTE retains the cached mapping until CR3 reload; cold and cached
  MMIO checks have no guest-memory or device effects, and wrapping spans decline.
  Warming only the first page cannot certify a split operand's second page.
- Both arms reach independently calculated service deadlines 733,334 and
  1,466,667 at the same PC, flags and interrupt eligibility.
- Pending IRQ entry is correct at every phase. A real PIC request at clock
  733,334 delivers after the inhibited NOP at 733,338. The executed guest
  handler records the expected saved PC/flags and RDTSC 733,342, then uses
  IRET; both arms retain identical final CPU and device/DMA histories.

The public Release suite passes **69/69**, and the behavioral fixture also
passes in the census build. The separate cached-TB observer-activation issue
`beads-xbox.1.29` remains open: the observer test installs hooks before execution,
as normal diagnostics do. Its scope does not claim to fix dynamic activation.

## Dashboard comparison

All three runs complete the same 27B Settings-entry/return route without a CPU
exception or device stop: native O2 plus skipping, floor plus skipping, and
floor with skipping disabled. All ten same-clock images are byte-identical
across these three arms and the preceding qualified run. The images therefore
retain its visually verified main menu, Settings interior and return states.

The two skip-enabled arms have identical **270-row** checkpoint streams.
Skipping changes slice boundaries, so the no-skip stream is not claimed to be
byte-identical: its **100 common-clock rows** match on PC and cumulative device/
DMA hashes and counts. The preceding four-phase run has 234 common-clock rows,
with those same fields matching. All four runs agree on the complete endpoint:

| Endpoint | All four runs |
| --- | --- |
| Clock | 27,000,000,000 |
| PIT IRQ0 pulses / delivered interrupts | 36,796 / 46,341 |
| Pending IRQ boundaries / guest TSC reads | 28,481 / 14,342 |
| Device events / digest | 18,059,827 / `bb5d546fb1f0bc08` |
| DMA writes / digest | 2,476,411 / `041fbd7997416182` |

The native run executes 437,873,463 native instructions, 8,569,461,161 fallback
instructions and skips 17,992,665,376 idle instructions. These sum exactly to
the final clock. Native instruction count is unchanged. The new phases skip
an additional **4,294,266,416** idle instructions compared with the previous
qualification, leaving less executed work. No executed work is discarded from
the coverage denominator.

The new census retains **57,304 block identities, zero overflow**. The full
sum and all six phase intervals reconcile:

| Interval | Native instructions | Fallback instructions | Idle skipped | Native / executed |
| --- | ---: | ---: | ---: | ---: |
| Main-menu idle | 27,607,827 | 78,404,143 | 744,654,696 | 26.0422% |
| Select Xbox Live | 27,209,434 | 78,490,121 | 744,967,112 | 25.7422% |
| Select Settings | 27,612,463 | 79,556,339 | 743,497,864 | 25.7654% |
| Enter Settings | 32,084,455 | 221,196,584 | 1,330,718,960 | 12.6675% |
| Settings idle | 9,780,736 | 51,588,629 | 671,963,968 | 15.9375% |
| Return to main menu | 55,503,712 | 169,637,239 | 1,358,859,048 | 24.6529% |

This is an idle optimization, not newly translated menu code. The main-idle
interval's largest remaining fallback block is the measured x87 sequence,
with 9,626,784 instructions. Required x87 arithmetic and broader kernel/memory
execution are the next coverage work.

Reproduce the preceding route command with `--idle-fast-forward` or
`--no-idle-fast-forward` and `--aot-native` or `--no-aot-native`, keeping the
same executable, assets, script and 27B endpoint. Private logs, captures and
census files use the `idle-eight-*` prefix under `build/aot-20261003/`.
The private validation JSON records the compared fields, hashes and image
identities; its six interval JSON files are produced through the census
validator. The replay executable is retained as `xlle-idle-eight-qualified.exe`
(SHA-256 `83b3683f6bfc4f2efbef6ee8482424b22ca45e3841cf6e5b767af4c62fd02f80`),
alongside `xlle-before-idle-eight.exe`.

These concurrent, instrumented runs are correctness checks. Full dashboard
AOT, required menu interiors, UI audio, hardware qualification and quiet-host
60 FPS acceptance remain open. Source changes remain uncommitted.
