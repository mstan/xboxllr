# Memory and resource lifetime LLE

Active serial family `beads-xbox.1.106`, after display `.105`. The family remains
open: seven of nine gates remain. General virtual memory now has a selectable
original-kernel route; this is one coherent part of the memory/lifetime family.
LLE performance is not an acceptance requirement.

## Shared virtual-memory contract

`PILOT_MEMORY=NATIVE` retains the native implementation. `PILOT_MEMORY=LLE`
executes the original NtAllocateVirtualMemory, NtFreeVirtualMemory,
NtProtectVirtualMemory and NtQueryVirtualMemory through the existing maintained
kernel interpreter and initialized capture. It requires `PILOT_IO_DEVICE=LLE`
and `PILOT_USB=LLE`, which currently select the shared original pool, contiguous
allocation, PTE/PFN and DMA bridge. Input and display may remain native.
There is no live switch or duplicate production allocation.

The original kernel owns reservations, physical pages, zeroing, protection,
queries and release. The adapter publishes actual allocated pages into the
native guest address space and shares the same backing with interpreter aliases
and pinned DMA. Host access protection mirrors the original query results.
Kernel accesses temporarily lift host page restrictions on the affected range
while the single guest CPU executes; caller permissions are restored. Native
ownership records describe returned allocations; they do not substitute PTEs.

Supported inputs include ordinary reserve/commit and recommit, rounded ranges,
read-only/read-write/no-access protection, query, decommit, whole and partial
release, top-down allocation with ZeroBits=0, and failure/output preservation.
Allocation placement can differ between backends. Valid pointers, non-overlap,
data preservation/zeroing, protection, ownership and reported outcomes cannot.
The runtime's existing 64 MiB admission limit remains; the original allocator
can also reject allocation because real kernel pages already occupy RAM.

`tools/native_memory_reference.py` independently executes the owned kernel
software without substituted callees and records 50 cases. The snapshot hash
is `31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`.
Its shared Unicorn transport does not independently qualify x86 page faults:
query/PTE effects come from original execution; caller access permissions are
also tested through actual host read/write protection in the maintained route.

Original execution corrected native assumptions:

| Operation | Observed original contract and native correction |
| --- | --- |
| Sized MEM_RELEASE | Releases the rounded pages; surviving prefix/suffix become independent reservations, including a middle-page hole |
| Zero-length release | A base within the first page rounds down and releases the reservation; another interior page returns C000009F |
| Release extending outside reservation | Returns C000001A, preserving caller arguments and the live reservation |
| Query free storage | Protect is PAGE_NOACCESS; AllocationBase/AllocationProtect/Type are zero |
| User-space endpoint in the pinned kernel | Query rejects 7FFE0000 and above; an unconstrained top-down 4 KiB reserve returns 7FFD0000 |

Run the private original-code qualification:

```powershell
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_memory_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --output build/blinx/memory-reference.aot-private.json
```

Configure the existing pilot with `-DPILOT_MEMORY=LLE -DPILOT_USB=LLE` and its
existing owned calendar/kernel/device inputs. No new firmware asset is shipped.
Native focused memory/statistics tests pass; the selected original path passes
the same bounded allocation, protection, partial-release, query and two complete
600-allocation/reuse cycles. The combined original selection passes **46/46
tests (21.09s)**, including no-access release and original MEM_NOZERO forwarding.
Graphics fixtures reserve their declared static
image in the original allocator, just as the production loader does. Cache
budget failure injection does not assume that the original kernel leaves every
console page free; actual original allocation failure is tested separately.

Actual `memory-lle-nozero-attract-20261008` completes **190.203s**, 19 captures,
no guards, with a deliberate harness deadline (exit 1). Captures 8/12/17 show
Demo play, title return after Start and Demo play reentry. Executable SHA-256:
`a8b269b2717e36f2cf0bc1d1999b307dd6a29ba97a4f5583d9ce296bfaef7d22`.
Input/display remain native while virtual memory, pool/contiguous allocation
and the existing page bridge are selected original services. This checks title
progress, not a new speed or endurance claim.

## Limits and remaining family work

The next memory batch also executes original ExQueryPoolBlockSize,
MmQueryAddressProtect, MmQueryAllocationSize, MmQueryStatistics and saved-display
pointer get/set. Native ownership records are checked against actual returned
extents/policy; they do not replace these original queries. Original statistics
include the captured kernel's own allocations and the actual page-table cost.
HLE may have different allocation placement and implementation overhead. Its
counts must still describe its live modeled allocations, not invented capacity.

