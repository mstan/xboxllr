# Changes

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
