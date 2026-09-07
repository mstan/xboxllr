# AOT machine integration

Current AOT-4 changes, including ABI2 bounded code segments, private dashboard
execution and invocation-local RAM certification caches, are documented in
[the AOT-4 results](RESULTS-AOT4-2026-09-07.md) and
[the private workflow](AOT-PRIVATE-WORKFLOW.md). The AOT-3 record below describes
the earlier implementation and its measured baseline.

Status: AOT-3 implementation and local acceptance validated on 2026-09-07 under
`beads-xbox.1.25.3`; integration is recorded in that issue and Git history.
This document records the AOT-3 adapter; it does not claim dashboard AOT coverage
or a performance improvement. See [the acceptance goal](GOAL-AOT3-BURNDOWN.md).

## Execution and state ownership

`XboxMachine` registers immutable generated region descriptors with its CPU floor.
Their code, functions, and descriptors must outlive the machine. The default
registry is empty: the dashboard still executes through Unicorn until AOT-4.

The dispatcher lives inside `UnicornFloor::run`, sharing its authoritative
instruction counter. `XboxMachine::run` retains device scheduling, host polling,
interrupt delivery, and idle-loop fast-forward. A generated invocation receives
the remaining slice budget. It returns at a budget, event, unsupported instruction,
memory decline, external branch, or code mismatch. Fallback must actually retire
instructions before handing control back; an aborted self-modifying-code attempt
does not count as progress.

Native budgets stop at an instruction boundary. The existing Unicorn block path
can overrun a short slice by one translation block to make progress; AOT-3 does
not turn that fallback into an instruction-stepped scheduler.

Only GPRs, EIP, EFLAGS, and the retirement count cross the generated C ABI. Unicorn
retains all x87/MMX/SSE, segment-cache, control-register, and other hidden state.
Tests seed every XMM and x87 register and the FP control/status state, then check
them across repeated native/fallback transitions. Future generated FP instructions
must extend this ownership contract before they can execute.

## Memory and code coherence

The adapter accepts only the certified flat, protected, ring-zero IA-32 context.
The private bridge checks cached segment descriptors, rather than rereading GDT
memory. It declines debug/watchpoint, interrupt-shadow, snapshot, and other
unmodeled contexts. Diagnostic instruction/memory observers retain Unicorn
execution and their existing event stream.

Memory callbacks inspect the current MMU mode's direct and victim TLB entries.
They never walk page tables or fill a TLB. A cold access, MMIO, missing permission,
or unsupported mapping returns to Unicorn before effects. This preserves cached
translations after a PTE edit and lets the real MMU establish accessed/dirty bits.
Cross-page accesses certify every page first and require contiguous host backing.
Noncontiguous spans safely fall back. A write may have QEMU's software NOTDIRTY
flag because the adapter explicitly invalidates translated code after committing.

Each generated instruction checks its region's live bytes. Native writes update
the same backing bytes and invalidate fallback translations. Xbox's exact 64 MiB
RAM mirror shares the base mapping's internal RAM identity, so QEMU host-pointer
reverse lookup and physical invalidation agree. This registration occurs before
execution and only supports identical host pointers and lengths. It is not an
implementation of arbitrary overlapping host mappings.

At TB generation, QEMU protects cached writes by host span, covering direct and
victim TLB entries for every alias/MMU mode. Host writes explicitly invalidate
physical code because this pinned Unicorn's `uc_mem_write` only copies RAM.
These changes require requalification when upgrading the pinned dependency.

## Faults and interrupts

The build derives hash-checked copies of pinned QEMU sources; it does not edit the
downloaded source tree. Unhandled load/store errors restore the faulting EIP and
condition-code metadata. A cold callback from QEMU's precise restore path removes
the unexecuted block suffix from the clock, including self-modification restarts.
This callback adds no normal per-instruction call.

