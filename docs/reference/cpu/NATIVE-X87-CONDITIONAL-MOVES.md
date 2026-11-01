# Native x87 conditional moves

Tracking: beads-xbox.1.153; actual title symptom beads-xbox.2.2.

JSRF's logo fade advanced a component by 1/120 each frame, then its clamp helper
replaced that component with the wrong bound. Native watchpoints traced both
writes. The original helpers use FCOM, FNSTSW and TEST AH before FCMOVE/FCMOVNE.
The pinned donor emitted those moves as bare `FPU:` comments, so the value
selection never ran. This was an instruction coverage gap, not a stalled frame
clock. Native generation's zero failed-function count did not expose this gap.

`native_x87_lifting.py` extends the immutable pinned Phobos CPU tools inside the
framework-owned generation process. Its block adapter reuses the existing
CFG-aware CPU CF/ZF/PF expressions for all eight FCMOV conditions. It preserves
the flag state across the move, x87 condition codes and stack TOP. Ordinary
instruction spans use the pinned generator unchanged. Missing CPU flag evidence
or unsupported operands emit a nonreturning instruction guard.

Other unsupported x87 instructions also guard explicitly; a remaining bare
FPU fallback comment fails generation hardening. A private generation report
records each lowered/guarded original site. The original donor checkout and
image remain unchanged; generated original code stays private.

The reached nonwaiting FNCLEX instruction clears the represented exception,
stack-fault, summary and busy status bits while retaining C0/C1/C2/C3, TOP,
control word and stack values. The existing native model does not deliver
unmasked x87 exceptions; this change does not claim that behavior. Waiting
FCLEX remains guarded.

Authored original-ISA fixtures pass through the actual pinned disassembler,
function/CFG translator, framework extension and optimized native C compiler.
They run 8,960 condition/source/TOP/flag combinations, 128 min/max cases including
signed-zero ties, infinities and NaNs, a predecessor-block flag case, and five
FNCLEX status/stack/control/CPU-flag preservation cases. Negative checks require
guards for missing flags and an unsupported opcode. A hardening check rejects
the old silent fallback. No original game bytes or host inline assembly are
required. Existing ten interrupt-lowering and nine continuation/portable
AArch64 regression checks also pass.

The actual regenerated JSRF image lowers two conditional moves and two FNCLEX
sites. Its original SEGA fade now completes, followed by Smilebit, Dolby Digital
and the anti-graffiti notice. A later resource-construction failure is separate
required work; these startup frames do not establish title or attract coverage.

For iteration, the shared driver accepts `prepare --reuse-discovery`: it keeps
the image-bound function inventory while regenerating original bodies and all
selected service proofs. It writes genuine preparation/configuration/build
receipts; it does not accept manual generated-source edits.
