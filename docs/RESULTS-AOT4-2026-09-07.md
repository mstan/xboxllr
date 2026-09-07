# AOT-4 implementation and validation

Status: AOT-4 capability qualification is complete. The native-versus-Unicorn
performance gate is REJECT; optimization continues in AOT-5. Integration tracking:
`beads-xbox.1.25.4`. Requirements:
[AOT-4 goal](GOAL-AOT4-BURNDOWN.md). Reproduction:
[private AOT workflow](AOT-PRIVATE-WORKFLOW.md).

## Implemented capability

Selected real dashboard regions execute through generated portable C in the Xbox
machine, with Unicorn continuation and the existing instruction clock. The normal
build still uses Unicorn. A private generated build explicitly enables regions
with `--aot-native`; it contains asset-derived code and must not be distributed.

The upstream qualification uses authored synthetic code, a pinned revision and
independent/Unicorn expectations. Direct upstream backend adoption was rejected
after reproduced cross-function flags, unsupported-instruction and instruction
budget failures. The memory/event interface assessment is source evidence, not
an injected-fault experiment. See [the qualification audit](AUDIT-XBOXRECOMP-AOT4-2026-09-07.md).
Root reproduced the helper's results and fixed a source-identity bypass: a matching
Git HEAD can no longer accept modified source files. The selected source hash,
including the imported package initializer, is
`dcf56d90affb6a797ca1dc85c99a763a1c4ee2dd0bc35c295c1f12d2a097a929`.
Qualification used Capstone 5.0.9 as an external dependency, not a production decoder.

## Region selection and measured execution

The optional block-census build counted actual admitted fallback instructions,
subtracting unretired fault prefixes. It captured first-executed code for all
23,666 observed PC/size pairs, with zero overflow or missing captures. All census
work is compiled out of ordinary builds. The original boundary-page table was
insufficient for selecting blocks because it attributed whole slices to boundary PCs.

The baseline instruction partition was 7,301,356,681 fallback instructions plus
4,698,643,320 idle-skipped instructions: 12,000,000,001 total. Selected regions:

| Region | Shape | Baseline instructions | Native instructions |
| --- | --- | ---: | ---: |
| `0x800184f0`, 5 bytes | Register countdown loop | 2,834,757,912 | 2,834,757,912 |
| `0x80056cb0`, 11 bytes | Stack-memory countdown loop | 1,889,858,712 | 1,833,454,476 |

Both regions are already minimal, single-page loop bodies. These are mainly
startup/delay instructions. They are not evidence of steady rendering coverage.
The baseline pair accounts for 39.4% of all guest-clock instructions and 64.7% of
executed fallback instructions. Actual native retirement is 4,668,212,388 (38.9%
of the full clock), with 2,633,144,301 fallback and 4,698,643,312 idle-skipped
instructions. Those counts sum exactly to 12,000,000,001.

The native run recorded 2,310,748 dispatch invocations, 15,257,853 fallback-to-AOT
handoffs, and 458,363,619 native reads and writes each. The stack region produced
14,101,059 code-TLB declines, concentrated early in the run. No context declines,
memory declines, unsupported exits or changed-code exits occurred in this route.
`invocations` means dispatches past preflight; if replacement hooks are installed,
accepted replacements count too. No replacement hooks were installed here.

## Correctness findings

The first real probe retired zero native instructions and repeatedly bounced back
to Unicorn. Diagnosis showed a cached kernel CS limit of `0xbfffffff`, while the
AOT-3 adapter required `0xffffffff`. DS/SS were full-range and all bases were zero.

ABI2 now carries the cached CS limit. Entry certifies the entire region's
instruction bytes. A taken near branch checks its target before changing EIP or
retiring; a disallowed target returns to Unicorn for fault handling. DS/SS, hidden
CPU state, MMU permissions and unsupported modes retain their existing checks.
This fixes a real context mismatch rather than suppressing the eligibility guard.

The decoder adds bounded MOV32 ModRM/SIB addressing and sign-extended `83 /5` SUB.
Independent and Unicorn tests cover effective addresses, flags, source/destination
overlap, access denial, events and budgets. Additional machine tests exercise
limited CS, warm native writes followed by revoked page permission, and unaligned
little-endian stores. Root strengthened the permission test so it must reach native
read/increment execution after CR3 reload, then decline the write; an early code-TLB
rejection alone cannot pass it.

