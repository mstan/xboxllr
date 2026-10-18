# Blinx renderer enhancements and mod integration

Owner direction, 2026-10-17: keep the faithful native game and HLE renderer as
the default; ship draw-distance, fog, culling, projection and replacement-renderer
enhancements as independently selectable mods. Use shared recomp-ui and the
established package/provider model. An enhancement must not edit generated
original game bodies or require a patched base executable/image to disable it.
Correctness repairs still belong in the default implementation.

## Start now, in bounded steps

The UI/mod foundation can start alongside ordinary gameplay bring-up. The first
visual experiment needs a reproducible baseline scene, not full campaign
completion or further exhaustive LLE validation. First-stage entry, combat,
doors and the original exit/results/save transition now have bounded evidence;
the exit coverage explicitly used authorized HP/position cheats.

1. Integrate the shared Xbox UI profile and `RecompLauncherCModProvider` with
   title identity, user configuration, resolved plans and visible activation
   errors. Keep the existing keyboard/controller and normal-audio playtest
   usable while this integration is developed.
2. Add one default-off presentation feature, such as a fog-strength or
   projection adjustment at an existing HLE graphics boundary. Compare a fixed
   scene with the feature off/on. Reuse texture/shader/resource ownership and
   the existing D3D11 renderer before considering a second renderer backend.
3. Extend visibility for identified resident renderables through narrow,
   revision-bound title hooks. Preserve AI, object update/spawn, streaming,
   collision and time-control behavior. Qualify separate effects if a title
   routine combines those responsibilities.
4. Introduce a separate HLE scene renderer when the enhancements require scene
   information or batching that the current backend cannot provide. Keep that
   renderer selectable through the same mod plan and the faithful renderer
   available. LLE qualification remains an independent reference activity.

Increasing a backend clip distance cannot recover objects the game never
submitted. Draw-distance/culling work therefore needs title scene-submission
knowledge as well as renderer settings.

## Reuse the existing mod boundary

Follow `F:/Projects/recomp-template/MODS.md`: data-only packages, stable feature
IDs, exact title/revision identities, expected-byte/data guards, complete plan
validation before activation, explicit conflicts and deterministic fingerprints.
Independent features can compose; alternatives within one feature are mutually
exclusive. Native plugins are trusted code linked at build time, not arbitrary
DLLs/scripts loaded from a downloaded archive. Keep user installation and
selection state separate from framework-bundled packages.

The common UI provider presents the framework's resolved plan; it is not a
second title-specific mod manager. Reuse the PSX/SNES concepts and C provider
ABI, while keeping Xbox media, saves, input and native hook semantics in their
own adapters. The current Xbox theme/profile lives in recomp-ui; wiring that
UI into the Blinx package is separate work.

Static x86 recompilation needs an explicit hook mechanism. Editing guest code
bytes does not change already compiled C bodies, and patching the indirect-call
dispatch table misses native direct calls. Prefer a few reviewed renderer/API
hooks and selected title entry wrappers. Do not put a mod lookup on every
instruction or every compiled function. Disabled mods should preserve the
ordinary fast HLE path. Dynamic code invalidation from a different emulator is
not evidence that this static dispatcher supports code-overlay packages.

Extra draw submissions need owned, bounded vertex/index/constant/command
storage, GPU lifetime tracking, ordering and reset/release cleanup. A wider
frustum must not silently overrun a title buffer or reinterpret temporary
scratch data as a longer-lived resource. Attract/cutscene/HUD scope is explicit.

Netplay must follow the shared compatibility policy. Visibility changes that
affect updates, spawning or time are simulation changes. Do not label those
presentation-only, advertise cross-build compatibility, or invent live backend
renegotiation. Savestate/rewind compatibility is a separate implemented contract,
not a promise implied by fields in a mod manifest.

## Local precedents reviewed

| Source and reviewed revision | Useful pattern | Limit of the comparison |
| --- | --- | --- |
| `BattleArenaToshindenRecomp`, `cd372b5015449157026eade00fd9e3c34237c585` | Separate `src/mods` trusted plugins and feature/option manifests for widescreen and backdrop behavior; framework stages the catalog and UI. | Title hooks and ordering/HUD assumptions are PSX-specific. |
| `Tomba2Recomp`, `24ca4b7dfeef7a13225b8e7f44f4f08d27cc0442` | Separate terrain, actor-queue and effect visibility plugins; original selected terrain prefix followed by bounded additional resident terrain; per-flip owned packet arenas; distinct terrain HLE/LLE execution modules. | Removing every far rejection is unsafe: effects can combine culling and animation. Its HLE terrain path still calls original guest routines; it is not proof of a fully native scene renderer. |
| PSX framework, `93a93c8e07c647f1414d759d370ce390b818de7a` | `runtime/include/mod_plugins.h`, `runtime/src/mod_runtime.cpp`, `docs/MOD_PACKAGES.md` and the shared C mod-provider boundary. | PSX guest-code invalidation, GPU DMA and disc identity do not directly implement Xbox AOT hooks. |
| SNES framework, `runner/src/mod_runtime.h` and `docs/MOD_PACKAGES.md` | Data-only packages, independently chosen features, trusted prelinked native plugins and shared launcher provider. | Different package format and machine-specific lifecycle; reuse the contract, not unchecked assumptions about field/layout equivalence. |

Tomba 2 documents the concrete failure to avoid: extra terrain overflowed the
stock packet arena into other game data. Its later owned arenas and explicit
capacity limits are more valuable here than copying a far-plane constant.

## First widescreen implementation

The native pilot now exposes a small trusted title graphics ABI in
[`TITLE-MODS.md`](../pilots/native-title/TITLE-MODS.md). The Blinx repository
owns a default-off `blinx.enhancement.widescreen` package and the static
`blinx.widescreen` plugin. One configurable aspect covers 4:3 through 32:9,
including 16:9, 21:9, 3440:1440 and resize-driven adaptive presentation.

The mod changes the original perspective matrix data before the game's
SetTransform/matrix-copy boundary, so its CPU projection consumers share the
wider matrix. Identified overlay sprite/text draws use proportional left,
right or center anchoring. Gameplay widens; results and menus retain 4:3
between bars. Guest render buffers remain at their original size. Higher
internal resolution, further visibility enhancements and the shared launcher
provider remain separate work, rather than prerequisites for this first mod.

First-feature acceptance: off preserves the baseline route; on changes the
intended scene behavior; invalid identity/conflicting plans fail before effects;
resource bounds and cleanup hold. Reuse existing device/LLE family evidence and
add a focused comparison for the behavior actually changed. Track remaining
enhancement work in `beads-xbox.1.7.4`. The earlier 4:3 owner gameplay package
remains a separate baseline.

Actual adaptive title run `wide-stage-006` reached Stage 1 gameplay, rendered
the same paused scene at 16:9, 21:9 and 32:9, and used explicitly logged enemy
HP/position cheats to reach original Results, save and Stage 2 unlock. Results
and the stage selector retained their centered 4:3 presentation. These checks
do not establish full campaign, all effect/HUD producers or enhanced-resolution
coverage. Those remain in the title's campaign and enhancement ledgers.
