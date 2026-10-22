# xboxrecomp CPU helper template

Unmodified `templates/runtime/recomp_types.h` from
[sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp/tree/1409a7d7801d3e931fb1074be6104209ddd9a33e),
revision `1409a7d7801d3e931fb1074be6104209ddd9a33e`. Copyright 2026 sp00nz;
the complete upstream MIT license is retained in `LICENSE`.

- Header SHA256: `16891854eb3821c582c3f02cfd391bae8df9e59c655e9cb3be2ae95e6256a732`
- License SHA256: `de6601cea3a6bf2d821b6b89f59b9d6e719cc017e530bcb4bbe2fcf7306e6dda`

The native preparation tool derives a private header from these bytes and the
separate authored `pilots/native-title/native_cpu_helpers.h` bridge. It adapts
call diagnostics and exact return-cleanup checks to our existing runtime ABI;
the upstream arithmetic, floating-point and SIMD helper bodies are retained.
Thread checkpoints and CPU interrupt contracts remain separate authored passes.

This import covers the header template. It does not establish the provenance
of every other generated compiler-output fragment or clear a combined renderer
and game executable for distribution. Existing third-party terms still apply.
