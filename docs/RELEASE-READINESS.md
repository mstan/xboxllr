# xboxllr release readiness

## Public beta/alpha — October 29, 2026

The owner explicitly authorized public XboxLLR and Blinx releases. XboxLLR is
beta; Blinx is the only publicly supported game and its playable alpha is tested
through World 2, with no end-to-end guarantee. JSRF remains outside public support.
Separate Dashboard and build-tool EXE releases, corresponding-source bundles,
screenshots, setup and repeatable scripts are documented in [Public releases](PUBLIC-RELEASES.md).
The build toolkit now includes a real `xboxllr.exe` entry point plus bundled
Python/Capstone; MSVC, CMake and prepared title profiles remain required.

## Historical private previews


Controller hotfix updated October 23, 2026. Tracking: `beads-s7mu`;
initial preparation `.1.187`; licensing `.1.21`.
At that milestone all three repositories were private. The owner selected GPL distribution and
authorized playable Windows prereleases. Native Linux releases remain deferred.

| Repository | Windows preview and source downloads |
| --- | --- |
| [xboxllr](https://github.com/mstan/xboxllr/releases/tag/windows-preview-2026-10-23.2) | Recompiler/framework source toolkit, native Xbox Dashboard Windows application, source kit and prepared dependency-source supplement |
| [BlinxRecomp](https://github.com/mstan/BlinxRecomp/releases/tag/windows-preview-2026-10-23.2) | Blinx Windows application, recursively pinned source kit and prepared dependency-source supplement |
| [JSRFRecomp](https://github.com/mstan/JSRFRecomp/releases/tag/windows-preview-2026-10-23.2) | JSRF Windows application, recursively pinned source kit and prepared dependency-source supplement |

## Completed release preparation

- End-user feature pages, real screenshots/artwork, credits and setup/developer guides.
- Shared optimized native services, Player 1 selection and Xbox-themed launcher.
- Once-only optional console setup and kernel-free local identities; automatic
  private per-game save folders, optional shared folders or existing raw/QCOW2
  FATX images. No automatic 8 GB disk image. Invalid/busy selected disks fail
  visibly rather than silently changing storage.
- Shared visual-mod delivery, adaptive game presentation, correct title captions
  and clean package manifests. Dashboard retains its documented resolve limits.
- GPL project grant with retained third-party terms, full license texts and an
  explicit original-rightsholder notice. The notice creates no license exception.
- Source kits include exact framework pins. Prepared dependency source and build
  provenance accompany the Windows downloads. Original title inputs stay separate.
- Dashboard's full native startup now uses the title's rendering window, retaining
  position and size through the handoff. Full startup defaults on for Dashboard;
  the skip-animation mod defaults off. Startup assets come from a compatible
  user-owned complete kernel dump. New packages open through `Start Game.exe`
  without a developer console. Blinx camera mods are excluded from Dashboard.

The Dashboard-only October 23 refresh has owner acceptance, a real native
startup-to-menu/navigation/normal-close check, four native startup fixtures,
and 61 passing Python regression tests (one additional private-input skip).
Blinx and JSRF builds, pins and releases are unchanged by this refresh.

The subsequent shared controller hotfix updates all three native applications.
SDL's Windows notification thread now processes RawInput messages even though
the native renderer does not initialize SDL video. A controller could previously
appear connected while its buttons and sticks remained zero. The fix is enabled
in the executable, preserves selected controller GUIDs and existing console/save
profiles, and requires no Steam or global environment changes. The real Windows
message-dispatch regression fails on the old transport and passes with the fix;
physical same-GUID samples and the focused virtual transport/host tests cover
the input correction. See [release notes](RELEASE-NOTES-2026-10-23.2.md).

## Packaging and evidence

`tools/native_release_package.py` stages pristine self-contained Windows
applications with the launcher, embedded Python, verified DLL closure, notices
and data-only mods. `tools/native_release_archive.py` verifies manifest hashes,
rejects extra files/links and original-input extensions, and refuses overwrite.
`tools/source_release_package.py` exports exact Git source and recursive pins.
Use [Development](DEVELOPMENT.md) for the build and
[first-run packaging](NATIVE-FIRST-RUN-PREVIEW.md) for the package layout.

Accepted native gameplay binaries are retained for this packaging-only update.
Release manifests distinguish their original build inputs from the source
revision supplying current notices and packaging. No license change is presented
as a new gameplay qualification pass. Earlier serial muted native runs covered
all three titles, the expected mod plans, and game resizing to 1680x720.
Existing owner reviews, assisted campaigns and original-save evidence remain
accepted with their recorded bypasses. Uploaded artifact checksums are verified.

Windows apps contain translated execution code; users supply original data and
assets. Source generation uses their own original input. See
[license scope](../LICENSE-SCOPE.md) and [rights notice](../RIGHTS-NOTICE.md).
Our source license and notice do not grant Microsoft/Sega permission.

## Deferred, without blocking this Windows preview

Native Linux host, general firmware admission, complete fresh
virtual-profile and per-title image-backed save matrices, populated imported
Xbox Live cache (`.1.188`), Dashboard Gaussian upscaling (`.1.189`) and supporting
startup hardware/audio precision (`.1.195`). Native startup presentation is
available; full firmware boot is separate. Xbox Live is unavailable. Cheat-assisted coverage is not
exhaustive natural-mechanics qualification. Legacy full-system binaries are not
included. The older previews provided source/Python tooling; the public beta adds the
EXE entry point described above.

Native ISO/XISO selection and a reusable extracted cache are implemented; see
[disc import](DISC-IMPORT.md) and `beads-xbox.1.187.10` for qualification.
