# BUILDING - xlle

Supported host: Windows x64. Supported shell for the commands below: PowerShell.
Supported compiler toolchain: MSYS2 MinGW64 GCC/G++ with Ninja and CMake.

A clean checkout builds and runs the unit/spike tests without private Xbox firmware,
EEPROM, HDD, dashboard, or game material. Stock-dashboard replay and interactive
navigation require the private sibling `machine` directory documented in
`docs/INPUTS.md`.

## Prerequisites

Install or provide these on PATH:

- MSYS2 MinGW64 `gcc`, `g++`, and `ninja`, normally from `C:\msys64\mingw64\bin`.
- CMake 3.20 or newer.
- Git and network access for CMake `FetchContent`.
- Python 3, including the Windows `py -3` launcher. CMake can discover Python through
  `find_package(Python3)`, but replay scripts call `py -3` directly.
- A Windows graphics driver with OpenGL 4.3 for the SDL/OpenGL renderer. Current
  GPU shader paths additionally use `GL_ARB_gpu_shader_int64` and
  `GL_ARB_fragment_shader_interlock`; the core version alone is insufficient.

CMake fetches these pinned dependencies:

| Dependency | Pin | Used for |
|---|---|---|
| Unicorn Engine | `2.1.1` | x86 CPU floor |
| `nv2a_vsh_cpu` | `863fc0f50aa4027af113718d2a63e45ac3be7a42` | NV2A vertex-shader interpreter reference |
| SDL | `release-3.2.0` | host window/input/presentation when `XLLE_WITH_SDL=ON` |

## Configure, build, and test

Use a Release build for replay work. The stock replay harness refuses a non-optimized
`build-release` cache.

```powershell
Set-Location F:\Projects\xboxlle\xboxlle
$env:Path = 'C:\msys64\mingw64\bin;' + $env:Path

$cmakeExe = 'C:\Program Files\CMake\bin\cmake.exe'
$ctestExe = 'C:\Program Files\CMake\bin\ctest.exe'
& $cmakeExe -S . -B build-release -G Ninja `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" `
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" `
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

& $cmakeExe --build build-release --config Release -j
& $ctestExe --test-dir build-release --output-on-failure
```

Use the actual Windows CMake installation path. The campaign was validated with
Visual Studio's bundled CMake 3.29 at
`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`;
its `ctest.exe` is adjacent. Keep the MinGW64 bin directory on PATH even when
compiler paths are absolute: compiler subprocesses need it too.

The build command builds all default targets,
including the executable and C++ test binaries, before CTest runs them. Do not build
only `xlle` and one focused test when validating a fresh setup.
Check that configure reports a found Python3 interpreter: if it is missing,
CMake omits the Python guard/report tests. Inspect
`& $ctestExe --test-dir build-release -N` if the test count is unexpectedly small
(the current normal build registers
41 tests). The replay harness's `-Build` switch builds only `xlle` and
`test_nv2a`; it does not replace the full build/test sequence above.

For a faster core-only development build, configure a separate directory with
`-DXLLE_WITH_SDL=OFF`; that build is not sufficient for the default stock-dashboard
OpenGL replay or interactive presentation path.

`XLLE_NATIVE_OPTIMIZATIONS=ON` is the default on x86-64 GNU/Clang and is required by
the replay harness cache checks. `XLLE_DISPATCH_MICRO=OFF` is the default and should
stay off unless a specific dispatch micro-partition measurement asks for it.

For a binary intended for another host CPU, use `-DXLLE_NATIVE_OPTIMIZATIONS=OFF`
in a separate build directory; the performance harness intentionally requires the
host-native configuration. Keep the built `SDL3.dll` next to `xlle.exe` when
copying an interactive build. No separate Unicorn DLL is needed.

## Direct executable convention

Run commands from the repo root and name executables/scripts directly:

```powershell
./build-release/xlle.exe
./build-release/xlle.exe --serve
./build-release/xlle.exe --window
./tools/run_stockdash_replay.ps1 -Label defaults
```

The bare executable runs the no-firmware fixture spike. `--serve` also opens the
loopback TCP debug server on port 4370. `--window` opens the SDL host window when the
build includes SDL.

## Stock-dashboard replay

Before running stock-dashboard replay, prepare the sibling private machine directory
from `docs/INPUTS.md`:

```text
F:\Projects\xboxlle\machine\
  flash-dump-ff000000.bin
  bios-extract\bldr.bin
  xbox-hdd-stockdash.img
  eeprom.bin
```

Then run:

```powershell
./tools/run_stockdash_replay.ps1 -Label defaults
```

The harness defaults to noperf, OpenGL, 12,000,000,000 max instructions, 500,000,000
instruction frame-series checkpoints, 768 MiB host memory limit, 4096-instruction CPU
chunks, the `tests\scripts\clock-accept.txt` input script, and the canonical 12B final
SHA gate.

For a route whose final image differs from the canonical 12B frame, clear the default
final-SHA gate and provide the route-specific script/window:

```powershell
./tools/run_stockdash_replay.ps1 -Label settings-v2-22b `
  -MaxInsns 22000000000 `
  -FrameInterval 250000000 `
  -InputScript tests\scripts\stockdash-settings.txt `
  -ExpectedFinalSha ''
```

Use `-PerfAccounting` only for attribution. Headline replay timings use noperf runs.

## Interactive keyboard run

Current live keyboard input requires the test synthetic Controller-S hub and a large
positive instruction budget. Omit `--test-input-script` for live keyboard control.
The combination is supported for scripted presentation, but scripted input owns
the pad and live keyboard state is not published.

```powershell
./build-release/xlle.exe `
  --nv2a-renderer opengl `
  --boot ..\machine\flash-dump-ff000000.bin `
  --run2bl ..\machine\bios-extract\bldr.bin `
  --hdd ..\machine\xbox-hdd-stockdash.img `
  --eeprom ..\machine\eeprom.bin `
  --net-disconnected `
  --reduced-retire-diag `
  --test-synthetic-controller-hub `
  --max-host-memory-mb 768 `
  --machine-chunk-insns 4096 `
  --max-insns 100000000000000 `
  --interactive `
  --interactive-present-stats-ms 1000
```

Keyboard mapping: arrows = D-pad, `Z` = A, `X` = B, Enter = Start, Backspace = Back.
The synthetic hub is test plumbing through the modeled OHCI/XID path; it is not proof
that real Controller-S topology is complete.

## Private-material and log hygiene

Do not commit or publish private console bytes, extracted boot blobs, EEPROM contents,
HDD images, dashboard files, game discs, generated reports, logs, PPMs, PNGs, or local
machine evidence packages. EEPROM dumps contain per-console secrets and HDD-key
material. Replay logs and reports can still be sensitive because they may include local
paths, timing, input schedules, hashes, and private asset identities.

See [repository hygiene](REPOSITORY-HYGIENE.md) and
[`check_repository_hygiene.py`](../tools/check_repository_hygiene.py).

## Debug server protocol (tcp/4370, loopback)

Line-based; one JSON object per line in replies (JSONL).

| Command | Reply |
|---|---|
| `ping` | `pong` |
| `rings` | newline-separated ring names |
| `query <ring> <n>` | the most-recent `n` entries of `<ring>` as JSONL |

Example with `--serve` running:

```text
query mem 1
{"ring":"mem","seq":1,"write":true,"addr":"0x2000","size":4,"value":"0xdeadbeef"}
```
