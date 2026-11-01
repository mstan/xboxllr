# Character material alpha follow-up

Tracking: central Beads `beads-xbox.1.7.16`.

Blinx's shopkeeper and player fur/whisker planes appeared as opaque white
polygons. A separate native 640x480 run reproduced the shop defect with all
enhancements disabled. Captures retained the actual submitted vertices,
texture bytes, render/stage state and before/after GPU images.

The whisker BC2 texture contains the expected thin lines and transparent
background. GPU storage matches the CPU upload byte for byte. The actual
sampled resource, source-alpha/inverse-source-alpha blend and fixed-function
constants were inspected; this was not missing texture data or stale blend
state. A later stage adds independent RGB lighting while its alpha operation
names an unbound texture. Sampling opaque white there replaced the mask
computed by the preceding textured stage.

The shared adapter now preserves the preceding alpha for that later unbound
alpha stage. Its independent RGB operation is retained. Stage zero still
supplies neutral empty-sampler alpha for the Dashboard's factor/diffuse
material. This changes the existing constant-buffer state only: no additional
draw pass, shader variant, game-address special case or interpreter path.

The authored GPU regression uses a real 4x4 BC2 texture with transparent and
opaque texels, followed by an unbound RGB ADD stage. Before the fix, the
transparent case incorrectly produced `(132,72,48)` over blue. After the fix,
it retains the blue `(0,0,64)` destination; opaque texels produce the intended
lit `(132,72,48)`. Both existing Dashboard null-alpha cases and the later
empty-RGB movie compatibility check remain passing. The ordinary GPU and
CURRENT/TEMP result checks also pass.

A new native shop capture shows thin whiskers and clean face/ear fur. This
is observed title correctness plus the explicit native material policy;
general original-XDK fixed-function combiner equivalence remains in
[NTD-17](../runtime/NATIVE-TITLE-DEFERRALS.md). No physical-hardware pixel qualification
is claimed.

The owner suggested [apexJCL/blinx2-recomp](https://github.com/apexJCL/blinx2-recomp)
as an additional reference. Its MIT license and pinned toolkit renderer were
reviewed at game revision `066691b494a9cceb26568996c95a91e33262154c` and toolkit
revision `1c434a090897597da20f00fee5f3129342701191`. That project is useful
for further engine comparisons; this fix extends the existing shared adapter.

Private evidence, including the baseline failing and corrected passing GPU
case, original masks, actual GPU state and isolated native captures, is retained
under `G:/Temp/Codex/xboxllr/tester-diagnostics-20261022/`.
