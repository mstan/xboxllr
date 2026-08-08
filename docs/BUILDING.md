# BUILDING — xlle

Host: Windows x64. Toolchain: **MSYS2 MinGW64** (gcc/g++ + Ninja) + CMake. No copyrighted
firmware is needed to build or to run the A0 CPU-only tests.

## Prerequisites

- MSYS2 MinGW64 with `gcc`, `g++`, `ninja` on PATH (`/c/msys64/mingw64/bin`).
- CMake ≥ 3.20, Git.
- Dependencies are fetched automatically (no manual install): **Unicorn** (CPU floor, built
  x86-only) and **SDL3** (host shell) via CMake `FetchContent`.

## Configure + build

```sh
export PATH=/c/msys64/mingw64/bin:$PATH
cd xboxlle            # the framework repo

# CMAKE_POLICY_VERSION_MINIMUM is needed because CMake 4.x rejects the older
# cmake_minimum_required() some dependencies declare.
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build build -j
```

First build compiles Unicorn (QEMU x86 core) and SDL3 from source — several minutes. To skip
the SDL host layer (faster; core A0 only) add `-DXLLE_WITH_SDL=OFF`.

## Run

```sh
export PATH=/c/msys64/mingw64/bin:$PATH
cd build

./xlle.exe            # runs the CPU-floor fixture spike; exit 0 iff all fixtures pass
./xlle.exe --serve    # also keeps the TCP debug server (tcp/4370) up
./xlle.exe --window   # also opens the SDL host window (if built with SDL)

ctest --output-on-failure   # ring + jsonl unit tests + the spike acceptance test
```

Long firmware replays may add `--reduced-retire-diag`. It omits only the boot
command's exact PC-history and per-thread bookkeeping; guest instruction timing,
interrupt-boundary handling, device diagnostics, raster hashes, and scanout capture
remain enabled. The default keeps the full diagnostics.

## Debug server protocol (tcp/4370, loopback)

Line-based; one JSON object per line in replies (JSONL).

| Command | Reply |
|---|---|
| `ping` | `pong` |
| `rings` | newline-separated ring names |
| `query <ring> <n>` | the most-recent `n` entries of `<ring>` as JSONL |

Example (`--serve` running):
```
$ printf 'query mem 1\n' | nc 127.0.0.1 4370
{"ring":"mem","seq":1,"write":true,"addr":"0x2000","size":4,"value":"0xdeadbeef"}
```
