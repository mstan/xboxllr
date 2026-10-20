# Native indexed mesh submission

Tracking: `beads-xbox.1.137`. This extends the shared native renderer; it has
no game-name conditions or title-owned rendering implementation.

The exact dashboard profile proves `SetIndices` (134 bytes),
`DrawIndexedVertices` (724 bytes), `CreateIndexBuffer2` (52 bytes), device
`AddRef` (21 bytes) and `Release` (55 bytes). Discovery signatures remain
historical metadata: the admitted original body and corrected signature
establish the actual ABI. The indexed draw reads three stack arguments and
returns with 12-byte cleanup. The existing adapter already preserves that ABI.

## Ownership and submission

- Original `CreateIndexBuffer2` allocates a 12-byte header followed by index
  words. The native creator preserves that layout, real CPU storage, reference
  counts and allocation failure. Original-created headers are borrowed without
  changing their already published data pointers.
- Index binding acquires the new internal reference before releasing the old
  one, including self binding with no remaining user reference. It publishes
  the original CPU-data global, index header and base-vertex cache. The global
  is an exact-image profile fact checked against both setter branches.
- A borrowed index header contains no byte length. The consumer does not
  invent a length from its surrounding heap page. Consumed CPU words use the
  real guest mapping; stream accesses are bounded by their actual vertex
  owners after applying the base vertex and index minimum/maximum.
- The existing draw preparation still owns shaders, typed input layouts,
  transforms, texture uploads, render states, target hazards and completion.
  A reusable native GPU index buffer submits real `DrawIndexed` calls. CPU
  vertices are not expanded. Existing nonindexed and user-pointer draws use
  the same preparation with their original submission route.
- Sixteen-bit source indices widen into cached native 32-bit index storage.
  This preserves valid `0xffff` strip indices instead of triggering D3D11's
  implicit 16-bit strip restart. Fan/quad topology conversion changes index
  sequences only, preserving the existing flat-color conventions.
- Index-buffer host ownership and state-block references are distinct from
  vertex-buffer ownership. Last use records actual native submission progress;
  idle checks use the existing completion owner. Caller storage stays intact
  when its borrowed host view retires. Actual archive-unload reclamation by
  the original guest allocator still requires menu/lifecycle qualification;
  host-view retirement alone does not prove it.
- Device `AddRef` and nonfinal `Release` update the original profile-owned
  count without closing graphics. Final release follows the existing complete
  teardown and clears the original device storage. Original inline reference
  edits remain visible at the next native boundary.

Profiles gain these consumers by publishing the proved index-data and device
reference fields. Previously generated profiles keep their existing API
admission; absent enum entries are not assumed. Exact additive metadata refresh
preserves generated CPU bodies and wrappers and rejects unrelated drift.

## Evidence and remaining scope

Seventeen source/profile tests pass, including original method/layout checks,
wrong-field rejection and exact metadata upgrade. Twelve Windows checks pass
in 8.63 seconds, covering 20 fatal guards, two device lifetimes, owned/borrowed
index headers, inline references, live CPU edits and real pixels. Dedicated GPU
checks compare list, strip, fan and quad draws with expanded reference inputs,
flat/smooth colors, positive/negative bases, unaligned words and index `0xffff`.
Existing nonindexed renderer and programmable-shader/reference checks pass in
the same batch. All 104 generated CPU objects were retained.

Actual native execution submits a 6,624-index mesh spanning 1,223 vertices,
passes device `AddRef`, and creates subsequent mesh index/vertex buffers. The
next stop is a missing native indirect CPU entry at `d258f`, an original mesh
lock wrapper. This is not a complete menu or navigation pass. Retained run:
`build/dashboard-native-cache-20261019/host/runs/20261019-201301-e06uvv8z`.

Ordinary GPU image readback remains available. Programmable-only raw input
capture does not yet serialize indexed/fixed-function inputs; those optional
diagnostics retain explicit guards. No physical hardware is needed for the
current software dependency or the remaining first-menu work.
