# Combined GPU vertex and triangle shadow

Owner: `beads-xbox.1.15.11`. This follows the complete-program shadow in
`PERF-VSH-GPU-PROGRAM-2026-09-06.md`. The production renderer remains
CPU-authoritative. This checkpoint validates batch ownership and a second GPU
dispatch; it does not establish a performance improvement or 60 FPS.

## Ownership and capture

`XLLE_VSH_CAPTURE=ON` enables
`--debug-vsh-batch-capture path interval count`. The normal build refuses the
flag. The count bounds **selected** batches, including batches subsequently
excluded by raster state. Selection samples eligible producer batches uniformly
by ordinal, never by program fingerprint. At most 64 selected batches each have
16,384 submitted references/actual executions and 272 decoded operations.

Each selected batch owns decoded operations and constants captured before
execution, actual input registers, CPU raw consumed outputs, and the actual
postprocessed position. Submitted indices reference actual executions. The
existing 1,024-entry direct cache's hash, hits, and collisions are preserved;
an indexed batch below the cache threshold still records original source
indices. Nonindexed batches use sequential source IDs. Capture sequence has a
dedicated counter because the existing draw counter is telemetry-dependent.

The payload moves into `DrawJob`. Capture does not change renderer admission or
set its generic diagnostic-callback flag. Resident-primary triangle setup runs
inline and records its actual output order, all-behind/degenerate/cull drops,
and whole-batch declines. Constant-color draws using the same setup are included.
Before recording, every mapped execution is compared with the actual packet's
position, diffuse, specular, and texture fields. Selected batches that never
reach this resident-primary state are not written.

The bounded binary codec checks lengths, completion, sequence ordering,
chronological execution IDs, reconstructed cache mapping, and expanded-stream
relationships. Strip/fan expansion can contain almost three times the submitted
reference count. Writer failure fails the final capture result. Private captures
remain ignored and outside the publication tree; no guest program is embedded
in source or used as a dispatch identity.

## GPU pipeline and independent checks

The standalone probe's `--batch-capture path` mode reexecutes every captured
input through the vendored CPU VSH implementation. Its independent numeric
trace checks intermediate declines; its triangle oracle checks the recorded
CPU expansion. These checks are separate from the GPU result comparison.

One GPU invocation shades one actual execution. A storage barrier then exposes
the output/status buffers directly to the triangle compute dispatch, without
CPU readback between stages. List, strip, and fan candidates occupy fixed slots
in original order, preserving winding changes and culling. The diagnostic
reads back after both dispatches, compacts in order, and compares execution IDs
against the CPU's recorded expansion. Guard slots detect stray output/status
writes. Geometry or VSH decline discards the entire batch's candidate stream.
No raster destination is written.

The fixed-coordinate path uses halfway-away-from-zero rounding and the CPU's
1,000,000 bound. Integer-valued doubles represent its bounded area products
and difference exactly. The large-target path uses explicit ordered double
arithmetic. Agreement is device/input evidence: the production compiler's
contraction behavior and extreme coordinate cancellation still need admission
analysis before this shader can become authoritative.

The diagnostic upload ABI still duplicates operations/constants per execution.
A production implementation should share them per batch, retain the CPU guest
fetch/fault ordering, and resolve decline before destination writes. Ordered
GPU compaction, persistent-buffer reuse, fallback synchronization, and renderer
integration remain work. Only a complete candidate can run the same-session
interleaved 3+3 performance gate and interactive frame/latency checks.

## Validation

Measured device: RTX 3080 Ti, GL 4.3 / NVIDIA 616.86. Capture executable
SHA-256: `0b5990dce0cfc33ac576973a62dc3c240f59714e3b8a32bab1d7776b0da85238`.
Final standalone probe SHA-256:
`1cc032150b76795144683a3f166cd241390863f7daaaaa77b82784a46d26ffe1`.
Both captures use interval 8,192 and selection budget 64; neither exhausts it.

| Extent | Eligible producer batches | Selected | Written primary batches | Actual executions | Submitted refs | Expanded vertices |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 B | 309,145 | 38 | 2 | 5,148 | 28,368 | 13,566 |
| 16 B | 343,257 | 42 | 6 | 8,688 | 42,084 | 21,522 |

Every written batch uses the dominant 23-operation program, indexed triangle
lists, the large-target area path, and back-face culling. Strip/fan and other
culling/decline cases are covered by synthetic tests. Every accepted
raw/postposition word and expanded index matches, with no declines or guard
failures. These are six sampled batches, not exhaustive trajectory coverage.
The 12 B file is an exact prefix of the 16 B file excluding its completion
footer; do not sum their counts as independent samples. All captured XY values
are finite and within +/-62,500. That suggests a conservative exact-area
admission bound to investigate; it is not evidence about uncaptured inputs.

The 12 B capture matches all 28 image checkpoints and checked guest state/
timeline against the published normal build. The 16 B capture matches all 40
image checkpoints and the same state/timeline fields against the preceding
validated 16 B program capture at the same 500 M cadence. These are fidelity
comparisons only, with no timing inference.

The final synthetic combined test has 19 batches, 260 actual executions,
263 references, 237 emitted vertices, five expected whole-batch declines,
and zero mismatches. It includes signed-zero W classification, cache
collision/reexecution/hits, and 198 references producing 66 triangles across
multiple workgroups. Complete-program regression remains 10,039 records with
two numeric declines, one unsupported-program decline, and zero mismatches.
The explicit-rounding dot regression remains 302,635 exact accepted records
plus five declines. Private logs, captures, and fidelity checks are under
`build/vsh-batch-shadow/`.

After restoring both CMake options to OFF, all 41 CTest tests pass. The normal
binary refuses the batch-capture flag before creating a file. Normal executable
SHA-256: `8979e7444750b467413442208408a18d556be403ff2dfb11c24d82bf5c86fbca`.
Its separate canonical 12 B replay matches all 28 image checkpoints and checked
guest state/timeline against the preceding published normal build. The
publication-tree scan covers 187 files with zero Gitleaks findings.

## Reproduce

Use the README's native Windows toolchain setup. Configure
`XLLE_VSH_CAPTURE=ON` and `XLLE_VSH_GPU_NUMERIC_PROBE=ON`, then build `xlle`,
`xlle_vsh_dot_probe`, and `test_vsh_triangle`.

```powershell
.\build-release\test_vsh_triangle.exe
.\build-release\xlle_vsh_dot_probe.exe --batch-self-test
.\tools\run_stockdash_replay.ps1 -Label batch-local -ExtraXlleArgs @('--debug-vsh-batch-capture','build/batch-local.bin','8192','64')
.\build-release\xlle_vsh_dot_probe.exe --batch-capture build/batch-local.bin
```

Probe exit 0 means exact accepted outputs with no declines; 4 means accepted
outputs match with recorded declines; 3 means a mismatch; 2 means setup,
CPU/format validation, or guard failure. Preserve counts alongside the exit
code. Restore both CMake options to OFF for a normal build.
