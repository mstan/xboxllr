> **BETA:** XboxLLR is an early Xbox recompiler ecosystem. Expect bugs, incomplete hardware coverage and unsupported software. **Blinx is the only publicly supported game at this time.** Please [file reproducible GitHub issues](https://github.com/mstan/xboxllr/issues).

# XboxLLR

**Xbox Low Level Recompiler** is an early, low-level original Xbox recompilation
ecosystem: an ahead-of-time x86 recompiler pipeline, reusable console services,
native title host, Xbox-themed launcher and tools for preparing and building
individual titles. It was previously called **xboxlle**; some internal names
and checkout directories retain that spelling.

XboxLLR establishes console behavior through low-level devices and original-code
reference paths, then supplies native implementations under those contracts.
The fast Windows title profiles execute translated CPU code ahead of time.
Unresolved targets stop with diagnostics. This is ongoing compatibility work;
the downloads do not run arbitrary Xbox games.

## Downloads

**[Download XboxLLR beta](https://github.com/mstan/xboxllr/releases/tag/beta-2026.10.29.2)** — one release contains both the
Dashboard app and the developer build tool. Choose the ZIP you need below.

| Download | Who it is for |
| --- | --- |
| **[Xbox Dashboard beta — Windows x64](https://github.com/mstan/xboxllr/releases/tag/beta-2026.10.29.2)** | Player app: download `XboxDashboard-windows-x64-2026.10.29.2.zip`, extract it, then open **Start Game.exe**. Supply your own Dashboard 5960 executable and assets. No compiler required. |
| **[XboxLLR build tool beta — Windows x64](https://github.com/mstan/xboxllr/releases/tag/beta-2026.10.29.2)** | Developer package: `xboxllr-build-tool-windows-x64-2026.10.29.2.zip` contains `xboxllr.exe`, bundled Python/Capstone, framework source and build drivers. MSVC, CMake and prepared title inputs are still required. [Build-tool guide](docs/BUILD-TOOL.md). |
| **[Blinx playable public alpha](https://github.com/mstan/BlinxRecomp/releases/tag/alpha-2026.10.29.1)** | Players: a ready-to-run native game package. No compiler required; supply your own supported NTSC-U game dump. |

Current playable downloads target **Windows x64 with D3D11**. Linux component
checks do not establish a working Linux game release. JSRF and other experimental
titles are not publicly supported in this beta.

## Blinx: The Time Sweeper

[BlinxRecomp](https://github.com/mstan/BlinxRecomp) is the first publicly supported
game. **Tested through World 2; end-to-end completion is not guaranteed.** Please
[report game bugs in BlinxRecomp](https://github.com/mstan/BlinxRecomp/issues).

![Blinx gameplay with adaptive widescreen and edge-anchored HUD](docs/screenshots/blinx-widescreen.jpg)

![Blinx native gameplay in a 16:9 window](docs/screenshots/blinx-alpha-16x9.png)

The game package includes adaptive **Fit to screen** through 32:9, internal
resolution up to 2160p, FXAA, supersampling, anisotropic filtering, controller
selection, camera-invert options and optional blended 60/120 Hz presentation.
Blending preserves original simulation timing; it can add double edges and
latency. Actual higher-rate simulation is not currently available.

Optional gameplay mods remove the **ten-minute stage limit**, expand time-control
stock to 20, or provide infinite controls with non-heart crystals suppressed.
Those cheats are independent and off by default; heart crystals are preserved.
The disposable utility cache now stays in RAM, allocated on demand within a
750 MiB volume. Native in-game saves stay on persistent storage.
See [Blinx features and mods](https://github.com/mstan/BlinxRecomp#features).

## Original Xbox Dashboard

![Original Xbox Dashboard Home menu running in the native host](docs/screenshots/dashboard.png)

![Original Xbox Dashboard Settings screen running in the native host](docs/screenshots/dashboard-settings.png)

The native Dashboard preview supports Home, Memory, Music and Settings, local
console settings, persistent state and Credits/System Info screens. Xbox Live
stays offline. Launch it like the game packages: **Start Game.exe** opens the launcher,
then starts the native Dashboard app. It does not launch arbitrary Xbox games.

### Dashboard support

“Tested” means the listed action was exercised in the native Dashboard; it is
not a claim that every submenu, device or hardware setting is complete.

| Feature | Status | What works / remaining limit |
| --- | --- | --- |
| Dashboard 5960 startup and Home | **Tested** | Native launch, controller navigation and normal close. Matching executable and complete assets required. |
| Player 1 input | **Tested** | Launcher controller selection and keyboard support; shared Windows controller event-pump fix included. |
| Video settings | **Tested** | Normal / Letterbox / Wide Screen selection, visible status and saved user settings. |
| Language and audio settings | **Tested** | Language changes; Mono / Stereo / Dolby Surround selection and persistence. This does not establish surround-output fidelity. |
| Parental controls | **Tested** | Rating settings and passcode set/verify, including a fresh-process passcode check. |
| Timezone and daylight savings | **Tested** | User-setting changes persist. |
| Clock / date Apply | **Partial** | Changes the console clock for the session. Persistent RTC behavior across restarts is not qualified. Host clock is untouched. |
| Auto Off | **Partial** | Enabled/Disabled setting saves. The actual six-hour shutdown was not exercised. |
| System Info and Credits | **Tested** | Original info panel; custom scrolling Credits/notices under Settings, with Back/reopening. |
| Memory | **Partial** | Menu opens and returns. End-to-end save copying/deletion and memory-unit workflows are not qualified. |
| Music | **Partial** | Menu opens and returns. Soundtrack management, CD playback and ripping are not qualified. |
| Menu audio | **Partial** | Native audio output and nonzero stereo samples verified. Subjective fidelity/listening coverage remains limited. |
| Save storage / existing HDD images | **Partial** | Local profiles and persistent user settings work. Supported raw/QCOW2 import has identity/layout checks; the complete Dashboard save-management matrix is not qualified. |
| Network Settings / Auto Sign-In | **Offline only** | Native offline notice and Back work. Original XoDash network configuration and Xbox Live are unsupported. |
| Physical optical drives / USB memory units | **Unsupported** | The beta uses an empty optical-drive policy and does not provide these hardware devices. |
| Launch arbitrary games / full firmware boot | **Unsupported** | Dashboard is its own native port. Optional startup animation is a reconstruction, not firmware execution. |
| Visual precision and smoothness | **Partial** | Main menus are usable. Some menu-entry hitches and graphics precision questions remain open. |
| Native Linux app | **Not released** | Playable beta targets Windows x64 / D3D11. |

[Dashboard details and coverage](docs/DASHBOARD.md). Please
[report reproducible failures](https://github.com/mstan/xboxllr/issues), including
the release version and the menu/action that failed.

### Load the Dashboard

1. Download the **Dashboard** Windows ZIP above and extract the entire package
   into a writable folder. Open **Start Game.exe**.
2. Supply your own original **Dashboard 5960** backup: `xboxdash.xbe` and its
   complete related asset tree from the Xbox C: partition. Preserve names and
   layout. A homebrew dashboard or the executable alone is insufficient.
3. Choose **Skip** in the optional console-files setup to create a new local
   console identity and save folders. No BIOS, EEPROM or large HDD image is
   required for direct native Dashboard launch.
4. On the main launcher screen, select that `xboxdash.xbe`. Select your
   controller in **Player 1**, then start. [Exact supported hash and setup details](docs/SETUP.md#dashboard-beta-quick-start).

The optional Xbox startup animation is on by default for Dashboard. It needs
startup data prepared from your compatible complete kernel dump. Without that
optional input, the title launches directly. This is a native reconstruction
of the animation, not a full firmware boot. Settings can import original console
files or select an existing supported raw/QCOW2 FATX image. Keep the package's
`data` folder, external save storage and any `<image>.xboxllr` profile when updating.

## Development and releases

Start with [the documentation index](docs/README.md),
[architecture](docs/ARCHITECTURE.md), [developer setup](docs/DEVELOPMENT.md), the
[native-title driver](docs/NATIVE-TITLE-DRIVER.md) and [build-tool guide](docs/BUILD-TOOL.md).
Title repositories own revision metadata, mods and their tested framework pin;
shared CPU, graphics, audio, input, kernel and storage services belong here.
The older whole-console reference route has its own
[setup guide](docs/LEGACY-FULL-SYSTEM-SETUP.md).

One release command stages both the Dashboard and build-tool archives for the
same GitHub beta release: [`release_xboxllr.py`](tools/release_xboxllr.py).
The component scripts remain available:
[`release_dashboard.py`](tools/release_dashboard.py),
[`release_toolkit.py`](tools/release_toolkit.py), and the shared
[`release_title.py`](tools/release_title.py) for Blinx.
See [public release instructions](docs/PUBLIC-RELEASES.md). Each playable ZIP
includes its corresponding project and prepared dependency source. Original
title bodies are regenerated privately from the user's matching input.

For failures, include the release version, Windows/GPU details, selected mods
and reproducible steps in a [GitHub issue](https://github.com/mstan/xboxllr/issues).
The launcher retains [crash diagnostics](docs/TESTER-DIAGNOSTICS.md). Review
attachments before sharing; never upload game files, keys, EEPROMs or saves
containing private console material.

## Credits and license

Development and integration: **Matthew Stanley**. Thanks to
[sp00nz / sp00nznet](https://github.com/sp00nznet/xboxrecomp),
[Phobos](https://github.com/phobos665/DirectXbox-Recomp),
[NoRain](https://github.com/NoRain211/doaxbv-re),
[xemu contributors](https://github.com/xemu-project/xemu) and
[apexJCL](https://github.com/apexJCL/blinx1-recomp).
The RAM-cache design was inspired by apexJCL's Blinx work; the implementation
here uses XboxLLR's own FATX storage. See [Credits](CREDITS.md) and
[exact component attribution](THIRD_PARTY_ATTRIBUTION.md).

Owner-original source uses [GPL version 3 or later](LICENSE), subject to
[license scope](LICENSE-SCOPE.md). Third-party work keeps its own terms.
Users supply original game and Dashboard data; no firmware, media, keys or
personal saves are bundled. [Original rights notice](RIGHTS-NOTICE.md).
