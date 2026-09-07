# xboxrecomp AOT-4 synthetic qualification (2026-09-07)

This audit qualifies the pinned upstream `sp00nznet/xboxrecomp` frontend/runtime shape before using it to expand xboxlle's IA-32 AOT decoder. It uses authored synthetic bytes only. It does not import game code, generated game output, or private assets.

Pinned source:

- DOAXBV audit checkout: `F:/Projects/xboxlle/refs/doaxbv-re-audit-20260906` at `df63e090e1a91844b73d9a53754036403a56c3f3`.
- Its `tools/xboxrecomp` submodule is pinned to `32da23872a552b12b4a932c9d5a6e952bb3f24bb`.
- The standalone qualification fixture also pins a SHA-256 over selected upstream files, so an archived source tree can be checked without touching the shared reference checkout.

The upstream `tools/xboxrecomp` source at the qualified commit is MIT licensed. The DOAXBV runtime repository is GPL-3.0-or-later, so DOAXBV runtime code is not a drop-in source import for xboxlle's current licensing plan.

## Reproduce

Use an ignored build directory and an externally pinned source tree. If the existing reference checkout is not at `32da238...`, archive that commit to a private qualification directory first:

```powershell
$Ref = 'F:/Projects/xboxlle/refs/sp00nz-xboxrecomp'
$Commit = '32da23872a552b12b4a932c9d5a6e952bb3f24bb'
$Out = 'F:/Projects/xboxlle/xboxlle/build/aot4-xboxrecomp-qual/xboxrecomp-32da238'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
git -C $Ref archive --format=tar --output='F:/Projects/xboxlle/xboxlle/build/aot4-xboxrecomp-qual/xboxrecomp-32da238.tar' $Commit
& 'C:/Windows/System32/tar.exe' -xf 'F:/Projects/xboxlle/xboxlle/build/aot4-xboxrecomp-qual/xboxrecomp-32da238.tar' -C $Out
```

First complete the README's Release build so the tool can link its synthetic
Unicorn reference. The qualification uses the MinGW compiler at
`C:/msys64/mingw64/bin/gcc.exe` and Unicorn headers/library under `build-release`;
`--compiler`, `--unicorn-include` and `--unicorn-build` override those paths.
Install the measured decoder dependency into your Python environment, then run:

```powershell
py -3 -m pip install capstone==5.0.9
py -3 tools/qualify_xboxrecomp.py `
  --source F:/Projects/xboxlle/xboxlle/build/aot4-xboxrecomp-qual/xboxrecomp-32da238 `
  --build-dir F:/Projects/xboxlle/xboxlle/build/aot4-xboxrecomp-qual/run
```

The tool writes generated C, compiler output, executable output, a Unicorn comparison executable, and `qualification_summary.json` under the supplied build directory. It requires a content match for the selected upstream files recorded in `tests/fixtures/xboxrecomp_qualification.json`, even when Git HEAD matches the pinned commit. The caller must choose an ignored output directory.

## What passed

The upstream translator successfully generated and compiled synthetic C for seven authored IA-32 functions. The normal in-generated-world cases matched the independent expectations and Unicorn:

- direct caller/callee state transfer through `eax`;
- a callee memory write through the upstream `MEM32` model;
- intra-function `cmp`/`je`/`jmp` branch behavior;
- upstream's own direct-C-call stack convention, where top-level `ret` increments the synthetic `esp` because there is no real caller frame.

These cases establish a bounded reference for decoder expansion. They do not qualify the upstream decoder or lowering generally; additional instructions and control-flow shapes need their own independent tests.

## Tested failures against xboxlle's AOT ABI

Items 1 and 2 and the instruction-budget portion of item 3 were reproduced with upstream-generated synthetic C and compared to a Unicorn C reference built from the same authored bytes. Event/code-guard and memory-contract conclusions are source inspections, not injected-event or memory-fault runtime experiments. Item 5 is a compiler probe.

1. **Cross-function EFLAGS are not ABI state.** The upstream translator emits functions as `void(void)` and uses local reconstructed flag variables for conditional branches. In the synthetic caller, `je` after a call branches on the caller's `_flags`, while the callee's `cmp eax, ebx` is emitted as a comment and does not publish flags back to the caller. Unicorn preserves EFLAGS across the call/ret and takes the branch; upstream-generated C does not.

2. **Unsupported opcodes are comments, not structured exits.** The synthetic `ud2` lowered to `/* TODO: ud2 */` and execution continued into a later memory write and `eax` assignment. Unicorn stopped at the UD2 address with the preceding `eax` value and no later memory write. xboxlle's AOT contract requires an `UNSUPPORTED` exit before later guest-visible effects.

3. **Instruction-budget mismatch; event and code guards absent in inspected source.** A five-`inc eax` synthetic function ran all five increments in generated C. Unicorn with `count=1` stopped after one increment. The emitted code also lacks xboxlle's instruction-boundary event and live-code checks; those absences were inspected, not tested with event injection or modified code.

4. **The inspected memory interface lacks a decline-without-effects contract.** Upstream's runtime template uses direct volatile memory lvalues through `MEM8/MEM16/MEM32`. DOAXBV's runtime uses `recomp_memory_u32` and staged MMIO handling, but the expression remains an lvalue interface. These interfaces cannot directly supply xboxlle's `read32/write32` callback contract. This qualification did not inject memory faults into either upstream runtime.

5. **Strict warning-as-error builds are not qualified.** A strict `-Wall -Wextra -Werror` probe fails on unused generated labels and unused `_flags`. The successful synthetic build suppresses those specific warnings. This is a tooling hygiene issue, not a runtime semantic mismatch.

## Source evidence

At `32da238...`:

- `templates/runtime/recomp_types.h` declares global register storage and `MEM32` as a volatile pointer lvalue.
- `tools/recomp/translator.py` emits translated functions as `void(void)`.
- `tools/recomp/lifter.py` emits unsupported instructions as `TODO` comments.
- `tools/recomp/lifter.py` lowers `ret` as `esp += 4; return;` and direct calls as `PUSH32(esp, 0); target();`.

In xboxlle, the current AOT ABI carries explicit `gpr[8]`, `eip`, `eflags`, and `retired`; checks code identity, context, pending events, and budget; and routes memory through callbacks that can decline without effects.

## Recommendation

Adopt/adapt only the frontend ideas for AOT-4:

- Capstone operand normalization and basic-block construction patterns;
- generated label/function naming conventions;
- synthetic branch/call/unsupported-instruction qualification cases;
- DOAXBV's flag/SSE regression themes as test ideas.

Do not adopt the upstream generated backend/runtime directly. It is built around whole generated functions, global registers, local flag reconstruction, direct/lvalue memory access, and title-specific runtime/manual adapters. That conflicts with xboxlle's required fallback, EIP/EFLAGS preservation, budget/event scheduling, all-or-nothing memory faults, and Unicorn handoff.

The safe next step is to keep xboxlle's existing AOT runtime ABI and expand its decoder/lowering with source-independent tests. Any xboxrecomp frontend reuse should be qualified instruction-by-instruction against this ABI before it is allowed to replace xboxlle lowering.

## Decode-only dependency note

Using Capstone directly remains the lowest-risk decode-only borrowing path. xboxrecomp's current frontend already depends on Capstone's x86-32 detail mode for operand classification, but importing the upstream translator wholesale would bring its runtime assumptions with it. A bounded xboxlle-side decoder expansion can use Capstone as an external decoder dependency or reference oracle while still emitting xboxlle's own region ABI and structured exits.
