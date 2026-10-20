# Source-bound float beta declarations

Tracking: `beads-xbox.1.166`, which remains open for the actual drawing consumer.
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

Declaration layout and transform execution have separate acceptance. Fixed
weighted transforms remain guarded until their original matrix/state consumer
exists. Programmable consumers must use their original program and constants
through the native shader owner. Parsing this declaration does not qualify a
weighted draw or complete title scene.
