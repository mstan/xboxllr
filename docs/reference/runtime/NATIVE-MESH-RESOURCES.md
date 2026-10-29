# Native mesh methods and resource storage

Tracking: `beads-xbox.1.137`. Mesh method discovery belongs to the dashboard
title profile; allocation and retirement belong to the shared renderer.

## Original virtual methods

Original constructors `d247e` and `d2d4c` publish tables `13938` and `139a8`.
All 54 slots were audited against the current CPU inventory. Fourteen missing
entries, totaling 396 original bytes, are now generated through the existing
pinned native CPU lowering:

`d250f`, `d252b`, `d258f`, `d25ab`, `d2743`, `d2756`, `d2e4d`, `d2e57`,
`d2e61`, `d2ea5`, `d2eca`, `d2edf`, `d6451`, `d93b4`.

The reached lock wrapper at `d258f` is 28 bytes, including its `RET 12`. It
calls the already admitted vertex-buffer lock and publishes its real result.
Constructor/table/body hashes live in `titles/dashboard/native-seeds.contract.json`;
the fresh-generation seed list and incremental recovery plan retain the same
entries. Original return-only unlock methods remain original code, not new
success substitutes. Generating a method does not establish that it ran.

The shared recovery transaction staged and published all 14 entries together.
Only the dispatch unit and those new CPU objects compiled; 103 unrelated
existing CPU objects stayed intact. Thirteen extent and six dispatch checks
pass. Actual startup now builds and converts the dashboard's text meshes.

## Created vertex buffers

The new original CPU coverage exposed a shared allocation mismatch. The
original creator at `1481b0` stores `Data & 0x0fffffff`; lock at `148200` and
inline CPU consumers read `Data | 0x80000000`. Ordinary virtual data cannot
satisfy that physical/cached-alias contract. Actual conversion faulted at
`811a0000` when an old native virtual pointer was treated as a physical offset.

Created vertex buffers now use the shared real contiguous-memory owner. Their
headers contain the original physical offset and locks return the same cached
CPU alias used by original inline consumers. Uploads use those actual bytes;
release frees that same physical owner and its separate header. Registered
caller-owned buffers retain their existing backing leases. This correction
applies by default to every title using the shared native allocator.

## Original destruction boundary

The original release path may call `D3D_DestroyResource` while the last user
count is still one; another inline path can publish zero first. Native-owned
vertex, index and texture objects now retire through the existing real
GPU/CPU/header owners in either case. Additional references and internal
bindings remain explicit guards. An original allocation is not passed to an
unrelated native allocator, or vice versa.

Destruction through an original caller-owned allocation remains guarded until
its original allocator retirement is admitted. Existing borrowed-view teardown
preserves that caller's storage and does not qualify guest heap reclamation.
Any reached dependency on that path remains required product work.

Twelve integrated Windows checks pass in 8.92 seconds, including 22 guard
children, real cached-alias CPU edits reaching indexed GPU pixels, direct
destruction with count one/zero, reference/binding rejection and repeated
device lifetimes. Existing renderer/shader, audio and scheduling checks pass in
the same batch. The source/profile checks for indexed drawing remain separate.

Actual executable `f2caf6a8420cc42d...` passes mesh generation, cached-alias
conversion and temporary resource destruction. It next reaches edge AA state
151 with value one; its native consumer is not admitted yet. No menu frame
has been captured. Retained run:
`build/dashboard-native-cache-20261019/host/runs/20261019-203939-2otp2lbm`.