The current memory candidate writes once to certified shared RAM and invalidates
every physical alias. It caches one exact four-byte read and write address only
within a native invocation, resetting both before every generated call. The
supported subset cannot refill/invalidate architectural TLBs, change segment/MMU
registers or run devices. Such instructions require fallback. Snapshot and
diagnostic-observer contexts remain excluded. Extending that subset requires
revisiting these cache assumptions.

The final full CTest suite passed 49/49, including the strengthened machine tests.
Private generated C at O2 and O0 completed the full reference dashboard route:

- All 29 applicable checks in the 31-check golden report pass. The executable and
  Git identities are informational differences, not matching-content claims.
- All 28 saved images match the previous reference byte for byte, and O0 matches
  O2 byte for byte. Native/fallback/idle retirement counts also match across O0/O2.
- Final device digest `54582fed08496cb7`, DMA digest `f5c98df2a12072bb`, 16,342 IRQ0
  events and 19,577 injected interrupts match the corrected AOT-3 baseline.
- XID endpoint-2 report counts match at every scheduled frame checkpoint.

CPU slice digests differ because native retirement permits precise boundaries.
Eight checkpoint filenames shift by one to three instructions. The 5.5 B capture
instead shifts by 733,330 instructions, about one guest millisecond: it falls on
the next host-service checkpoint. That snapshot contains one extra IRQ0 event,
two extra injected interrupts, 40 more cumulative device events and 961 more DMA
writes. The following checkpoint's cumulative device/DMA streams match again;
the next interval compensates the interrupt counts. Images remain identical.
Comparisons pair scheduled deadlines explicitly, not raw filename suffixes.

The earlier default-GPU probe had 24/28 byte-identical images. Four contained
isolated channel differences: 5, 1, 56 and 15 pixels, maximum channel deltas
1, 1, 1 and 2, with no connected component larger than one pixel. This is a
separate numerical-scatter observation, not a claim of GPU byte identity.

The final native-versus-floor and default-disabled sets provide stronger
same-configuration evidence: all 28 images match byte for byte in each of six
A/B pairs. Across all 18 timing legs, every endpoint-2 checkpoint count and the
final device/DMA/IRQ counters match. All nine native timing legs also reproduce
the complete cumulative AOT counter record, including the exact retirement
partition and decline counts. Evidence: `build/aot4-final-evidence.json`.

## Performance evidence

The memory-path gate used one same-session interleaved A1/B1/A2/B2/A3/B3 set,
with three uninstrumented runs per arm and no recorded host contamination. Both
arms executed both private native regions. The baseline precedes direct stores
and invocation-local pointer caching; the candidate includes those two related
changes. This measures the combined memory-path change, not each part separately.

| Total 12 B wall | Initial native path | Memory candidate |
| --- | ---: | ---: |
| Run 1 | 91.161 s | 69.616 s |
| Run 2 | 89.808 s | 68.472 s |
| Run 3 | 91.792 s | 70.065 s |
| Median | 91.161 s | 69.616 s |
| Minimum | 89.808 s | 68.472 s |

**PASS:** median wall time improves 23.63%, minimum improves 23.76%, both above
the 5% gate. All six final images and stderr checks pass. The final-four-window
indicator changes by -0.33% median / +0.06% minimum improvement, which does not
establish a steady dashboard gain. Source reports:
`build/aot4-memory-ab-leg0[0-5]-*.report.json`; aggregate:
`build/aot4-memory-ab-abba-gate.json`.

The native-versus-floor comparison uses the saved, corrected AOT-3 binary. Its
earlier 2.88% median / 3.54% minimum cost is not silently removed. This separate
quiet, interleaved three-per-arm set measures the complete native implementation
against the existing Unicorn route, with default GPU settings in both arms:

| Total 12 B wall | Corrected Unicorn floor | Final native path |
| --- | ---: | ---: |
| Run 1 | 49.898 s | 69.705 s |
| Run 2 | 50.227 s | 73.275 s |
| Run 3 | 52.797 s | 70.181 s |
| Median | 50.227 s | 70.181 s |
| Minimum | 49.898 s | 69.705 s |

**REJECT:** native execution increases total wall time by 39.73% at the median
and 39.70% at the minimum. All six final images and stderr checks pass, with no
recorded contamination. The final-four-window indicator regresses 3.28% median /
1.64% minimum. Native retirement is a capability result, not a speedup. Native
execution remains explicitly opt-in. Source:
`build/aot4-floor-native-ab-abba-gate.json`. Single probes remain
diagnostic evidence only.

The separate default-disabled set compares the same corrected baseline against
the private candidate with `--no-aot-native`:

