# Read-only native SSE control

Tracking: beads-xbox.1.158. After the geometry capacity correction, JSRF reaches
its original Now Loading screen and executes STMXCSR in a buffer initializer.
The admitted original inventory contains one STMXCSR and no LDMXCSR. Its only
saved-value consumer checks rounding-control bits 13/14 and rejects modes other
than nearest. Those source facts limit the reached workload; the implementation
is a shared CPU instruction, with no title-specific address or success value.

The framework-owned instruction adapter stores the actual query result through
the normal guest DWORD addressing interface. It preserves CPU condition flags
through the donor's CFG-aware flag state. Missing/invalid operands and guest
SSE-control writes remain explicit guards. The donor checkout is unchanged.

On the x64 ABI, SSE is mandatory and the helper reads MXCSR with the compiler
intrinsic without changing it. Other targets use standard C fenv to report the
native rounding mode and represented exception flags under the masked reset
control profile. An unrecognized rounding mode stops explicitly. No x86 import
or assembly enters that universal path. This read does not change rounding,
exception masks, sticky flags or denormal controls. Existing native conversions
and callbacks retain their established floating-environment ownership.

Exact guest sticky exception provenance across all native arithmetic, unmasked
trap delivery, and portable denormal-status/DAZ/FTZ behavior remain unqualified.
The portable path does not claim nondefault guest control writes or cross-build
floating-point equivalence. A reached consumer of those features requires
additional modeling; it cannot be admitted by returning a constant success.

Authored instruction fixtures pass through the pinned disassembler, translator,
framework extension and optimized native compiler. Forty absolute/indexed stores
cover four rounding modes, CPU flag consumers, register/x87/environment
preservation and neighboring bytes. The original 8,960 conditional-move and 128
min/max checks still pass. Separate host fixtures execute both the x64 and
forced portable paths with clear/raised status and every rounding mode. A
representative AArch64 object compiles using automatic architecture selection;
this is compilation evidence, not an ARM execution claim.

The actual regenerated JSRF image now lowers five extended floating-point sites
with no guarded site in that report. It passes the original STMXCSR rounding
check, loads additional scene geometry, and reaches a separate texture-state
guard. This establishes the reached query consumer, not complete attract output.
