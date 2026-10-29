# Windows release dependency preflight

Tracking: `beads-xbox.1.187.2`. The current release target is Windows x64;
native Linux is deferred. All three repositories remain private.

`tools/windows_dependencies.py` is shared framework tooling for Blinx, JSRF,
Dashboard and the recompiler/launcher. It reads PE files without executing them.
It follows ordinary and delay-loaded DLL imports transitively, checks x64
architecture and reports unresolved imports or conflicting DLL versions.
Resolution uses only explicitly selected directories, one level deep. It does
not search PATH, System32 or a drive. Each selected file has a SHA256 identity.

```powershell
python tools/windows_dependencies.py --entry build/Release/BlinxRecomp.exe --entry build/launcher/Release/xlle_launcher.exe --search-dir build/redist/x64 --output build/windows-dependencies.aot-private.json
```

Repeat `--entry` for explicitly loaded plugin/runtime DLLs and `--search-dir`
for reviewed dependency locations. Missing imports return exit status 1;
malformed inputs, wrong architecture and ambiguous versions return 2. Reports
include private build paths and stay in ignored `.aot-private.json` files.
The checker does not copy binaries or publish a package.

The selected system-DLL policy assumes Windows 10 or later. OS components and
Windows API sets are recorded separately; VC++ runtime DLLs, SDL and XAudio
redistributables are not implicitly treated as installed. In particular,
`bcryptprimitives.dll` is an OS component documented by Microsoft's
[ProcessPrng reference](https://learn.microsoft.com/en-us/windows/win32/seccng/processprng).

The initial real-binary audit found the same missing app-local VC++ files in
the existing Blinx, Dashboard and JSRF output directories: `msvcp140.dll`,
`vcruntime140.dll` and `vcruntime140_1.dll`. SDL2 was adjacent and resolves.
The JSRF CPU-helper candidate has the same imported-DLL requirements as the
accepted JSRF preview. Microsoft's `dumpbin /dependents` independently confirms
the candidate's direct module list. None of these read-only checks changes
accepted executables, controller settings or native saves.

A follow-up data-only extraction selected Microsoft's signed runtime bundle
`14.51.36247.0`, downloaded from the [official current x64 link](https://aka.ms/vc14/vc_redist.x64.exe).
The installer SHA256 is
`843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c`.
Its installer was not executed. The required x64 DLLs are available in the
private dependency cache; each title's runner import graph resolves when that
directory is supplied. This is source selection for packaging, not a delivered
archive or first-launch result. The bundle's license link is
[Microsoft's runtime terms](https://aka.ms/VCRedistLicense).

The shared launcher also imports the platform's DirectWrite and Direct2D
libraries. These are OS components under the selected policy, as documented
by Microsoft's [DirectWrite](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-dwritecreatefactory)
and [Direct2D](https://learn.microsoft.com/en-us/windows/win32/direct2d/createfactory-lt-factory-gt--d2d1-factory-type---factory-)
references. They are not copied from the builder's System32 directory.

This establishes **imported module presence**, not complete release readiness.
It does not check imported export versions, export forwarders, arbitrary
LoadLibrary calls, assets, licenses or the correctness of title code. Explicit
runtime modules need their own entries, and the final package still needs
clean-profile first launch, controller and real save/load checks. A successful
inventory must not be used as a blanket redistribution clearance.

Use redistributable files from the selected toolchain's licensed redist source
and retain required notices. Do not harvest arbitrary system DLLs. Microsoft
documents [redistributable source locations and terms](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)
and [local deployment](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method).
The selected CRT must also meet the build toolset's version requirements;
finding a filename alone does not establish that contract.

The format reader follows Microsoft's
[PE specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
Fifteen ROM-free tests cover import-graph contracts, cycles, transitive
delay-load failures, explicit runtime entries, version ambiguity, architecture,
declared-directory boundaries, malformed/path-bearing import data, CLI failure
status and protection against overwriting an input via a report alias. No
compiler, game content, Windows SDK or hardware is required to run these tests.
