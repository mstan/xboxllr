# Native-title Unicorn IRQ boundary package

USB/PIC LLE requires a source-built Unicorn 2.1.4 package containing
`xlle_unicorn_irq_inhibited`. The helper compiles against that engine's own
private headers and reads its maintained interrupt-inhibition state. The
full-machine Unicorn 2.1.1 build uses the same helper source against its own
headers. Do not share structure offsets or binaries across those versions.

The pilot package also applies `fxstate.cmake` to an exact pinned private copy
of the engine's FP helper. It saves/restores the defined 32-bit FX state opcode,
instruction/data pointers and selectors, preserving ownership across original
kernel lazy-NPX context switches. The upstream checkout remains unmodified.
The package includes the compiled helper source and transformation/source
hashes; title configuration requires their agreement. Rebuild older
source-built packages to obtain this default correctness fix. Its qualification
covers executed 32-bit FX state transport; it does not establish general FP
exception metadata generation, 64-bit/XSAVE behavior or the separate 2.1.1 build.

The current transformation also supplies Pentium III last-opcode generation,
protected-mode32 environment metadata and documented FNINIT/FNSTENV effects.
The package carries both private compiled helper and translator sources and
their hashes. Selected original execution uses Pentium III capabilities,
explicit initialized-kernel SSE enablement and shared configured Xbox CPUID0/1
policy; native HLE outputs remain unchanged. Other opcode-generation models,
unmasked exceptions and16-bit/real/VM86 environments remain outside this proof.

Use an unmodified checkout of Unicorn commit
`8028ec436f2d9376525352dd38ed9ed6b9f6be10` (tag `2.1.4`). For example, from
the repository root in PowerShell, using the installed executable paths:

```powershell
& 'C:\Program Files\Git\cmd\git.exe' clone --branch 2.1.4 --depth 1 https://github.com/unicorn-engine/unicorn.git build/unicorn-irq-2.1.4/src
& 'C:\Program Files\CMake\bin\cmake.exe' -S pilots/native-title/unicorn-engine -B build/unicorn-irq-2.1.4/host -G 'Visual Studio 17 2022' -A x64 -DXLLE_UNICORN_SOURCE="${PWD}/build/unicorn-irq-2.1.4/src"
& 'C:\Program Files\CMake\bin\cmake.exe' --build build/unicorn-irq-2.1.4/host --config Release --parallel 4
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build/unicorn-irq-2.1.4/host -C Release --output-on-failure
```

Configure the title profile's `PILOT_CALENDAR_UNICORN` and
`PILOT_CALENDAR_UNICORN_NOTICES` to the resulting
`build/unicorn-irq-2.1.4/host/package` directory. Other title-profile inputs
remain required. Existing calendar-only profiles can retain the reviewed
stock package; USB/PIC needs the actual inhibition API and rejects that package.

The authored static package combines Unicorn's three x86 object targets and
avoids the upstream optional archive's Windows symbolic-link requirement.
It carries the original `COPYING` and `COPYING.LGPL2`. Its private identity
manifest records the library, helper and public headers; title configuration
checks their agreement. This manifest establishes source identity, not
qualification. Actual engine checks cover STI's following instruction, split
slices, context restoration, GPR canaries and independently executed CMP flags.
Original-kernel service and title qualification remain separate checks.

`native_unicorn_fxstate` independently executes eight authored 32-bit
FXRSTOR/FXSAVE state images and four saved engine contexts, checking all defined
metadata, x87/XMM values, controls and unchanged input/output guards. Original
CPU compute/preemption evidence is recorded in the scheduling document.

`native_unicorn_fp_metadata` executes54 independent checks across three actual
Pentium III engines: original memory/register opcodes, prefixed pointers and
selectors, transparent controls, reset/environment/save/restore and SSE2
rejection. The scheduling document records the preceding33-check failure and
the44 passing current-generation original-service checks.

Do not commit the generated package, upstream checkout, private kernel inputs
or qualification captures. See [scheduling evidence](../SCHEDULING-LLE.md).