Loaded image and registered stack pages now acquire original physical backing
when made resident, retaining the real loader bytes. Reserved deferred pages
remain uncommitted; section unload retires their backing when the last page owner
releases it. Tests cover idempotent residency, shared CPU/DMA bytes, exact pin
boundaries, release access faults and 32 load/release counter round trips.
The original bulk allocator gets a larger bounded instruction budget because
Unicorn counts REP iterations while it actually zeroes megabytes of RAM. This
does not skip zeroing or treat an unfinished call as complete.

Original contiguous allocation can also relocate an **unpinned** virtual page
to obtain the requested physical range. A forced-range original-code probe
demonstrates the changed PTE and preserved page bytes; the same request fails
while that page is pinned. The maintained bridge now preserves the original
copy's results, changes its physical-backing index, and refreshes interpreter
aliases before publishing the contiguous allocation. Caller VA/data stay stable.
CPU/DMA tests verify the new physical address, independent contents of the
contiguous allocation, pin protection and allocation-counter restoration. This
was discovered by actual Blinx execution after materializing all loaded pages;
preventing compaction by pinning all resident pages would not satisfy the floor.

At checkpoint `55dddc9`, qualification of this next batch was **not complete**. The 50 original cases and
46/46 shared tests (24.21s) pass. Actual `memory-relocation-retimed-20261008`
gets through startup and movie/title rendering, then reaches the disc-error
screen during attract loading. `memory-residency-accounting-20261008` identifies
a genuine original `C0000017` allocation failure: six available pages,
48,848,896 committed VM bytes, 88,186,880 reserved bytes, 14,500 published pages.
The old small-VM checkpoint's attract result does not qualify full residency.
That checkpoint was not adopted by the game pin.

The HLE texture allocator previously allocated a separate 4 KiB page per header.
The failed run's 500 texture creations total 12,701,788 payload bytes but
2,641,828 additional bytes of header pages/data rounding (creation totals, not
a live-allocation census). Texture and surface-view headers now reuse the
maintained small-pool service: original pool allocation in the selected floor,
native pool allocation in HLE. Header layout, references and binding ownership
are preserved; allocation addresses may differ. Payloads still own their real
pages. No capacity inflation or false success is introduced.

The 520-live-texture test now measures **2,170,880 committed guest bytes** on
the original allocator selection, versus the old layout's 4,259,840-byte page
cost. Both complete release/reuse cycles retain actual uploaded/readback pixels
and restore the starting commitment. Pool ownership assertions replace the old
VM-reservation assertions for headers. Combined tests pass **46/46 (20.36s)**;
the additional footprint assertions pass the focused graphics test (2.31s).
An initial title probe reaches visible Demo play and then the subsequent movie,
with real deferred-section unload. It was interrupted before the harness wrote
its final manifest, so its 15 captures are progress evidence, not a completed
qualification run. A controlled interruption/reentry run remains in progress.

This is the memory boundary of the existing native loader and thread provider.
It does not claim original Xe loader, filesystem-cache or thread-construction
semantics. Those providers remain in their later serial families. Image/stack
backing created through Nt allocation is reported by that original allocator;
it does not reproduce the original loader/thread allocator's usage tags. The
captured kernel's cache pages are distinct from the selected native file cache.
Thread stacks currently last for the title process lifetime; recycling belongs
with original thread termination/object lifetime qualification. These boundaries
must not be mistaken for exhaustive original memory statistics across providers.

- Nonzero ZeroBits address-constrained search is explicitly guarded in the
  original route. The exhausted low-address case enters an original interrupt
  path not qualified by this transport; the native-only failure test is not
  claimed as original evidence. No such request has been qualified for Blinx.
- Guard/cache policies, dynamic executable-code invalidation and arbitrary CPU
  physical aliases remain outside the declared VM slice. Unknown behavior stops.
- Pool/contiguous queries, statistics and residency now have original execution
  and focused tests. Actual title qualification and the consolidated resource
  lifetime audit remain pending for this next batch. Existing USB pinning and
  graphics alias/lifetime evidence must be retained.
- The slow combined input/display run recorded private host memory growing from
  768,409,600 bytes at 30s to 1,013,420,032 at 510s. Its cause and eventual bound
  are not established; it is not proof of a guest allocation leak. Keep this
  observation separate from any production HLE endurance claim.
- VM checkpoint `c65629b` is integrated by private Blinx commit `98eb899`.
  Its native title run reaches Demo/interruption/reentry in 190.125s with no
  guards. Native tests initially pass 39/40: the graphics clock's process-wide
  handle equality fails once, then passes three unchanged focused reruns.
  That failure remains recorded, and the assertion now prints both counts on
  failure; its cause is not yet established. Do not close the family based on
  VM fixtures alone.

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` memory services were inspected.
Its guest/host namespace separation informs comparison, but its coarse query
and no-op decommit bodies are not adopted. No new donor implementation or GPL
body was copied. Original software remains owned private input; existing private
pilot dependency restrictions remain unchanged.

