# External whole-service qualification probes

Private reference executables, separate from the normal xboxlle build. They
compile pinned external Phobos renderer/audio sources; they do not install or
import those sources into the shipping product. No substitute implementations
are supplied for missing device, kernel or audio calls.

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S pilots/donor-services -B build/donor-services -G 'Visual Studio 17 2022' -A x64 -DPHOBOS_SOURCE=F:/Projects/xboxlle/refs/xboxrecomp-phobos665-review-20261007 '-DGIT_EXECUTABLE=C:/Program Files/Git/cmd/git.exe'
& 'C:\Program Files\CMake\bin\cmake.exe' --build build/donor-services --config Release --parallel 2 --target donor_renderer_probe donor_audio_probe
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build/donor-services -C Release --output-on-failure
```

Only run after a successful build. An audio device is required; missing output
is a failure, not a silent simulated clock. The 100 ms PCM buffers are silent.
The renderer window stays hidden; this test makes no presentation/FPS claim.

At the current pin the audio probe passes and the renderer probe deliberately
retains a discovered failure: an X8R8G8B8 surface exposes RGBA native bytes.
CTest exits nonzero. Do not label it expected-pass or relax the check; adapt
the CPU-visible surface contract during integration. See the full
[adoption map and evidence](../../docs/reference/runtime/DONOR-SERVICE-ADOPTION-2026-10-07.md).
