# xboxlle

A faithful, LLE-first low-level emulation platform for the original Xbox (2001,
IA-32 "Coppermine" Pentium III + NV2A). The current product path runs the real
Microsoft 2BL/kernel/dashboard bytes, but it does **not** yet perform authentic
MCPX cold boot: the executable loads a maintainer-supplied flash image and an
already extracted/decrypted 2BL blob, then starts from the approximate 2BL handoff
state used by the current bring-up path.

The first product goal is the real Xbox dashboard, fully navigable and functional,
with faithful LLE at 60 FPS with headroom. That goal is not achieved yet. HLE is
allowed only as an opt-in layer that matches the faithful LLE floor; it is not a
replacement for making the LLE path correct and fast.

It belongs to the recomp family (nesrecomp / psxrecomp / gbarecomp / gcnlle) and
inherits its doctrine: LLE-first, run the real firmware/dashboard where the current
boundary permits it, validate against ground truth, keep observability cheap and
explicit, and treat artifacts as evidence rather than authority. The Xbox guest is
x86-on-x86-64, so this is a dynamic execution platform (CPU floor + JIT + harvested
device models), not a guest-to-C static recompiler. That distinction is why it is
`xboxlle`, not `xboxrecomp`.

## Strategy in one paragraph

Harvest the hard, already-solved parts and re-express them in our own modular,
observable, hardware-correctable architecture: the CPU floor from Unicorn Engine
(embeddable QEMU x86 core, GPL-2), NV2A/APU/device behavior from xemu references
and local hardware probes, and the host presentation layer from SDL3/OpenGL. The
value we build here is deterministic machine wiring, observability/cosim,
hardware-conformance harnesses, the LLE floor, opt-in matching HLE layers, and
per-title packaging. Real Xbox hardware, via the companion `xboxlle-probe` repo,
is the ground-truth oracle; xemu is a reference to harvest from and check against.

## The two arcs

- **Arc 1 - the real Xbox dashboard, fully navigable and functional.** Boot through
  the current firmware boundary, launch and run the dashboard from a real HDD image,
  and make settings, clock, memory manager, music, and navigation work end-to-end.
- **Arc 2 - Blinx: The Time Sweeper.** The first game target, in its own per-title
  repo (`BlinxLLE`), after the Arc-1 platform is proven.

## Fresh Windows setup

The supported development host is Windows x64 with PowerShell, MSYS2 MinGW64,
CMake, Ninja, Git, and Python 3 with the Windows `py -3` launcher. Dependencies
are fetched by CMake: Unicorn Engine 2.1.1, `nv2a_vsh_cpu` commit
`863fc0f50aa4027af113718d2a63e45ac3be7a42`, and SDL `release-3.2.0` when the SDL
host layer is enabled.

Install Git for Windows, Python 3 with its launcher, Windows CMake (3.20 or newer),
and MSYS2. In the MSYS2 MinGW64 terminal, install the compiler and build tool:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-ninja
```

Use a Windows graphics driver supporting OpenGL 4.3 for the GPU renderer. The
current shader paths also use `GL_ARB_gpu_shader_int64` and
`GL_ARB_fragment_shader_interlock`; OpenGL 4.3 alone does not guarantee these
extensions. In
PowerShell, clone into a workspace with room for the private full HDD image and
build products (the current development HDD image alone is 8 GiB):

```powershell
New-Item -ItemType Directory -Force F:\Projects\xboxlle | Out-Null
Set-Location F:\Projects\xboxlle
git clone https://github.com/mstan/xboxlle.git xboxlle
```

Adjust these example paths to your machine. Build all Release targets before
running CTest. Call Windows CMake directly to avoid MSYS/Cygwin path shims:

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
git config core.hooksPath .githooks
py -3 tools/check_repository_hygiene.py --history
```

Use direct executable paths from the repo root in examples and scripts, e.g.
`./build-release/xlle.exe` or `./tools/run_stockdash_replay.ps1`. Do not rely on
changing into the build directory unless a command explicitly says so.

You can immediately run the built-in CPU fixture demo without console dumps:

```powershell
./build-release/xlle.exe
```

This runs synthetic instruction tests; it does not boot the Microsoft dashboard.
The source build currently has no installer or input-creation wizard. If Windows
blocks the repository's PowerShell scripts, review them and use a process-scoped
policy where your host policy permits it:
`Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned`.
Keep `SDL3.dll` beside `xlle.exe` when copying a build. Host-native builds target
the build machine's CPU; see [portable build guidance](docs/BUILDING.md).

## Private machine inputs

A clean checkout builds and runs the unit/spike tests without private console
material. Stock-dashboard replay needs a sibling private directory, outside this
repo and ignored if copied into the repo root by mistake:

```text
F:\Projects\xboxlle\machine\
  flash-dump-ff000000.bin      # flash image used by --boot
  bios-extract\bldr.bin        # already extracted/decrypted 2BL used by --run2bl
  xbox-hdd-stockdash.img       # full sector-for-sector stock-HDD image
  eeprom.bin                   # 256-byte per-console EEPROM dump
```

