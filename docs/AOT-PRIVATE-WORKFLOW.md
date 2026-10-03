# Private dashboard AOT workflow

Current work uses ABI 4 fixed MMX/x87 services and the qualified 27B menu route;
see [STATUS](../STATUS.md) and the
[x87 evidence](RESULTS-AOT-X87-2026-10-03.md). Full menu AOT and 60 FPS remain open.
The examples below retain the original capture/build workflow and startup
selections; they do not select the newer menu regions automatically.

Historical checkpoint: AOT-4's real dashboard execution and correctness checks are validated.
The measured regions execute 38.9% of a 12-billion-instruction replay through
generated C, mostly in startup/delay loops. All 28 images match the corrected
baseline, but native execution takes about 40% longer overall. Keep it opt-in
for development. Performance work continues under `beads-xbox.1.25.5`; this is
not a 60 FPS claim.
See [the validation record](RESULTS-AOT4-2026-09-07.md) for completed checks and
the current performance-gate status.

## What this builds

The input is code captured from your own Xbox boot/dashboard assets. The tool
translates selected regions to portable C, and your C compiler builds them into
your local emulator. Unicorn executes everything outside those regions, including
unsupported instructions and accesses that native execution cannot safely handle.

Complete the asset preparation and ordinary dashboard launch in [the README](../README.md)
first. These extra steps are for developers testing native execution. Keep the
capture, prepared JSON, generated C, libraries and executable private. The resulting
executable contains code derived from your assets, even though no original dump
file is needed to read those embedded bytes. Do not distribute that executable.

## 1. Capture executed blocks

Use PowerShell from the repository root. Use the same MinGW and CMake setup as
the README. Set `$CMake` to the absolute path of that CMake executable. For the
validated Windows toolchain:

```powershell
$CMake = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$env:Path = 'C:/msys64/mingw64/bin;' + $env:Path
& $CMake -S . -B build-cpu-census -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe `
  -DXLLE_CPU_BLOCK_CENSUS=ON
& $CMake --build build-cpu-census --target xlle -j 12
./tools/run_stockdash_replay.ps1 -Label aot-capture `
  -BaselineExe build-cpu-census/xlle.exe `
  -ExtraXlleArgs @('--cpu-block-census','build/aot-capture.csv',
                  '--hdd-security-file','..\machine\hdd-security.key')
py -3 tools/summarize_cpu_blocks.py build/aot-capture.csv --top 20
```

The replay harness expects the private asset paths documented in its command
output, including `../machine/xbox-hdd-stockdash.img`. It also checks the existing
`build-release` configuration. Keep that ordinary Release build configured first.
This stock-dashboard script is tied to the tested input route; another dashboard
version needs its own validated route and reference outputs.

The CSV counts actual admitted Unicorn block instructions, removes unretired
instructions on faults, and separates idle skips and native retirement. The
summary rejects overflow or an inconsistent instruction partition. The companion
`build/aot-capture.csv.blockpack` stores the first executed bytes for each PC/size.
Counts aggregate subsequent code versions at that address; they do not prove that
the first captured version remained resident for all those executions.

Do not select code from a final RAM snapshot alone. In the measured dashboard,
some boot-code pages later held framebuffer data. The census build captures code
when first executed and is for attribution only. Its extra work disqualifies its
wall time from headline performance comparisons.

### Separate menu intervals from startup

With a census build, input-script image checkpoints also write cumulative census
snapshots. Supply both `--cpu-block-census <path>` and
`--test-input-capture-prefix <prefix>` with a validated `--test-input-script`.
The log's `[cpu-census-phase]` rows associate the image label, guest clock and
controller endpoint-report count with each CSV path. Collection runs continuously
from reset; a checkpoint does not reset or arm the counters.

Subtract two snapshots from the **same run**:

```powershell
py -3 tools/summarize_cpu_blocks.py <later.csv> --since <earlier.csv> --top 20
```

Each new census has a host-only random run identity. Interval analysis rejects
missing/mismatched identities, decreasing counters, disappearing blocks, overflow
and totals that do not reconcile. Legacy files without the identity still support
cumulative summaries, but cannot be paired. Summaries written with `--json` are
marked private too. Inspect the corresponding images before naming an interval:
script labels alone do not establish which screen was reached. The same PC/size
version-aggregation limitation applies to interval counts and first-code packs.
See the [measured menu intervals](RESULTS-DASHBOARD-PHASES-2026-10-02.md).

## 2. Prepare selected regions

Inspect the measured blocks before choosing their addresses and lengths. This
example selects the two largest blocks from the tested stock-dashboard capture:

```powershell
$CaptureHash = (Get-FileHash -Algorithm SHA256 build/aot-capture.csv.blockpack).Hash
py -3 tools/prepare_aot_input.py build/aot-capture.csv.blockpack `
  --expected-sha256 $CaptureHash `
  --output build/dashboard.aot-private.json `
  --block 0x800184f0:5 --block 0x80056cb0:11
```

Those addresses are not universal. If your capture differs, investigate it instead
of changing the identity check to hide the difference. The preparation tool rejects
missing, overlapping or malformed selections. Each selected block begins at an
explicit entry point. The prepared file records the capture format and SHA-256;
the generated manifest also records region bytes, decoded boundaries and ABI.
The capture hash identifies the source file, not ownership or permission to share it.

