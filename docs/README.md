# XboxLLR documentation

XboxLLR is beta. Blinx is the only publicly supported game; the Dashboard app
and build tool share one [XboxLLR release](https://github.com/mstan/xboxllr/releases/tag/beta-2026.10.29.2).

| Guide | Purpose |
| --- | --- |
| [Dashboard](DASHBOARD.md) | Tested features, partial support and unavailable functionality |
| [Setup](SETUP.md) | User-owned content, local consoles, saves and Dashboard 5960 |
| [Build tool](BUILD-TOOL.md) | xboxllr.exe and developer prerequisites |
| [Development](DEVELOPMENT.md) | Source checkout, dependencies and builds |
| [Architecture](ARCHITECTURE.md) | Native CPU pipeline and low-level console contracts |
| [Native title driver](NATIVE-TITLE-DRIVER.md) | Preparation/binding/build interface |
| [Title revisions](NATIVE-TITLE-REVISIONS.md) | Exact-image admission and per-revision data |
| [Public releases](PUBLIC-RELEASES.md) | One release command, component archives and verification |
| [Diagnostics](TESTER-DIAGNOSTICS.md) | Reports and private input replay |
| [Disc import](DISC-IMPORT.md) | ISO/XISO import and reusable content caches |
| [Startup animation](NATIVE-BOOT-INTRO.md) | Optional native startup and required private input |
| [RAM cache](RAM-UTILITY-CACHE.md) | Disposable utility storage and memory cost |
| [Legacy reference setup](LEGACY-FULL-SYSTEM-SETUP.md) | Separate full-system route |
| [Repository hygiene](REPOSITORY-HYGIENE.md) | Private-input boundaries and publication checks |
| [Technical references](reference/README.md) | Subsystem contracts and provenance |

[Changes](../CHANGELOG.md), [credits](../CREDITS.md) and
[license scope](../LICENSE-SCOPE.md) are at the repository root.

## Historical notes

Obsolete progress dumps, agent handoffs, investigation results and duplicate
release notes were removed from the current tree. They remain in
[Git history](https://github.com/mstan/xboxllr/tree/5faaafad269d671b39737edbebaed66c81b2b9fb/docs).
Accepted machine-readable [optimization evidence](optimization/2026-10-28/README.md)
and active technical contracts are retained.
