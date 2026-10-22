# Native Windows first-run previews

Tracking: `beads-xbox.1.187.7`, October 21, 2026. Shared owner-use packaging for
Blinx, JSRF and Dashboard lives in xboxllr. It is separate from publication and
is not the [GPL source/tooling release](RELEASE-READINESS.md). Combined
original-title code remains local; the former no-GPL policy is superseded.

`tools/native_release_package.py` accepts title metadata, an optimized native
runtime, its CMake cache, the common recomp-ui launcher, a reviewed official
embedded-Python archive and explicitly declared Windows runtime directories.
It checks ordinary and delay-import closure, then copies the selected binaries,
launcher assets, bundled data-only mod manifests, Python modules and notices
to a new destination. It refuses to replace an existing destination.

```powershell
python tools/native_release_package.py `
  --definition <title-metadata.toml> --runtime <native-game.exe> `
  --cache <native-build/CMakeCache.txt> --launcher <xlle_launcher.exe> `
  --embed <reviewed-python-embeddable-amd64.zip> `
  --runtime-dir <reviewed-runtime-dll-directory> --output <new-preview-directory>
```

The same tool accepts Blinx's legacy `game.toml` and the generic native-title
metadata used by JSRF and Dashboard. Compiled title-mod capability determines
which bundled visual controls are available. The launcher discovers title
artwork at runtime, so a separate frontend compilation per game is unnecessary.

Archive only a pristine staged directory:

```powershell
python tools/native_release_archive.py --source <new-preview-directory> --output <new-windows-x64.zip>
```

This verifies every manifest hash and the resulting ZIP, rejects additional
files/links and owned-image extensions, and refuses to overwrite an archive.
Do not archive an owner review folder after running it. Archive creation does
not resolve the original-title content boundary in `LICENSE-SCOPE.md`.

`Start Game.cmd` runs the bundled Python host. `release.json` records package
hashes, native runtime settings and title identity. This detects accidental
changes; it is not a signed update mechanism. The host redirects process-local
TEMP, TMP, APPDATA and LOCALAPPDATA beneath `data` before starting the launcher
or title. An exclusive session lock prevents concurrent profile writers.
Unfocused owner launchers use reduced redraw frequency. Only the explicit
`--start-minimized` handoff option starts a launcher minimized.

First run asks once about optional original-console files. Skipping immediately
persists acknowledgement, then leaves game/XBE selection on the main screen.
Advanced console inputs are under Settings. Default setup creates a new
authenticated EEPROM and persistent virtual signing identity without a kernel
dump. Local saves use real HMAC/SHA verification under that identity; there is
no zero-key or accept-all path and no claim of original-save/key equivalence.
Optional original-compatible setup retains the admitted kernel-derived signing
producer and authenticated EEPROM import.
A profile becomes usable only after all required inputs are validated and
written. Existing identities/native progress are never replaced. Setup does
not execute the original kernel or require Unicorn. Virtual and original
identities differ; an unmodified original LLE kernel does not become compatible
with local virtual keys merely by sharing their ABI.

The optional designation appears once in the console-settings section heading.
Selecting an HDD image or save folder disables the other field; **Clear** lets
the user switch. An existing legacy selection containing both retains image
priority. Empty storage fields use automatic private per-game folders.

Title metadata can explicitly select enhancement defaults without changing the
bundled mod packages' default-off policy. Saved user feature selections win.
Blinx defaults to adaptive **Fit to screen**, initially 16:9 at 1280×720,
1080p internal rendering, 2×2 supersampling, FXAA, 8× anisotropic filtering and
60 FPS blended presentation interpolation. Actual simulation speedup remains
unavailable. Users can change these in Mods; interpolation
retains the original update rate and can add latency or double edges.

Optional shared native storage folders carry one identity/virtual signing root
and actual native partition files. An exclusive lease covers setup and the
whole game. Nonempty unrelated folders and conflicting identities are rejected.
This is shared persistent storage, not an image-backed FATX replacement.

Optional existing HDD images now have a shared native storage API and a
first-party raw/QCOW2/FATX implementation. Settings shows **HDD image** separately
from **Save folder**. An image takes priority; without one, the selected folder
or automatic private per-game storage is used. No disk image is generated.
Invalid/busy images never silently fall back to folder saves. Image setup
requires validated original signing inputs and EEPROM; a sibling
`<image>.xboxllr` directory retains its console identity and host-only settings.
Actual game saves are stored inside the image's FATX partitions. Preserve both
the image and its sibling profile. The default folder route remains available.

The same block owner is used by LLE IDE and native filesystem services. Native
image admission checks QCOW2 reference counts/features and FATX reachable
allocation ownership before mutation. It rejects unsupported backing chains,
snapshots, compression, encryption, dirty/corrupt state and unsafe layouts.
Only retail partitions 1–5 are mounted. No formatter or original guest software
is bundled. A metadata check cannot determine which EEPROM signed every
proprietary existing save; matching supplied identity remains necessary and
the game still verifies its save signatures normally.

The packages start with no media, identity or saved controller choice. Title
metadata supplies enhancement defaults; saved user choices override them.
The owner receives owned setup inputs separately; those are never
part of the package manifest. Owner play is windowed with normal audio.
Automated checks use hidden windows and muted output.

Validation covers the real compiled provider protocol, hidden first-run layout,
embedded-Python startup and package hashes, ROM-free profile ownership/error
cases, and disposable original-compatible profiles from all three admitted
owned titles. Virtual identity persistence, shared identity and writer exclusion
have component checks. New save/restart/load gameplay under virtual keys remains
a release check. Existing accepted builds, saves
remain intact. Updated title pins identify the shared source milestone; the
build evidence also records the prepared dependency and CPU-output inputs.

Deferred: ISO extraction/import, general firmware import, virtual-profile
gameplay/save qualification, complete per-title image-backed save/load QA, optional-file import into an
existing profile, remaining source/license review, Linux and release publication.
Older preview packages use native host folders; the new image route is being
reviewed in the refreshed Blinx, JSRF and Dashboard packages. Dashboard uses the
shared quality/presentation registry without a game-specific camera mod; its
original menu aspect is retained. The owner's Dashboard review selects a
verified copy of the existing owned HDD with original signing inputs. No
internal-resolution or supersampling controls are exposed for its original
Gaussian resolve yet; FXAA, anisotropic filtering and blended presentation
are available. The review copy's disposable `CACHE/LocalCache08.bin` was
backed up and removed to use the qualified offline empty-cache path. Existing
Xbox Live cache-record decoding remains deferred; this preparation does not
qualify populated-cache support or alter the original image. No
per-game disk image is provisioned automatically. Do not describe this as
the completed reduced-input public release.

## Deferred review work

| Tracking | Deferred behavior | Current review boundary |
| --- | --- | --- |
| `beads-xbox.1.188` | Imported Xbox Live cache-record decoding | Keep the existing presence guard. The review disk's one disposable cache file is backed up separately; the empty-cache run does not qualify populated-cache imports. |
| `beads-xbox.1.189` | Higher internal resolution and supersampling with guest Gaussian resolve | Dashboard hides those two unsupported controls. Preserve its original guest-visible resolved image while adding a separate enhanced presentation owner later. Blinx/JSRF use their existing qualified raster path. |
