# Raw and viewport-adjusted projection caches

Tracking: `beads-xbox.1.137`. The dashboard main menu now renders through the
shared native renderer. The fix corrects title profile evidence; it adds no
title-specific matrix arithmetic to the shared draw path.

The original `SetTransform` copies ten raw matrix slots starting at device
offset `c60`. Slot one, at `ca0`, is the raw projection. The complete original
projection/viewport helper passes that matrix as its left input, a constructed
viewport matrix as its right input, and device offset `980` as its destination.
The previous profile incorrectly identified the left input as the output.
Consequently, the native derived-matrix writer overwrote the raw projection.
Fixed-function text then received a matrix containing viewport scaling and
24-bit depth scaling, putting its glyphs outside native clip space.

A six-second focused trace captured 16 actual text draws. Their submitted
FLOAT3 positions/normals and white diffuse colors were valid; the before/after
images were unchanged, and all sampled glyph vertices were outside clip space
under the captured cache. The paired diagnostic now accepts a shader filter
and retains fixed-input/state observations with a distinct versioned format.
It does not fabricate a vertex program or claim general replay qualification.

The profile now identifies `980` as the derived destination. The LTCG binder
checks the original destination-producing instruction separately from the raw
projection input and rejects overlap with the ten raw matrix slots. The old
check had validated the input operand instead. An exact metadata migration
changes only this formerly aliased field; wrapper, record or header drift is
rejected. Retained original CPU bodies remain unchanged.

Nineteen profile/admission checks pass, including original-body proof and
rejection of the old alias and nearby incorrect destinations. The original
projection/viewport helper and original matrix multiply also execute unchanged
under Unicorn for three viewport/projection cases. All 640 bytes of the raw
cache remain unchanged and the separate output matches the expected matrix.
The runtime fixed-function fixture now calls the production transform writer
and checks raw preservation and repeated-update stability; seeding identity
matrices alone had not exercised that boundary.

Eighteen combined Windows checks pass in 8.28 seconds. Actual native runs show
stable Memory/Music/Settings labels and the controller hints. The menu batch
opens Music and captures its item list. Returning with B reaches original
caller-owned resource destruction, which remains guarded. DB03 passes;
DB04 continues with that reached allocator/lifetime boundary. Audio acceptance,
Credits, FINAL builds and the same-framework Blinx regression remain pending.

Private evidence is retained under `build/dashboard-native-cache-20261019`:
the original source/execution receipts, fixed-text diagnostic, profile tests,
matrix batch checks and TCP menu batch. Original XBE bytes and captured title
inputs remain private.
