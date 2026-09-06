# Generated GL constant uploads

Tracker: `beads-xbox.1.15.13`. This experiment is informed by sp00nznet's
xboxrecomp bulk constant submission, reviewed in `PERF-EXTERNAL-REVIEW-2026-09-06.md`.
No external renderer code, shader semantics, or game assets are imported.

The generated fragment shader declares two eight-element `vec4` arrays. The old
host code queried element zero and issued scalar uploads to `base + i` for all
eight elements. A linked program can retain fewer active elements; an unchecked
location increment can then address another uniform or an invalid location.
The new scalar path queries each element once per cached program and skips `-1`.
This is a source-level correctness concern, not a reproduced dashboard defect.

The separate bulk candidate uses `glUniform4fv(base, 8, values)`. Khronos
explicitly specifies that values beyond the end of a uniform array are ignored;
the reviewer's initial claim that a shorter active array necessarily makes
`count=8` invalid was rejected. See the official
[glUniform specification](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml).
The candidate retains the exact byte-to-float arithmetic and program/state/draw
ordering. It falls back to safe scalar uploads if the entry point is unavailable.

The runtime switch is `--[no-]nv2a-gpu-generated-uniform-batching`, default off.
`[host] generic-uniforms` reports cumulative scalar calls, bulk calls and requested
vec4 values, with explicit scope/deadline. Bulk counts all eight requested values,
including ignored trailing elements. Use differences between guest-anchored
samples for interval work; do not interpret these as timers or active-value counts.
Candidate legs must demonstrate nonzero bulk calls to exclude silent fallback.

## Validation and experimental controls

GPT-5.5 High implemented the bounded renderer candidate and fixtures; the primary
reviewed the GL specification, changed packed storage to a flat 32-float array,
and required full-buffer comparisons and inactive-array/switchback coverage.
The release native `-O3 -DNDEBUG` build passes CTest **33/33**, with actual GL 4.3
contexts and no skipped generated-uniform fixture. Cases include one/eight-stage
programs, one/two active arrays, no active arrays, changed constants, alternating
cached programs, and returning from bulk to scalar. Parser tests preserve explicit
deadlines and do not invent samples for older logs or missing checkpoints.

The archived pre-change executable has actual SHA-256
`586d2cf3b9973447b94f09c0c6ce88df18a98d87cc7e5d4932d58c2437fd9aa5`.
First compare it against new safe scalar in interleaved 3+3, then compare scalar
and bulk on the same new executable. This separates the scalar correction and
counter/default-branch costs from the batching experiment. Both arms retain the
same vertex-diagnostic build configuration. All headline runs are noperf;
performance acceptance forbids overlapping builds/tests/other emulator instances.
The recorded narrow process pattern excludes resident idle Beads/Python services.

Artifact prefix: `build/uniform-ab/`. Every set retains individual argv, actual
binary hashes, process checks, six reports, complete checkpoint PPMs, min/median
gate JSON, and an independent image/stable-state comparison. No performance
conclusion should be drawn from the test-suite result alone.

## Safe scalar comparison

`correctness-20260906-r1`, old binary versus new scalar, 3+3:

| Pair | Old scalar (s) | Safe scalar (s) |
|---|---:|---:|
| 1 | 58.3509474 | 50.2248308 |
| 2 | 48.6151977 | 50.2401392 |
| 3 | 53.2979699 | 49.5717440 |

Median improves 5.7660%, minimum regresses 1.9676%: **5% gate REJECT**.
CV is 9.11% / 0.76%; the old-build variation makes a precise performance claim
unwarranted. All process censuses are clean under the recorded pattern, which
does not establish that every source of host variation is absent. Retain all
legs. All 28 images and stable retired/scanout/flip/input/idle/device/DMA/generic/
seam/timeline fields agree across the six runs. New scalar legs each issue 4,744
active stage-element uploads, with zero bulk calls. The source-level location
repair is kept for correctness, not as a claimed throughput gain.

## First bulk set: contaminated, no timing conclusion

`canonical-20260906-r1` uses the same executable in both arms (actual SHA-256
`ca36d2129278db277203040d0e4fe095e3f742ab24297f8569d05c5d54223198`).
The candidate demonstrably reduces stage-uniform calls **4,744 -> 593**, with
4,744 requested vectors in either mode and zero scalar fallback in B. All 28
images and stable guest-state/timeline fields agree across all six runs.

Foreign CMake/Ninja/compiler processes contaminated legs 3, 4 and 5; an active
compiler snapshot identified another project's build. Preserve the whole set
and do not use the apparent -4.77% median / -0.99% minimum change as a causal
performance result. A/B CV is 10.50% / 5.91%. The gate rejects for both performance
thresholds and contamination. The independent fidelity JSON explicitly marks
`timing_eligible=false`. Batching stays default off. Repeat a complete clean set
before any performance conclusion; product promotion would also require the
separate interactive navigation gate.

The final release configuration compiles vertex-shadow integration out by
default. Both diagnostic ON and normal OFF builds pass CTest 33/33, and the
normal build's `final-reference` replay accepts **31/31 golden checks** (29
executed; executable/Git identity are two configured diagnostic skips). The
golden was not re-pinned. This is a behavior check, not a timing sample: its
process census also reports other activity.

The final normal-build executable has SHA-256
`0984b680c443029f4dfb8d8814b3375391f1cf4f0eb84cbcd539feb55885bd83`;
its SDL DLL is `4b81b530a0237262dd58aeb067ed7fd20876a4f5697b73a4030fc80f1b1c0b6a`.
The dedicated diagnostic ON executable is
`484a9d43c78a775d8d0b50ed394c0f5db39ba60dba5137cdaa08ed510f030276`,
using that same SDL build. Preserve these identities when comparing build modes.

## Final same-binary set: gate rejects; batching remains off

`final-canonical-20260906-r2` compares scalar A and bulk B on the final normal
build above. Three interleaved pairs have clean before/after process censuses;
bounded whole-pair retries were enabled but none were needed. The earlier
contaminated set remains archived separately.

| Pair | Scalar (s) | Bulk (s) |
|---|---:|---:|
| 1 | 61.0530123 | 62.0058161 |
| 2 | 68.5156157 | 65.1303416 |
| 3 | 59.3212971 | 59.7430015 |

Median regresses **1.5606%**, minimum **0.7109%**: **5% gate REJECT**. Only one
pair favors bulk. CV is 7.76% / 4.34%; these data do not establish a precise
small regression, and sampled process checks do not exclude all host noise.
They do not justify enabling the candidate. Do not compare these absolute
times against another session to infer improvement or regression.

All **28 PPMs** and stable retired/scanout/flip/input/idle/device/DMA/generic/seam/
timeline fields agree across all six legs. Every A leg has 4,744 scalar calls
and zero bulk; every B has 593 bulk calls and zero scalar, with the same 4,744
requested vectors. Fewer API calls alone did not meet the wall-time criterion.

Keep the safe scalar location repair, the live-GL counterexamples, and the
default-off bulk comparison switch. No interactive promotion set is warranted
for this revision after canonical rejection; that gate remains mandatory before
any future default promotion. Reopen only with a changed workload or upload
mechanism. Next measure generated CPU packing/upload/submission costs and GPU
issuance windows, rather than infer that the unassigned GL time is uniforms.