STI sets IF immediately but delays maskable interrupt recognition through the
following instruction. Both scheduling paths and external delivery consult the
cached inhibit state. Tests cover one slice, separate slices, IRET continuation,
and the instruction observer. This follows the STI definition in
[Intel's instruction reference](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf).

Existing error-code exception delivery remains unmodeled and stops with a fault;
AOT does not invent a replacement exception policy. The diagnostic `RetiredInsn`
callback historically records pre-execution dispatch. Clock repair cannot retract
an observer event already sent before a fault.

TSC-read/write instruction sites publish their exact index within the translated
block. Adding that index to the block's starting clock makes RDTSC independent
of native/fallback boundaries; the instruction observer already publishes the
exact clock and disables that addition. RDMSR and WRMSR for MSR `0x10` share this
clock and Unicorn's retained TSC offset. P6 writes clear the high half, as specified
in [Intel's TSC documentation](https://cdrdv2-public.intel.com/858456/253669-088-sdm-vol-3b.pdf).

## Opt-in replacements

Version-1 `AotReplacement` callbacks propose register-state transactions. An
explicit nonzero profile enables a registered identity bound to an exact compiled
region. Version, registration, uniqueness, live bytes, context, budget, and
protected flags gate execution. Profile zero is the faithful default.

The callback receives no machine or memory bus. A false or invalid proposal is
discarded and ordinary generated execution continues. A reviewed callback may
call an independent service model, with any future host backend behind that model.
A C++ function pointer is not a sandbox: callbacks must have no external effects.
Memory/device replacements need a later transactional interface. AOT-3 does not
implement D3D8 service replacement or enhanced display/game behavior.

## Validation record

The machine tests verify native/fallback/native continuation,
RAM and paging, stale TLBs, physical aliases, cross-page access and write faults,
MMIO access ordering, native self-modification, FP state ownership, and guarded
synthetic replacements. The simple 101-instruction loop records 100 native and
one fallback instruction. The alias self-modification test records one native
store and six fallback instructions; both executors reach the independently
calculated changed immediate.

The complete Release CTest suite passes **47/47**, including both generated-code
optimization levels. Additional tests cover rejected registry entries, invalid
replacement proposals, split-slice STI shadows, unmapped stores, exact RDTSC and
TSC MSR behavior, and host service at instruction counts 733334 and 1466667 with
a deliberately oversized CPU slice. These are independent 1 kHz deadline
expectations for the configured 733333333 Hz instruction clock.

The canonical 12-billion-instruction reference replay (`build/aot3-ref-20260907`)
passes all 31 report assertions: final image, 24 exercised profile hashes, empty
stderr, and configuration identity. Its 28 saved PPMs are byte-identical to the
latest pre-AOT reference (`build/rb-ref`). Compared with `build/golden-ref-v6`,
25/28 are exact; the remaining three have the same four isolated pixels already
documented in [PERF-METHODOLOGY](PERF-METHODOLOGY.md), section 3m. No new image
drift or golden replacement is needed. Final SHA-256:
`36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`.

Reproduce the reference check from a configured Release build:

```powershell
& ./tools/run_stockdash_replay.ps1 -Label aot3-reference `
  -GoldenRecord goldens/golden-noperf-defaults12-2026-09-05d.json `
  -ExtraXlleArgs @('--nv2a-draw-threads', '0', '--nv2a-profile-hashes') `
  -RequireQuiescence `
  -QuiescencePattern '^(xlle|g\+\+|cc1plus|ld|collect2|cmake|ninja)([-._0-9]|$)'
```

The process pattern treats resident Python/Beads services as residents. Avoid
other active workloads while measuring; a process census is not a whole-host
CPU-utilization guarantee. Replay artifacts contain private dashboard images and
remain ignored in `build/`.

### Dashboard overhead and coverage

One same-session interleaved A1/B1/A2/B2/A3/B3 set compares the saved pre-AOT-3
Release executable from `efd5bce` against this implementation. Both use shipping
defaults, OpenGL, noperf, chunk 4096, and the same 12 B replay/private inputs.
Session: `aot3-overhead-20260907-20260907T011053`; reports and the unmodified
gate result are in `build/aot3-overhead-20260907-*`.

| Total 12 B wall, seconds | Before AOT-3 | AOT-3 |
|---|---:|---:|
| Pair 1 | 46.625335 | 48.314841 |
| Pair 2 | 46.962899 | 48.274923 |
| Pair 3 | 49.746836 | 50.940953 |
| Median | 46.962899 | 48.314841 |
| Minimum | 46.625335 | 48.274923 |

Wall time increases **2.88% median / 3.54% minimum**; all three pairs favor the
old build. The existing 5% speedup gate reports **REJECT**, without an override.
Retain this as required correctness and machine-integration capability, not as
a performance optimization. This is the cost of the combined floor fixes and
adapter changes, not an isolated native-call benchmark. No dashboard region is
registered: **dashboard AOT retirement share is 0%**. AOT-4 must measure real
native coverage and boundary costs against this corrected baseline.

All six legs passed final-image/stderr checks and the recorded process census.
Every one of their 28 images is byte-identical across both arms, as are all
24 frame checkpoint instruction counts and hashes. CPU/device history digests
change across the old and corrected floors; image equality is not a claim that
the old CPU timing remains unchanged. The synthetic TSC, fault, and interrupt
tests establish the corrected behavior independently.

Each arm repeats its CPU/device/DMA digests and event counts at all 24 checkpoints
across all three runs. Final digests are:

| History | Before AOT-3 | AOT-3 |
|---|---|---|
| CPU/slice | `ed86cf6a1fe11c4f` | `7734e674ec465b94` |
| Device | `bbadac889020716f` | `54582fed08496cb7` |
| DMA | `f5c98df2a12072bb` | `f5c98df2a12072bb` |

Device events change from 17,588,661 to 17,588,625; IRQ0 pulses (16,342),
injected interrupts (19,577), and DMA writes (1,237,379) are unchanged. The logs
show a stable changed timeline but do not isolate each fix's causal contribution.

Executable identities (PE build identities, not reproducible source hashes):

- Before: `f682d891510a4200495b07d362fd7a8cfb79ca3f043697fc9095bdd635b6b488`.
- AOT-3: `fc97c8acc7b12326b3a767e2d89aa92e0bbf7157e822cf7fc731ebf8d78257d7`.

Reproduce the overhead protocol after saving the old executable and its DLLs
outside the active build directory:

```powershell
& ./tools/run_stockdash_replay.ps1 -Label aot3-overhead `
  -BaselineExe '<saved-before-build>/xlle.exe' `
  -Candidate build-release/xlle.exe -Repetitions 3 -RequireQuiescence `
  -QuiescencePattern '^(xlle|g\+\+|cc1plus|ld|collect2|cmake|ninja)([-._0-9]|$)'
```

The script's nonzero exit for a rejected speedup gate is expected for this
record; inspect the six successful leg reports and the gate JSON separately.
The headless replay does not measure interactive presented FPS or input latency.

Remaining product work is [AOT-4](GOAL-AOT4-BURNDOWN.md): bounded upstream
frontend qualification and an actual dashboard region. General x86 decoding,
native FP ownership, transactional device replacements, and host ARM execution
remain unqualified; these machine tests do not establish those capabilities.
