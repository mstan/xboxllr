# libsamplerate source-rate donor

The maintained MCPX source-rate stage links unmodified
[libsndfile/libsamplerate 0.2.2](https://github.com/libsndfile/libsamplerate/tree/c96f5e3de9c4488f4e6c97f59f5245f22fda22f7),
commit `c96f5e3de9c4488f4e6c97f59f5245f22fda22f7`. Credit belongs to Erik de
Castro Lopo and the upstream contributors. The compiled library sources carry
**BSD-2-Clause** notices. The exact upstream `COPYING` is retained in
`LICENSES/libsamplerate-BSD.txt`, SHA-256
`2c1f76ce2effdddb425018405d5690c0b1ab4e6976e35296b0a6db65c5e1a55d`.
Copies of this provenance and license accompany the built adapter's artifacts.

`cmake/ApuRate.cmake` fetches that immutable revision, builds the static library
without examples/tests/install, and scopes its CMake compatibility policy to
the dependency. Source files and their notices are not rewritten. The upstream
developer helper `src/check_asm.sh` is GPL; it is neither built nor packaged.

`src/machine/apu_rate.cpp` is our independently authored adapter. It supplies
actual SGE-backed source frames, manages cursor/lifetime/end conditions and
uses `SRC_SINC_FASTEST`, following the pinned xemu reference's quality choice.
The exact MCPX interpolation kernel is not established by that choice. No
xemu resampler wrapper or starvation-silence workaround is incorporated.
See `pilots/native-title/AUDIO-LLE.md` for tests and remaining limits.
