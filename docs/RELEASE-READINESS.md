# XboxLLR release readiness

Audit: 2026-10-21. Tracking: `beads-xbox.1.187`; distribution provenance and
licensing: `beads-xbox.1.21`. The owner prioritizes this work ahead of further
JSRF recovery/extra-mode qualification. Existing accepted builds remain intact.

## Current repositories

| Repository | Current presentation | Release gap |
| --- | --- | --- |
| XboxLLR, currently hosted/named xboxlle | Legacy full-system/Unicorn setup guide; does not describe the current fast native Dashboard/Blinx/JSRF route accurately | Refresh product name and architecture, supported-title gallery, developer setup, shared release tooling and Dashboard-as-title packaging |
| BlinxRecomp | Private developer-first README with workstation layout, build modes and qualification history | Player-first features, curated widescreen screenshots, simple owned-game setup, controls/mods, known limits, releases and concise credits |
| JSRFRecomp | Native development preview and coverage/CLI summary | Same player-facing presentation, explicit top-level licensing/attribution, repository publishing setup and release packaging; local checkout has no origin remote |

The inspected Blinx source is `5ec1620`, pinned to framework `8cdc4d9`.
JSRF source is `c52007e`, pinned to framework `b657fc6`. Latest private
compatibility builds of Blinx and Dashboard with `b657fc6` render successfully;
that does not itself change Blinx's checked-in pin or create a release.
Remote repository visibility and existing GitHub release assets are not verified
by this audit; GitHub CLI authentication was not available in the agent's
redirected configuration. Do not infer that a remote repository does not exist.

## Priorities

1. **Resolve shared distribution and platform gates.** Inventory the exact
   shipping profile, incorporated code, notices and user-supplied inputs. Keep
   the owner's current non-GPL shipping policy unless explicitly changed.
2. **Build one reusable release path in XboxLLR.** Games provide metadata and
   adapters; framework code owns build, staging, dependency checks and packaging.
3. **Use Blinx as the first end-user release.** It has the most mature assisted
   campaign/time-control/save coverage and visual implementation. Refresh its
   README and prove setup on a clean profile without development paths.
4. **Apply the same release path to Dashboard and JSRF.** Dashboard is a title
   artifact of XboxLLR; JSRF remains its own game repository. Carry forward the
   accepted rapid-coverage scope and explicit deferrals rather than claiming
   exhaustive gameplay qualification.

## Actual shipping gates

### License and provenance

The existing [license scope](../LICENSE-SCOPE.md) explicitly excludes the
GPL-dependent private pilot from the intended non-GPL product. Current native
profiles still use attributed GPL fork components, including renderer/runtime
and audio material. Acknowledgments and bundled notices do not remove that
restriction. Review the actual selected dependency graph: do not assume a native
HLE-only profile links Unicorn merely because the legacy full-system build does.

Resolve incorporated-source permissions or replace excluded implementations
under the retained interfaces before publishing the relevant executable.
Do not silently relicense the owner's work. JSRF also needs an explicit license
and scope file consistent with that decision; the audit found no top-level LICENSE.

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

- XboxLLR owns normalized release metadata, build profiles, staging/verification,
  runtime dependency closure, notices and provenance, version/build identity,
  archive generation and reusable CI workflows.
- Game repositories own title identity/revisions, mod catalogs, artwork,
  documentation and thin adapters to the pinned framework tooling. Reconcile
  Blinx's `game.toml` and the generic `native-title.toml` driver interfaces.
- Produce a self-contained Windows x64 ZIP and a native Linux x64 AppImage or
  equivalent self-contained package after that host is qualified. Dashboard
  uses the same title path; XboxLLR also has a separate recompiler-tool artifact.
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

XboxLLR's README explains native CPU translation and the LLE correctness floor,
with fast native HLE implementations under retained caller contracts. It must
not claim to be HLE-free or imply that all other recompilers lack accuracy.
Show Dashboard, Blinx and JSRF, with current support scopes and a developer path.

Give prominent thanks to sp00nz/sp00nznet's xboxrecomp, Phobos's fork/native CPU
and renderer work, NoRain's DOAXtreme Beach Volleyball work, xemu and the other
recorded contributors. Use verified project names and links. Keep this readable
recognition separate from the exact component/revision/file-level notices in
[THIRD_PARTY_ATTRIBUTION.md](../THIRD_PARTY_ATTRIBUTION.md); preserve both.

No executable releases, visibility changes or license changes were made by
this initial readiness audit.
