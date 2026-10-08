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
software without substituted callees and records 38 cases. The snapshot hash
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

- Nonzero ZeroBits address-constrained search is explicitly guarded in the
  original route. The exhausted low-address case enters an original interrupt
  path not qualified by this transport; the native-only failure test is not
  claimed as original evidence. No such request has been qualified for Blinx.
- Guard/cache policies, dynamic executable-code invalidation and arbitrary CPU
  physical aliases remain outside the declared VM slice. Unknown behavior stops.
- Original pool/contiguous query paths, statistics, loaded image/stack physical
  residency, and resource-retention coverage still need a consolidated audit.
  Existing USB pinning and graphics alias/lifetime evidence must be retained.
- The slow combined input/display run recorded private host memory growing from
  768,409,600 bytes at 30s to 1,013,420,032 at 510s. Its cause and eventual bound
  are not established; it is not proof of a guest allocation leak. Keep this
  observation separate from any production HLE endurance claim.
- Final native/title regression and game pin integration are pending for this
  memory batch. Do not close the family based on VM fixtures alone.

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` memory services were inspected.
Its guest/host namespace separation informs comparison, but its coarse query
and no-op decommit bodies are not adopted. No new donor implementation or GPL
body was copied. Original software remains owned private input; existing private
pilot dependency restrictions remain unchanged.

