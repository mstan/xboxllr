# Native shutdown registration ownership

Tracking: `beads-xbox.1.137`. This module implements registration state for
`HalRegisterShutdownNotification` (ordinal47), not shutdown, callback delivery,
firmware entry or physical power control.

## Primary contracts

Local nxdk declares VOID/NTAPI `(HAL_SHUTDOWN_REGISTRATION*, BOOLEAN)` and a
16-byte x86 structure: callback pointer0, signed LONG priority4, LIST_ENTRY8/12.
The callback ABI is VOID/NTAPI with one registration-pointer argument.

The owned flat kernel SHA256
`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`
(version4983) export47 is800182C9..80018350. Its135-byte body SHA256 is
`39e6cc24dbaf388015151d68f627473996d10d79df29f36f178902d72725d514`.
Independent disassembly establishes:

- Insert by descending **signed** priority; equal priorities remain FIFO.
- Unregister by registration-object address. An absent address is a no-op and
  is not dereferenced, including an arbitrary nonmember pointer.
- Insertion writes the new node's two links and its two neighbors. Removal
  updates neighbors but preserves the removed node's own stale link words.
- Register observes the BOOLEAN's low byte. VOID has no success result; native
  transport preserves EAX and consumes return address plus8argument bytes.
- The original critical section raises/lowers IRQL around list operations.

The original firmware implementation drains this list for
`HalQuickRebootRoutine` (value2), removes each first node before invoking its
callback, and passes the registration object's address. That observation is
not permission to pretend the later firmware/device lifecycle completed.

## Native owner and integration

Include `kernel_shutdown.inc` only for the native IO/device-off route. Keep
original device ownership for IO_LLE builds. Root allocates and commits a distinct
8-byte native guest sentinel (currently export backing+8000); it must not overlap
signing keys or another exported object. Then call
`kernel_shutdown_initialize(head)`. The module does not select an address or
commit arbitrary guest pages itself.

`kernel_shutdown_dispatch(ordinal)` recognizes only47. Registration stores real
callback/priority/address ownership and maintains actual guest LIST_ENTRY links.
Only a nonzero callback resolving through the generated-code catalog is admitted;
this establishes address ownership, not callback execution/ABI qualification.
No callback executes during registration or unregister. A short host SRW lock
serializes publication; no guest callback, scheduler handoff or blocking wait
occurs while that ownership lock is held.

`kernel_shutdown_owns_range(address,bytes)` supports allocator lifetime checks.
Registration storage must remain mapped and unchanged until unregister. The
module detects callback, priority or link mutation before subsequent operations;
allocator integration can reject freeing/unmapping an owned range even earlier.
An unregistered object can be reused/re-registered and becomes the newest member
of its priority group. There are256simultaneously active records; capacity,
overlap, duplicate registration, address wrap and owned reinitialization reject
before modifying the ledger/list. Direct mutation is an explicit unsupported
usage, rather than silently replaying stale host metadata.

**Delivery remains guarded.** `kernel_shutdown_delivery_guard()` always stops
with the retained callback count and performs no callback/removal/power effect.
The runtime's ordinal49 guard must remain until a real native shutdown phase owns
callback execution, guest stacks/CPU eligibility, ordering, unregister during
callback, actual device drain and the requested process/firmware transition.
Closing a host window or ending an automated test does not claim that firmware
lifecycle ran. No pending registrations are discarded as fabricated completion.

## Validation

`tests/test_native_kernel_shutdown.c` passes MSVC C11/O2/W4/WX and GCC C11/O2
Wall/Wextra/Werror on the current Windows host. Authored tests cover signed
extremes, FIFO ties, exact guest topology, retained removed links, absent removal,
BOOLEAN byte behavior, re-registration, range ownership, VOID stack/register
behavior and zero callback execution. Ten isolated fatal cases check capacity,
duplicate/overlap, live mutations, invalid callback/address, reinitialization
and delivery guard; failures preserve the guest bytes, ledger, stack and EAX.

`tests/check_native_shutdown_original.py` separately executes the original
export47 plus its actual bounded raise/lower-IRQL helpers in Unicorn with authored
registrations and an explicitly empty pending-interrupt state. Signed order,
FIFO ties, stale links after removal, unknown removal and BOOLEAN behavior all
pass across523original component instructions. The fixture traps execution
outside those list/IRQL bodies. It invokes no callbacks and performs no device or
power operations. This is a source-derived list contract, not silicon timing,
interrupt arbitration, cross-build netplay or successful dashboard boot proof.
