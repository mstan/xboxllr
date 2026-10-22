# xboxllr release readiness

Updated October 22, 2026. Tracking: `beads-xbox.1.187`; licensing `.1.21`.

The owner explicitly selected GPL distribution on October 22. Owner-original
source in xboxllr, BlinxRecomp and JSRFRecomp is GPL-3.0-or-later. The former
no-GPL shipping restriction is superseded. The existing Phobos renderer can
remain under its retained GPL terms; a replacement or personal exception is
not required merely to release our software under GPL.

## Current release

All three repositories remain private. The October 22 release is a **GPL
source/tooling preview**. Native Windows behavior has owner playtest and bounded
integration evidence; native Linux releases remain deferred.

| Repository | Release contents |
| --- | --- |
| [xboxllr](https://github.com/mstan/xboxllr/releases/tag/preview-2026-10-22) | Framework/recompiler source kit and a separate Dashboard source-kit asset using the same shared framework |
| [BlinxRecomp](https://github.com/mstan/BlinxRecomp/releases/tag/preview-2026-10-22) | Title metadata, mods, build scripts, documentation and exact pinned framework source |
| [JSRFRecomp](https://github.com/mstan/JSRFRecomp/releases/tag/preview-2026-10-22) | Title metadata, mods, build scripts, documentation, sourced launcher cover and exact pinned framework source |

The archives do not contain precompiled title executables. Those executables
statically include original code translated from the supplied XBE; that code
is not licensed by our owner's GPL grant and remains private under the existing
input rules. Omitting it from a corresponding-source bundle is not clearance
for publishing the combined executable. This is a distinct content boundary,
not the former GPL-renderer policy conflict. See [license scope](../LICENSE-SCOPE.md).

## Completed source-release work

- End-user feature pages, actual screenshots, credits and developer/setup guides.
- Real JSRF cover and native Dashboard Home artwork, with separate rights notices.
- Shared optional first-launch setup, kernel-free local profiles, automatic
  per-game save folders and optional raw/QCOW2 FATX disk images. No automatic
  8 GB image; selected invalid or busy images do not fall back silently.
- Shared visual-mod transport and clean window captions. Adaptive/quality mods
  activate in Blinx and JSRF; Dashboard retains its qualified presentation limits.
- GPL license/grant/scope updates with preserved third-party copyrights/notices.
- Shared source archive tool: exact commit and recursive pinned submodule source,
  per-file hashes, reviewed images and rejection of private/generated input data.

## Build and package flow

Use [Development](DEVELOPMENT.md) for prerequisites, exact dependency preparation,
owned title inputs, native Windows profiles and separate reference builds.
Game code is generated and compiled locally from the user's admitted original.
Toolchain/source requirements remain developer-oriented in this preview.

`tools/source_release_package.py` creates the source release from Git objects;
untracked/dirty files are never taken as release contents. Initialized submodules
are exported at the exact gitlink commit, not whatever HEAD happens to show.
It hashes and verifies the resulting archive, rejects private input extensions,
compiled outputs and generated-original markers, and refuses overwrite. The
existing exact-hash documentation artwork list applies; title-specific reviewed
images are admitted explicitly by `--image relative/path=sha256`.

```powershell
python tools/source_release_package.py --repo <source-repo> --output <new-source.zip>
```

The [native package stager](NATIVE-FIRST-RUN-PREVIEW.md) separately creates local
owner-use Windows candidates with embedded Python, frontend and DLL closure.
Those remain useful for QA but are not assets of this source-only release.

## Evidence and retained limits

The preceding integration batch passed 21 profile/archive tests, six compiled
launcher-provider checks, four visual resolver checks and six hygiene guards.
Serial muted 30-second native runs of all three titles activated the expected
mod plans; both games also passed an actual resize to 1680x720. Prior assisted
campaign, original-save and owner visual reviews remain accepted with their
documented bypasses. No full campaign repeat is required for a license change.

The source archive tool has authored tests for exact nested pins, exclusion of
local dirty/untracked content, original/translated payload rejection and output
preservation. Final release records carry exact commits and SHA256 checksums.

Deferred/unqualified: direct ISO import, native Linux host, general firmware
admission, fresh virtual-profile and full per-title image-backed save matrices,
populated imported Xbox Live cache (`.1.188`), Dashboard Gaussian upscaling
(`.1.189`) and firmware startup (`.1.190`). Xbox Live remains unavailable.

## Next distributable binary milestone

Separate the redistributable runtime/tooling from original-title generated code,
or supply an appropriate local-generation/build distribution. Verify exact
corresponding source, dependency terms, reconstruction and clean-profile setup
for that artifact before labeling it a downloadable playable game. A root GPL
license does not confer rights over an original Xbox game or firmware.
