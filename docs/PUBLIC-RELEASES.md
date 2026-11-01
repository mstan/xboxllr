# Public Windows alpha/beta releases

XboxLLR is beta. Blinx's playable public alpha is tested through World 2;
end-to-end completion is not guaranteed. Blinx is the only publicly supported
game. One XboxLLR beta release contains both the Dashboard player ZIP and the build-tool
ZIP. Blinx keeps its separate game release.

Commit clean source first. Use separate output directories outside checkouts,
build and validate the native applications, then run the scripts below. They
refuse existing outputs and never package player state. No script changes GitHub
visibility or publishes automatically. Publish only the verified `assets/`
contents, never staging/build/QA directories.

## One XboxLLR release

Use **tools/release_xboxllr.py** to stage both components and combine their
verified assets and checksums in one output directory. It accepts the common
Dashboard arguments below plus `--tool-launcher <xboxllr.exe>` and
`--capstone-wheel <pinned-wheel>`. Use `--help` for the complete interface.
It packages the already validated Dashboard executable and build-tool EXE;
it does not compile or run games. Publish the combined `assets/` directory to
one `beta-<version>` prerelease. The two component packagers below remain useful
for development and are called by the combined command.

## Dashboard component

```powershell
python tools/release_dashboard.py `
  --version 2026.10.29.2 `
  --runtime-framework-revision <full-framework-commit> `
  --runtime-source-revision <full-framework-commit> `
  --runtime D:\Build\dashboard\native-title\Release\XboxDashboard.exe `
  --cache D:\Build\dashboard\CMakeCache.txt `
  --launcher D:\Build\launcher\Release\xlle_launcher.exe `
  --boot-intro D:\Build\boot\Release\xboxllr_boot_intro.exe `
  --embed D:\Inputs\python-3.13.17-embed-amd64.zip `
  --runtime-dir D:\Inputs\reviewed-msvc-redist-x64 `
  --renderer D:\Build\prepared-d3d --ui D:\Build\prepared-ui `
  --dsp D:\Build\prepared-dsp `
  --notes docs/RELEASE-NOTES-BETA-2026.10.29.2.md `
  --output D:\Release\xboxllr-beta-2026.10.29.2
```

Supply additional `--runtime-dir` arguments for declared runtime DLL directories
when they are not adjacent to the executables. The checked DLL closure includes
SDL2 and the storage library. The script exports exact source and prepared
renderer/UI/DSP source, stages a pristine launcher/runtime package, attaches
corresponding source inside it, verifies the archive and writes SHA256SUMS.txt.
Users still supply Dashboard 5960 and its complete asset tree.

## Build tool component

Build `tools/windows-build-launcher` as described in [BUILD-TOOL](BUILD-TOOL.md).
Use an official Python embeddable archive and the pinned PyPI Capstone wheel:

```powershell
python -m pip download --only-binary=:all: --no-deps --platform win_amd64 --dest D:\Inputs capstone==5.0.9
python tools/release_toolkit.py `
  --version 2026.10.29.2 --launcher D:\Build\xboxllr-tool\Release\xboxllr.exe `
  --embed D:\Inputs\python-3.13.17-embed-amd64.zip `
  --capstone-wheel D:\Inputs\capstone-5.0.9-py3-none-win_amd64.whl `
  --output D:\Release\toolkit-beta-2026.10.29.2
```

The archive contains xboxllr.exe, embedded Python/Capstone, their notices, the
exact framework source snapshot and a hashed toolkit manifest. MSVC/CMake/Git
remain developer prerequisites. The toolkit is separate from the playable
Dashboard and does not contain original Xbox files.

## Blinx

Use `tools/release_title.py` with the same common arguments, plus
`--repo D:\Dev\BlinxRecomp --definition game.toml --name BlinxRecomp` and its
validated runtime/cache and alpha release notes. Initialize the title's pinned
framework submodule before exporting recursive source. Its gameplay profile
enables the RAM utility cache; cheats remain selectable and off by default.
Record the compiled runtime's exact framework and title source hashes with the
two `--runtime-*-revision` options. Packaging/docs-only follow-ups can differ;
the provenance must preserve that distinction.

## Validate and publish

Keep archive inventories, source/binary identities and smoke-test evidence.
Extract a disposable copy; verify the bundled interpreter, EXE commands and
normal first-run/profile/game flow without changing native saves. Unattended
games must be hidden and muted. Never run the pristine staging tree.

Create a GitHub draft prerelease at the exact committed source, upload only the
asset files, compare the returned GitHub asset SHA-256 digests with SHA256SUMS.txt,
then publish. Use one XboxLLR tag `beta-<version>` for both components,
and Blinx's separate `alpha-<version>`. Keep alpha limitations
and user-supplied-input instructions in the release body. Verify unauthenticated
public access after publication. Existing private-preview history is historical.