| Total 12 B wall | Corrected Unicorn floor | Candidate, AOT disabled |
| --- | ---: | ---: |
| Run 1 | 53.180 s | 57.019 s |
| Run 2 | 52.424 s | 51.575 s |
| Run 3 | 58.137 s | 50.022 s |
| Median | 53.180 s | 51.575 s |
| Minimum | 52.424 s | 50.022 s |

No total-time regression was observed: median and minimum improve 3.02% and
4.58%. The formal speedup gate is **REJECT**, because both must clear 5%.
This supports retaining the disabled-by-default capability without claiming a
speedup or zero overhead. Variability is material (5.69% / 6.95% coefficient of
variation); the final-four indicator regresses 4.37% median / 0.20% minimum.
All six legs pass final-image and stderr checks, with no recorded contamination.
Source: `build/aot4-default-overhead-ab-abba-gate.json`. This measures the
private binary with registration disabled, not a separate public-build timing.

Measured binary identities (SHA-256; byte sizes include local PE metadata):

| Binary | SHA-256 | Bytes |
| --- | --- | ---: |
| Corrected AOT-3 floor | `fc97c8acc7b12326b3a767e2d89aa92e0bbf7157e822cf7fc731ebf8d78257d7` | 3,657,710 |
| Initial native memory path | `984bbd003c61f357c97aa2c6c354387eaa1a27c27e566eb2c936c34dccfebff7` | 3,671,928 |
| Final native candidate | `7c7478d340881adfa50b3174932f83a3dc2da28c2d735bd67ce589581c2b0cc9` | 3,671,416 |

The final candidate is 13,706 bytes larger than the floor and 512 bytes smaller
than the initial native checkpoint. Rebuilding O2 after the O0 validation changed
the executable identity; gate reports record the exact restored O2 binary above.

Retention decision: retain the explicitly requested AOT capability as an
experimental, opt-in architectural milestone. This is a labelled override of
the performance-retention rule, not a timing PASS. The memory optimization
separately clears its performance gate. Do not enable these regions by default
or retry their performance claim without a measured reduction in generated-body
or handoff costs. The broader performance goal remains open.

The first instrumented reference run, before memory-path changes, measured:

| Exclusive bucket | Seconds | Scope |
| --- | ---: | --- |
| `aot_body` | 43.324 | Generated instructions, guards and memory callbacks |
| `aot_exec` | 0.576 | Eligibility, entry/exit state transfer and dispatch accounting |
| `tcg_exec` | 12.040 | Fallback self time |

This reference configuration uses serial software raster work for profile hashes;
its 195.772-second total is not a shipping-default headline. The timing partition
does not isolate memcpy, alias invalidation, callback dispatch or code comparisons
inside the body. Their relative costs remain hypotheses until separately measured.

Attempts to refresh final-candidate attribution were excluded: the first refused
to start on a busy host, and `aot4-final-attribution-default-r2` and `-r3` recorded
foreign compilers. In `-r4`, root observed CMake/Ninja during execution even though
the harness's before/after census reported clean. That run is also contaminated
and supplies no timing conclusion. The table above remains the clean, initial
native-path attribution; do not describe it as the optimized candidate's cost.
AOT-5 must refresh that attribution and detect in-run foreign work, rather than
relying only on endpoint process snapshots. All attempted artifacts are retained.

### Reproduce the paired comparisons

Preserve each binary and its adjacent SDL library before rebuilding. The local
paths below refer to the measured copies; a fresh checkout must build and preserve
the corresponding versions first. The floor is commit `c843839`, including AOT-3
correctness fixes. The initial native copy is a development checkpoint before
the two memory changes, so its local saved executable is required to repeat that
historical subgate. It is not a separately published source revision.

```powershell
$Quiet = '^(xlle|g\+\+|cc1plus|ld|collect2|cmake|ninja)([-._0-9]|$)'
./tools/run_stockdash_replay.ps1 -Label memory-repeat `
  -BaselineExe ../private-audit-20260907/aot4-before-direct-store/xlle.exe `
  -Candidate build-release/xlle.exe -Repetitions 3 -RequireQuiescence `
  -ExtraXlleArgs @('--aot-native') -QuiescencePattern $Quiet
./tools/run_stockdash_replay.ps1 -Label floor-native-repeat `
  -BaselineExe ../private-audit-20260907/aot4-baseline/xlle.exe `
  -Candidate build-release/xlle.exe -Repetitions 3 -RequireQuiescence `
  -ExtraXlleArgsB @('--aot-native') -QuiescencePattern $Quiet
./tools/run_stockdash_replay.ps1 -Label default-overhead-repeat `
  -BaselineExe ../private-audit-20260907/aot4-baseline/xlle.exe `
  -Candidate build-release/xlle.exe -Repetitions 3 -RequireQuiescence `
  -ExtraXlleArgsB @('--no-aot-native') -QuiescencePattern $Quiet
