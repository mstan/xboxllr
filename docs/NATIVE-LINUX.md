# Native Linux titles and AppImages

The Linux title host builds x86_64 ELF executables from the same generated game
code, native guest services, Phobos renderer and DSP graph used on Windows.
It uses POSIX host services, SDL2 windows/input, DXVK Native D3D11 on Vulkan,
FAudio and the native DSP56300 library. No Wine or Proton runtime is used.

The current Linux configuration is native scheduling, memory, files, display,
input and audio; HLE calendar; IO/USB off. Windows reference/LLE configurations
remain available. The optional Xbox firmware startup animation and GDI debug
overlay are unavailable on Linux. Game startup movies remain game content.
Native debug input and presented screenshots use the existing TCP interface.

## Build

Use a native Linux filesystem for build output and AppDir staging. WSL is a
supported build environment; keep AppDir outside `/mnt` so permissions and
library symlinks survive. These builds use Ubuntu 24.04's toolchain/glibc;
older distributions require rebuilding against their supported baseline.

Install a C/C++ toolchain, CMake, Ninja, Python 3.11+, SDL2 2.24+ development
files, OpenSSL development files and FAudio development/runtime files. Ubuntu's
FAudio package also requires its `libstb` runtime dependency.

Prepare the existing reviewed renderer and DSP dependencies using the shared
tools, then prepare a separate Linux renderer:

```sh
python3 tools/native_phobos_linux.py --source /path/to/prepared-phobos --output /path/to/phobos-linux
```

The adapter validates the prepared input hashes and retains donor notices and
an exact modification manifest. CMake validates that manifest on configuration.
Use the native DXVK 2.6.2 SDK (`dxvk-native-2.6.2-steamrt-sniper.tar.gz`, SHA-256
`6bdb955ff3c272f8a7da06a6ae89a636d03c65564e23eba47689009ee1064a50`) and
native vkd3d 2.1 with `libvkd3d-utils`. The native shader adapter converts the
compiler's Microsoft x64 ABI and blob objects to DXVK's System V interfaces;
it also supplies IEEE-754 NaN/infinity classification missing from that HLSL
compiler. Its real shader compilation/error tests run under CTest.

Build the pinned DSP source (`c60aeedb6a6014850204c71e53bc0cf5b3104222`) with
Rust 1.98.0 and its lockfile, selecting `dsp56300-emu-ffi`. Supply the resulting
`libdsp56300_emu_ffi.a`, not a Windows `.lib`.

Private title path files accept `d3d_source`, `dsp_source`, `dsp_library`,
`dxvk_native`, `vkd3d` and `faudio`. The last three are installation prefixes
containing `include`/`lib` (FAudio's Debian multiarch layout is supported).
SDL2 can be discovered from the system or selected with `sdl2_dir`.

Use each title's normal preparation/build driver with its owned input and
reviewed generation proofs. JSRF uses the shared `tools/native_title.py` driver
with Ninja. Blinx uses `tools/game.py configure/build --mode play` and the same
Linux dependency keys. The title/framework pins must be updated together.
Generated game bodies and original inputs stay private.

Build the shared SDL launcher with `pilots/native-launcher` and the prepared,
pinned recomp-ui source (`XLLE_UI_SOURCE`). Its native controller handoff test
takes a disposable state-file path as its only argument.

## Package

First stage the built ELF payload with `tools/native_linux_package.py`:

```sh
python3 tools/native_linux_package.py --definition /title/native-title.toml \
  --runtime /build/native-title/Game --cache /build/CMakeCache.txt \
  --launcher /launcher-build/xlle_launcher \
  --storage /build/native-title/storage/libxboxllr_storage.so --output /scratch/payload \
  --source-archive /sources/title-source.zip \
  --source-archive /sources/prepared-dependencies.zip \
  --dependency-notices /sources/native-dependency-notices
```

Blinx uses `game.toml` and its `framework/` build subdirectory. For JSRF, use
the default rev4 definition and pass `--revision usa-standalone RUNTIME CACHE`
for the separately built USA executable. Each declared revision is required.
The package selects the original revision by XBE hash and keeps its console
profile/saves separate. Use USA for representative runtime/performance checks;
rev4 needs targeted correctness coverage.

Create the exact tracked project source kit with `source_release_package.py`
after committing the framework and updating the title's framework gitlink/pin.
Use `native_release_sources.py` for the receipt-verified prepared renderer, UI
and DSP source. Both archives are included in the AppImage. Supply retained
copyright/license notices and exact source package versions for the native
library closure, including DXVK Native, vkd3d, FAudio and libstb.

Then build the AppImage, supplying the runtime dependency search directories:

```sh
bash tools/package_appimage.sh --payload /scratch/payload \
  --output /delivery/Game-linux-x86_64.AppImage --stage /scratch/new-appdir \
  --tools /scratch/appimage-tools --library-dir /dxvk/usr/lib \
  --library-dir /vkd3d/lib --library-dir /faudio/lib/x86_64-linux-gnu \
  --library-dir /build/native-title/storage
```

The packager pins and verifies linuxdeploy, appimagetool and bundled CPython.
It deploys the native library closure; host Vulkan/GPU drivers remain system
requirements. Retain the dependencies' exact source/license notices alongside
the project notices and source. Original games, generated title sources,
firmware, console keys and player data must never enter the payload.

AppRun keeps the immutable image mounted for the complete launcher/game
session and seeds managed application files into
`${XDG_DATA_HOME:-~/.local/share}/xboxllr/xbox-<title-id>/`. Player data is under
its `data/` directory. A per-game lock protects both replacement and execution.
Upgrades replace managed code while preserving console identities, settings,
imported disc files, native saves and unknown user files. External chosen save
folders and HDD images stay in place. Keep these data directories across updates.

`--verify-package` validates the packaged payload. `--prepare-only --image XBE`
prepares a disposable private console for unattended QA. FUSE-less hosts can
use the AppImage runtime's `--appimage-extract-and-run` fallback. Verification
or successful compilation alone does not qualify gameplay: retain actual
rendered/input/audio and native save/relaunch evidence for each delivery.
