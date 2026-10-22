# Native Windows first-run previews

Tracking: `beads-xbox.1.187.7`, October 21, 2026. Shared owner-use packaging for
Blinx, JSRF and Dashboard lives in xboxllr. It is separate from publication and
does not resolve the [remaining release gates](RELEASE-READINESS.md).

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
backup. Local saves use real HMAC/SHA verification under that identity; there is
no zero-key or accept-all path and no claim of original-save/key equivalence.
Optional original-compatible setup retains the admitted kernel-derived signing
producer and authenticated EEPROM import.
A profile becomes usable only after all required inputs are validated and
written. Existing identities/native progress are never replaced. Setup does
not execute the original kernel or require Unicorn. Virtual and original
identities differ; an unmodified original LLE kernel does not become compatible
with local virtual keys merely by sharing their ABI.

Optional shared native storage folders carry one identity/virtual signing root
and actual native partition files. An exclusive lease covers setup and the
whole game. Nonempty unrelated folders and conflicting identities are rejected.
This is shared persistent storage, not an image-backed FATX replacement.

The packages start with no media, identity, saved controller choice or selected
enhancements. The owner receives owned setup inputs separately; those are never
part of the package manifest. Owner play is windowed with normal audio.
Automated checks use hidden windows and muted output.

Validation covers the real compiled provider protocol, hidden first-run layout,
embedded-Python startup and package hashes, ROM-free profile ownership/error
cases, and disposable original-compatible profiles from all three admitted
owned titles. Virtual identity persistence, shared identity and writer exclusion
have component checks. New save/restart/load gameplay under virtual keys remains
a release check. Existing accepted builds, saves
and title submodule pins remain intact.

Deferred: ISO extraction/import, general firmware import, virtual-profile
gameplay/save qualification, a shared image-backed FATX backend, optional-file import into an
existing profile, remaining source/license review, Linux and release publication.
Current writable game state uses native host folders. Do not describe this as
the completed reduced-input public release.
