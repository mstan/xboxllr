# Native XAPI kernel startup owner

Tracking: `beads-xbox.1.137`. Adapter `native-xapi-kernel-startup-v1` is an
explicit native execution-domain contract. It is not kernel CPU execution,
a mapped substitute kernel image, or host GDT access.

## Reviewed component

Owned original dashboard5960 SHA256
`832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d`
contains these reviewed boundaries. Complete body hashes and RET cleanup are
stored in the dashboard startup proof fragment; names/addresses alone cannot
select the adapter.

- 00072966,197bytes/RET0: XapiApplyKernelPatches. The name is supported by
  XbSymbolDatabase Xapi/4831's mainXapiStartup call annotation and matching owned
  startup order. Its legacy patch cases cover3944,4034,4039,4816..4818.
- 00072902,100bytes/RET0: inspect the kernel's final INIT section and prepare a
  code-selector8 limit. Its exact public source name remains unqualified.
- 00072AC4,37bytes/RET8: SGDT, atomically exchange selector8's two descriptor
  words under CLI/STI, far-jump to reload CS, return old low word in EAX/ECX and
  high word in EDX.
- 00072A6C,86bytes/RET4: IDE quiescence followed by saved-descriptor restoration.
  Its body is proved as a consumer, **not replaced**. Device operations remain
  original/guarded until their own native owner exists.

Only the first three receive wrappers. Original bodies are retained with renamed
symbols; ORIGINAL mode still reaches their existing privileged-operation guards.
The binder modifies declaration names without normalizing LF/CRLF body bytes.

## Source identity versus native policy

Two distinct owned sources were independently read and verified:

| Source | SHA256 | Observed facts |
|---|---|---|
|64MiB physical RAM capture|`eacc9552abe48622644d450aaf48b1822d598659968358e5de559a53dec4a7b1`|Kernel4983; GDT8004FCD8, selector8 `00CB9B000000FFFF`; PCR8003DA3C, PRCB8003DA64; PRCB+250 is zero.|
|458752-byte flat kernel|`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`|PE base80010000, version1.0.4983.6, final INIT RVA41520, VA80051520.|

The descriptor has base0, ring0, present/readable/accessed code,32-bit operands,
4KiB granularity, and effective limitBFFFFFFF. Original setup selects descriptor
`00C89B0000000051`, effective limit80051FFF. It includes the INIT start's partial
page exactly as the original algorithm does.

The framework already advertises **native5838 compatibility**. This adapter
retains that selected policy; it does not claim either source contains a5838
kernel. Both observed4983 and selected5838 take the proved no-legacy-patch
branch. Descriptor/INIT observations are selected as source-derived native model
inputs. Aligning the framework's version policy with its original reference is
an open follow-up under the owning issue, not a silent change to Blinx.

Local nxdk/Cxbx declarations do not name PRCB+250. Original kernel consumers use
it as a pointer with bugcheck/debug flags and debugger callbacks; dashboard
startup also reads callback+24. A debug-monitor interface is the supported
inference. The exact field name remains unqualified. The donor's old 'D3D cache'
comment does not describe this observed consumer. No nonzero PRCB value is
fabricated to skip setup.
Apply also checks the current runtime-owned native FS/PRCB relationship before
following it: FS must be aligned/in range, FS+20 must contain FS+28, and that
PRCB's current +250 word must be zero. A retained arbitrary pointer or active
debug-monitor owner rejects before descriptor initialization or saved-word
publication. The offline observation never substitutes for this live check.

## Native observable contract

`startup.inc` requires explicit `PILOT_STARTUP_NATIVE`, export-only kernel5838,
IO/device OFF, USB OFF and native scheduling. Runtime identity words must match
that policy, with interrupts enabled and no interrupt shadow. A host SRW lock
serializes one native CPU descriptor owner. It records the actual previous
modeled descriptor, publishes both saved guest words, and exchanges/restores it
on the reviewed descriptor call. No host selector/register is accessed.

Only the initial and derived restricted descriptor can be exchanged. Unknown
call sites, descriptors, hardware/version identities, disabled interrupts and
mixed original-device ownership guard. Callee-saved registers remain untouched;
exchange returns old low in EAX/ECX and old high in EDX with RET8 cleanup.

The startup caller immediately enters its next initializer, whose first SUB
writes arithmetic flags before any flag use. Exchange itself preserves arithmetic
flags; its observed callers only store outputs or return. Reviewed return sites
are guarded. SGDT's transient writes to subsequently discarded argument stack
bytes are not reproduced as invented host-GDT addresses. No admitted caller reads
those bytes after the call.

## Validation and limits

- Authored native model/ABI tests pass MSVC C11/O2/W4/WX and GCC C11/O2 with
  Wall/Wextra/Werror, including contract/caller/IF/shadow/version negatives.
- Authored binder tests cover body/RET/consumer/contract rejection before writes,
  original-body preservation, CRLF, missing declarations and idempotence.
- `tests/check_native_startup_original.py` executes the complete owned original
  ApplyKernelPatches/setup/exchange and a restoring exchange in Unicorn using
  the verified source RAM. It checks descriptor/state, EDX:EAX/ECX, nonvolatile
  registers, RET cleanup and unchanged exchange flags. Code execution is confined
  to those XAPI bodies; no original kernel instructions or host GDT access occur.

Deferred native CPU segmentation scope (D-NATIVE-STARTUP-01, owning issue above):
full x86 instruction-fetch limit faults, #GP priority/error codes, general far
control transfers and arbitrary SGDT consumers are not reconstructed. Current
admitted title functions are below the selected code limit and retain the
existing bounded generated-code lookup; native kernel ABI thunks are bridges,
not guest instruction fetches. Reopen if a title needs another descriptor,
executes across the limit, consumes discarded SGDT scratch bytes, requests a
debug monitor, selects another kernel/device policy, or reaches the IDE teardown.
No silicon, timing, cross-build netplay, or successful dashboard boot claim is
made by these component tests.
