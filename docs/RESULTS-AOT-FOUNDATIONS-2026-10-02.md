# AOT foundations: qualification record, 2026-10-02

Owner: `beads-xbox.1.25.5`. Launch investigation: `beads-xbox.1.7.1`.
This is a bounded implementation checkpoint, not closure of C0-C6 or proof that
Blinx boots. Changes are uncommitted atop `c3fbd22`; pre-existing working-tree
changes, including the native-required runtime path, have been preserved.

## Reference admission

Read-only review clones were pinned to these commits:

| Reference | Pin | Useful candidates | Rejected assumptions |
| --- | --- | --- | --- |
| [sp00nznet](https://github.com/sp00nznet/xboxrecomp/tree/1409a7d7801d3e931fb1074be6104209ddd9a33e) | `1409a7d7801d3e931fb1074be6104209ddd9a33e` | Discovery, indirect-control-flow and flag test patterns | Double-backed x87, host kernel replacement, unsupported-op continuation, DSP passthrough/forced acknowledgements |
| [NoRain211](https://github.com/NoRain211/xboxrecomp/tree/7adaf210a89814c5ca4444e4876fccb51b938878) | `7adaf210a89814c5ca4444e4876fccb51b938878` | Control-flow and flag fixes, many already present upstream | Shared HLE/runtime contracts are not a qualified LLE replacement |
| [phobos665](https://github.com/phobos665/xboxrecomp/tree/012e35616034ba1a3a310c4958988467996fb212) | `012e35616034ba1a3a310c4958988467996fb212` | Discovery and cache ideas; graphics backend organization for later comparison | Zero control-register reads and ignored writes; unresolved JSRF bring-up is not playable evidence |

The reviewed Python suites reported sp00nznet 496 pass/1 skip (+106 subtests),
NoRain211 31 pass (+22 subtests), and phobos665 471 pass/4 fail/1 skip
(+147 subtests). The phobos failures were runtime-harness signature/global drift,
double-shift harness dependencies and a SAR expectation mismatch. The sanitizer
skip is not counted as validation. These are donor tool tests, not Xbox game or
hardware tests. No donor runtime code was imported into xboxlle.

Retain provenance for any later harvest. The phobos fork carries GPL terms;
MIT ancestry does not automatically cover later contributions. Existing release
constraints remain in [LICENSE-SCOPE.md](../LICENSE-SCOPE.md); this work changes
no third-party licenses.

## Inventory and private artifact preparation

`tools/aot_inventory.py` validates XBE file/header/section extents and records
image/section hashes, public title/library identities, executable candidates,
explicit entry evidence and recursive IA-32 discovery. Capstone is decode-only.
Calls, continuations, returns, unresolved indirect transfers, overlapping decode
claims, exceptional transfers and unclassified ranges remain distinguishable.
A call continuation is a candidate, not proof that the call returns. Explicit
limits retain the pending work list. It never declares whole-program recovery.

The original extracted Blinx input is NA retail, title `4D530013`, XDK 4831,
SHA-256 `e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`.
The entry-rooted inventory recovered 413 instructions / 1,201 bytes, of which
282 match supported generator forms after this change. It records 13 unresolved
indirect calls, one system/exception transfer and 52,112,027 unclassified bytes
within executable-candidate virtual ranges. That byte denominator includes
data/zero-fill candidates and is not an executable-code size estimate. The
282 forms are syntax support, not 282 independently qualified runtime paths.

The owner subsequently supplied network images for Blinx and JSRF. Full-size
Blinx Rev 1 contains a different XBE. Do not combine inventory counts or claim
the isolated test below qualifies that revision. The local input audit records
file identities and bounded filesystem extents; it does not certify optical
authentication. Input results belong to the [launch gate](BLINX-BURNDOWN.md).

`tools/prepare_aot_xbe.py` requires an expected image hash and explicit byte
spans backed by executable-candidate sections. It rejects overlapping, unbacked
and malformed selections. Private inputs, generated code and manifests retain
the publication guards used by the existing blockpack workflow. Unsupported
instructions remain explicit exits. No certificate secrets are emitted by the
inventory. Generated guest bytes stay in ignored private build directories.

Install the analysis dependency with `py -3 -m pip install capstone==5.0.9`.
The inventory records both Python binding and engine versions; package metadata
and the module's version string differ in the installed package.

## Read-only media audit

The owner-provided share contains three inspected images. Each was read in full
for SHA-256, and its XDVDFS tree was traversed with bounded offsets, cycle and
overlap checks. Source images were not modified. Exact XBE copies, hashes and
the local audit script are under `build/aot-20261002/`.

| Image | Bytes | Game-partition offset | File entries | XBE identity |
| --- | ---: | --- | ---: | --- |
| Blinx, original input | 2,240,948,224 | `0` | 3,197 | XDK 4831; `e6b7cd8a5527...`, matches prior compiler input |
| Blinx Rev 1 | 7,825,162,240 | `0x18300000` | 3,094 | XDK 5558; `6f7dcd702f1f...`, distinct revision |
| Jet Set Radio Future | 2,504,411,136 | `0` | 3,226 | XDK 4134; `f4441696fdd0...`, matches extracted directory |

Full image hashes:

- Blinx original: `cb36b71fd53e577ad80c5f5654888f2a93635aed9e84f1c57593b67674d062c2`.
- Blinx Rev 1: `0e7a024d3d9edf6c6394e9a2943a04d5c93b81bea95ce0137bb3cb459e74fd6b`.
- JSRF: `bf51813f29f2604df361e0e442416a1ad809bf4e217476694370d2f15286c7f7`.

All three XBEs declare region 1 and media mask `2` (DVD-X2 only); copying to HDD
does not satisfy that declared launch constraint. Format references were the
[extract-xiso reader](https://github.com/XboxDev/extract-xiso/blob/master/extract-xiso.c)
and pinned [nxdk XBE definitions](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/tools/cxbe/Xbe.h).
The local manifest is `build/aot-20261002/media.aot-private.json`.
It explicitly records authentication as unqualified. Blinx Rev 1's inspected ZIP
contains just the ISO; no separate authentication sidecar was present there.
Filesystem structure and hashes do not prove retail signatures, optical
challenge behavior, data correctness against the original disc or gameplay.

## Integer and call path

The portable-C generator now supports bounded 32-bit near CALL/RET, register
indirect CALL/JMP, register PUSH/POP and immediate PUSH. It uses the actual
guest stack and return PC. It checks target limits before committing stack
changes, captures aliased ESP operands correctly and preserves per-instruction
guards, budgets and event exits. General register ADD/SUB/CMP immediate forms
and both register-encoding directions of ADD/SUB/XOR/AND/CMP use existing
qualified arithmetic helpers. Memory arithmetic forms remain unsupported.

Authored tests exercise callee flags reaching the caller, every prefix budget,
PUSH ESP / POP ESP / CALL ESP, RET cleanup, declined stack accesses, invalid
targets and an event arriving during the CALL stack write. Arithmetic boundary
and randomized differential cases run in optimized and unoptimized builds.
Native-required refusal tests separately exercise the initial CPU-stop state
and a cold registered code page after a different page has been warmed. Neither
may enter fallback or advance guest state within the strict scope.

The isolated real-code test selects a caller prefix at `0x000123d0` (18 bytes)
and a callee at `0x000fa220` (13 bytes). It executes ten instructions: argument
setup, a real call, an indexed RAM lookup, return and stack cleanup. Controlled
RAM inputs produce an independently expected result. At every prefix from one
to ten instructions, architectural state and all 64 MiB RAM match the Unicorn
reference; undefined XOR AF is excluded until the final ADD defines it.
Generated C passes strict C11 compilation and execution at **O0 and O2**.

Preparation executes a bootstrap and nine guest instructions using the normal
floor, outside the declared strict scope. Native-required mode then executes
each tested prefix with zero fallback instructions/runs and no added Unicorn
TB entries. This proves warm-scope execution only. The test has no initialized
kernel, devices or running game. It does not prove the full caller works.
The private reproducer is `build/aot-20261002/qualify.ps1 -GameImage <pinned-XBE>`;
its harness, manifests, generated bodies and O0/O2 logs are retained there.

Two harness findings remain explicit. The ordinary block scheduler admits whole
blocks and is not an arbitrary exact-prefix oracle; the reference uses diagnostic
instruction scheduling configured before any translation. Enabling that mode
after warming produced missing instruction accounting in cached code, tracked
separately as `beads-xbox.1.29`. Native-required mode also correctly refused the
initially unsupported reverse-encoding XOR; general lowering and independent
tests were added before the selected path was counted as passing.

## x87 helper experiment

The test-only adapter stages existing pinned QEMU extended-precision FPU state
and calls fixed FLD64/FADD64/FSUB64/FSTP64 helpers outside guest JIT execution.
It publishes only FPU fields on explicit commit. Both O0 and O2 adapter builds
pass stepwise comparisons of register encodings, tags, TOP, control/status,
instruction/data pointers, selectors and arithmetic exception state.

Five independently specified results cover extended intermediates beyond host
double precision, 53-bit and 24-bit precision control, rounding upward and a
normal exact result. Discarded proposals leave live state unchanged; unmasked
exceptions and empty-stack arithmetic are explicitly declined. The differential
reference shares QEMU arithmetic ancestry, so independent expected results are
essential and the comparison alone is not an independent hardware oracle.

This adapter is linked only into tests. Production x87 state ownership, generated
instruction lowering, real memory faults, unmasked exception delivery, stack
faults, NaN/denormal coverage and interrupt integration remain open. No x87 or
SIMD coverage milestone is closed. DSP device execution is a separate subsystem
and is not replaced with acknowledgement stubs.

## Initial validation checkpoint and next gates

- Release build succeeded. Final full CTest: **60/60 pass**, including the
  new IDE security refusal test and all 11 AOT checks.
- After the final public launch-metadata fields and cold-code refusal case were
  added, both inventory/private-input tests and both native-required variants
  passed again. Working-tree private-material scan and whitespace checks pass.
- Generator tests: 20; XBE inventory tests: 9; private-input tests: 6. Fixtures
  contain independently authored bytes, not captured Xbox software.
- Private selected Blinx path: O0/O2, all ten prefixes, exact state/RAM and
  zero strict-scope fallback.
- The existing unconditional HDD unlock and freeze-lock success paths were
  removed. Tests prove explicit faults, unchanged locked state and absence of
  fabricated completion IRQs. They do not claim implemented ATA security.
- A bounded real-kernel startup with the existing flash/2BL/HDD/EEPROM inputs
  stops at HDD unlock: 17,940,376 instructions, EIP `80018566`, process exit 2.
  The private log is `build/aot-20261002/security-stop.log`. At that checkpoint
  the tree intentionally blocked the previously demonstrated dashboard route;
  the follow-up below supersedes that stop.
- No full dashboard replay or performance benchmark was run. No new speedup,
  playable title or full-machine fidelity claim is supported.

C0 still needs kernel/workload inventory and reconciliation with observed PCs.
C1 still needs broader widths/addressing and machine control-flow qualification.
C2 needs scalable dispatch/artifacts; C3 needs cold-memory, device/system and
fault paths; C4 needs production extended-state integration. C5/C6 performance
and coverage closure remain open. Blinx launch is separately gated by real media
behavior and replacement of the existing unconditional HDD security path
(`beads-xbox.1.28`). No placeholders or spoofed successes close any gate.

## Drive-security follow-up

The owner authorizes an explicit unlocked HDD policy while retaining a correct
locked path. Independently provisioned user/master credentials now back the
locked path; the guest password is checked, never learned or trusted on arrival.
Retry exhaustion, freeze, protected PIO/DMA, IDENTIFY and software/hardware reset
states have authored behavior tests. An offline tool authenticates the EEPROM
and derives the virtual drive's user password. **61/61 Release tests pass**.

Provisioned retail startup passes one billion instructions without fault,
reads 5,665 sectors and makes one copy-on-write sector update. Security ends
enabled/unlocked (`0x0003`). Explicit unlocked mode completes a bounded 30M run
with security disabled (`0x0001`); its final guest boot outcome is not established
by that short run. No Blinx launch or new performance claim follows.
See [HDD security](HDD-SECURITY.md) for commands, source references and limits.

Follow-up 12B clock-accept replay reached the dashboard menu, verified visually,
with the exact existing reference PPM SHA-256
`36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`.
No fault; 42,360 sector reads; two COW sector updates; security `0x0003`.
The dashboard route is restored with authentication enabled. This replay is a
correctness check and supplies no new speedup claim.
