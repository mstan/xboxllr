# xboxllr release readiness

Audit: 2026-10-21. Tracking: `beads-xbox.1.187`; distribution provenance and
licensing: `beads-xbox.1.21`. The owner prioritizes this work ahead of further
JSRF recovery/extra-mode qualification. Existing accepted builds remain intact.

## Current repositories

| Repository | Current presentation | Release gap |
| --- | --- | --- |
| xboxllr | Current native/LLE/HLE overview, three-title gallery and linked developer references | Shared release tooling and Dashboard-as-title packaging |
| BlinxRecomp | Player-first features, widescreen capture, controls/mods, scoped status and credits; old build notes preserved separately | Clean first-run package and download instructions after shipping gates are resolved |
| JSRFRecomp | Player-first features, widescreen capture, controls/mods, scoped status and credits; developer notes preserved separately | Release packaging and clean first-run setup; source license/scope and private remote are now present |

The inspected Blinx source is `5ec1620`, pinned to framework `8cdc4d9`.
JSRF source is `c52007e`, pinned to framework `b657fc6`. Latest private
compatibility builds of Blinx and Dashboard with `b657fc6` render successfully;
that does not itself change Blinx's checked-in pin or create a release.
Authenticated GitHub API checks on October 21 confirm these repositories:

| Repository | URL | Current visibility |
| --- | --- | --- |
| xboxllr | https://github.com/mstan/xboxllr | Private pending release preparation |
| BlinxRecomp | https://github.com/mstan/BlinxRecomp | Private |
| JSRFRecomp | https://github.com/mstan/JSRFRecomp | Private |

The requested framework rename is applied; its former `mstan/xboxlle` API URL
resolves to `mstan/xboxllr`. JSRF now has a private remote and matching origin.
No visibility was changed. Neither existing framework nor Blinx remote had
GitHub releases at this check. The intended final arrangement is a public
framework and private game repos; the framework remains private until its
public-source release review is complete.

## Priorities

| Release work | Status |
| --- | --- |
| Player-facing game pages, framework overview, screenshots and credits | Complete and pushed |
| Requested repository rename and private game remotes | Complete; framework public visibility awaits source review |
| Replace the runtime's GPL ADPCM codec with the maintained permissive adapter | Implemented; reference, actual mixer and native JSRF smoke checks pass |
| Generated CPU helper provenance and selected renderer implementation | File-level review/replacement remains open |
| Shared release packaging and minimal first-run setup | Open |
| Native Linux host/backends and title execution | Open |
| Final clean-profile packages and release publication | Open |

1. **Resolve shared distribution and platform gates.** Inventory the exact
   shipping profile, incorporated code, notices and user-supplied inputs. Keep
   the owner's current non-GPL shipping policy unless explicitly changed.
2. **Build one reusable release path in xboxllr.** Games provide metadata and
   adapters; framework code owns build, staging, dependency checks and packaging.
3. **Use Blinx as the first end-user release.** It has the most mature assisted
   campaign/time-control/save coverage and visual implementation. Refresh its
   README and prove setup on a clean profile without development paths.
4. **Apply the same release path to Dashboard and JSRF.** Dashboard is a title
   artifact of xboxllr; JSRF remains its own game repository. Carry forward the
   accepted rapid-coverage scope and explicit deferrals rather than claiming
   exhaustive gameplay qualification.

## Actual shipping gates

### License and provenance

The existing [license scope](../LICENSE-SCOPE.md) explicitly excludes the
GPL-dependent private pilot from the intended non-GPL product. Current native
profiles still use attributed fork components with unresolved renderer/runtime
shipping provenance. Native-title ADPCM now uses the existing permissive
dr_wav adapter; its GPL counterpart is comparison-only, as recorded in the
[codec integration](NATIVE-ADPCM-RELEASE-2026-10-21.md).
Acknowledgments and bundled notices do not remove any remaining
restriction. Review the actual selected dependency graph: do not assume a native
HLE-only profile links Unicorn merely because the legacy full-system build does.

