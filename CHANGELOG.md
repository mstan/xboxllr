# Changes

## Beta 2026.11.02.1

- Current Blinx and JSRF releases now include native Linux AppImages alongside
  their Windows ZIPs. WSL checks cover rendering, input and external settings;
  brief Blinx and JSRF USA gameplay was observed. JSRF remains private.

- Updated Windows build toolkit and matching framework source.
- The same XboxLLR engine supports Blinx and Jet Set Radio Future. JSRF's beta
  is currently private while its public release is prepared; it has not been
  exhaustively playtested or tested end to end.
- Companion game builds expose higher-resolution rendering and 1x–8x
  supersampling. Performance depends on the game, scene, settings and hardware.
- New [contributor guide](CONTRIBUTING.md) and game-admission FAQ explain how to
  help implement missing functionality and send shared fixes upstream.
- The Dashboard application remains on its separately validated November 1
  release; this toolkit update includes no new Dashboard runtime.

## Beta 2026.10.29.2

- One XboxLLR release contains the playable Dashboard package and developer
  build-tool package, with separate clear download names.
- Dashboard feature/status table, consolidated setup and documentation index.
- Opaque screenshot export fixes washed-out Dashboard and invisible Blinx PNGs
  on web pages. Captured RGB pixels are unchanged.
- Obsolete progress reports, agent handoffs and duplicate release notes removed;
  maintained technical references organized by subsystem. History remains in Git.
- Gameplay/Dashboard native executables are unchanged from the validated beta;
  this refresh updates documentation, screenshot tooling and packaging.

## Beta 2026.10.29.1

- First public Windows XboxLLR Dashboard and build-tool previews; Blinx's playable
  public alpha is released separately and is tested through World 2.
- Build-tool EXE with embedded Python/Capstone and corresponding source.
- Shared sparse RAM-backed disposable FATX cache, enabled by Blinx; native saves
  remain persistent. Design inspiration credited to apexJCL.

Older private-preview release notes and detailed investigation logs remain in
[Git history](https://github.com/mstan/xboxllr/tree/5faaafad269d671b39737edbebaed66c81b2b9fb/docs).
