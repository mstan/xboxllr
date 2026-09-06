# CPU tier: measured decision between tuned TCG, WHPX, and a custom JIT

> Tracker `beads-xbox.1.16` (children `.1` decomposition, `.2` WHPX floor, `.3` JIT scoping).
> Written 2026-09-05 under owner decision `docs/PERF-METHODOLOGY.md` §1 item 11 — *"if Unicorn
> must be replaced, replace it."* Acceptance standard is §1 items 9–11 and §3: behavioural
> faithfulness plus speed. Every number below was measured on this host in this session; where
> something was not measured it says so.
>
> **Host:** AMD Ryzen 7 9800X3D, 16 logical processors, Windows 11 26200. Toolchain
> `C:\msys64\mingw64\bin\g++ -O3 -DNDEBUG -std=c++17`, the same compiler and flags
> `build-release` uses, linking the same three Unicorn archives `xlle.exe` links.

## 0. The answer, first

**2026-09-06 scope amendment:** the owner has reaffirmed faithful LLE at 60 FPS with
headroom. The measurements below remain useful, particularly the WHPX exit-cost result.
They do not establish that TCG tuning or memory-system optimization is exhausted. The
synthetic benchmark's unsplit residual is not a proven irreducible floor, and section 5.1's
blanket sub-5% conclusion is stronger than its own 7.5% hook envelope permits. See
[the amended campaign plan](PERF-CAMPAIGN-2026-09-06.md) for the production measurements
needed before retaining or rejecting further changes. The original decision follows.

**None of the three replacements is justified, and the reason is arithmetic rather than taste.**

| option | measured ceiling on `tcg_exec` (today 14.700 s) | correctness risk | verdict |
|---|---|---|---|
| **A. Tune/patch the vendored Unicorn TCG** | **≈ 9.90 s** even with the block hook removed *entirely*; the single best individual patch is worth **0.995 s** | low | **Not started.** Precondition ("can A get under ~5 s?") **fails on measurement.** |
| **B. WHPX hardware virtualization** | **56.6 – 59.1 s** — i.e. **3.7×–4.4× SLOWER** than what it replaces | very high (loses instruction-exactness) | **Rejected on measurement.** |
| **C. Custom x86→x64 dynamic recompiler** | bounded by the same 8.25 s bucket, most of which is genuine work; a *perfect* JIT saves ≤ 14.7 s of a 63.9 s run | very high, multi-quarter | **Design + beads only** (this document). |

And the fact that dominates all three: **`tcg_exec` is 14.700 s of a 63.909 s emulation thread —
23.0 %.** Deleting the CPU tier outright, to zero, is a 1.30× whole-run speedup. The other 47 s
is the NV2A/device path. The CPU tier stopped being the lever on 2026-09-04
(`PERF-AUDIT-2026-09-02.md` §8h) and this session confirms it quantitatively against all three
alternatives.

## 1. The measurement base

One canonical 12 B replay at shipping defaults, `--perf-accounting`,
`build/p4-perf-B.stdout.log`, `[perf-excl] scope=cumulative` plus the always-on
`[unicorn-memory]` and `[host] idle-census` lines from the same run. Wall 63.909 s, accounted
63.815 s, **unaccounted 0.147 %**, `discarded_deltas=0`.

**Caveat, stated because it moves the denominator:** this is an *instrumented* run. §3b measures
`--perf-accounting` at **+11.0 %**, so the shipping noperf wall for the same work is ≈ 57.6 s
(consistent with the ~59 s figure quoted elsewhere). Attribution is what an instrumented run is
for (§2 rule 6), and the *shares* are what this document reasons about; every absolute second
below is on the instrumented run and should be read as ~10 % high in wall terms. None of the
conclusions turn on it — the WHPX floor is 3.7×–4.4× over, not 10 % over.

| quantity | value | source |
|---|---:|---|
| `tcg_exec` | **14.700 s** | `[perf-excl]` |
| `mmio_dispatch` | 0.728 s over **17 546 710** entries | `[perf-excl]` |
| non-NV2A MMIO (`other_device`) | 45 672 entries → **99.74 % of MMIO is NV2A** | `[perf-excl]` |
| `io_port` | 0.016 s over 41 951 | `[perf-excl]` |
| `intr_delivery` | 0.010 s over 19 609 | `[perf-excl]` |
| block entries | **2 302 057 484** | `[unicorn-memory] tb-entries` |
| instructions executed | **7 301 344 897** | `[unicorn-memory] tb-insns` |
| instructions skipped (idle FF) | 4 698 655 104 (39.2 %) | `tb-skipped-insns` |
| distinct TB identities | **23 679**, `tb-icount-misses=0` | `tb-icount-entries` |
| **instructions per block** | **3.172** | derived |
| **ns per block entry** | **6.386** | derived (14.700 s ÷ 2.302 G) |