```

The narrower process pattern excludes resident Python-based issue/tool servers.
No concurrent compiler, emulator or CPU-heavy analysis is allowed during these
sets. A process census is a contamination diagnostic, not proof that every source
of host noise is absent. The harness exits unsuccessfully on a timing REJECT;
read the saved gate and per-leg correctness records rather than rerunning until
a favourable number appears.

## Evidence locations

These are local, ignored artifacts, not files to publish:

- `build/aot4-census-code-12b.csv` and its `.blockpack`; source SHA-256
  `8fabfe6dedf13842448bf63199e836c00bf4aa54d1871d21d3d2a24e49101ccf`.
- `build/aot4-xboxrecomp-qual/root-review-final/qualification_summary.json`.
- `build/aot4-limit-native-12b.*`: first successful native default replay.
- `build/aot4-attribution-ref-12b.*`: initial exclusive-time attribution.
- `build/aot4-cache-ref-o2.*` and `build/aot4-cache-ref-o0.*`: full reference routes.
- `build/aot4-cache-ref-o2.aligned-comparison.json` and
  `build/aot4-cache-o0-o2.aligned-comparison.json`: explicit checkpoint pairing.
- `build/aot4-ctest-final.txt`: final 49/49 whole-suite validation including
  the strengthened permission test.

The pre-integration index/history check found zero findings across 1,356 unique
blobs. The separate Gitleaks scan found zero findings in 234 publishable files.
These checks detect known private formats and secrets; they are not a general
proof of legal provenance. Captures and generated private artifacts remain
ignored and publication-guarded. CI now includes the private-input and census
guards. The ordinary `build-release` was restored with an empty
`XLLE_AOT_PRIVATE_INPUT`, census OFF, a successful rebuild and passing smoke test.
The measured private executable is saved outside Git at
`../private-audit-20260907/aot4-final-native-o2/xlle.exe`.

## Acceptance audit

| Requirement | Evidence and scope |
| --- | --- |
| Pinned upstream qualification and reuse decision | Synthetic qualification fixture/tool; root reproduction; bounded backend rejection in the linked audit; no production source import |
| Representative measured regions | First-execution census with zero overflow/missing captures; the two hottest bounded loops; actual native retirement recorded separately from idle and fallback |
| Reproducible private build | Preparation/generation tests, source and manifest hashes, O0/O2 builds, documented capture-to-build workflow, publication guards |
| Faithful real-machine continuation | Machine differential tests for precise faults, permissions, aliases, events and state ownership; live dashboard execution; image/input/device comparisons above |
| Correctness and measured costs | Final 49/49 tests; full O0/O2 reference routes; nine matching native timing records; clean initial exclusive attribution; three full interleaved 3+3 sets with negative results retained |
| Default and enhancement boundaries | Native registration opt-in, ordinary build restored, existing explicit modification profiles and Unicorn fallback retained; no ARM or console claim |
| Reproduction, integration and next work | This record and the private workflow; integration commit/branch recorded in central Beads; ranked AOT-5 work below remains open |

This closes the representative-region capability, not predominant AOT coverage,
steady rendering coverage, performance parity with Unicorn or the 60 FPS target.

## Ranked next work

1. Attribute generated-body costs before choosing another optimization. Measure
   guards, callback/alias work and flag computation with bounded experiments.
   Full-region `memcmp` runs very often, but the regions are only 5 and 11 bytes;
   call frequency alone does not prove it dominates. Any generation/dirty-page
   replacement must catch same-invocation and host/DMA alias code changes.
2. Diagnose the actual early stack-loop code-TLB misses. A proposed unused-tail
   explanation was rejected: this region is already 11 bytes on one page. Check
   cached TCG execution after fetch-TLB eviction and quantify needless handoffs;
   do not bypass permissions or page-table side effects to inflate coverage.
3. Evaluate general, decoded loop/flag-liveness lowering under exact budgets and
   event exits. Use independent final-state and every-exit-boundary tests. Report
   delay/startup gains separately from steady interactive performance.
4. Capture residual fallback work in a defined post-boot interactive window.
   Rank real blocks by instruction and wall-time attribution, then qualify the
   smallest useful opcode families and their memory/fault contracts. Measure
   successful presents and latency alongside guest speed before claiming progress
   toward 60 FPS with headroom. ARM hardware setup remains deferred.