Resolve incorporated-source permissions or replace excluded implementations
under the retained interfaces before publishing the relevant executable.
Do not silently relicense the owner's work. JSRF now includes the same unmodified owner-original license, notice and a
local scope/component inventory. These files do not resolve the combined-build
restriction.

### Native Linux support

The current native host directly uses Win32/NT memory, threading and service
APIs, D3D11 presentation and XAudio2. Portable generated C and portable component
tests do not establish a working Linux game host. A Linux release needs portable
host interfaces/backends, real compilation and title execution on Linux, followed
by packaging. Do not label a skipped CI job, stub backend or Wine wrapper as a
qualified native Linux build. Keep Windows behavior working during the port.

### Minimal first-run inputs

Target: the owner's matching game dump; optional original Dashboard files.
Generate writable storage, configuration, cache and a virtual-console profile
locally where supported. Audit current EEPROM, signing/settings, media, generated
CPU and preparation dependencies before promising game-only setup. Define a
ROM-free setup host/first-run generation boundary; don't package development
paths, original binaries/media, keys, user saves or private generated output.

## Shared build and packaging architecture

Use Mega Man X as a reference for the user flow and packaging contracts:
its feature gallery, owned-ROM setup, Windows PE dependency closure, Linux
AppImage layout and preservation of writable state. Its existing scripts contain
title-specific and duplicated logic; copying them into every Xbox game would
miss the owner's requested shared architecture.

- xboxllr owns normalized release metadata, build profiles, staging/verification,
  runtime dependency closure, notices and provenance, version/build identity,
  archive generation and reusable CI workflows.
- Game repositories own title identity/revisions, mod catalogs, artwork,
  documentation and thin adapters to the pinned framework tooling. Reconcile
  Blinx's `game.toml` and the generic `native-title.toml` driver interfaces.
- Produce a self-contained Windows x64 ZIP and a native Linux x64 AppImage or
  equivalent self-contained package after that host is qualified. Dashboard
  uses the same title path; xboxllr also has a separate recompiler-tool artifact.
- Stage tracked release inputs only. Resolve the actual transitive DLL/ELF
  dependency graph; include launcher assets, shipped mod catalogs, licenses and
  applicable source/relink materials. Separate optional developer symbols.
- Preserve native in-game saves on upgrade. Keep writable settings/saves outside
  an AppImage's read-only payload. Save states remain disposable/build-specific.
- Public CI builds/tests the ROM-free host and package layout without owned game
  inputs. Private owned-content smoke tests are separate evidence. Require a
  clean-machine first launch, controller selection and real save/load before
  attaching release artifacts. Draft releases precede final publication review.

## README and recognition requirements

Game READMEs lead with what players can do, actual widescreen screenshots,
available features, supported game revision, setup, controls/mods and known limits.
Move detailed build/qualification history into linked developer documents.
Only show curated documentation images; don't publish diagnostic capture trees.

xboxllr's README explains native CPU translation and the LLE correctness floor,
with fast native HLE implementations under retained caller contracts. It must
not claim to be HLE-free or imply that all other recompilers lack accuracy.
Show Dashboard, Blinx and JSRF, with current support scopes and a developer path.

Give prominent thanks to sp00nz/sp00nznet's xboxrecomp, Phobos's fork/native CPU
and renderer work, NoRain's DOAXtreme Beach Volleyball work, xemu and the other
recorded contributors. Use verified project names and links. Keep this readable
recognition separate from the exact component/revision/file-level notices in
[THIRD_PARTY_ATTRIBUTION.md](../THIRD_PARTY_ATTRIBUTION.md); preserve both.

This pass refreshed the three front pages and credits, preserved developer
setup notes, added five curated screenshot copies and applied the requested
repository rename/private JSRF remote. JSRF source licensing now records the
existing ecosystem policy. No executable releases or visibility changes were
made, and no tested runtime pins were advanced.
