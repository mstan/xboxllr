# XboxLLR architecture

XboxLLR is an early original Xbox low-level recompilation ecosystem. Blinx is
the only publicly supported game; the original Dashboard is a separate native
application. Current playable builds target Windows x64/D3D11.

## Native CPU execution and console behavior

The title pipeline translates admitted original Xbox x86 functions to C and
compiles them ahead of time. Title metadata binds the original image, calling
conventions, layouts and recovered targets to an exact revision. The fast title
profiles stop at unresolved targets rather than falling back to a guest x86
interpreter. Users supply their own matching executable and complete assets.
Original translated bodies are generated privately from that input.

Native CPU execution is separate from the LLE/HLE distinction. Low-level devices
and original-code reference routes establish console-service contracts. Shared
native implementations accelerate files, memory, scheduling, input, graphics,
audio and other services while preserving caller-visible behavior. Reference
and fast profiles are selected at build time; normal play does not run both
implementations in parallel. Unreached behavior remains an explicit acceptance
gap rather than implied complete hardware compatibility.

## Repository boundaries

| Area | Responsibility |
| --- | --- |
| `tools/`, `pilots/native-title/` | Shared preparation/build drivers, native host and console-service implementation |
| `src/` | Shared components and retained full-system/reference implementation |
| `titles/dashboard/` | Exact Dashboard revision profile and launcher metadata |
| Separate game repositories | Original-image identity, title bindings/mods, framework pin and game release |
| `pilots/native-launcher/`, `pilots/native-boot/` | Shared launcher and optional native startup presentation |
| `tests/`, `goldens/` | Component/contract checks and reproducible reference data |

The Dashboard is launched directly as a native title. Its optional startup
animation is a reconstruction, not execution of the original firmware. The
legacy full-system route has [separate setup](LEGACY-FULL-SYSTEM-SETUP.md).

## Correctness and persistence

New service work needs meaningful contract checks and actual built-title
validation. A passing component test, static coverage count or screenshot does
not establish complete gameplay. Performance is evaluated per game on matched
built workloads; a gain in one game cannot cancel another game's regression.

Native in-game saves and console identity are persistent user data. Disposable
utility-cache contents and build-specific save states are separate. Blinx uses
a sparse RAM utility volume; the shared default stays off. See
[RAM utility cache](RAM-UTILITY-CACHE.md) and [setup](SETUP.md).

[Engineering principles](reference/architecture/PRINCIPLES.md) and
[architecture direction](reference/architecture/NORTH-STAR.md) retain detailed
contracts. [Developer setup](DEVELOPMENT.md) and the
[native-title driver](NATIVE-TITLE-DRIVER.md) describe the supported pipeline.
