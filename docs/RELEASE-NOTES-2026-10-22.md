# Windows preview - October 22, 2026

Precompiled Windows x64 applications are available for Xbox Dashboard, Blinx
and JSRF, alongside GPL project source and a prepared dependency-source
supplement. The xboxllr toolkit is distributed as source/Python tools. All three
repositories remain private; Linux is deferred. You supply your own supported
original game or Dashboard files. No original media or console dumps are bundled.

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

## Release contents and rights

Download the Windows ZIP for the title, extract it to a writable folder and open
`Start Game.cmd`. Use **Skip** for the optional console setup, then select your
supported extracted XBE on the main screen and keep its complete asset folder.
No compiler is needed to run a prebuilt Windows title download.

Project contributions are GPL-3.0-or-later; incorporated components retain their
terms. Original software, artwork and trademarks remain the property of their
respective rights holders, with all original rights reserved. The notice is not
permission from those owners or an exception to the GPL. Native executables
include translated execution code; original-title source bodies are regenerated
from the user's input. See [rights notice](../RIGHTS-NOTICE.md),
[license scope](../LICENSE-SCOPE.md) and [release readiness](RELEASE-READINESS.md).

Source kits and the dependency supplement preserve source/build information and
notices. No original XBE/disc/firmware/HDD/EEPROM, signing keys, saves or used
profiles are included. Preserve your existing data and selected save storage
when updating; do not overwrite or discard native progress.
