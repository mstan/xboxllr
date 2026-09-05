# Build the WHPX exit-cost / throughput benchmark (beads-xbox.1.16.2).
# Same compiler and Release flags as build-release/CMakeCache.txt. Unlike the other two bench
# targets this one links NOTHING from Unicorn -- it talks to the Windows Hypervisor Platform
# directly, so it measures the hypervisor rather than the emulator.
#
# The headers and import libraries ship with the mingw64 toolchain already
# (winhvplatform.h / winhvemulation.h, libwinhvplatform.a / libwinhvemulation.a); the Windows
# SDK copies under "Windows Kits/10/Include/*/um" are the MSVC equivalents if ever needed.
$ErrorActionPreference = 'Stop'
$root = 'F:/Projects/xboxlle/xboxlle'
& C:\msys64\mingw64\bin\g++.exe -O3 -DNDEBUG -std=c++17 `
    -o "$root/tools/bench/whpx_bench.exe" `
    "$root/tools/bench/whpx_bench.cpp" `
    -lwinhvplatform -lwinhvemulation -lkernel32 -luser32 -ladvapi32
Write-Output "exit=$LASTEXITCODE"
