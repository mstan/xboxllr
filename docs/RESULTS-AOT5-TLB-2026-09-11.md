# AOT-5: remove host time from software TLB sizing

The observer checkpoint found different native/fallback work counts despite
identical dashboard images. A controlled test now proves a mechanism: Unicorn's
software translation cache shrank according to host time, changing whether a
cached code page qualified for native entry. This concerns the emulator's host
cache, not exact Xbox hardware TLB capacity or timing.

## Controlled proof and fix

The fixture compiles the actual pinned TLB implementation, with existing fault
restoration, into its test executable. It injects timestamps into the real resize
function; it does not sleep or imitate the algorithm. Ten code pages collide in
a 64-entry table, overflowing its eight victim entries, while a 256-entry table
retains them. With identical mapping/access/flush operations:

| Legacy elapsed time | Entries | First code page eligible for native entry |
| --- | ---: | --- |
| 50 ms | 256 | Yes |
| 150 ms | 64 | No |

Both execute the same authored guest program to the same result. No Xbox inputs
are used. This demonstrates the mechanism, not the exact cause of every
historical counter difference or a performance gain.

The default policy now ages a separate window for each MMU mode by **100 resize
opportunities**. These occur when the entire mode is flushed, including selected
mode flushes and large-page invalidations that require a whole-mode flush.
Ordinary page invalidations do not age it. Growth/shrink occupancy thresholds,
size limits, allocation fallback, access checks, permission handling, victim
lookup and invalidation behavior are unchanged. The counter reuses a private
field and resets each window; no per-instruction callback or engine ABI is added.
This is a deterministic heuristic, not a demonstrated optimal window for every
workload.

The legacy policy remains available with `-DXLLE_HOST_TIMED_TLB=ON` for diagnostic
builds. Normal builds use `OFF`. Third-party source is derived in the build
directory from the pinned revision with its license retained. Generated Xbox
code and private executables remain outside Git.

## Validation

The final public build passes **54/54 CTest tests**. The TLB fixture checks both
policies, the 99/100 opportunity boundary, adaptive growth, 250 opportunities
with bounded counters, independent mode windows, and large-page versus ordinary
invalidation. Review caught an include path initially selecting upstream code
for both fixtures. Explicit generated paths and compile-time policy expectations
now prevent that false pass; both intended variants were rerun successfully.

Both **O2 and O0** private native builds pass the full 12 B reference dashboard
golden: 29 applicable assertions, with two executable/Git identity entries kept
as informational skips. All **28 saved images** match each other and the preceding
reference byte for byte. All **77 native records and 25 full CPU-census records**
match between O0 and O2, including every recorded phase and digest. The slower
unoptimized generated code no longer changes this route's native work partition.
This is two implementations of the same native operations, not an exhaustive
determinism guarantee for all workloads.

The ordinary build with no private native input also passes all 29 applicable
golden assertions, with the same device/DMA digests and final image. Its 28
corresponding captures match the native build byte for byte. Capture instruction
labels differ at nine periodic checkpoints: eight by 1--3 instructions and the
5.5 B checkpoint by 733,330 instructions. The exact-filename comparison therefore
reports nine missing pairs. A separate one-to-one comparison by scheduled 0.5 B
window and named input checkpoint verifies all 28 hashes; it does not claim the
captures occurred at identical instruction counts. Both comparison artifacts
are retained.

| Full 12 B native reference counter, equal at O0/O2 | Value |
| --- | ---: |
| Native instructions | 4,724,616,624 |
| Fallback instructions | 2,576,740,065 |
| Idle skipped instructions | 4,698,643,312 |
| Native attempts and invocations | 2,359,944 each |
| Code-TLB declines | 0 |
| Slice digest | `80f61129e7edb3ff` |
| Device digest | `54582fed08496cb7` |
| DMA digest | `85503c5ea8906c03` |

The prior reports had 14,101,059 or 30,874,359 code-TLB declines. The new policy
executes more work natively without weakening entry certification. The changed
slice digest is recorded as a new implementation trace; rendering/device golden
identities did not need changing. Ten recorded rendering families were unexercised,
so golden success does not mean exhaustive rendering coverage.

Private executable SHA-256 identities:

| Build | SHA-256 |
| --- | --- |
| O2 | `573e537c7798e5127948451bf3e1abd38f4d6235cc1a7cc6086dffb22d30bced` |
| O0 | `04edd980b4b3dbed814626ae9aad4d9d20d181d7d9bdf9fc1d6755a9373dad2b` |

Local artifacts: `build/aot5-flush-clock-o[02]-ref.*`,
`build/aot5-flush-clock-o[02]-images.txt`, `build/aot5-tlb-o0-o2-work-check.json`,
`build/aot5-flush-clock-public-ref.*`, `build/aot5-flush-clock-public-images.txt`,
`build/aot5-flush-clock-public-checkpoint-hashes.json`,
and `build/aot5-tlb-final-ctest.log`. Private binaries are preserved under
`../private-audit-20260911/aot5-flush-clock-o[02]/`. The ordinary build is restored
with private input disabled.

## What this does not establish

These are correctness and work-count observations. Foreign builds contaminated
the host; wall times cannot establish a speedup. Retain this as a reproducibility
fix, with timing cost still unqualified, not a performance-gate override presented
as a win. Controlled observer cost, clean attribution and native-versus-native
interleaved timing remain open. The next compiler candidate is direct internal
edges with every existing guard and exact observable exit preserved.
