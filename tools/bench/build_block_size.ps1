# Build the block-size / hook-shape sweep (beads-xbox.1.16.1).
# Same compiler and Release flags as build-release/CMakeCache.txt, and the same three Unicorn
# archives xlle.exe links -- see tools/bench/build.ps1, which this mirrors deliberately so the
# two benchmarks' numbers are comparable.
$ErrorActionPreference = 'Stop'
$root = 'F:/Projects/xboxlle/xboxlle'
$b    = "$root/build-release/_deps/unicorn-build"
& C:\msys64\mingw64\bin\g++.exe -O3 -DNDEBUG -std=c++17 `
    -I "$root/build-release/_deps/unicorn-src/include" `
    -o "$root/tools/bench/block_size_bench.exe" `
    "$root/tools/bench/block_size_bench.cpp" `
    "$b/libunicorn-static.a" "$b/libx86_64-softmmu.a" "$b/libunicorn-common.a" `
    -lpthread -lm -lkernel32 -luser32 -ladvapi32
Write-Output "exit=$LASTEXITCODE"