`7.301 G ÷ 14.700 s = 497 M insn/s`, against the bare-Unicorn benchmark's 2 991 M on MIXED
(`beads-xbox.1.14.2`). That apparent **6×** is what this work was asked to explain.

## 2. Why production TCG is "6× slower" — it mostly is not (`beads-xbox.1.16.1`)

**The premise was wrong, and that is the finding.** The MIXED workload the 2 991 M figure comes
from runs 170 instructions in **five** translation blocks (161 + 1 + 2 + 2 + 4) ≈ **34
instructions per block**. Production runs **3.172**. Per-block cost — TB dispatch, the hook call,
QEMU's `la_global_kill` register spill at the hook boundary — is amortized over **ten times fewer
instructions** in the real workload than in the benchmark used to predict it. The two numbers
were never comparable.

### 2.1 The instrument

`tools/bench/block_size_bench.cpp` (+ `build_block_size.ps1`) sweeps **block size** as the
independent variable at four hook shapes:

| arm | what it is |
|---|---|
| `B0` | no hook — the TCG floor at that block size |
| `B2` | trivial one-counter block hook — what `unicorn_hook_bench.cpp`'s H2 measured |
| `BP` | **faithful transcription of `UnicornFloor::block_trampoline`** (`src/cpu/unicorn_floor.cpp:568-658`): same order, same branches, the same 2^18-slot open-addressed icount table with an 8-probe window and a one-entry inline cache, the same 8192-entry PC ring, the same `std::string stop_reason` check, with a real `UC_HOOK_EDGE_GENERATED` populating the table |
| `BX` | `BP` with the **icount lookup removed**, modelling the count arriving *with* the block |

Workloads are chains of `k`-instruction blocks (k−1 register ALU ops + a terminator). The
**direct** arm chains with `jmp rel32`, which QEMU patches into a direct branch — the *best case*,
and therefore the honest lower bound on per-block cost. The **indirect** arm chains through a
shared `call`/`ret` stub and is reported as an upper bracket only; it is layout-pathological (it
produces a *negative* `B2−B0` at 3.488 insns/block) and is **not** used as a lever measurement.

Discipline: 5 interleaved reps, 200 M instructions per cell, quiet host, **0 failures**, **no row
above the 10 % median-to-min spread flag**, every block count exact against the emitted program,
and the only instruction deficit is the uncharged bootstrap block (exactly k−1, explained — xlle
repays it via `settle_bootstrap_block()`; the benchmark deliberately does not). Three independent
runs agree to ~0.03 ns on the direct rows.

### 2.2 The measured table

ns per block entry, median of 5, direct (chained) arm:

| insn/blk | `B0` | `B2` | `BP` | `BX` | `B2−B0` | `BP−B2` | `BP−BX` |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 2.000 | 0.468 | 1.361 | 2.731 | 2.360 | 0.893 | 1.370 | 0.371 |
| **2.985** | **0.704** | **1.426** | **2.791** | **2.360** | 0.722 | 1.365 | 0.431 |
| **3.969** | **0.780** | **1.691** | **2.857** | **2.419** | 0.911 | 1.166 | 0.438 |
| 5.938 | 0.897 | 1.900 | 2.981 | 2.608 | 1.003 | 1.082 | 0.373 |
| 7.908 | 0.826 | 2.059 | 3.049 | 2.648 | 1.233 | 0.989 | 0.401 |
| 15.785 | 1.466 | 3.144 | 4.729 | 4.211 | 1.678 | 1.586 | 0.519 |
| 33.508 | 3.318 | 4.625 | 4.861 | 4.613 | 1.307 | 0.236 | 0.248 |
| 63.046 | 6.401 | 7.557 | 8.868 | 8.446 | 1.156 | 1.311 | 0.422 |

The 2.985 and 3.969 rows bracket production's 3.172.

### 2.3 The decomposition of `tcg_exec`

Interpolating the direct arm to 3.172 insn/block and multiplying by 2 302 057 484 block entries:

| bucket | ns/block | seconds | share | what would remove it |
|---|---:|---:|---:|---|
| TCG dispatch + **register-only** execution (`B0`) | 0.718 | **1.654** | 11.3 % | nothing — this is TCG running code |
| block-hook boundary (`B2−B0`) | 0.758 | **1.745** | 11.9 % | removing the hook entirely — then the instruction clock and the slice deadline must come from somewhere else |
| hook body: icount lookup (`BP−BX`) | 0.432 | **0.995** | 6.8 % | emitting the icount into the TB instead of a side table |
| hook body: everything else (`BX−B2`) | 0.895 | **2.060** | 14.0 % | trimming the PC ring / `stop_reason` / TSC publish — bounded and small |
| **everything real guest code does that synthetic register-ALU code does not** | 3.582 | **8.246** | **56.1 %** | *mostly nothing* — see below |
| **total** | 6.386 | **14.700** | | (measured: 14.700) |

**Read this table as: the entire hook is 4.80 s of 14.700 s (32.7 %).** Removing it completely —
more than any realistic patch achieves, since the clock and the deadline have to come from
somewhere — still leaves **≈ 9.90 s**.

**What the last bucket is, stated precisely, because the label matters.** The benchmark's
workload is register-only ALU; the guest's is not. So that 8.246 s is *not* a pile of removable
overhead — it is the difference between executing synthetic ALU blocks and executing the real
instruction mix at the same block size with the same hook, and it contains **genuine work**:
loads and stores through softmmu, the 17.5 M MMIO slow-path exits, TB-cache and i-cache pressure
across 23 679 live blocks instead of a handful, and IDT delivery. **This instrument cannot split
it further**, and it would be wrong to book it as headroom. It is reported as one bucket for that
reason.

**Small blocks alone do not explain the gap either — a second premise falls.** At production's
3.172 instructions per block the *unhooked* engine still reaches **4.42 G insn/s** on
register-only code (measured directly: `BLK3 direct B0` = 4237 M insn/s), which is *faster* than
the 2 991 M MIXED figure at 34 insn/block. Production's 497 M is an order of magnitude below even
the small-block bare floor. So the gap is dominated by what the real workload *does* — memory and
device traffic — not by how its control flow is shaped and not by the hook.

Why the one-entry inline cache does not help: at 3.172 instructions per block, **consecutive
blocks are almost never the same block**, so `unicorn_floor.cpp:586-588` misses on essentially
every entry and every entry pays the full `icount_hash` plus probe. The benchmark reproduces that
condition exactly (64 distinct blocks in a cycle). Fixing it is real but worth **0.995 s = 1.6 %
of the 63.9 s run**.

**Disproved and recorded, two premises:** (1) *"production TCG is 6× slower than bare Unicorn
because of hook overhead"* — the hook is 32.7 % of `tcg_exec`, i.e. 7.5 % of the run. (2) *"…
because the guest's blocks are small"* — at 3.172 insn/block the unhooked engine still does
4.42 G insn/s. Neither premise survives; the cost is in the real workload's memory and device
behaviour, which is exactly the part a faster CPU tier does not remove.

## 3. WHPX: measured, and it loses badly (`beads-xbox.1.16.2`)

### 3.1 Availability — the gate passes

`Get-WindowsOptionalFeature` **requires elevation and could not be run**. The stronger check was
run instead and it passes: `WHvGetCapability(WHvCapabilityCodeHypervisorPresent)` → `hr=0`,
`present=1`; `Features=0x2FF`; `ProcessorVendor=0` (**AMD**, so SVM/NPT); `ExtendedVmExits=0x7FFF`;
and `WHvCreatePartition` → `WHvSetPartitionProperty(ProcessorCount=1)` → `WHvSetupPartition` →
`WHvCreateVirtualProcessor` all return `S_OK`. No installation was needed: mingw64 already ships
`winhvplatform.h` / `winhvemulation.h` and `libwinhvplatform.a` / `libwinhvemulation.a`, and
`C:\Windows\System32\WinHvPlatform.dll` is 10.0.26100.8972.

### 3.2 The instrument

`tools/bench/whpx_bench.cpp` (+ `build_whpx.ps1`): one partition, one vCPU, 64 MB guest RAM at
GPA 0, flat 32-bit protected mode with a **real GDT and IDT in guest RAM**, an unmapped MMIO hole
at `0x04000000`, I/O port `0x8000`. It runs the *same three workloads* as
`unicorn_hook_bench.cpp` so the throughput numbers are directly comparable, plus five exit arms
each measured against a RAM-store control so the loop overhead subtracts out. 5 interleaved reps,
200 M instructions per throughput cell, 200 000 events per exit cell, **0 failures**.

