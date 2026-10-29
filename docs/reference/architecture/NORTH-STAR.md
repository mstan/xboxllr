# Architecture direction

XboxLLR is an early original Xbox recompilation ecosystem. The public support
scope is [Blinx plus the Dashboard preview](../../../README.md). Current task
priorities come from the owner and issue tracker, not historical session queues.

## Native execution and reference contracts

Build useful native title ports with a maintained, runnable low-level reference
foundation. Any subsystem may use a qualified native/HLE implementation under
its observable contract. State, outputs, errors, ownership, ordering and declared
numerical/presentation tolerances determine compatibility. Native internals may
differ from hardware mechanisms. Live switching, real-time LLE performance and
duplicate production execution are not requirements. An old external branch is
not a substitute for the maintained reference implementation.

Do not fabricate device success, replace gameplay with captured frames or use
stubs as evidence of complete behavior. Define the supported boundary, exercise
it, and record missing precision and untested paths honestly. HLE progress may
lead delivery while reference coverage advances; one successful scene does not
prove complete hardware correctness.

## Reuse and validation

Prefer established implementations and existing reference fixtures. xemu is a
primary low-level device/reference donor; Cxbx-Reloaded and the xboxrecomp family
supply useful kernel, renderer and native-service contracts. Adopt coherent
boundaries when ownership fits, retaining source provenance, notices and known
limitations. Shared implementation ancestry is not independent corroboration.

Longer generation and compilation are acceptable for better built runtime.
Measure game CPU/frame cost, content cadence, startup and memory costs on matched
workloads. Keep original updates, presents, interpolation, audio and latency
separate. Preserve original simulation timing unless a title-specific enhancement
has been qualified. Net-neutral results are acceptable; gains in one game cannot
offset an individual reproducible regression. Record uncertainty and raw data.

## Portability and persistence

Keep portable native output as the long-term CPU direction. Optional host
assembly/SIMD requires explicit CPU/OS/ABI qualification and a universal fallback.
Windows x64 is the validated native release platform; Linux component tests do
not establish a playable native Linux host. ARM qualification remains separate.

Native saves and console identity are persistent user data. Build-specific save
states are disposable; preserving old save-state compatibility is not required.
Use isolated test profiles and preserve owner saves/settings.

## Distribution

Owner-original material uses GPL-3.0-or-later, with retained third-party terms and
corresponding source. Original media, firmware, keys and generated original-title
source bodies are private inputs, not part of that source grant. See
[license scope](../../../LICENSE-SCOPE.md) and [credits](../../../CREDITS.md).

[Engineering principles](PRINCIPLES.md) supplies the detailed contracts.
