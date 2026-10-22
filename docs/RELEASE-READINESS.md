# xboxllr release readiness

Updated October 22, 2026. Tracking: `beads-xbox.1.187`; licensing `.1.21`.
All three repositories remain private. The owner selected GPL distribution and
authorized playable Windows prereleases. Native Linux releases remain deferred.

| Repository | Windows preview and source downloads |
| --- | --- |
| [xboxllr](https://github.com/mstan/xboxllr/releases/tag/windows-preview-2026-10-22) | Recompiler/framework source toolkit, native Xbox Dashboard Windows application, source kit and prepared dependency-source supplement |
| [BlinxRecomp](https://github.com/mstan/BlinxRecomp/releases/tag/windows-preview-2026-10-22) | Blinx Windows application, recursively pinned source kit and prepared dependency-source supplement |
| [JSRFRecomp](https://github.com/mstan/JSRFRecomp/releases/tag/windows-preview-2026-10-22) | JSRF Windows application, recursively pinned source kit and prepared dependency-source supplement |

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

Direct ISO import, native Linux host, general firmware admission, complete fresh
virtual-profile and per-title image-backed save matrices, populated imported
Xbox Live cache (`.1.188`), Dashboard Gaussian upscaling (`.1.189`) and firmware
startup (`.1.190`). Xbox Live is unavailable. Cheat-assisted coverage is not
exhaustive natural-mechanics qualification. Legacy full-system binaries are not
included. The toolkit is source/Python tooling, not a standalone compiler EXE.