Guards that make the numbers trustworthy: every arm asserts its **exact exit count**, the
injection arm asserts the guest actually **took** the interrupts (its own `EDI` counter), and a
watchdog thread cancels a runaway vCPU so a mis-loaded image fails loudly instead of hanging.
The exit-count assertion earned its keep immediately — it caught a wrong RIP advance
(`mov [disp32], eax` is **6** bytes, not 5) that had silently reported 72 exits where 200 000 were
expected.

### 3.3 The numbers

Guest throughput (native execution — note that under WHPX no instrumentation of it is possible):

| workload | WHPX median / min | TCG (`beads-xbox.1.14.2` hookless) | ratio |
|---|---:|---:|---:|
| TIGHT | 10 537 / 10 667 M insn/s | 10 552 M | 1.0× |
| MIXED | **21 768 / 21 900 M** | 2 991 M | **7.3×** |
| IDLE | 2 582 / 2 614 M | 702 M | 3.7× |

Exit round-trip cost, ns per event, median / min, marginal over the RAM control:

| arm | median | min | marginal |
|---|---:|---:|---:|
| MMIO exit, **bare** (host advances RIP only) | 3199.1 | 3172.5 | 3198.8 |
| MMIO exit, via `WHvEmulatorTryMmioEmulation` | 3693.0 | 3671.0 | 3692.7 |
| I/O port exit | 2869.9 | 2859.2 | 2869.6 |
| HLT exit | 2805.8 | 2786.6 | 2805.5 |
| HLT + interrupt injection | 4936.7 | 4913.9 | 4936.4 |
| → **interrupt injection alone** | | | **2130.9** |

200 000 of 200 000 interrupts verified delivered. Spreads 0.4–1.2 %.

### 3.4 The floor computation

Applying the measured exit costs to the canonical workload's measured exit census:

| term | count | × cost | seconds |
|---|---:|---:|---:|
| MMIO exits (bare floor) | 17 546 710 | 3199 ns | **56.14** |
| MMIO exits (stock emulator) | 17 546 710 | 3693 ns | *64.80* |
| I/O port exits | 41 951 | 2870 ns | 0.12 |
| interrupt injections | 19 577 | 2131 ns | 0.04 |
| guest instructions at native speed | 7 301 344 897 | MIXED → IDLE rate | 0.34 – 2.83 |
| **WHPX floor (bare, needs own decoder)** | | | **56.6 – 59.1 s** |
| **WHPX floor (stock emulator)** | | | **65.3 – 67.8 s** |

Against what it would replace — `tcg_exec` 14.700 s + `mmio_dispatch` 0.728 s = **15.43 s** —
WHPX is **3.7× to 4.4× slower**. Worse, 56 s of MMIO exits alone is almost the *entire* current
63.9 s emulation-thread wall.

**Guest instructions being free does not matter when the workload takes one MMIO exit every 416
instructions and each exit costs ~16 600 host cycles at 5.2 GHz.**

### 3.5 What would have to change, and why it is not available

To merely **break even** with 15.43 s, MMIO would have to fall from 17.55 M to **4.82 M** exits
(3.6× reduction); to be worth the rewrite, to ~2.4 M (7.3×). The census says that is not on the
table:

* **99.74 % of MMIO is NV2A register traffic** (only 45 672 of 17 546 710 reach any other
  device). A physical NV2A also sees these as MMIO; they carry side effects and cannot be backed
  by plain RAM.
* **The high-volume pushbuffer path is already not MMIO.** `nv2a_dispatch` fires 82 712 961 times
  and `pfifo-queue-pushes` is 83 289 212, against 17 546 710 MMIO accesses — most method dispatch
  already comes from DMA out of guest RAM, exactly the optimization one would propose. It has been
  done; what remains is irreducible register traffic.

### 3.6 Two findings that outlive this result

Both were learned the expensive way and are recorded so no future session repeats them.

1. **On AMD/SVM, WHPX gives you no instruction length on an MMIO exit.** Measured on this host:
   `VpContext.InstructionLength = 0`, and `MemoryAccess.InstructionByteCount = 16` — where the 16
   is *the size of the `InstructionBytes` buffer that was filled*, **not** a decoded length. Using
   it as a length walks RIP into the middle of the next instruction. So advancing past a faulting
   MMIO instruction **requires your own x86 decoder**; the 3199 ns bare figure is only reachable
   by a backend that carries one, and the honest off-the-shelf number is 3693 ns.
