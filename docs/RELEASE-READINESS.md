# xboxllr release readiness

Audit: 2026-10-22. Tracking: `beads-xbox.1.187`; distribution provenance and
licensing: `beads-xbox.1.21`. The owner prioritizes this work ahead of further
JSRF recovery/extra-mode qualification. Existing accepted builds remain intact.

The owner narrowed the current release target to **Windows** on October 21.
Native Linux host/backend work is deferred and does not block these releases.
All three repositories remain private.

## Current repositories

| Repository | Current presentation | Release gap |
| --- | --- | --- |
| xboxllr | Current native/LLE/HLE overview, three-title gallery and linked developer references | Shared Windows stager and Dashboard package implemented; combined-binary publication blocked by retained license policy |
| BlinxRecomp | Player-first features, widescreen capture, controls/mods, scoped status and credits; old build notes preserved separately | Clean candidate package implemented; publication awaits shared shipping gate |
| JSRFRecomp | Player-first features, widescreen capture, controls/mods, scoped status and credits; developer notes preserved separately | Clean candidate package and real boxart implemented; publication awaits shared shipping gate |

The October 22 source checkpoint brings title pins together with the shared
storage, launcher and mod fixes. Pin changes identify source, not a claim that
all generated CPU preparations have identical provenance: JSRF uses the reviewed
MIT CPU helper, while the current Blinx/Dashboard candidates still need their
prepared CPU output migrated. Authenticated GitHub checks confirm these repos:

| Repository | URL | Current visibility |
| --- | --- | --- |
| xboxllr | https://github.com/mstan/xboxllr | Private; retain private visibility |
| BlinxRecomp | https://github.com/mstan/BlinxRecomp | Private |
| JSRFRecomp | https://github.com/mstan/JSRFRecomp | Private |

The requested framework rename is applied; its former `mstan/xboxlle` API URL
resolves to `mstan/xboxllr`. JSRF now has a private remote and matching origin.
No visibility was changed. The owner confirmed on October 21 that `xboxllr`
should remain private, superseding the earlier public-framework plan. All three
repositories stay private. Neither existing framework nor Blinx remote had
GitHub releases at this check.

## Priorities

| Release work | Status |
| --- | --- |
| Player-facing game pages, framework overview, screenshots and credits | Complete and pushed |
| Requested repository rename and private game remotes | Complete; all three repos remain private |
| Replace the runtime's GPL ADPCM codec with the maintained permissive adapter | Implemented; reference, actual mixer and native JSRF smoke checks pass |
| Generated CPU helper provenance | MIT upstream replacement and authored bridge pass component checks plus representative native JSRF save/load/gameplay; title release migration remains |
| Selected renderer implementation and remaining emitted glue provenance | File-level review/replacement remains open |
| Shared release packaging and minimal first-run setup | Private stager, strict manifest archive, once-only optional setup, kernel-free virtual profiles, shared folders and raw/QCOW2 FATX implemented; ISO import and remaining fresh-profile save matrix deferred/unqualified |
| Imported Windows DLL preflight | Preview stager resolves selected app-local imports, including reviewed VC++ runtime files; public distribution review remains |
| Native signing-input preparation | Stdlib default matches original-derived files for all three titles; no normal Unicorn dependency; owned kernel/profile input remains |
| Native Linux host/backends and title execution | Deferred by owner; not a Windows release gate |
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

The CPU helper replacement is tracked in `beads-xbox.1.187.4` and
[its provenance/qualification record](NATIVE-CPU-HELPER-PROVENANCE-2026-10-21.md).
Representative native JSRF original-save loading, movement and active rendering/
audio now pass with the attributed MIT template. Existing owner playtest builds
and title pins remain unchanged. Component Linux checks do not qualify native
Linux title execution.

## Actual shipping gates

The [shared first-run preview path](NATIVE-FIRST-RUN-PREVIEW.md) now stages
local owner-use candidates for all three titles, with a bundled frontend,
Python and selected native DLLs. It preserves existing profiles and admits
owned setup inputs separately. These candidates are not GitHub releases and
do not clear the distribution gates below.

The shared [Windows dependency preflight](WINDOWS-RELEASE-DEPENDENCIES.md)
checks ordinary and delay-import module closure without running a title. Its
real-binary audit found three app-local VC++ runtime DLLs missing in each of
the existing Blinx, Dashboard and JSRF output folders. Module presence does not
clear licensing, toolset version compatibility or clean-profile package QA.

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

### Deferred native Linux support

Owner decision (October 21): defer this substantial port until after the Windows
release. Retain the requirements below for future work; Linux does not gate
current artifact acceptance.

The current native host directly uses Win32/NT memory, threading and service
APIs, D3D11 presentation and XAudio2. Portable generated C and portable component
tests do not establish a working Linux game host. A Linux release needs portable
host interfaces/backends, real compilation and title execution on Linux, followed
by packaging. Do not label a skipped CI job, stub backend or Wine wrapper as a
qualified native Linux build. Keep Windows behavior working during the port.

### Minimal first-run inputs

Owner follow-up (October 21): make original console-file import optional for
the intended fresh local-play setup. Use a title-aware first-use explanation
and acknowledgement: missing optional files generally do not matter for
single-player play such as Blinx, while imported saves or supported Xbox network
features can need original identity material. Require actual credentials at
features that need them, never fake success. Preserve existing profiles/saves
when importing later. [Complete setup](SETUP.md) and
[shared kernel/profile design](KERNEL-SERVICES-SETUP.md) record the concrete
input matrix, current limitations, upstream references and implementation order.
The native Dashboard does not inherently need original-firmware boot either.
ISO-only setup remains unfinished. Shared raw/QCOW2 FATX storage is implemented
and component-qualified; the remaining per-title save matrix is explicit
(`beads-xbox.1.22`). No per-game HDD image is generated.

Target: the owner's matching game dump; optional original Dashboard files.
Generate writable storage, configuration, cache and a virtual-console profile
locally where supported. Audit current EEPROM, signing/settings, media, generated
CPU and preparation dependencies before promising game-only setup. The current
`native_save_keys.py` preparer now uses the original-checked stdlib HMAC
derivation. Its normal path does not import Unicorn or execute the owned kernel;
original execution is an explicit optional verification mode. Optional original import still uses admitted private certificate material.
Fresh local profiles instead generate a persistent virtual signing root and
use real HMAC verification; they do not claim original-key equivalence.
See [signing preparation](NATIVE-SIGNING-SETUP.md) and
[current package setup](NATIVE-FIRST-RUN-PREVIEW.md). Native title gameplay does
not link Unicorn. Full fresh-profile save/restart/load qualification remains;
no signatures are accepted blindly and existing save identities are preserved.
Define a
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
- Produce a self-contained Windows x64 ZIP for the current release. A native
  Linux x64 AppImage or equivalent package is deferred until that host is
  qualified. Dashboard
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
