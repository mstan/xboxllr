# Native title revision packages

Tracking: beads-xbox.1.198; JSRF admission: beads-xbox.2.17.

Each original XBE identity requires its own verified generation, binding profile,
native executable and title-mod facts. A second hash is never an alias for an
existing program. The launcher accepts a bounded list of exact SHA-256 values;
the package host selects the matching executable before setup or execution.
Unknown or ambiguous identities remain errors. Window captions keep the game name.

A primary title definition can declare another complete definition:

```toml
[revisions.usa-standalone]
definition = "native-title-usa.toml"
```

Prepare, bind/recover and build each definition into separate private directories.
Supply each declared additional build to `native_release_package.py` with
`--revision <id> <runtime.exe> <build/CMakeCache.txt>`. The additional executable
must match its native build receipt. All variants must use the package's qualified
optimized native policy and launcher settings. The dependency preflight still
rejects ambiguous DLL versions; supply the same qualified shared modules for all
executables in the package.

The primary revision retains `data/console` and `data/state`. Additional defaults
use `data/revisions/<id>/console` and `data/revisions/<id>/state`. Switching revisions
does not replace an existing identity or save tree. Explicit shared-folder and
image-backed storage retain their existing locks, console identity and original
save-signature checks. This does not promise arbitrary cross-revision save sharing.

Mod catalogs can list multiple exact `game_id`/`xbe_sha256` targets. Runtime plans
still contain the selected single identity, and the compiled title registry must
admit that identity and its own addresses. System-wide visual ABI targets keep
their existing exact schema.

Validation includes authored executable routing and preservation of primary saves,
rejection of unknown/ambiguous identities, compiled launcher settings round trips,
and a real two-revision package. The JSRF project records original-code mod checks,
actual gameplay, adaptive aspect changes and save/restart/load evidence separately.