2. **Setting the hidden segment state is enough to enter protected mode but not enough to take an
   interrupt.** Delivery through a gate makes the CPU *reload CS from the descriptor table*; with
   no GDT that raises `#GP`, which needs the GDT to deliver, which triple-faults. The symptom is
   exit reason `0x4` `UnrecoverableException`. A real GDT must exist in guest RAM.

**Verdict: option B rejected on measurement. DO NOT RETRY WITHOUT** a workload whose MMIO exit
count is under ~5 M per 12 B instructions, or a WHPX/Hyper-V release whose exit round trip is
under ~800 ns.

## 4. Custom JIT: scoped, not started (`beads-xbox.1.16.3`)

Sized from ring and census data rather than from the manual:

| quantity | value |
|---|---:|
| distinct TB identities executed | **23 679** (`tb-icount-misses=0`, so the table saw every block that ran) — still growing slowly at the end of the run, so a **lower bound** |
| instructions per block | 3.172 |
| code concentration | `0x80018000` 2.833 B + `0x80056000` 1.901 B = **64.8 % of non-idle instructions in two 4 KB pages** |
| guest threads observed | 16; three dominate (idle 4.735 B, `0xd00086b8` 3.799 B, `0xd0008a98` 3.170 B) |
| device surface still to cross | 17.5 M MMIO, 41 951 I/O, 19 577 injections, 16 342 IRQ0, 11 878 RDTSC |
| paging | on (the `CpuFloor` carries `translate()` for walking guest page tables) |

Caveat: the boundary-page table is top-N with `pages_dropped = 149 586`, so it is the hot head,
not an exhaustive histogram.

**Not measured this session, and said plainly:** the x87/MMX/SSE instruction histogram. No opcode
census exists in the tree, and the only cheap route — the per-instruction hook — reports `pc` and
`size`, not opcodes. Doing it properly means dumping the 23 679 `(pc,size)` identities plus a
guest RAM image and disassembling exactly those byte ranges. **It is not on the critical path**,
because option C is excluded on ceiling grounds before its correctness surface matters: a custom
JIT still pays the 8.25 s bucket (it must replace softmmu, still take the MMIO exits, still
deliver interrupts), so even a *perfect* one saves at most 14.7 s of a 63.9 s run.

