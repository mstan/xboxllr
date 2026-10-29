# XboxLLR beta build tool

The Windows x64 toolkit provides an actual console executable, **xboxllr.exe**,
with embedded Python and Capstone 5.0.9. It starts the shared, readable build
drivers without a shell and returns their exit status. Extract the complete ZIP;
the EXE needs the adjacent `python/` and `tools/` folders. No installation or
machine-wide Python configuration is required.

This is a developer tool. It does not automatically convert any Xbox game into
a working port. **Blinx is the only publicly supported game.** A new title needs
revision metadata, recovered function targets, console-service bindings and real
gameplay qualification. Players should use the separate playable game download.

## Prerequisites

Install Git, Visual Studio 2022 C++/MSVC x64, a Windows SDK, CMake and the supported
dependency toolchains described in [Development](DEVELOPMENT.md). The toolkit
does not bundle those compilers. Obtain your own supported original executable
and complete game assets. Do not put keys, saves or media in a public checkout.

Use a Git checkout for source identities and recursive title pins. The source
snapshot in the toolkit is corresponding source and reference documentation;
native driver provenance checks expect Git when preparing/building titles.

```powershell
git clone https://github.com/mstan/xboxllr.git D:\Dev\xboxllr
git clone --recursive https://github.com/mstan/BlinxRecomp.git D:\Dev\BlinxRecomp
.\xboxllr.exe --help
.\xboxllr.exe version
.\xboxllr.exe --framework D:\Dev\xboxllr title --help
.\xboxllr.exe python D:\Dev\BlinxRecomp\tools\game.py --help
```

Follow Blinx's `docs/DEVELOPMENT.md` to populate ignored `local/paths.json`,
prepare pinned dependencies, regenerate private CPU bodies and build its play
profile. Run its existing commands with the bundled interpreter, for example
`xboxllr.exe python D:\Dev\BlinxRecomp\tools\game.py build --mode play` after
preparation. The title driver uses its own pinned `xboxlle` submodule.

For the generic Dashboard/native-title route, `--framework` selects the framework
checkout used by the `title` and `prepare` commands. The
[native-title guide](NATIVE-TITLE-DRIVER.md) explains title definitions, private
path files and the prepare/bind/configure/build sequence. Relative arguments
retain the caller's working directory. Quote paths containing spaces.

`xboxllr.exe python <script> ...` also supports `-m` and `-c`; it runs the bundled
interpreter. `prepare` exposes the lower-level original-XBE translation driver,
and `dependencies` packages the exact prepared renderer/UI/DSP source closure.
Run each command with `--help` for its options. Errors return a nonzero exit.

## Rebuild the EXE and package

```powershell
cmake -S tools/windows-build-launcher -B D:\Build\xboxllr-tool -A x64
cmake --build D:\Build\xboxllr-tool --config Release
python tools/release_toolkit.py --help
```

The entry point is a small GPL console host using Windows process APIs. Python
retains its PSF license; Capstone retains its BSD notices in the wheel. The
release manifest records source revision, input-archive hashes and every file's
hash. See [public releases](PUBLIC-RELEASES.md) for packaging instructions.