Supply dumps from your own console. The repository does not bundle a BIOS
extractor; [the input guide](docs/INPUTS.md#preparing-the-bootloader) describes
the external XboxBiosTool extraction step and its private MCPX/key prerequisites.
Current launch tooling consumes the decrypted blob as a supplied artifact. The HDD image is
the whole disk, including FATX partitions and dashboard files such as the C drive
contents, not just `xboxdash.xbe`. EEPROM data contains per-console secrets and
HDD-key material; never publish it, derived keys, or raw bytes.

Default HDD attachment is copy-on-write. Guest writes are served back within the
run but are not persisted to the image unless `--hdd-writeback` is explicitly used.

### What must I supply, and what could be generated?

| Use | Inputs required today |
|---|---|
| Build, CTest, built-in CPU fixture demo | No console material. |
| Run the Microsoft dashboard | Flash, decrypted 2BL, full raw HDD image with dashboard files, and a 256-byte EEPROM. |
| Extract encrypted 2BL from flash | External extraction tool plus matching private MCPX/key prerequisites; see the input guide. |

The replay script currently fixes the four sibling paths shown above. The direct
interactive command accepts your chosen paths. A `.qcow2`/VHD file is not a raw
HDD image; renaming it does not convert it. Size/file-existence checks alone cannot
establish that a BIOS, decrypted bootloader, EEPROM and dashboard are compatible.
Stock-dashboard bring-up has been validated against one private firmware/disk
profile; compatibility with arbitrary BIOS revisions is not yet established.

We can reduce future requirements by generating a fresh virtual EEPROM and a
formatted FATX disk locally, then importing only the user's authorized dashboard
files. Neither generator/import workflow is implemented or validated here yet.
A blank disk contains no dashboard, and random EEPROM bytes are not a valid
console profile. Microsoft firmware, bootloader, dashboard and game content
remain supplied inputs for their respective stock-software paths. See
[the generation plan and legal boundaries](docs/INPUTS.md#reducing-user-supplied-inputs).

## Stock-dashboard replay commands

Canonical 12B replay uses the private files above, OpenGL, disconnected modeled
network, reduced retire diagnostics, the test synthetic Controller-S hub, and the
stock clock-acceptance script:

```powershell
./tools/run_stockdash_replay.ps1 -Label defaults
```

That final-frame gate is specific to the maintainer's private fixture. For a first
run with your own console dumps, use `-ExpectedFinalSha ''`; a different result is
not automatically an emulator regression. Pin your own inputs and reference
outputs before making repeatability or performance claims.

For fixture-specific or longer routes whose final frame is not the canonical 12B
image, clear the default final-SHA gate explicitly:

```powershell
./tools/run_stockdash_replay.ps1 -Label settings-v2-22b `
  -MaxInsns 22000000000 `
  -FrameInterval 250000000 `
  -InputScript tests\scripts\stockdash-settings.txt `
  -ExpectedFinalSha ''
```

Interactive keyboard presentation is a direct executable run. The current dashboard
input route still requires the test synthetic Controller-S hub; do not add a test
input script when using live keyboard input.

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
The synthetic hub is a current coverage limitation, not proof of real Controller-S
wiring.

## Where to read

| Question | File |
|---|---|
| Current goal and next milestone | `NORTH-STAR.md` |
| What is true right now | `STATUS.md` |
| Build/test/run setup | `docs/BUILDING.md` |
| Private inputs and layout | `docs/INPUTS.md` |
| Repository hygiene and sensitive-material checks | [Guide](docs/REPOSITORY-HYGIENE.md), [guard](tools/check_repository_hygiene.py) |
| Architecture | `docs/DESIGN.md` |
| Roadmap | `docs/ROADMAP.md` |
| Performance methodology | `docs/PERF-METHODOLOGY.md` |
| Interactive presentation metrics | `docs/PERF-PRESENT-2026-09-06.md` |
| Golden replay identities | `goldens/README.md` |
| Debugging a black or stalled dashboard | [Diagnostic guide](docs/DASHBOARD-DEBUGGING.md) |

## Acknowledgments

Credit to [sp00nznet's xboxdashboard](https://github.com/sp00nznet/xboxdashboard)
for the dashboard investigation lessons incorporated into our
[debugging guide](docs/DASHBOARD-DEBUGGING.md).
Our [source review](docs/PERF-EXTERNAL-REVIEW-2026-09-06.md) also examines
sp00nznet's xboxrecomp GPU techniques and their limits against the LLE floor.

## Repository and publication posture

No private console bytes belong in this repo. MCPX ROM dumps, flash images, extracted
2BL blobs, EEPROM dumps, HDD images, dashboard files, game discs, generated logs,
reports, PPMs/PNGs, and local machine evidence packages are local-only material.
Some of those files are copyrighted; some are per-console secrets; some are merely
sensitive because they reveal machine paths, input schedules, timing, or private
asset identities. Review before publishing any replay log or report.

The tracked `.gitignore` protects common private asset names and generated evidence;
it is a safety net, not a substitute for review. Primary history/security checks live
in [the hygiene guide](docs/REPOSITORY-HYGIENE.md) and
[the publication guard](tools/check_repository_hygiene.py).

The latest asset audit found no proprietary console payloads or secret-pattern
findings in the checked source/history. This is a scoped audit, not a copyright
clearance: source code and dependencies have their own copyrights and licenses.
The repository currently has no top-level project license; redistribution terms
and third-party notices still need to be settled before presenting it as a
fully licensed release. See [dependency and license status](docs/REPOSITORY-HYGIENE.md#source-licensing-is-a-separate-check).
