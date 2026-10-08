# xboxlle

xboxlle runs original Xbox software by emulating the console's hardware. This is
called **low-level emulation (LLE)**. The first goal is to run the original Xbox
dashboard, the menu with Memory, Music, Xbox Live, and Settings.

The active target is a [faithful LLE floor with interchangeable enhanced execution](docs/GOAL-AOT-DASHBOARD.md).
The current [Blinx milestone and burndown](docs/BLINX-BURNDOWN.md) preserves working
FMVs and real-time attract gameplay. Next, complete and qualify the maintained LLE
routes beneath the native services before expanding into New Game. The accepted
28.5-28.8 fps is tracked performance debt; dashboard performance remains ongoing.
Follow [recomp-template HLE.md](../../recomp-template/HLE.md): LLE supplies the
correctness foundation, HLE supplies performance under the same caller contract,
and developers select implementations at build time. Both configurations remain
buildable and runnable; LLE need not be fast. Full AOT without dynarec remains
the final CPU goal.

**This is development software.** Earlier builds rendered the dashboard with
the tested files. HDD startup now requires independently provisioned credentials;
the kernel must supply the correct password. An explicit unlocked-drive setting
is also available. See [HDD security setup and limits](docs/HDD-SECURITY.md).
Support for other firmware and disk images is not yet established.
Original-speed, responsive output is the product goal; it is not yet achieved.
Any subsystem may have a native/HLE replacement backed by its implemented LLE
contract. Internal algorithms may differ, with compatible observable behavior
and documented numerical/presentation tolerances. Current implementation status
is in [STATUS](STATUS.md); the new delivery policy does not imply those enhanced
paths already exist.

Portable native versions of selected Xbox software are a required project goal,
following psxrecomp's architecture. The planned pipeline translates Xbox x86 code
to portable C, then compiles it for the host computer. The runtime will provide
places to add wider camera views, higher resolution, and higher frame rates later.
Generated portable C runs in **machine-adapter tests** and an experimental private
dashboard build, including transitions to and from Unicorn. Normal builds use
Unicorn. The [dashboard AOT validation record](docs/RESULTS-AOT4-2026-09-07.md)
reports matching dashboard images and a slower native path, which stays opt-in.
Developers can follow the [private dashboard AOT workflow](docs/AOT-PRIVATE-WORKFLOW.md)
to capture, generate, build and measure selected regions locally. This remains
experimental; native coverage does not imply a performance improvement.
See [machine integration and validation](docs/DESIGN-AOT-MACHINE.md).
See the [Blinx launch burndown](docs/BLINX-BURNDOWN.md) for the game-launch
dependencies and the distinction between first execution, playable LLE and
strict AOT. The drive-security gate must pass before qualifying a faithful boot.
The current tested runtime setup is Windows x64; ARM hardware testing is deferred.
See the [AOT implementation plan and test commands](docs/PLAN-AOT-BURNDOWN-2026-09-06.md)
and the
[comparison with xemu and psxrecomp, and the implementation milestones](docs/COMPARISON-XEMU-NATIVIZATION-2026-09-06.md)
for what exists today and what we still need to prove.

This guide takes you from a fresh Windows setup to a dashboard window you can
control with your keyboard. Read the file requirements before installing the
build tools. You can build and test xboxlle without Xbox files, but you need
some private files to reach the Microsoft dashboard.

## 1. Check what you need

Use an **x64 Windows PC (Intel or AMD)** for this guide. You also need an internet connection
for the first build and enough free disk space for the tools, build, and your HDD
image. Our development HDD image alone uses 8 GiB; allow extra space for backups.

