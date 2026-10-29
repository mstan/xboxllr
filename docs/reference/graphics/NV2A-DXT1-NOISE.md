# Bounded DXT1 reconstruction reference

The maintained software NV2A route reconstructs DXT1 texels before filtering.
Both control banks must agree. Each DrawJob latches the enabled bit, and owns
its decoded images; later control/source writes cannot alter an existing job.
The conversion cache includes the decode profile as well as exact source bytes,
pitch and format. No captured image table or frame-dependent random generator
is used. The implementation is portable scalar integer code.

## Independent physical evidence

On2026-10-16, the separate guarded probe captured the original first reached
primitive and authored controls under its original sampler/geometry/program.
Four original off/on/on/off observations were followed by12 geometry/role
controls,12 low-color role controls, and24 lower/upper channel-range controls.
Every off/off and on/on repeat is exact. Translated-geometry controls distinguish
texture-relative from screen-relative phase. Both endpoints and both opaque
interpolants share the measured behavior. The range batch covers encoded red
and blue0,1,2,3,28,29,30,31 and green0,1,2,3,60,61,62,63.

The compact coordinate rule plus channel limits matches all64 original enabled
samples exactly. Across832 logical samples it matches795 exactly and all832
within one byte unit per RGB channel; all208 conservative block-center samples
are exact. The37 residuals occur at block-left boundaries. A small left-texel
filter contribution explains them, but both1/255 and1/256 weights fit, so the
exact weight is not established. This remains the existing GFX-D04 filtering
precision deferral. It does not justify disabling noise or ignoring low colors.

Private evidence lives in the probe checkout's
`results/noise-physical-{fifo,controlled,low-red,range}-20261016/`, with pinned
source/build/capture receipts and independent analysis scripts in its build tree.
No title texture, original program, console identity or network configuration is
included here. Physical data is the oracle; mock GPU tagged pixels only validate
the probe's inputs, publication, readback and retention.

## Executable rule and limits

Expand RGB565 endpoints by bit replication. Form the ordinary BC1 palette,
using nearest integer opaque thirds and the existing transparent half palette.
With noise off, reduce palette RGB to565 and expand it again. With noise on,
choose the nearest byte congruent to the texture-coordinate phase modulo8 for
red/blue or4 for green, ties downward. For step S, values below3*S/2-1 map to0;
values at least255-S/2 map to255. Alpha and the transparent selector retain BC1
semantics. The exact coordinate equation is in
`src/machine/nv2a_dxt1_noise.hpp`.

This is a fitted reconstruction contract, not a claim about internal silicon
stages. Unmeasured fractional/interpolant precision, other original hardware
revisions, and exact subtexel/mip filter arithmetic remain GFX-D04 questions.
Observed range behavior is implemented, not waived. GPU/graphics qualification
uses this component together with the later accepted connected original
title/attract/exit/reentry and lifetime evidence in the
[closing checklist](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/BLINX-QUALIFICATION-CLOSURE.md).

The native HLE backend retains its efficient compressed-texture path and the
owner-accepted appearance baseline. Its omission of this small reconstruction
variation is an explicit approximation; no per-frame CPU upload is added.

## Authored observation fixture provenance

`tests/nv2a_dxt1_noise_observed.hpp` contains only32 authored constant-selector
blocks:24 range blocks,4 mixed palette-role blocks, and4 common-low-color role
blocks. Its RGB expectations are direct physical observations, not decoder
output. The extraction reads only those patch coordinates, removes the unused
output alpha, and verifies same-mode repeats. Left-column comparisons permit
one byte unit; every other sample is exact. Decoder alpha is tested separately.
The original draw geometry/program/texture are not included in the fixture.

Private extraction receipt SHA256:
`5fda6a263870f0e1f380999927887cc19c3649e9c148e07fa105bf260f4a063e`.
Independent cross-dataset analysis receipt SHA256:
`272d2cc756f56ad5d938dc865e73e6ecf187b669eb69ee2c9455b58ba40ae10a`.

## Native compressed sampler comparison

A separate authored fixture links the existing production native backend archive
and uses its texture create/upload/bind, shader/combiner, sampler and GPU readback
interfaces. Every draw verifies an actual BC1_UNORM4x4 one-mip resource/SRV;
the original compressed8 bytes round-trip unchanged.256 draws provide4096 RGB
observations across32 blocks, point/linear filtering and off/on/on/off. Same-mode
and across-mode framebuffers repeat exactly; native noise state remains its
accepted compressed-sampling approximation.

On the observed RTX3080Ti/NVIDIA32.0.16.1686 driver, maximum absolute RGB error
against physical authored blocks is1/2/1 with noise off and8/4/8 with noise on
(out of255). These are measured bounds for this corpus/driver, not universal
limits for arbitrary fractional samples/mips or another native backend. They
retain the owner's accepted HLE appearance and do not add CPU texture decoding.
Opaque alpha255 is checked separately from physical output alpha0.

The exact native archive, observation source/objects/executable, driver metadata,
inputs and outputs are pinned in `build/blinx/noise-native-physical-20261016/`.
The reused archive's current source mtimes provide only a freshness hint;
source-to-archive equivalence was not independently rebuilt for this observation.
Sealed receipt SHA256:
`9ded827c38b82327b190a5be0e440106bc22789e9b8af6834f6cf035afc0ce8f`.
