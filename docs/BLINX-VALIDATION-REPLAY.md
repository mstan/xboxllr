# Separate graphics validation from title startup

Owner direction, 2026-10-17: finish the bounded Blinx qualification with captured,
replayable contracts and short event-driven integration checks. Keep the working
LLE reference and fast native HLE build. Full native LLE conversion is a separate
milestone; it does not gate a useful HLE implementation or these replays.

## Choose the check that answers the question

| Question | Existing or maintained path | Evidence boundary |
| --- | --- | --- |
| Can a retained shader's consumed values be admitted? | `native_vsh_dependency_capture.py` and `xlle_vsh_dependency_replay` | Original captured program, constants and fetched attributes; explicit availability counterexamples |
| Do original XDK calls deliver the expected shader/state/vertex behavior? | `native_frame_state_reference.py`, `native_frame_shader_state_reference.py` and their independent oracles | Captured requests replay through original routines with preowned resources; retain each report's fixture limits |
| Does a draw or producer chain produce the expected pixels? | Existing `native_scene_*_reference.py` and producer/continuation checks | Independent expected math/storage for the admitted domain; after-images are observations, never replacement expected inputs |
| Do startup, moving gameplay, real Start/cleanup and reentry work together? | [Connected acceptance](BLINX-GRAPHICS-ACCEPTANCE.md) | One maintained original route plus actual output, delivered input, completion and content ownership; reuse unchanged component evidence |

The current G01-G04 evidence remains applicable when its dependencies do not
change. Do not run the entire old matrix for a diagnostic/tool-only change.
G05 still requires the connected normal gameplay/exit/reentry sequence. A passing
replay does not close that integration requirement by itself.

## Standalone captured admission replay

Build `xlle_vsh_dependency_replay` with the existing root CMake configuration.
It links the maintained NV2A shader decoder/CPU helpers and uses the same
`nv2a_vsh_dependencies.hpp` as the reference renderer. It needs no game process,
original kernel, graphics device, writable HDD image or audio output.

From a private complete-frame capture, export one representative input per
distinct programmable shader:

```powershell
python tools/native_vsh_dependency_capture.py <private-frame-inputs.bin> --output <private-admission.bin> --constants-available
<build>/xlle_vsh_dependency_replay <private-admission.bin>
```

`--draws N ...` selects specific retained input sequences, and
`--all-programmable` exports every programmable draw. Keep the generated binary,
its adjacent provenance receipt and any game-derived inputs outside Git. The
default representative selection is intended for quick iteration, not an
exhaustive frame claim.

The exporter validates the capture and referenced asset hashes, derives vertex
attributes through the existing independent fetch code, and stores the original
program/constants with explicit lane masks. The required `--constants-available`
argument declares a fixture assumption: captured constant values are available
to this test. It does not assert that live context LOAD produced those values.

The executable checks admission for every supplied vertex. It then removes a
consumed constant lane and a consumed attribute lane, where present, and requires
each counterexample to reject. It also rejects malformed/truncated inputs,
duplicate draw identities, invalid extents and trailing bytes. Version 1 of the
earlier private preflight format is accepted with that format's explicit
all-constants-available fixture policy; no fixed 20/516-case count is embedded.

This is a dependency/availability check. It does not execute numerical shader
math or rasterize, and does not establish live context provenance, exact silicon
arithmetic, pixel agreement, allocation, teardown or full-title qualification.
Its success field is `captured_contract_passed`; `complete_scene_pixel_qualified`
remains false. Existing original-call and independent pixel replays answer those
other questions.

## Measured first use

On the retained first-world inputs, the representative pack contains 20 programs
and 284 vertices. The first replay passed in 0.35 seconds with 284 missing-constant
and 284 missing-attribute rejection controls. The separate retained indexed pack
passed 516 draws/3700 vertices in 6.09 seconds. Truncated and trailing-byte packs
both rejected. Export took 9.81 seconds; compiling the standalone helper took
2.99 seconds. These are observed local tool timings under shared host load, not
production HLE performance or a claim about all scene pixels.

Private receipts are under `build/blinx/validation-separation-20261017` in the
graphics replay worktree. Re-export only when captured inputs or fetch semantics
change; rebuild the small helper for admission changes. Neither operation needs
another title startup.

## Remaining integrated check

The 071e9f8 replacement reached visible title after a genuine opening skip, then
exited with host code0x20474343 at2147.344s before attract. Its229 inputs are sealed.
The [closing checklist](BLINX-QUALIFICATION-CLOSURE.md) records this distinct
failure and the new fast C ABI/host exception controls. Do not infer a shader,
compression or memory-pressure cause from the status alone. Diagnose that failure
before claiming the new depth writer has passed the original gameplay route.

The previously suspended title was checked and resumed under fresh supervision.
It reached initialized attract132, then exited2 after369.95 resumed seconds at
the required compressed depth/stencil ZROP-writer guard (`beads-xbox.1.127`).
All216 prior inputs were sealed unchanged before implementation edits; obsolete
supervisors were retired after the guest's terminal exit. That process cannot
resume. Keep the pause/takeover/seal receipts with its guest-clock, completion
and ownership observations. Host termination is not guest teardown.

After the bounded writer passes queued tests, start a fresh connected check.

Use original state hints only to request pictures; classify actual pictures
before counting gameplay or arming the scored Start marker. The bounded G05
requirements and existing nonessential deferrals remain unchanged. Coherent
fast-forward is a later candidate for wait-bound tests, not a substitute for
executing CPU/device work or checking real completion.
