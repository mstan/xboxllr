# Source-bound float beta declarations

Tracking: `beads-xbox.1.166`, source-bound declarations and disabled blending.
The optional classic profile field `float_beta_fvf` admits a checked original
FVF decoder's declaration behavior. Its complete original body is hash-bound;
the inspector additionally verifies the beta component/type/offset calculation.
The generated capability enables only declaration construction at shader binding.

The checked helper maps XYZBn to XYZ float3 followed by one FLOATn attribute in
register 1. Normal float3, colors and texture coordinates follow those beta
bytes. The helper ignores bit `0x1000`. In the reached `0x1118` declaration,
position occupies bytes 0..11, FLOAT2 beta occupies 12..19, normal occupies
20..31 and FLOAT2 coordinates occupy 32..39. This original Xbox decoder behavior
must be established from its source rather than a desktop FVF interpretation.

The explicit backend layout capability supports FLOAT1 through FLOAT4. The
earlier layout interface keeps its original admission scope. Binding preserves
the original queryable descriptor, and rebinding an ordinary FVF clears the
beta/normal types. Authored layout and ABI fixtures cover those facts, colors,
coordinate widths and repeated device lifetimes.

The reached indexed draw explicitly has vertex blending disabled. The native
disabled-blend consumer preserves all beta bytes in the stride while feeding
position, normal, colors and coordinates into the existing fixed transform.
Runtime admission checks the authoritative original blend cache before
submission and publishes that observed disabled state to the native owner.
The backend requires the known disabled state too. Nonzero inline cache writes
stop explicitly rather than silently using the disabled shader.

Independent GPU checks cover four beta strides and two sampled UVs. Finite,
infinite and NaN values in the unused weights leave the expected pixels
unchanged. Declaration/ABI checks separately verify the normal offset, while
the shared indexed, initializer and immediate regressions remain green.
Actual JSRF gets beyond the beta input checks to its separate active lighting
boundary (`beads-xbox.1.167`).

Active weighted transforms remain guarded until their original matrix/state
consumer exists. Programmable consumers must use their original program and
constants through the native shader owner. The disabled path does not qualify
an active weighted draw or complete title scene.
