# XboxLLR documentation

XboxLLR is beta. The same engine powers Blinx and JSRF; JSRF's beta remains
private while its public beta is prepared. See the [main README](../README.md)
for current downloads and testing limits.

| Guide | Purpose |
| --- | --- |
| [Contributing](../CONTRIBUTING.md) | Bug reports, setup and upstream pull requests |
| [Dashboard](DASHBOARD.md) | Tested features, partial support and unavailable functionality |
| [Setup](SETUP.md) | User-owned content, local consoles, saves and Dashboard 5960 |
| [Build tool](BUILD-TOOL.md) | xboxllr.exe and developer prerequisites |
| [Development](DEVELOPMENT.md) | Source checkout, dependencies and builds |
| [Native Linux release guide](https://github.com/mstan/xboxllr/blob/375d6d1be6938ebcfce24a2d8509d0d88b29353f/docs/NATIVE-LINUX.md) | Pinned Linux integration, dependencies and AppImage packaging |
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
