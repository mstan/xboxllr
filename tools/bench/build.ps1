# Build the standalone Unicorn hook-cost benchmark (beads-xbox.1.14.2).
# Same compiler and Release flags as build-release/CMakeCache.txt
# (CMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe, CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG).
# No -march=native: the project applies it only to xlle_machine / test_nv2a, not to
# xlle_cpu or the Unicorn target, and all measured time is inside the prebuilt libunicorn.
$ErrorActionPreference = 'Stop'
$root = 'F:/Projects/xboxlle/xboxlle'
$b    = "$root/build-release/_deps/unicorn-build"
& C:\msys64\mingw64\bin\g++.exe -O3 -DNDEBUG -std=c++17 `
    -I "$root/build-release/_deps/unicorn-src/include" `
    -o "$root/tools/bench/unicorn_hook_bench.exe" `
    "$root/tools/bench/unicorn_hook_bench.cpp" `
    "$b/libunicorn-static.a" "$b/libx86_64-softmmu.a" "$b/libunicorn-common.a" `
    -lpthread -lm -lkernel32 -luser32 -ladvapi32
Write-Output "exit=$LASTEXITCODE"