The measured pair consists of a register countdown loop and a stack-memory
countdown loop. They account for 39.4% of baseline instructions, mostly during
startup/delays. That percentage is not steady dashboard rendering coverage.
Native execution still executes and counts those instructions; it does not skip
the delays or advance guest time by an invented amount.

## 3. Build and run locally

```powershell
$PrivateInput = (Resolve-Path build/dashboard.aot-private.json).Path
& $CMake -S . -B build-release `
  "-DXLLE_AOT_PRIVATE_INPUT=$PrivateInput" `
  -DXLLE_AOT_PRIVATE_OPTIMIZATION=O2
& $CMake --build build-release --target xlle -j 12
./tools/run_stockdash_replay.ps1 -Label aot-native `
  -ExtraXlleArgs @('--aot-native','--hdd-security-file','..\machine\hdd-security.key')
```

Provision the private HDD credentials first using [HDD security setup](HDD-SECURITY.md).
Native execution is opt-in. `--no-aot-native` uses the fallback on the same binary.
Normal builds use a deterministic software-TLB resize window. Keep
`XLLE_HOST_TIMED_TLB=OFF` for repeatable native/fallback work counts. The `ON`
setting restores the legacy host-time-dependent heuristic for diagnostics;
it can change native eligibility under host load. See the
[controlled TLB experiment](RESULTS-AOT5-TLB-2026-09-11.md).
`--aot-native-mask 1` selects the first generated region; `2` selects the second;
`3` selects both. Omitting the mask selects **all** generated regions, including
indices above 63. An explicit mask is the legacy diagnostic selector for indices
0..63; higher indices are excluded in that mode. Region ordering follows ascending selected address. Registration
alone does not prove execution: read the `[aot]` and `[aot-region]` retirement counts.

ABI version 4 retains the cached code-segment limit and MMX/cache-hint services,
and adds fixed x87 memory/register callbacks. Rebuild generated artifacts;
older ABI artifacts are rejected. The measured kernel uses a
zero-based 3 GiB code segment and full-range data/stack segments. Native entry
checks that all region instruction bytes fit inside CS; taken branches check their
target before retiring. Unsupported contexts return to Unicorn. Code identity is
checked against live RAM at each supported instruction. Memory callbacks certify
the full access and preserve faults, permissions, aliases and code invalidation.
MMX transfers certify all eight bytes before modifying memory or x87/MMX state.
The engine owns the shared register file; helpers never execute guest code.
The x87 path uses the engine's 80-bit SoftFloat state and implemented precision,
rounding and environment services. Current native eligibility requires masked
exceptions, a valid stack for the operation and a fully cached RAM span.
Unsupported controls, pending exceptions, stack faults, cold memory and MMIO
decline before x87 effects; they remain CPU-floor work and count as fallback.
This bounded support is not a claim of complete x87 semantics.

General integer support also includes 32-bit register ROL/ROR (one, immediate,
or CL count), OR register/immediate forms and BSWAP. Rotate counts use five-bit
masking; zero counts preserve flags and old CL is used for ECX destinations.
For multi-bit rotates, undefined OF follows the pinned CPU floor's policy;
logical operations retain the documented undefined-AF policy. Widths, prefixes
and memory forms outside the implemented subset still exit explicitly. See
[integer qualification](RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md).

To remove embedded private code, clear the cached input and rebuild:

```powershell
& $CMake -S . -B build-release '-DXLLE_AOT_PRIVATE_INPUT='
& $CMake --build build-release --target xlle -j 12
```

Old generated files and libraries remain private even after reconfiguration.
Do not publish the build directory.

## 4. Validate and measure

Use the same binary for interleaved A/B. Stop other emulator runs and builds first.
The harness reports contamination; a busy run is not a valid performance leg.

```powershell
./tools/run_stockdash_replay.ps1 -Label aot-ab `
  -Candidate build-release/xlle.exe -Repetitions 3 -RequireQuiescence `
  -ExtraXlleArgsA @('--no-aot-native') `
  -ExtraXlleArgsB @('--aot-native')
```

Use the minimum and median total wall-time gate from
[the methodology](PERF-METHODOLOGY.md). A negative result remains a negative
result, regardless of native coverage. Use `-PerfAccounting` in separate runs:
`aot_exec` now measures entry/exit eligibility and state-transfer work exclusively;
`aot_body` measures generated execution including instruction guards and memory
callbacks. `tcg_exec` remains fallback execution. These are instrumented costs,
not uninstrumented speed estimates.

The full reference check additionally uses:

```powershell
./tools/run_stockdash_replay.ps1 -Label aot-reference `
  -GoldenRecord goldens/golden-noperf-defaults12-2026-09-05d.json `
  -ExtraXlleArgs @('--aot-native','--nv2a-draw-threads','0','--nv2a-profile-hashes')
```

Validate generated code with `XLLE_AOT_PRIVATE_OPTIMIZATION=O0` as well as `O2`.
Run the CTest suite and inspect intermediate screenshots, device events and native
retirement. A matching final screenshot alone cannot establish correctness.
Checkpoint filenames contain actual instruction counts, which may shift at native
boundaries; compare the same scheduled checkpoints and report that shift explicitly.

The publication checker rejects captured blockpacks, private prepared inputs,
marked generated source/manifests and census CSVs even when renamed. Run the
repository's normal publication checks before staging any artifacts. The
[upstream qualification](AUDIT-XBOXRECOMP-AOT4-2026-09-07.md) explains why this
workflow retains xboxlle's lowering and ABI rather than importing another runtime.
