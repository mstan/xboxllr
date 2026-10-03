# AOT dashboard registry scaling checkpoint

Tracking: `beads-xbox.1.25.5`. This is a bounded implementation milestone toward
the [dashboard goal](GOAL-AOT-DASHBOARD.md), not full menu coverage or a speedup.

The launcher no longer rejects private generated artifacts with more than 64
regions. Default selection registers every region. An explicitly supplied
`--aot-native-mask` preserves the existing bit-to-index mapping for indices
0..63, with higher indices excluded. The bounds check precedes the shift.

The machine adapter indexes non-overlapping compiled address ranges by base
address. Lookup uses an ordered predecessor search, while generated-region and
statistics vectors retain registration order. Neighbor checks reject overlaps,
including duplicate bases. This replaces linear entry search; no measured
performance improvement is claimed. Existing context, memory, code-identity,
event and instruction-budget checks remain on the execution path.

## Evidence

`tests/generate_aot_registry_fixture.py` constructs 257 independently authored
MOV/JMP regions and sends them through the production compiler. They span two
code pages and form a closed branch cycle. The test registers them in a shuffled
order, then executes one complete cycle under native-required enforcement after
explicitly disclosed bootstrap/page warming outside that scope.

Both generated C O0 and O2 pass: 514 native instructions, every region visited,
independently expected registers/flags/PC, no fallback run or instruction and no
JIT translation-block entry inside the strict scope. Tests also check address
boundaries, gaps, overlap/wrap rejection, stable per-region counters, and refusal
to execute a compiled body after the live guest bytes change. The last case
reports `code-changed` and retires nothing.

A separate authored private-format corpus exercises the real application:
default registers 257 regions, bit 63 registers exactly index 63, and mask zero
registers none. These three launcher probes run only one startup instruction;
their purpose is option/registration validation, not native execution proof.
The ordinary build is restored with private input disabled afterward.

The full ordinary Release suite passes **63/63**, including both new registry
variants and the existing strict-execution, memory, IRQ and code-coherence tests.

The existing two-region private dashboard build also completes the full 12B
clock-accept regression after the index change: no fault, final security `0x0003`,
and final PPM SHA-256
`36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe` matches
the established dashboard image exactly. Native retirement is 4,724,616,624;
fallback retirement 2,576,740,065; idle skips 4,698,643,312. They sum to
12,000,000,001 and match the prior deterministic-cache qualification. Both
context and code-TLB declines are zero. This remains partial AOT, mostly
startup/delay loops. Private evidence: `build/aot-20261002/registry-dashboard-native.log`
and `.ppm`. This single replay is a correctness check, not a timing comparison.

Private local probe logs are under `build/aot-20261002/registry-launcher/`;
the code bytes in those specific fixtures are authored, not captured Xbox code.

## Remaining work

Generated compilation-unit partitioning, arbitrary explicit subset selection,
broader memory/system/x87 coverage and useful post-boot dashboard attribution
remain open. Overlapping compiled identities still reject; this change does not
claim general overlapping/reloaded-image management. C2 and dashboard D2 remain
in progress. The renderer reuse audit is separate from these CPU adapter changes.
