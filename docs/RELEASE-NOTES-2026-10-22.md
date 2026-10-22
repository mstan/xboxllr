# Windows preview candidate — October 22, 2026

Applies to xboxllr's Dashboard title, BlinxRecomp and JSRFRecomp. The release
contains **GPL source/tooling**, including exact pinned framework source in the
game archives. All three repos remain private. Windows is the tested native
host; Linux is deferred. No precompiled original-title executable is attached.

The owner selected GPL-3.0-or-later for owner-original contributions on October
22. Third-party terms are retained. This supersedes the former no-GPL shipping
restriction and removes the policy requirement to replace the Phobos renderer.

## What changed

- One shared self-contained packaging path, Xbox-themed launcher and Player 1
  controller selection across all titles. JSRF has its actual North American
  cover; Dashboard has the native Home capture. Artwork retains its own rights
  notices.
- Once-only optional console setup. **Skip** uses a new local identity and
  automatic private per-game save folders. Console imports live in Settings;
  select the supported extracted XBE on the main screen. No automatic 8 GB disk.
- Optional existing raw/QCOW2 FATX images use the same block owner as LLE IDE.
  Images require validated signing inputs and matching EEPROM. An image takes
  priority over folders; invalid or busy images fail visibly. Preserve the disk
  and its `<image>.xboxllr` identity directory.
- Shared mod-plan delivery fixes Blinx settings being ignored. Game defaults
  are adaptive Fit to screen, a 16:9 window, 1080p, 2x2 SSAA, FXAA, 8x filtering
  and blended 60 FPS presentation. Simulation keeps its original cadence.
- Dashboard uses the shared quality/presentation registry. FXAA, filtering and
  interpolation are available; higher internal resolution and SSAA remain
  hidden until its Gaussian resolve path supports them correctly.
- Clean game-name window captions. Shared archives verify manifest hashes and
  reject additional files, links, owned images and used profiles.

## Evidence and limits

Fresh staged packages passed embedded-host/import closure and manifest checks.
Serial muted 30-second native runs of all three titles activated the intended
mod plans; both games also passed a resize to 1680x720. Prior assisted campaign,
native save/load and owner visual evidence is retained. This batch does not
claim exhaustive gameplay, a new campaign pass, or full new virtual-identity
and per-title image-backed save/restart/load qualification.

Direct ISO import, Linux, general kernel-dump admission, populated imported
Xbox Live cache records and Dashboard Gaussian upscaling remain deferred.
Xbox Live is unavailable. No firmware boot runs in these launchers. Its
follow-up target is default off for games and on for Dashboard if qualified.

## Release contents and local binaries

Source kits contain framework/title source, mods, build tools, documentation and
notices, with commit/hash manifests. Build the native title locally with your
own supported game and the documented toolchain.

Existing combined title executables contain code translated from the original
XBE. Our GPL grant does not license those original bodies, and repository input
rules keep them local. They are not included in this source/tooling release.
See [license scope](../LICENSE-SCOPE.md) and [release readiness](RELEASE-READINESS.md).

No original game/firmware/Dashboard files, HDDs, EEPROM, signing inputs, saved
profiles, native saves or generated original source are in the release archives.
