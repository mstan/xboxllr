cmake_minimum_required(VERSION 3.20)
project(xboxllr_native_boot LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BOOT_CORE "${CMAKE_CURRENT_LIST_DIR}/../../src/third_party/xbox-boot-re/core")
add_library(xboxllr_boot_core STATIC
  ${BOOT_CORE}/src/core.c ${BOOT_CORE}/src/pack.c
  ${BOOT_CORE}/src/Timing.c ${BOOT_CORE}/src/x87.c
  ${BOOT_CORE}/src/Overview.c ${BOOT_CORE}/src/Camera.c
  ${BOOT_CORE}/src/Chamber.c ${BOOT_CORE}/src/Blob.c
  ${BOOT_CORE}/src/Logo.c ${BOOT_CORE}/src/Textures.c
  ${BOOT_CORE}/src/Effects.c ${BOOT_CORE}/src/FrameOrder.c
  ${BOOT_CORE}/src/Audio.c)
target_include_directories(xboxllr_boot_core PUBLIC ${BOOT_CORE}/include)
if(MSVC)
  target_compile_options(xboxllr_boot_core PRIVATE /O2 /fp:strict)
  if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(xboxllr_boot_core PRIVATE /FI${CMAKE_CURRENT_LIST_DIR}/boot_msvc_math.h)
  endif()
else()
  target_compile_options(xboxllr_boot_core PRIVATE -O2 -ffp-contract=off -fno-fast-math)
endif()

# The portable core has no OS, audio, graphics, assembly or file dependencies.
# This first presentation host is Windows only; no other target imports Win32.
if(WIN32)
  add_executable(xboxllr_boot_intro boot_main.cpp boot_renderer.cpp)
  target_compile_definitions(xboxllr_boot_intro PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(xboxllr_boot_intro PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4;/O2;/fp:strict>)
  target_link_libraries(xboxllr_boot_intro PRIVATE xboxllr_boot_core d3d11 d3dcompiler dxgi winmm bcrypt user32 gdi32)
endif()
