# xboxllr developer setup

This guide covers the shared framework. For player inputs, optional console
imports and the original Dashboard, start with [Setup](SETUP.md).
Current native title builds target **Windows x64**. Linux component CI does
not establish a native Linux game host; that port is deferred.

There are two distinct source builds:

| Build | Purpose | Xbox files needed? |
| --- | --- | --- |
| Native title (`XBOXLLR_BUILD_MODE=NATIVE_TITLE`) | Ahead-of-time title CPU code with shared fast services; normal Blinx/JSRF/Dashboard previews | Yes, for title preparation and execution; ordinary native play does not boot a Microsoft kernel |
| Full-system reference (`XBOXLLR_BUILD_MODE=REFERENCE`, root CMake default) | Maintained CPU/device/original-software correctness route | No for its source/unit builds; yes for original-software boot/reference checks |

Do not configure the root with defaults expecting the fast title application.
Use the shared native driver and an admitted title definition for that path.

## Tools and checkout

- **Git**, with access to the private repositories. Keep `xboxllr`, BlinxRecomp
  and JSRFRecomp private under the current owner policy.
- **Python 3.11+** for `tomllib` and `hashlib.file_digest`; the current Windows
  tooling has been exercised with Python 3.12. The ROM-free checks below use
  the standard library. Translation/binding tools can additionally need
  Capstone and the pinned donor's declared Python dependencies; use a private
  virtual environment for those. Optional original-code verification uses
  Unicorn; it is not a normal signing-setup or native-title dependency.
- **Visual Studio 2022**, Desktop development with C++, MSVC x64 tools and a
  Windows SDK. Recent accepted builds used MSVC 14.41. Keep runtime DLLs at
  least compatible with the selected toolset; do not select an older redist
  simply because it is next to a different installed toolset.
- **CMake 3.20+**. The shared native driver defaults to the VS 2022 x64
  generator on Windows. Pass its `--cmake` option if your executable is not at
  the standard `C:\Program Files\CMake\bin\cmake.exe` location.
- For the selected DSP backend: **Rust/Cargo with the
  `x86_64-pc-windows-msvc` target**, matching the native compiler ABI.
- Native title/launcher builds use **SDL2**. The optional full-system host
  instead uses **SDL3**; their CMake packages are not interchangeable.

Use explicit tool executable paths when PATH contains MSYS/Cygwin wrappers.
From an ordinary PowerShell session, choose data/build locations with enough
space and establish process-local scratch/cache paths first:

```powershell
$workRoot = 'G:\Dev\xboxllr'
$scratchRoot = 'G:\Temp\xboxllr-dev'
New-Item -ItemType Directory -Force -Path $workRoot,$scratchRoot | Out-Null
$env:TEMP = "$scratchRoot\temp"
$env:TMP = $env:TEMP
$env:PIP_CACHE_DIR = "$scratchRoot\pip-cache"
$env:PYTHONDONTWRITEBYTECODE = '1'
New-Item -ItemType Directory -Force -Path $env:TEMP,$env:PIP_CACHE_DIR | Out-Null
git clone https://github.com/mstan/xboxllr.git "$workRoot\source"
Set-Location -LiteralPath "$workRoot\source"
git rev-parse HEAD
```

These are example paths, not required drive letters. Keep virtual environments,
downloads, generated code, captures and Cargo outputs on the selected data
drive as well. For a fresh Rust installation, set `CARGO_HOME` and `RUSTUP_HOME`
there **before installing**; do not move an existing shared toolchain to follow
this guide. `CARGO_TARGET_DIR` can independently redirect build output.
Use a private venv on that drive if installing generation-only Python packages.

For a game checkout, use `git submodule update --init` in that game repository
and retain its reviewed framework pin. Blinx currently uses the historical
submodule name `xboxlle`; JSRF uses `xboxllr`. Do not edit the name or follow
framework master implicitly. Explicit framework development and later pin
updates require a coherent title build/validation.

## First checks: no game, BIOS or console required

From the framework root:

```powershell
python tests/test_repository_hygiene.py
python tests/test_eeprom_generation.py
python tests/test_windows_dependencies.py
python tests/test_native_cpu_helpers.py
python tests/test_native_save_keys.py
python tests/test_native_title.py
```

These exercise tooling/contracts, not game boot or visuals. Temporary data uses
the process TEMP directory. A Windows symlink test can explicitly skip when the
host lacks the required privilege; report that accurately. More ROM-free CI
commands, including portable generated-C and normal/checked CPU-helper builds,
are in [the workflow](../.github/workflows/repository-hygiene.yml).

Recovery/translation checks additionally need the
[Capstone Python binding](https://www.capstone-engine.org/lang_python.html).
Install it in a private data-drive environment, then run the recovery checks:

```powershell
python -m venv "$workRoot\build\python-env"
& "$workRoot\build\python-env\Scripts\python.exe" -m pip install capstone==5.0.9
& "$workRoot\build\python-env\Scripts\python.exe" tests/test_native_title_recovery.py
```

Use that environment's Python for translation/binding commands too. Optional
original-code verification requires its separate reference dependencies.
The eight recovery tests pass with Capstone 5.0.9 on the current Windows host;
that component result is not a fresh original-title generation/boot claim.

For a headless full-system source build, use a **separate** directory. This
fetches its pinned reference dependencies and may take considerably longer
than the small Python checks. The following uses the MinGW/Ninja reference
toolchain from the [legacy guide](LEGACY-FULL-SYSTEM-SETUP.md); install those
additional tools first. Native title builds above still use MSVC.

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B "$workRoot\build\reference" -G Ninja -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_BUILD_TYPE=Release -DXBOXLLR_BUILD_MODE=REFERENCE -DXLLE_WITH_SDL=OFF -DXLLE_RENDERER=LLE -DXLLE_NATIVE_OPTIMIZATIONS=OFF
& 'C:\Program Files\CMake\bin\cmake.exe' --build "$workRoot\build\reference" --config Release --parallel 4
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir "$workRoot\build\reference" -C Release --output-on-failure
```

Use this for reference development, not as the optimized game release build.
Additional original-software fixtures have private dependencies described in
their respective guides. Plain CMake/component compilation does not qualify
full original boot or all hardware behavior.

## Prepare native dependencies

An admitted title needs private **content/preparation** and pinned **source
dependencies**. Do not confuse a source donor checkout with an original game
or firmware download. Retain upstream notices and use the preparers rather
than copying source directories manually.

| Dependency | Source/preparation |
| --- | --- |
| CPU translator | Reviewed Phobos xboxrecomp revision `63f18f0334fbd8c00313c4bb565accf00627a4fd`; `tools/native_pilot.py` verifies its pin and tracked source. The native driver invokes it for preparation. |
| CPU runtime helper | Attributed MIT upstream template plus authored bridge installed by the current preparation path; see [provenance](NATIVE-CPU-HELPER-PROVENANCE-2026-10-21.md). This is distinct from the translator source pin. |
| PHOBOS renderer | Reviewed fork revision `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, prepared by `tools/native_phobos_d3d.py --donor <checkout> --output <private-build-directory> --reference <prepared-SPOON-directory>`. Its reference is the output of the SPOON preparer below, containing `donor.json` and `nv2a-reference/`, not a raw source checkout or image capture. Distribution review remains open. |
| Alternative SPOON renderer | `tools/native_d3d_donor.py --donor <checkout> --output <private-build-directory> --vsh-reference <pinned-nv2a_vsh_cpu-source>`; donor pin `1409a7d7801d3e931fb1074be6104209ddd9a33e`. Do not change a title's renderer selection merely to bypass its required dependency. |
| DSP | `mborgerson/dsp56300` revision `c60aeedb6a6014850204c71e53bc0cf5b3104222`; `tools/native_dsp_donor.py` prepares guarded source and records Cargo dependencies/notices. |
| SDL2 | Supply a Windows x64 development package and its CMake directory, with the runtime DLL. Current previews used SDL 2.32.10. |
| Launcher UI | Clean reviewed recomp-ui revision `ccc186e097b362555292b157ffd303abcea26fbf`, prepared by `tools/native_launcher_ui.py` through the launcher builder. The builder retains notices and verifies the controller artwork download. |

Find exact repositories/notices in
[third-party attribution](../THIRD_PARTY_ATTRIBUTION.md). Source/asset licenses
remain separate from the framework's license. These are the current research
build inputs, not a claim that the selected combined executable is already
cleared for release.

For the DSP, after obtaining the admitted source checkout and an installed
Cargo executable, use new data-drive output directories. Replace the example
tool/source paths with your actual locations:

```powershell
$cargoExe = 'G:\Tools\cargo\bin\cargo.exe'
python tools/native_dsp_donor.py --donor 'G:\Dev\deps\dsp56300' --output "$workRoot\build\dsp-source" --cargo $cargoExe
& $cargoExe build --release --locked -p dsp56300-emu-ffi --manifest-path "$workRoot\build\dsp-source\Cargo.toml" --target-dir "$workRoot\build\dsp-target"
```

The selected `dsp_library` then points to
`build/dsp-target/release/dsp56300_emu_ffi.lib`; `dsp_source` points to the
prepared source. Keep its notices and preparation receipts. Current production
ADPCM uses the maintained dr_wav adapter; an old GPL comparison decoder is not
a required native dependency. See [ADPCM integration](NATIVE-ADPCM-RELEASE-2026-10-21.md).

## Prepare owned content and private paths

Use [Setup](SETUP.md) for extracting/checking your own supported title,
creating/importing an EEPROM and preparing its signing/settings inputs. Current
signing preparation still needs original-derived material. A game ISO alone
is the intended first-run contract, not today's working developer bootstrap.

The title definition is public metadata; the path file and generated output
are private. Relative paths in that JSON resolve against the JSON file.
Typical fields are:

```json
{
  "xbe": "G:/XboxProfiles/content/default.xbe",
  "eeprom": "G:/XboxProfiles/MyConsole/eeprom.bin",
  "media_root": "G:/XboxProfiles/content",
  "state_dir": "G:/XboxProfiles/MyConsole/state",
  "save_keys": "G:/XboxProfiles/MyConsole/title-save-keys.aot-private.bin",
  "console_settings": "G:/XboxProfiles/MyConsole/console-settings.aot-private.bin",
  "cpu_donor": "G:/Dev/deps/phobos-translator",
  "xdk_symbols": "G:/XboxProfiles/private-analysis/symbols.json",
  "d3d_source": "G:/Dev/xboxllr/build/d3d-source",
  "dsp_source": "G:/Dev/xboxllr/build/dsp-source",
  "dsp_library": "G:/Dev/xboxllr/build/dsp-target/release/dsp56300_emu_ffi.lib",
  "sdl2_dir": "G:/Dev/deps/SDL2-2.32.10/cmake",
  "launcher_ui": "G:/Dev/deps/recomp-ui"
}
```

This is a template, not a downloadable profile. Populate real existing paths;
create a new empty `state_dir` only for a **new** profile. `xdk_symbols` is the
source-bound analysis inventory used by selected service bindings, not a
Microsoft SDK directory. Admitting a fresh revision still requires that
analysis; arbitrary unreviewed symbols or a renamed XBE cannot substitute for
it. See the title's revision profile, binding proofs and recovery plan.

For Dashboard, set `xbe` to the matching `xboxdash.xbe` and `media_root` to its
complete original content directory. Its host creates its drive links according
to [the title definition](../titles/dashboard/title.toml); do not pre-mount
game-style content over its writable partition.

## Prepare, configure, build and run

The example below selects Dashboard as a native title. All generated code
must stay under a private path containing a `build` directory:

```powershell
$driverArgs = @(
  '--title', 'titles/dashboard/title.toml',
  '--paths', 'G:\XboxProfiles\private-analysis\dashboard-paths.aot-private.json',
  '--generated', "$workRoot\build\dashboard-generated",
  '--build-dir', "$workRoot\build\dashboard-host",
  '--jobs', '4',
  '--link-profile', 'ITERATIVE'
)
python tools/native_title.py prepare @driverArgs
```

Apply the title's reviewed `recover --recovery <plan.json>` step if its revision
has a required recovery plan, then continue with the **same** arguments:

```powershell
python tools/native_title.py configure @driverArgs
python tools/native_title.py build @driverArgs --with-checks
python tools/native_title.py check @driverArgs
python tools/native_title.py run @driverArgs --no-launcher --detach
```

`check` verifies title, preparation and generated-artifact identity; it does not
verify the final executable/build receipt or run a game test. Build/run have
their own additional identity checks.
`--with-checks` builds the native iteration test executables but does not run
them. Choose relevant CTest cases in the selected build when changing a service.
An owner-facing run is windowed, has normal audio and no deadline. The driver
stages a verified executable so future builds do not replace the running copy.

Dashboard's current definition does not declare the recomp-ui launcher block;
the direct run above is the applicable path. For game definitions that declare
the reviewed launcher and supply `launcher_ui`, build it with
`build-launcher @driverArgs`, then use `run @driverArgs --launcher --detach`.

JSRF's `native-title.toml` works with this generic driver or its thin
`tools/game.py` wrapper. Blinx still has the older `game.toml` schema: use its
wrapper (`build --mode play`, `build-launcher`, `run --mode play --launcher`),
not `--title game.toml` with the generic driver. See the
[Blinx](https://github.com/mstan/BlinxRecomp/blob/main/docs/DEVELOPMENT.md) and
[JSRF](https://github.com/mstan/JSRFRecomp/blob/main/docs/DEVELOPMENT.md) guides.

Keep `ITERATIVE` consistent across configure/build/run. It retains optimized
CPU compilation while using incremental linking. Use a separate/explicit
`FINAL` selection for final package candidates. A source/profile/framework
identity change intentionally invalidates old receipts; reconfigure/rebuild
rather than editing receipts to admit stale output. Reuse verified unchanged
CPU units, `bind`, bounded `recover` and `partition` where appropriate; see
[driver behavior and receipts](NATIVE-TITLE-DRIVER.md).

## Validate and contribute

For an unattended native smoke, select a short meaningful sequence and use:

```powershell
python tools/native_title.py run @driverArgs --automated --seconds 20 --no-launcher
```

Automated runs are muted/hidden and bounded. Optional `--input-replay <file>`,
`--capture` and `--debug-port <localhost-port>` support reproducible input and
presented screenshots. Captures cost I/O and must be off for performance
measurements. A harness deadline is not a crash, nor does reaching it prove
anything about visuals or save/load. Inspect actual evidence. Run only the
affected component checks and one practical combined regression per coherent
change; retain explicit unverified cases rather than repeating long attract
runs for documentation edits.

Shared CPU/kernel/device/renderer/audio/input/storage code belongs here. Title
repositories own identity, revision proofs, discovery/recovery facts, artwork
and data-only mods. Preserve the LLE caller/effect contracts while accelerating
the native implementation; unresolved behavior must stop rather than fabricate
success. Keep portable fallbacks for host optimizations. Backend selection is
fixed before execution, not live HLE/LLE switching.

Track substantive work in the central Beads database, under the Xbox
system/meta or owning game epic. Never initialize a per-repository database.
Respect the repository/global AGENTS instructions for storage, resource budget,
original input hygiene and native-save preservation. Save states are disposable
per build; real in-game saves are not.

Before a commit, use `git diff --check` and
`python tools/check_repository_hygiene.py`; use its `--history` mode before
publishing source. Do not commit game/generated original code, firmware, keys,
EEPROMs, profiles, raw images or diagnostic trees. The installed hooks/CI enforce
additional hygiene. See [Windows dependency preflight](WINDOWS-RELEASE-DEPENDENCIES.md)
and [release readiness](RELEASE-READINESS.md) for packaging, exact DLL closure,
licenses and clean-profile acceptance. No release or title-pin update is
established by these setup commands alone.
