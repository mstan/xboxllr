# Files, cache and installed-media LLE

Active serial family `beads-xbox.1.108`, after memory `.106`. Six of nine family
gates remain open. This checkpoint executes original filesystem operations in
a maintained reference runner and corrects native asynchronous notification.
**The title-selected LLE file route is not integrated yet.** Reference cases
alone do not close this family.

## Executable floor and independent evidence

`tools/native_filesystem_reference.py` executes the original kernel's Nt file,
FATX, filesystem-cache, allocator, ISR, DPC and completion routines. Its
`xlle_storage_reference` library reuses the existing IDE and PIC models; it
does not implement another filesystem or substitute an IRP result. The original
driver builds DMA descriptors and the modeled controller transfers real sectors.
Unknown ports, exceptions, invalid gates and execution limits stop the run.

The source image is opened read-only. Writes use the existing controller's
volatile sector overlay; this qualifies within-run write/flush/reopen behavior,
not cross-process persistence. The original software is private owned input.
The maintained hardware library remains a private reference dependency with its
existing license restrictions, outside the intended non-GPL product.

Inputs for the completed qualification:

- Initialized 64 MiB RAM capture SHA-256:
  `31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`.
- Owned 8 GiB HDD SHA-256:
  `8af2d1bb3f292bef122facdd7f9166f06b23ad69d1316bc63e674ed8ffba55dd`.
- Independently extracted comparison file: 1,961,984 bytes, SHA-256
  `832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d`.
  It is a convenient existing file on the owned disk; no dashboard application
  execution or dashboard work is involved.

The runner raises its isolated caller from priority 8 to 31 using original
`KeSetPriorityThread`. This prevents unrelated captured boot workers from
preempting the probe. No ready queue, wait result or completion is edited.
Scheduling under contention and precise interrupt latency remain outside this
fixture. The captured IDT and original handlers perform actual dispatch/EOI.

```powershell
cmake --build build/visibility-lle --target xlle_storage_reference --parallel 4
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_filesystem_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_storage_reference.dll --dependencies C:/msys64/mingw64/bin --hdd ../machine/xbox-hdd-stockdash.img --expected ../machine/hdd/C/xboxdash.xbe --output build/blinx/filesystem-qualified.aot-private.json
```

The completed run records 756 original interrupt deliveries, 6,544 sectors read
and 24 sectors written to the overlay. Qualified cases include:

- Complete original file reads matching the independent file hash, explicit
  offsets, cursor progression, short final reads and EOF.
- Open/missing-name/close/repeated-close and create collision outcomes.
- Real cache resizing through 0/1/2/16/32/2048 pages, rejection of 2049 pages,
  invalidation and subsequent actual reads.
- Original asynchronous reads with an explicit event, real completed DMA,
  immediate EOF/invalid-offset/alignment rejection and zero-length success.
- A 9,001-byte FATX create/write/flush/close/reopen/read/delete sequence with
  generated independent payload bytes and a short-read count check.
- Filtered directory enumeration of that real file, its actual length and the
  end-of-enumeration status; async file notification when no event is supplied.

## Native correction from the floor

An accepted asynchronous request and a rejected request have different effects:

| Original observation | Matching native behavior |
| --- | --- |
| An asynchronous read can return PENDING after real completion has already published bytes/IOSB and signaled its event | Keep actual host pending/completion ownership; do not require artificial delay |
| Immediate EOF or invalid offset clears the supplied event and file notification, preserves IOSB and returns the error | Release private transfer bookkeeping without publishing or signaling completion |
| Completed request with an explicit event signals that event, leaving the file notification unsignaled | Notify the selected event, or the file when no event was supplied; not both |
| Zero-length success publishes zero bytes and signals the supplied event | Retain actual successful completion |

`file_async.inc` previously published pending before knowing whether the host
accepted the request, then synthesized an error completion and signaled both
notification objects. It now publishes pending only for an accepted pending
request and retires immediate rejection without changing the caller's IOSB.
Genuine later failures of accepted pending requests still use the completion
path. Shared read/write tests cover explicit-event versus file notification,
retained ownership, EOF and invalid-offset rejection across repeated lifetimes.

The combined original-memory selection builds and passes **45/46 tests (21.24s)**.
All file/media/storage tests pass. The sole failure is the unchanged minimum
event-wait-duration assertion in existing `.99`; no timing tolerance is altered.
The game-owned native build/doctor pass and **40/40 tests pass (16.52s)** on
runtime `9f151c6`. Actual `local/runs/20261008-182422-attract/` records **190.094s**,
19 captures and no guards. Inspected captures 8/12/17 show Demo play, title
return after Start and Demo reentry. Exit 1 is intentional deadline termination.
Executable SHA-256:
`e04263c4697cf244cad361b4f86ea28cd8b50f882e4f32a559d3c87f189d9f77`.
The game adopts this runtime and the subsequent reference-test/documentation
update; that update does not change the runtime or require another title replay.
This validates the native notification correction, not an original-filesystem
title run, new cadence measurement or endurance plateau.

## Remaining family work and observed differences

Connect original file/cache execution to the title-selected service boundary,
including installed-media identity/mounts, namespace/handle ownership, actual
title read/write modes, directories, completion transport and repeated lifetime.
Use a separate writable reference image for persistence qualification. Preserve
the approved installed-media authentication policy and the optical reference;
no fake drive authentication response is introduced here.

Further independently observed differences await the broader contract pass:

- Original FATX supports `FileNetworkOpenInformation` (34), while the tested
  `FileStandardInformation` (5) request returns `C000000D`. Native currently
  accepts class 5; its desktop query test is not original-correctness evidence.
- A zero-page original cache makes the tested unaligned read fail `C0000017`;
  one cache page suffices. Native can directly read without an internal cache
  allocation. Different implementation resource costs require explicit contract
  treatment, not artificial HLE allocation failure solely to mimic internals.
- Synchronous EOF and missing opens also preserve the tested IOSB. The native
  synchronous/general-open error publication has not yet been reconciled.
- Broader directory enumeration, mount/repeated teardown, cross-run durability,
  pending error timing, cancellation and the title-selected original route
  remain open. Blinx's extracted 3,197 files total 2,235,000,402 bytes and have
  no file names over FATX's 42-byte component limit in the current audit. A
  title-specific installation on a separate HDD image is not yet provisioned.

Phobos remains the native file/path donor reference. Its current Fsc bridge
only stores a page count and cannot provide this executable cache floor; its
host-file strategies remain useful after qualifying their observable boundary.
No new donor implementation body is copied by this checkpoint.
