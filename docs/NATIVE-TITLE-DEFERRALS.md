# Native title precision and capability deferrals

Tracking: `beads-xbox.1.137`. This ledger separates bounded native-title
contracts from unverified precision and optional capabilities. These entries
do not qualify physical hardware or establish complete game/menu behavior.
Keep the original contracts and reusable implementation seams when extending
them; close an entry only with evidence covering its stated question.

| ID | Question / current boundary | Follow-up evidence | Blocks current offline preview? |
|---|---|---|---|
| NTD-01 | Exact NV2A Gaussian sampling centers, interpolation precision and final UNORM rounding. Native `0x2222` owns real raster/resolved images; authored pixels use a one-byte conversion tolerance and a source-grounded kernel. | Focused original/reference pixel comparison; physical hardware if software evidence cannot settle the precision. Preserve the dedicated guest resolve pass. | No; precision remains explicitly unqualified. |
| NTD-02 | Physical interlaced field/scanout behavior. Guest display metadata retains requested `0x20`; host presentation uses progressive windows and the current cadence owner. | Separate original display/encoder qualification. The host window does not establish physical scanout parity. | No. |
| NTD-03 | Elective internal upscale or additional host SSAA together with guest Gaussian `0x2222`. This combination is guarded to avoid silently losing enhanced detail in the logical-size resolved image. Blinx's `0x11` visual enhancements remain supported. | An enhanced resolve owner preserving both guest logical readback and higher-resolution host output, with focused resource/pixel checks. No physical hardware prerequisite. | No for the original dashboard profile; the enhancement stays unavailable. |
| NTD-04 | Exact RGBA8 hardware dithering matrix and rounding. Original dashboard setup requests dither; local xemu GL delegates it to implementation-dependent host behavior and Vulkan leaves it unresolved. The shared initializer owner retains the request and exposes its explicit native RGBA8 quantization policy. | Source-grounded algorithm or bounded original pixel captures. Preserve the reusable precision-policy/effect seam; current native-policy pixels do not qualify NV2A dithering. | No; focused native-policy checks pass with format/consumer guards. Actual startup passes the original initializer and proceeds to initial asset loading; menu rendering remains unverified. |
| NTD-05 | Optional Live, DVD and firmware/title-launch paths outside the supported offline dashboard menu. Missing imports and unsupported consumers remain explicit. | Reach and implement each capability under its own original contracts and native CPU coverage. | No, unless actual offline menu execution reaches a dependency. |
| NTD-06 | Original XNet random seed/stream equivalence and deterministic replay. The reached random-byte extension preserves owner precedence, buffer extent and XOR semantics while using host cryptographic entropy; the original seeded stream is separately retained as reference evidence. | A source-grounded owned PRNG or recorded-entropy interface with snapshot/restore and agreed simulation-profile identity before any netplay/rollback qualification. Native random output does not establish original-seed equality or cross-peer determinism. | No for the offline dashboard preview. Bounded component checks and the actual startup request pass. This profile makes no netplay/rollback compatibility claim. |

Implementation and evidence:

- [Guest presentation](NATIVE-GUEST-PRESENTATION-2026-10-19.md): resource,
  filtering, profile admission and precision boundaries.
- [Section data mods](NATIVE-SECTION-MODS.md): native-only loader scope,
  immutable original identity and title-reviewed relocation reservations.
- [Initializer states](NATIVE-GRAPHICS-INITIALIZER-2026-10-19.md): bounded
  state/cache ownership and the explicit native color precision policy.
- [Dashboard burndown](NATIVE-DASHBOARD-BURNDOWN.md): actual reached behavior,
  remaining menu/controller/audio/Credits work and same-framework Blinx check.
