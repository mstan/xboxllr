# Native kernel services: no speculative port

Branch `spike/kernel-services-audit-20261004`, parent `54c102a`;
tracking `beads-xbox.1.54`. This is a bounded audit, not an implemented or
benchmarked kernel replacement.

## Donor opportunity and contract mismatch

phobos `src/kernel/kernel_bridge.c`, reviewed at `f01b20d` with remote delta
through `7884426` checked (only the unrelated fog-coordinate fix):

- `bridge_RtlMoveMemory` / `bridge_RtlZeroMemory` call host memmove/memset.
  Whole-routine memory helpers are legitimate HLE candidates, with guest page
  translation, overlapping-copy semantics, GPU residency and invalidation handled.
  Copying this donor's null-pointer suppression and unconditional result into an
  unaudited Xbox call would not establish that contract.
- `bridge_KeSetEvent` / `bridge_KeWaitForSingleObject` use guest SignalState,
  notification/synchronization event distinctions, generation counters and host
  wake/wait primitives. These are useful mechanisms. The bridge also relies on
  its runtime's host/shadow thread and object ownership.
- Our guest kernel still owns scheduler wait queues, runnable threads, interrupt
  delivery and related object lifetime. Blocking the sole guest CPU executor on
  a host wait can prevent the guest thread or interrupt that signals it from
  running. Installing only the donor wait/set pair is therefore not a complete
  subsystem replacement. A native scheduler is possible, but substantially wider
  than a two-function adapter. Nominal HLE timing flexibility does not remove
  the need for actual wakeups and completion.

## Current dashboard evidence

Retained baseline `54c102a`, private replay `r2-xdk-direct-r1-leg00-A-r1`, final
1.5-billion guest-clock interval:

- 91,925,180 native, 40,306,151 fallback and 1,367,568,680 fast-forwarded idle
  instructions. Those idle instructions are already skipped cheaply.
- Covered kernel native regions total 2,258,187 instructions; 2,238,258 are the
  `0x8001e3e3` idle boundary. Only 19,929 covered native kernel instructions
  remain. This does **not** attribute all fallback CPU time or prove kernel
  services are globally cold.
- The 4,526,619 native/fallback handoffs are not a count of replaceable kernel
  services. One observed large exit population is the idle-loop boundary.
- Audited running-kernel export addresses: KeSetEvent ordinal 145 `8001d8c0`,
  KeWaitForSingleObject ordinal 159 `8001d083`, RtlMoveMemory ordinal 298
  `800269c0`, RtlZeroMemory ordinal 320 `80026990`. The existing covered-native
  profile does not demonstrate a substantial hot memory-helper opportunity.
- In contrast, the matrix loop alone accounts for 24,760,017 native instructions
  and has a narrow, independently testable caller contract. Prioritize that
  actual replacement and the graphics experiments.

## Decision and reopening condition

**Park this audit; port no kernel services in this round.** No speed result is
claimed. Do not spend an unbounded scheduler rewrite against an unmeasured
opportunity, or treat a host wait stub as a performance optimization.

Reopen with a host sample or bounded entry/call attribution showing a material
cost in a specific service, or a demonstrated guest busy-wait whose complete
wait/wake ownership can be replaced. Preserve the donor event-generation model
as a design reference, and use a build-selected native kernel/scheduler boundary
if that larger replacement becomes justified. This decision preserves LLE and
the option of genuine whole-subsystem HLE; it imposes no instruction-history or
exact-arithmetic restriction on a future implementation.