**Effort class: multi-quarter** — 23 679+ block identities over an unbounded ISA subset, a full
protected-mode paging/segmentation/IDT surface, self-modifying and decrypted-page invalidation
(already in `beads-xbox.1.4`'s acceptance criteria), and no oracle cheaper than the current
engine. **Design only.**

## 5. The decision, with the rule applied

The rule: *pick the option with the best measured ceiling per unit of correctness risk*, with the
stated preconditions.

* **(A) "If A can plausibly bring `tcg_exec` under ~5 s, start it now."** → **Precondition fails.**
  Measured: hook removal in full lands at ≈ 9.90 s; the best single patch is worth 0.995 s. Not
  started.
* **(B) "If B's floor is clearly lower and MMIO volume is tractable, start the WHPX skeleton."** →
  **Both clauses fail.** The floor is 3.7×–4.4× *higher*, and MMIO volume is irreducible at
  99.74 % NV2A register traffic. Not started.
* **(C) → "write the design doc and the bead breakdown only."** → **This document, plus
  `beads-xbox.1.16` and its three children.**

**What was started: nothing in the execution tier, deliberately, and that is the result.** The
task's own branching rule lands on (C) once (A) and (B) fail their preconditions, and (C) is
explicitly design-only. Starting a rewrite against these numbers would be the shortcut the
completeness rule forbids — it would buy at most 23 % of the run for a multi-quarter correctness
project, while the 47 s NV2A path sits untouched.

**First measurement of what was started:** the two benchmarks are themselves the deliverable, and
both are reproducible instruments rather than one-shot probes —
`tools/bench/block_size_bench.cpp` and `tools/bench/whpx_bench.cpp`, with logs
`tools/bench/run_block_size.log` and `tools/bench/run_whpx.log`.

### 5.1 If the CPU tier is revisited anyway, this is the ranked menu

Measured ceilings, largest first, all against `tcg_exec` = 14.700 s and a 63.909 s run:

| lever | ceiling on `tcg_exec` | ceiling on the whole run | risk |
|---|---:|---:|---|
| Attack the 8.25 s bucket (direct-mapped RAM instead of softmmu for the flat 64 MB; cheaper MMIO slow path). **Its ceiling is far below 8.25 s** — most of that bucket is genuine work, and this instrument cannot say how much | ≪ 8.25 s, unquantified | unquantified | high — touches the vendored memory system |
| Remove the block hook entirely (clock + deadline emitted into the TB) | 4.80 s | 7.5 % | medium — Unicorn/QEMU patch |
| ├ fold the icount into the TB (drop the side table) | **0.995 s** | 1.6 % | **low** — the cheapest real win |
| └ trim the hook body (ring / `stop_reason` / TSC publish) | ≤ 2.06 s | ≤ 3.2 % | low |
| Increase block size above 3.172 insn/block | not available — it is set by the guest's own control flow | — | — |

Nothing on this menu clears the §2 rule 7 bar of 5 % on the whole-run gate metric by itself, and
the top entry is the riskiest. That ranking is the reason the recommendation is to leave the CPU
tier alone.

## 6. Where the work actually belongs

The exclusive partition of the same 63.909 s run, largest buckets:

| bucket | seconds | share |
|---|---:|---:|
| `nv2a_raster_witnessed` | 8.896 | 13.9 % |
| `nv2a_vertex` | 8.551 | 13.4 % |
| `nv2a_raster_generic_sw` | 7.320 | 11.5 % |
| `nv2a_gl` | 5.925 | 9.3 % |
| `nv2a_gl_resolve` | 5.633 | 8.8 % |
| `nv2a_dispatch` | 4.792 | 7.5 % |
| `nv2a_pfifo` | 2.820 | 4.4 % |
| `nv2a_texture` | 1.781 | 2.8 % |
| `nv2a_clear` | 1.516 | 2.4 % |
| **NV2A/device subtotal** | **47.23** | **73.9 %** |
| `tcg_exec` | 14.700 | 23.0 % |

`docs/PERF-METHODOLOGY.md` §5 item 6 already ranks `nv2a_raster_witnessed` as the #1 lever. This
session's contribution is to close the CPU-tier question with numbers rather than leave it as a
standing "maybe Unicorn is the problem": **it is not, and hardware virtualization would be four
times worse.**

## 7. Reproducing this

```powershell
# Windows PowerShell 5.1; `pwsh` is not installed on this host. Both scripts pin the same
# compiler and flags build-release uses and print `exit=0` on success.
.\tools\bench\build_block_size.ps1
.\tools\bench\block_size_bench.exe --reps=5 --insns=200000000

.\tools\bench\build_whpx.ps1
.\tools\bench\whpx_bench.exe --reps=5 --insns=200000000 --exits=200000
```

Both print their own consistency checks first and exit non-zero on any failure. `whpx_bench`
prints `WHPX NOT AVAILABLE` and exits 2 if the capability gate does not pass, naming the
`Enable-WindowsOptionalFeature` command that would fix it.

Note that `.gitignore:56` ignores `*.log`, so `tools/bench/run_block_size.log` and
`tools/bench/run_whpx.log` are local artefacts of this session; `tools/bench/run_200M.log` is in
the tree only because a previous session force-added it. Every number quoted in this document
appears verbatim in those two logs, and both end in `failures=0`.

### 7.1 What was NOT done, so nobody assumes otherwise

* **No commits.** Branch `perf/measurement-first-2026-09`, HEAD unchanged at `43f9b1f`. The five
  new files (`docs/DESIGN-CPU-TIER.md`, `tools/bench/block_size_bench.cpp`,
  `tools/bench/whpx_bench.cpp`, and the two build scripts) are untracked additions.
* **No change to `xlle` itself.** No emulator source was edited, so no replay, byte-lock or gate
  run was needed or performed — and none of the numbers here depend on one, because every claim
  is either read from an existing instrumented run (`build/p4-perf-B.stdout.log`) or produced by
  a standalone benchmark.
* **The x87/MMX/SSE histogram was not measured** (§4).
* **`Get-WindowsOptionalFeature` was not run** — it requires elevation (§3.1). The capability +
  partition + vCPU probe was run instead and is the stronger check.
* **The ablation originally scoped as "build a scratch xlle with the hook body trimmed / removed"
  was deliberately replaced** by the `BP`/`BX`/`B2`/`B0` arms of the standalone benchmark. That
  measures the same quantities at matched block size, without a broken instruction clock, without
  a 60 s replay per data point, and without touching the emulator — and it yields a *curve* over
  block size rather than a single pair of points.