The GPU renderer needs OpenGL 4.3 and the extensions
`GL_ARB_gpu_shader_int64` and `GL_ARB_fragment_shader_interlock`. These are
features provided by your graphics driver. Having OpenGL 4.3 alone is not enough.
See [troubleshooting](#troubleshooting) if the renderer cannot start.

These are the files needed to boot the dashboard:

| File | What it means | How you get it |
|---|---|---|
| Flash / BIOS image | The Xbox startup firmware. | Supply a backup from your own console. |
| Decrypted 2BL bootloader | A small program inside the firmware that starts the Xbox kernel. | Extract it from your supplied firmware in step 6, or supply an already decrypted copy. |
| Full raw HDD image | A copy of the entire Xbox hard disk, including its partitions and dashboard files. | Supply a readable backup of your own disk. |
| EEPROM file | A 256-byte file with the virtual console's identity, keys, and settings. | Generate it in step 7, or supply your original console's EEPROM backup. |

Encrypted bootloader extraction also needs a **matching MCPX ROM backup or
private 2BL key file**. Step 6 explains where these fit.

Microsoft firmware, dashboard files, and game files are **not included**. The
project cannot generate replacements for them today. A tool to create a new
Xbox disk and import your dashboard files is planned, but is not implemented.
Creating an empty disk file will not give you a dashboard.

Keep your original backups untouched. Use working copies for this guide. Do not
upload firmware, disk images, EEPROMs, or keys to GitHub.

## 2. Install the tools

A **terminal** is a window where you type commands. We use two terminals here:
MSYS2 MINGW64 for installing the compiler, and PowerShell for everything else.

Install these tools from their official sites:

| Tool | Install choice | Why it is needed |
|---|---|---|
| [Git for Windows](https://git-scm.com/install/windows) | Windows x64; enable Git from the command line. | Downloads the project source. |
| [Python 3](https://www.python.org/downloads/windows/) | Install a Python 3 runtime and make sure `py -3` works. | Runs setup tools and tests. |
| [CMake](https://cmake.org/download/) | Windows x64 installer, version 3.20 or newer. This guide uses its default folder, `C:\Program Files\CMake`. | Prepares the build. |
| [MSYS2](https://www.msys2.org/) | Install in `C:\msys64`, the default folder. | Provides the C/C++ compiler and Ninja build tool. |

After installing MSYS2, follow its update instructions. Then open **MSYS2 MINGW64**
from the Start menu. If you cannot find that shortcut, run
`C:\msys64\mingw64.exe`. Do not use the MSYS or UCRT64 terminal for this step.

Run this command in **MSYS2 MINGW64**:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-ninja
```

Accept the package installation when prompted. Wait until it finishes. You can
then close this terminal.

Open a **new PowerShell window** from the Start menu. Run all remaining command
blocks in PowerShell, unless a step explicitly says otherwise. Administrator mode
is not needed for the project commands.

Check the tools:

```powershell
git --version
py -3 --version
& 'C:\Program Files\CMake\bin\cmake.exe' --version
& 'C:\msys64\mingw64\bin\gcc.exe' --version
& 'C:\msys64\mingw64\bin\ninja.exe' --version
```

Install the pinned decoder used by the XBE inventory and its tests:

```powershell
py -3 -m pip install capstone==5.0.9
```

It is a build/analysis dependency; it does not execute guest instructions.

**Check:** each command should print a version. If one cannot find the command
or file, fix that installation before continuing. If you installed a tool in
another folder, change its path in the examples below.

## 3. Clone the repository

The **repository**, or **repo**, contains the project's source code. **Cloning**
means downloading a working copy with Git.

These commands create a workspace inside your Windows user folder. They do not
require an `F:` drive. Run them once:

```powershell
$xboxWorkspace = Join-Path $env:USERPROFILE 'Projects\xboxlle'
New-Item -ItemType Directory -Force -Path $xboxWorkspace | Out-Null
Set-Location $xboxWorkspace
git clone https://github.com/mstan/xboxlle.git xboxlle
Set-Location .\xboxlle
git config core.hooksPath .githooks
```

The two folders named `xboxlle` are intentional. The outer folder holds everything;
the inner folder is the Git repository. The hooks enable checks for private files
before commits and pushes.

**Check:** `Get-Location` should show a path ending in
`\Projects\xboxlle\xboxlle`. `Get-Item .\CMakeLists.txt` should find a file.
This is the **repo root**. Run the remaining commands from here.

If you close PowerShell, open it again and run this block before continuing:

```powershell
Set-Location (Join-Path $env:USERPROFILE 'Projects\xboxlle\xboxlle')
$env:Path = 'C:\msys64\mingw64\bin;' + $env:Path
$cmakeExe = 'C:\Program Files\CMake\bin\cmake.exe'
$ctestExe = 'C:\Program Files\CMake\bin\ctest.exe'
```

`$env:USERPROFILE` means your Windows user folder. `.` means the current folder;
`..` means its parent folder. The PATH change lets Windows find the compiler's
helper programs. It lasts only for this PowerShell window.

## 4. Build and test xboxlle

**Building** compiles the source code into a program you can run. First, prepare
the build from the repo root:

```powershell
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
```

Copy the whole command block. A backtick at the end of a line joins it to the next
line; do not put spaces after it. The `&` tells PowerShell to run the executable
stored in the following path or variable.

CMake downloads the required libraries automatically: Unicorn 2.1.1, SDL 3.2.0,
and a pinned version of `nv2a_vsh_cpu`. You do not need to download these separately.
The first setup can take several minutes.

**Check:** CMake should finish with `Build files have been written to` and no
errors. Check that it also found Python 3; otherwise some tests will be missing.

Now build all targets:

```powershell
& $cmakeExe --build build-release --config Release -j
```

**Check:** the build should finish without errors. The program is
`build-release\xlle.exe`. The build also places `SDL3.dll` beside it.

Run the tests:

```powershell
& $ctestExe --test-dir build-release --output-on-failure
```

**Check:** expect `100% tests passed`. The normal build currently has 42 tests
when Python is available. Stop and investigate failed tests before trying to boot
the dashboard.

You can also run a small CPU test without any Xbox files:

```powershell
.\build-release\xlle.exe
```

**Check:** it should report `[spike] 15 passed, 0 failed` and exit. This command
only runs built-in tests. It does not open the dashboard.

## 5. Put your private Xbox files in place

Create a separate folder for the files from your console:

```powershell
New-Item -ItemType Directory -Force -Path ..\machine\bios-extract | Out-Null
```

Use File Explorer to copy your **flash / BIOS image** and **full raw HDD image**
into `machine`. Give the working copies the names shown below. Enable File name
extensions in Explorer's View menu so you can check the complete names.

The finished layout will be:

```text
Your Windows user folder\Projects\xboxlle\
  xboxlle\                         The repository; README.md is here.
  machine\                         Private files, outside the repository.
    flash-dump-ff000000.bin         Your flash / BIOS working copy.
    xbox-hdd-stockdash.img          Your full raw disk working copy.
    eeprom.bin                     Generated or supplied in step 7.
    bios-extract\
      bldr.bin                     Extracted or supplied in step 6.
```

The HDD image must contain the Xbox disk layout, its FATX partitions, and the
Microsoft dashboard with its supporting files. **A folder of files or
`xboxdash.xbe` alone is not enough.** A `.qcow2`, VHD, or VHDX is a different disk
format. Renaming one to `.img` does not convert it. Supply readable raw sectors,
not a compressed archive or a locked physical drive.

If you do not have these backups yet, this is where you need to pause the
dashboard setup. Xbox hardware backup steps depend on your console and its
current setup; xboxlle does not include a console-dumping wizard. See
[the private-input guide](docs/INPUTS.md#preparing-the-bootloader) for the required
artifacts. Do not erase, format, or unlock a physical disk using guessed commands.

## 6. Prepare the decrypted bootloader

If you already have the **decrypted 2BL from the same firmware**, copy it to
`..\machine\bios-extract\bldr.bin` and continue to step 7.

Otherwise, use the separate [XboxBiosTool project](https://github.com/tommojphillips/XboxBiosTool).
It is not included in the xboxlle build. Its upstream build uses
[Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/).
Install its **Desktop development with C++** workload if you
need to build this tool; this extra installation is only for the extractor.

From the xboxlle repo root, download its source:

```powershell
git clone https://github.com/tommojphillips/XboxBiosTool.git ..\XboxBiosTool
```

In Visual Studio 2022, open `..\XboxBiosTool\vc\XboxBiosTools.sln`. Select
**Release** and **Win32**, then **Build > Build Solution**. Follow the
[upstream build instructions](https://github.com/tommojphillips/XboxBiosTool#building)
if Visual Studio requests missing components. The expected output is
`..\XboxBiosTool\bin\xbios.exe`.

Return to PowerShell in the xboxlle repo root. For encrypted firmware, put your
matching private MCPX backup at `..\machine\mcpx.bin`, then run:

```powershell
$biosToolExe = '..\XboxBiosTool\bin\xbios.exe'
& $biosToolExe /extr ..\machine\flash-dump-ff000000.bin `
  /mcpx ..\machine\mcpx.bin /dir ..\machine\bios-extract
```

If you already have an extractor executable elsewhere, change `$biosToolExe` to
its full path. A private 16-byte 2BL key file can be supplied with `/key-bldr`
instead of `/mcpx`. Modified or already decrypted firmware may need different
options; use [the extractor's documentation](https://github.com/tommojphillips/XboxBiosTool#encryption--decryption).

**Check:** extraction must succeed and produce the decrypted
`..\machine\bios-extract\bldr.bin`. A file merely existing does not prove that
decryption worked. Keep all extraction output private.

xboxlle currently starts at an approximate **2BL handoff**, after the earliest
startup steps. It does not yet reproduce the complete MCPX cold boot. The MCPX
file is used by the extractor, not by the launch command below.

## 7. Generate an EEPROM, or use your original

Choose one option. **For a new virtual console, we recommend generating a file.**
If you need your original console identity for existing saves or accounts, copy
your own 256-byte EEPROM backup to `..\machine\eeprom.bin` and skip the generation
command. Keep an untouched backup of that original EEPROM.

To generate a new virtual console's EEPROM, run:

```powershell
py -3 tools/create_eeprom.py
```

**Check:** it should create `..\machine\eeprom.bin`, or report that an existing
valid file was reused. The tool never overwrites an existing file. An invalid
file or a different authentication revision produces an error.

Keep the generated file between runs. It contains a fresh virtual identity and
private keys. The default profile is `r2`, with North America, NTSC-M video, and
English settings. This profile reached the dashboard with our tested firmware
and disk. Other firmware can need a different EEPROM revision.

**A generated EEPROM is not guaranteed to work with every HDD image.** The
firmware, dashboard, disk layout, and EEPROM settings still need to be compatible.
It also cannot unlock a physical Xbox HDD or recover its lost keys. Provision the
virtual drive once from the EEPROM you chose:

```powershell
py -3 tools/provision_hdd_security.py --eeprom ..\machine\eeprom.bin --output ..\machine\hdd-security.key
```

Keep that private file between runs. Existing output is never overwritten.
The emulator checks the guest's password against these independent credentials.
See [HDD security modes and remaining limits](docs/HDD-SECURITY.md).
See [EEPROM profiles and compatibility](docs/INPUTS.md#generated-eeprom-setup).

## 8. Check the files, then open the dashboard

From the repo root, check that all five files exist and are not empty:

```powershell
& {
  $requiredFiles = @(
    '..\machine\flash-dump-ff000000.bin'
    '..\machine\bios-extract\bldr.bin'
    '..\machine\xbox-hdd-stockdash.img'
    '..\machine\eeprom.bin'
    '..\machine\hdd-security.key'
  )
  foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
      throw "Missing file: $file"
    }
    if ((Get-Item -LiteralPath $file).Length -eq 0) {
      throw "Empty file: $file"
    }
  }
  if ((Get-Item -LiteralPath '..\machine\eeprom.bin').Length -ne 256) {
    throw 'The EEPROM must be exactly 256 bytes.'
  }
  if ((Get-Item -LiteralPath '..\machine\hdd-security.key').Length -ne 64) {
    throw 'The provisioned HDD security file must be exactly 64 bytes.'
  }
  'All five input files are present.'
}
```

This checks names and basic sizes, not whether your files are compatible.
If it reports a problem, fix that before running the next command.

Now launch the emulator. Copy this whole block:

```powershell
.\build-release\xlle.exe `
  --nv2a-renderer opengl `
  --boot ..\machine\flash-dump-ff000000.bin `
  --run2bl ..\machine\bios-extract\bldr.bin `
  --hdd ..\machine\xbox-hdd-stockdash.img `
  --hdd-security-file ..\machine\hdd-security.key `
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

The file options load your private inputs. `--interactive` opens the window
and enables keyboard input. The synthetic controller option supplies the current
virtual controller connection; keep it even though you are using a keyboard.
The other options keep the network disconnected and use the current run settings.
You do not need to tune them for your first boot.

An experimental borrowed shader path is selectable with `--nv2a-renderer xemu`.
It uses xemu-generated pixel shaders for supported draws and the reference path
for the remainder. Return to `opengl` to opt out. This is an integration option,
not a promise of 60 FPS or a qualified shipping default; see the
[renderer contract and qualification notes](docs/RENDERER-XEMU-PSH.md).

**Check:** a window should open and eventually show Xbox video. A fresh profile
may first show the **CLOCK** screen. Let it finish loading, click inside the window,
and use the controls below to set or confirm the clock. The target is the main
dashboard with Memory, Music, Xbox Live, and Settings. Boot and navigation can be
slow while this project is being optimized.

If the window closes or stays black, look for an error in PowerShell and use
[troubleshooting](#troubleshooting). A working build alone cannot guarantee that
a new combination of console files will boot.

## 9. Use the dashboard and close it

Click the emulator window to give it keyboard focus.

| Keyboard key | Xbox control | Common use |
|---|---|---|
| Arrow keys | D-pad | Move between items or change a value. |
| Z | A | Select or confirm. |
| X | B | Cancel or go back. |
| Enter | Start | Start button. |
| Backspace | Back | Back button; different from the B button. |

These mappings use physical key positions; the Z/X labels refer to a US keyboard.
The launch command has no automated input script, so the keyboard controls the
virtual controller. Xbox Live will not connect in this disconnected setup.

Close the emulator window to end the run and return to PowerShell.

**Changes inside the virtual Xbox are temporary by default.** HDD writes stay in
memory and disappear when the emulator exits. EEPROM settings changed by the
guest also stay in memory. Keeping `eeprom.bin` preserves the identity created in
step 7, but it does not make later in-dashboard settings changes persistent.
Do not use this setup as the only copy of important saves. HDD writeback is an
advanced opt-in option described in [the input guide](docs/INPUTS.md).

To run again later, open PowerShell, use the return-to-repo block in step 3, and
repeat the launch command in step 8. You do not need to clone, build, or generate
the EEPROM again unless something has changed.

## Troubleshooting

| Problem | What to check |
|---|---|
| `git`, `py`, or another tool is not found | Finish installing that tool, then open a new PowerShell window. Run the version checks in step 2. |
| CMake or the compiler path does not exist | Check the install folder. These commands assume Windows CMake under `C:\Program Files\CMake` and MSYS2 under `C:\msys64`. |
| CMake cannot find Ninja, or GCC fails to start a helper program | Use the MINGW64 packages from step 2. Repeat the `$env:Path` line in your current PowerShell window. |
| CMake cannot download a dependency | Check your internet connection and Git installation, then repeat the configure command. |
| Build stops because the PC runs out of memory | Repeat the build command with `-j 2` instead of `-j` to run fewer compiler jobs at once. |
| CTest says a test program is missing | Run the full build command in step 4 before running CTest. |
| Tests pass, but Python tests are missing | Make `py -3 --version` work, rerun CMake configuration, and check that CMake found Python 3. |
| Only CPU test results appear; no Xbox window | Running `xlle.exe` alone runs tests. Use the full interactive command in step 8. |
| A required Xbox file cannot be opened | Return to the repo root. Run the file check in step 8. Check Explorer for doubled file extensions. |
| Bootloader extraction fails | Check that the MCPX or private key matches your firmware. Do not substitute the encrypted bootloader or a kernel file for decrypted `bldr.bin`. |
| EEPROM generation reports an invalid file or revision | Keep your original backup. Check the [profile guide](docs/INPUTS.md#generated-eeprom-setup); do not delete and regenerate files at random. |
| OpenGL context or shader creation fails | Install your GPU maker's current driver and check support for both extensions listed in step 1. An unsupported GPU may not run this rendering path. |
| A DLL is missing | Keep MinGW64 on PATH and `SDL3.dll` beside `xlle.exe`; do not copy just the EXE out of the build folder. |
| Keys do nothing | Click the window, keep `--test-synthetic-controller-hub`, and do not add a test input script. Use Z to select, not Enter. |
| The clock or settings return to their old values next run | Guest changes are temporary with the default disk and EEPROM behavior. See step 9. |
| Xbox video is black or stops changing | Read the PowerShell error and [dashboard debugging guide](docs/DASHBOARD-DEBUGGING.md). Firmware/disk compatibility is still limited. |

If you report a problem, include the source commit (`git rev-parse --short HEAD`),
Windows version, GPU/driver, and the relevant error after checking it for private
data. Do not attach your machine folder, raw logs, screenshots, keys, or dumps
without a privacy review. See [repository hygiene](docs/REPOSITORY-HYGIENE.md).

## Optional: record a scripted boot for development

You can skip this section if you only want to use the dashboard window.
A **replay** runs a fixed input script and saves diagnostic files. It is separate
from interactive keyboard use.

After completing setup, run from the repo root:

```powershell
.\tools\run_stockdash_replay.ps1 -Label first-run -ExpectedFinalSha ''
```

This uses the same four paths under `..\machine`, runs a fixed instruction budget,
and writes logs, reports, and captured frames under `build`. It is not the command
for an interactive dashboard window. Use a new label, such as `second-run`, for
another recording.

The empty `-ExpectedFinalSha ''` disables the final-image check against the
maintainer's private files. Your files or generated EEPROM can produce a different
image. This command does not establish that your run matches a reference console
or that a performance change is faster. See [the measurement rules](docs/PERF-METHODOLOGY.md)
before comparing performance, and [golden records](goldens/README.md) for reference
checks.

If Windows blocks the script, review it first. Where your PC's policy permits it,
allow local scripts for this PowerShell window only, then retry:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned
```

## More project information

The first milestone is a faithful, fully functional dashboard. The next software
target is **Blinx: The Time Sweeper**, in its own `BlinxLLE` project. Real Xbox
hardware is the reference for correctness; xemu is also an implementation and
behavior reference. The CPU uses Unicorn, with SDL3 and OpenGL for host graphics.

| Topic | Read more |
|---|---|
| Goals and current progress | [North star](NORTH-STAR.md), [status](STATUS.md), [roadmap](docs/ROADMAP.md) |
| Build options and portable builds | [Building](docs/BUILDING.md) |
| File formats, EEPROM profiles, and planned input generation | [Private inputs](docs/INPUTS.md) |
| How the emulator is organized | [Design](docs/DESIGN.md) |
| Measuring speed and interactive presentation | [Methodology](docs/PERF-METHODOLOGY.md), [presentation measurements](docs/PERF-PRESENT-2026-09-06.md) |
| Keeping private material out of Git | [Hygiene guide](docs/REPOSITORY-HYGIENE.md), [publication guard](tools/check_repository_hygiene.py) |

Credit to [sp00nznet's xboxdashboard](https://github.com/sp00nznet/xboxdashboard)
for investigation lessons used in our debugging guide. Our
[external source review](docs/PERF-EXTERNAL-REVIEW-2026-09-06.md) also covers
sp00nznet's xboxrecomp GPU techniques and their limits for faithful LLE.

## License and private material

The owner's original work uses [PolyForm Noncommercial 1.0.0](LICENSE).
Third-party code retains its existing licenses, including the LGPL EEPROM tool.
Read [license scope](LICENSE-SCOPE.md) and [third-party attribution](THIRD_PARTY_ATTRIBUTION.md).

The current executable links GPL Unicorn. That conflicts with distributing the
combined program under PolyForm alone; keeping notices does not resolve the
conflict. The combined-executable release licensing work is still open.

The latest source/history audit found no proprietary console payloads or
secret-pattern findings in the checked material. This is a scoped audit, not a
copyright clearance. The source and its dependencies have their own copyrights.
Private Xbox inputs and generated evidence stay local. `.gitignore` and the Git
hooks help protect them, but do not replace review before publishing.
