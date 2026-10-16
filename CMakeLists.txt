cmake_minimum_required(VERSION 3.20)
project(xlle CXX C)

# ---- xlle: LLE-first original-Xbox platform. A0 foundations. ----
# Harvest strategy (DESIGN §3): CPU floor from Unicorn (QEMU x86 core), host layer from
# SDL3. Both pulled via FetchContent so the tree stays clean and reproducible. The engine
# and its CPU-only tests build with NO copyrighted firmware.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(XLLE_WITH_SDL "Build the SDL3 host shell" ON)
set(XLLE_RENDERER "LLE" CACHE STRING "Whole graphics implementation: LLE or HLE")
set_property(CACHE XLLE_RENDERER PROPERTY STRINGS LLE HLE)
if(NOT XLLE_RENDERER MATCHES "^(LLE|HLE)$")
  message(FATAL_ERROR "XLLE_RENDERER must be LLE or HLE")
endif()
if(XLLE_RENDERER STREQUAL "HLE" AND NOT XLLE_WITH_SDL)
  message(FATAL_ERROR "The HLE renderer requires SDL and OpenGL")
endif()
add_compile_definitions(XLLE_RENDERER_HLE=$<STREQUAL:${XLLE_RENDERER},HLE>)
option(XLLE_NATIVE_DRAW "Build donor native triangle submission (HLE graphics); OFF retains the LLE renderer" OFF)
if(XLLE_NATIVE_DRAW AND NOT XLLE_WITH_SDL)
  message(FATAL_ERROR "XLLE_NATIVE_DRAW requires XLLE_WITH_SDL")
endif()
option(XLLE_CPU_BLOCK_CENSUS "Compile guest block execution census (attribution builds only)" OFF)
option(XLLE_AOT_FAST_MEMBERSHIP "Use exact sparse byte membership for registered AOT ranges" ON)
option(XLLE_HOST_TIMED_TLB "Use legacy host-time-dependent Unicorn TLB resizing (diagnostic comparison only)" OFF)
option(XLLE_NATIVE_OPTIMIZATIONS
  "Build local host-native optimized binaries (-march=native on x86_64 GNU/Clang)"
  ON)
# Temporary per-word micro-partition of the NV2A pushbuffer/dispatch path
# (src/machine/nv2a_dispatch_micro.hpp, beads-xbox.1.15.1 step 1). OFF here means
# the probes are empty inlines with no globals behind them, so the shipping
# binary's hot loop is compiled exactly as if the header did not exist and the
# decision A/B is not run against instrumented code. Turn it on into a SEPARATE
# build directory when the per-word breakdown needs re-measuring.
option(XLLE_DISPATCH_MICRO
  "Compile the temporary NV2A per-word dispatch micro-partition probes"
  OFF)
# Vertex reuse shadow is a diagnostic-only probe. Keep it compiled out of
# normal builds: the disabled integration did not establish zero overhead in
# the canonical interleaved set. Preserve the probe in dedicated builds.
option(XLLE_VERTEX_REUSE_DIAGNOSTICS
  "Compile the opt-in NV2A vertex reuse shadow diagnostic plumbing"
  OFF)
option(XLLE_GENERATED_SUBMIT_DIAGNOSTICS
  "Compile the opt-in NV2A generated-submit CPU phase diagnostic plumbing"
  OFF)
option(XLLE_VSH_DRAW_OVERLAP_CENSUS
  "Compile the opt-in NV2A vertex and draw-route overlap census"
  OFF)
option(XLLE_VSH_GPU_NUMERIC_PROBE
  "Build the standalone synthetic CPU/GPU VSH arithmetic diagnostic"
  OFF)
option(XLLE_VSH_CAPTURE
  "Compile bounded CPU-authoritative VSH capture for offline GPU validation"
  OFF)
option(XLLE_VSH_GPU_RESIDENT
  "Compile the opt-in verified GPU vertex/triangle resident renderer handoff"
  OFF)

include(FetchContent)

# --- Unicorn (harvested CPU floor); build only the x86 arch to keep it fast ---
set(UNICORN_ARCH "x86" CACHE STRING "" FORCE)
set(UNICORN_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(unicorn
  GIT_REPOSITORY https://github.com/unicorn-engine/unicorn.git
  GIT_TAG 2.1.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(unicorn)
target_sources(unicorn PRIVATE src/cpu/unicorn_metrics.c)
target_sources(x86_64-softmmu PRIVATE src/cpu/unicorn_aot_bridge.c src/cpu/unicorn_irq_boundary.c)
include(cmake/UnicornPreciseFault.cmake)
# Guest RDTSC must not read the host TSC (beads-xbox.1.4.6): rename the pinned QEMU
# definition out of the way so src/cpu/unicorn_metrics.c owns the symbol every RDTSC and
# TSC-MSR read resolves to. Nothing in the vendored tree is edited.
set_source_files_properties(${unicorn_SOURCE_DIR}/qemu/hw/i386/x86.c
  TARGET_DIRECTORY unicorn
  PROPERTIES COMPILE_DEFINITIONS cpu_get_tsc_x86_64=xlle_unused_host_cpu_get_tsc)

# --- NV2A CPU vertex-shader interpreter (same pinned reference used by xemu) ---
set(nv2a_vsh_cpu_UNIT_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nv2a_vsh_cpu
  GIT_REPOSITORY https://github.com/xemu-project/nv2a_vsh_cpu.git
  # Source-identical reachable twin of xemu's 561fe80 pin (same parent/tree/message).
  GIT_TAG 863fc0f50aa4027af113718d2a63e45ac3be7a42)
FetchContent_MakeAvailable(nv2a_vsh_cpu)
include(cmake/Nv2aVshArl.cmake)
xlle_correct_vsh_arl(nv2a_vsh_emulator
  "${nv2a_vsh_cpu_SOURCE_DIR}/src/nv2a_vsh_emulator.c")

# --- SDL3 (borrowed host layer) ---
if(XLLE_WITH_SDL)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

set(XLLE_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
include(cmake/ApuAdpcm.cmake)
include(cmake/ApuRate.cmake)

# --- observability (rings + jsonl are header-only; debug server is the .cpp) ---
add_library(xlle_obs STATIC src/obs/debug_server.cpp)
target_include_directories(xlle_obs PUBLIC ${XLLE_INCLUDES})
if(WIN32)
  target_link_libraries(xlle_obs PUBLIC ws2_32)
endif()

# --- opt-in host Ethernet transport ---
add_library(xlle_net STATIC src/host/ethernet_udp.cpp)
target_include_directories(xlle_net PUBLIC ${XLLE_INCLUDES})
if(WIN32)
  target_link_libraries(xlle_net PUBLIC ws2_32)
endif()

# --- CPU floor (Unicorn-backed) ---
add_library(xlle_cpu STATIC src/cpu/unicorn_floor.cpp src/cpu/aot_executor.cpp)
target_include_directories(xlle_cpu PUBLIC ${XLLE_INCLUDES})
target_include_directories(xlle_cpu PRIVATE ${unicorn_SOURCE_DIR}/include)
target_link_libraries(xlle_cpu PUBLIC unicorn)
target_compile_definitions(xlle_cpu PUBLIC XLLE_AOT_FAST_MEMBERSHIP=$<BOOL:${XLLE_AOT_FAST_MEMBERSHIP}>)
if(XLLE_CPU_BLOCK_CENSUS)
  target_compile_definitions(xlle_cpu PUBLIC XLLE_CPU_BLOCK_CENSUS=1)
endif()

# --- machine model (memory map + devices around the CPU floor) ---
add_library(xlle_machine STATIC
  src/machine/machine.cpp
  src/machine/ide.cpp
  src/machine/pci.cpp
  src/machine/smbus.cpp
  src/machine/nv2a.cpp
  src/machine/nvnet.cpp
  src/machine/ohci.cpp
  src/machine/pm.cpp
  src/machine/pic.cpp
  src/machine/pit.cpp
  src/machine/rtc.cpp)
target_include_directories(xlle_machine PUBLIC ${XLLE_INCLUDES})
target_compile_definitions(xlle_machine PUBLIC XLLE_NATIVE_DRAW=$<BOOL:${XLLE_NATIVE_DRAW}>)
target_link_libraries(xlle_machine PUBLIC xlle_cpu nv2a_vsh_emulator)
target_link_libraries(xlle_machine PUBLIC xlle_apu_adpcm)
target_link_libraries(xlle_machine PUBLIC xlle_apu_rate)
if(XLLE_DISPATCH_MICRO)
  target_compile_definitions(xlle_machine PUBLIC XLLE_DISPATCH_MICRO=1)
endif()
if(XLLE_VERTEX_REUSE_DIAGNOSTICS)
  target_compile_definitions(xlle_machine PUBLIC XLLE_VERTEX_REUSE_DIAGNOSTICS=1)
else()
  target_compile_definitions(xlle_machine PUBLIC XLLE_VERTEX_REUSE_DIAGNOSTICS=0)
endif()
if(XLLE_VSH_DRAW_OVERLAP_CENSUS)
  target_compile_definitions(xlle_machine PUBLIC XLLE_VSH_DRAW_OVERLAP_CENSUS=1)
else()
  target_compile_definitions(xlle_machine PUBLIC XLLE_VSH_DRAW_OVERLAP_CENSUS=0)
endif()
target_compile_definitions(xlle_machine PUBLIC XLLE_VSH_CAPTURE=$<BOOL:${XLLE_VSH_CAPTURE}>)
target_compile_definitions(xlle_machine PUBLIC XLLE_VSH_GPU_RESIDENT=$<BOOL:${XLLE_VSH_GPU_RESIDENT}>)
if(XLLE_NATIVE_OPTIMIZATIONS AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  target_compile_options(xlle_machine PRIVATE -march=native)
endif()

# --- fixture corpus ---
add_library(xlle_spike STATIC src/spike/fixtures.cpp)
target_include_directories(xlle_spike PUBLIC ${XLLE_INCLUDES})

# --- host shell ---
add_library(xlle_host STATIC src/host/sdl_shell.cpp src/host/xid_input.cpp
  src/host/nv2a_opengl_renderer.cpp src/host/nv2a_combiner_glsl.cpp
  src/host/nv2a_xemu_psh.cpp src/third_party/xemu/psh.c
  src/third_party/xemu/common.c)
if(XLLE_RENDERER STREQUAL "HLE")
  target_sources(xlle_machine PRIVATE src/machine/nv2a_hle.cpp)
  target_sources(xlle_machine PRIVATE src/machine/hle_allocation_observers.cpp)
  set(XLLE_XDK_DRAW_PROFILE "" CACHE FILEPATH "Private byte-pinned native XDK draw ABI header")
  if(XLLE_XDK_DRAW_PROFILE)
    if(NOT EXISTS "${XLLE_XDK_DRAW_PROFILE}")
      message(FATAL_ERROR "Private XDK profile header does not exist")
    endif()
    target_sources(xlle_machine PRIVATE src/machine/hle_xdk_draw.cpp)
    target_compile_definitions(xlle_machine PUBLIC XLLE_XDK_DRAW_SPIKE=1)
    target_compile_definitions(xlle_machine PRIVATE XLLE_XDK_DRAW_PROFILE_HEADER="${XLLE_XDK_DRAW_PROFILE}")
  endif()
  target_sources(xlle_host PRIVATE src/host/nv2a_hle_renderer.cpp
    src/third_party/xemu/vsh-prog.c src/third_party/xemu/hle_psh.c src/third_party/xemu/swizzle.c
    src/third_party/norain/d3d_texture_model.c)
  if(WIN32)
    target_link_libraries(xlle_host PUBLIC opengl32)
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(xlle_host PUBLIC OpenGL::GL)
  endif()
endif()
target_include_directories(xlle_host PUBLIC ${XLLE_INCLUDES})
target_compile_definitions(xlle_host PUBLIC XLLE_NATIVE_DRAW=$<BOOL:${XLLE_NATIVE_DRAW}>)
target_compile_definitions(xlle_host PUBLIC XLLE_VSH_GPU_RESIDENT=$<BOOL:${XLLE_VSH_GPU_RESIDENT}>)
if(XLLE_GENERATED_SUBMIT_DIAGNOSTICS)
  target_compile_definitions(xlle_host PUBLIC XLLE_GENERATED_SUBMIT_DIAGNOSTICS=1)
else()
  target_compile_definitions(xlle_host PUBLIC XLLE_GENERATED_SUBMIT_DIAGNOSTICS=0)
endif()
if(XLLE_WITH_SDL)
  target_compile_definitions(xlle_host PUBLIC XLLE_WITH_SDL)
  target_link_libraries(xlle_host PUBLIC SDL3::SDL3)
endif()

# --- A0 app ---
add_executable(xlle src/app/main.cpp src/host/heap_poison.cpp)
target_include_directories(xlle PRIVATE ${XLLE_INCLUDES})
target_link_libraries(xlle PRIVATE xlle_obs xlle_cpu xlle_machine xlle_spike xlle_host xlle_net)
if(WIN32)
  target_link_libraries(xlle PRIVATE psapi dbghelp)
endif()

# Put SDL3.dll next to the exe so `xlle --window` runs without a manual PATH.
if(XLLE_WITH_SDL)
  add_custom_command(TARGET xlle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:xlle>)
endif()

# --- tests (no firmware, no devices) ---
if(XLLE_VSH_GPU_NUMERIC_PROBE)
  if(NOT XLLE_WITH_SDL)
    message(FATAL_ERROR "XLLE_VSH_GPU_NUMERIC_PROBE requires XLLE_WITH_SDL")
  endif()
  add_executable(xlle_vsh_dot_probe tools/nv2a_vsh_dot_probe.cpp)
  target_include_directories(xlle_vsh_dot_probe PRIVATE
    ${XLLE_INCLUDES} ${nv2a_vsh_cpu_SOURCE_DIR}/src)
  target_link_libraries(xlle_vsh_dot_probe PRIVATE nv2a_vsh_cpu SDL3::SDL3)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xlle_vsh_dot_probe PRIVATE -fno-fast-math -ffp-contract=off)
  elseif(MSVC)
    target_compile_options(xlle_vsh_dot_probe PRIVATE /fp:strict)
  endif()
  add_custom_command(TARGET xlle_vsh_dot_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:xlle_vsh_dot_probe>)
  # A mismatch is an observed device capability, not a portable CTest assertion.
endif()

if(XLLE_VSH_GPU_RESIDENT)
  if(NOT XLLE_WITH_SDL)
    message(FATAL_ERROR "XLLE_VSH_GPU_RESIDENT requires XLLE_WITH_SDL")
  endif()
  add_executable(xlle_vsh_resident_probe tools/nv2a_vsh_resident_probe.cpp)
  target_include_directories(xlle_vsh_resident_probe PRIVATE
    ${XLLE_INCLUDES} ${nv2a_vsh_cpu_SOURCE_DIR}/src)
  target_link_libraries(xlle_vsh_resident_probe PRIVATE xlle_host nv2a_vsh_cpu)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xlle_vsh_resident_probe PRIVATE -fno-fast-math -ffp-contract=off)
  elseif(MSVC)
    target_compile_options(xlle_vsh_resident_probe PRIVATE /fp:strict)
  endif()
  add_custom_command(TARGET xlle_vsh_resident_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:xlle_vsh_resident_probe>)
endif()

enable_testing()
add_executable(test_ring tests/test_ring.cpp)
target_include_directories(test_ring PRIVATE ${XLLE_INCLUDES})
add_test(NAME ring COMMAND test_ring)

add_executable(test_jsonl tests/test_jsonl.cpp)
target_include_directories(test_jsonl PRIVATE ${XLLE_INCLUDES})
add_test(NAME jsonl COMMAND test_jsonl)

add_executable(test_perf_excl tests/test_perf_excl.cpp)
target_include_directories(test_perf_excl PRIVATE ${XLLE_INCLUDES})
add_test(NAME perf_excl COMMAND test_perf_excl)

add_executable(test_host_sample_windows tests/test_host_sample_windows.cpp)
target_include_directories(test_host_sample_windows PRIVATE ${XLLE_INCLUDES})
add_test(NAME host_sample_windows COMMAND test_host_sample_windows)
if(WIN32)
  add_executable(host_sample_probe tests/host_sample_probe.cpp)
  target_include_directories(host_sample_probe PRIVATE ${XLLE_INCLUDES})
  target_link_libraries(host_sample_probe PRIVATE psapi dbghelp)
endif()

add_executable(test_vertex_reuse_shadow tests/test_vertex_reuse_shadow.cpp)
target_include_directories(test_vertex_reuse_shadow PRIVATE ${XLLE_INCLUDES})
add_test(NAME vertex_reuse_shadow COMMAND test_vertex_reuse_shadow)

add_executable(test_vsh_dot_prefix tests/test_vsh_dot_prefix.cpp)
target_include_directories(test_vsh_dot_prefix PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_vsh_dot_prefix PRIVATE nv2a_vsh_emulator nv2a_vsh_cpu)
add_test(NAME vsh_dot_prefix COMMAND test_vsh_dot_prefix)

add_executable(test_vsh_capture tests/test_vsh_capture.cpp)
target_include_directories(test_vsh_capture PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_vsh_capture PRIVATE nv2a_vsh_emulator nv2a_vsh_cpu)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_vsh_capture PRIVATE -fno-fast-math -ffp-contract=off)
elseif(MSVC)
  target_compile_options(test_vsh_capture PRIVATE /fp:strict)
endif()
add_test(NAME vsh_capture COMMAND test_vsh_capture)

add_executable(test_vsh_triangle tests/test_vsh_triangle.cpp)
target_include_directories(test_vsh_triangle PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_vsh_triangle PRIVATE nv2a_vsh_emulator nv2a_vsh_cpu)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_vsh_triangle PRIVATE -fno-fast-math -ffp-contract=off)
elseif(MSVC)
  target_compile_options(test_vsh_triangle PRIVATE /fp:strict)
endif()
add_test(NAME vsh_triangle COMMAND test_vsh_triangle)

add_executable(test_vsh_draw_overlap tests/test_vsh_draw_overlap.cpp)
target_include_directories(test_vsh_draw_overlap PRIVATE ${XLLE_INCLUDES})
add_test(NAME vsh_draw_overlap COMMAND test_vsh_draw_overlap)

add_executable(test_generic_vertex_scratch tests/test_generic_vertex_scratch.cpp)
target_include_directories(test_generic_vertex_scratch PRIVATE ${XLLE_INCLUDES})
add_test(NAME generic_vertex_scratch COMMAND test_generic_vertex_scratch)

add_executable(test_generated_submit_perf tests/test_generated_submit_perf.cpp)
target_include_directories(test_generated_submit_perf PRIVATE ${XLLE_INCLUDES})
add_test(NAME generated_submit_perf COMMAND test_generated_submit_perf)

# These tests exercise the CPU renderer directly without a host GPU backend.
if(XLLE_RENDERER STREQUAL "LLE")
  add_executable(test_nv2a_surface_dma tests/test_nv2a_surface_dma.cpp)
  target_include_directories(test_nv2a_surface_dma PRIVATE ${XLLE_INCLUDES})
  add_test(NAME nv2a_surface_dma COMMAND test_nv2a_surface_dma)
  add_executable(test_nv2a_fp_timing tests/test_nv2a_fp_timing.cpp)
  target_include_directories(test_nv2a_fp_timing PRIVATE ${XLLE_INCLUDES})
  add_test(NAME nv2a_fp_timing COMMAND test_nv2a_fp_timing)
  add_executable(test_nv2a_fp_registers tests/test_nv2a_fp_registers.cpp)
  target_include_directories(test_nv2a_fp_registers PRIVATE ${XLLE_INCLUDES})
  target_link_libraries(test_nv2a_fp_registers PRIVATE xlle_machine xlle_host)
  add_test(NAME nv2a_fp_registers COMMAND test_nv2a_fp_registers)
  add_executable(test_nv2a_av_handshake tests/test_nv2a_av_handshake.cpp)
  target_link_libraries(test_nv2a_av_handshake PRIVATE xlle_machine xlle_host)
  add_test(NAME nv2a_av_handshake COMMAND test_nv2a_av_handshake)
  add_executable(test_nv2a_tile_layout tests/test_nv2a_tile_layout.cpp)
  target_include_directories(test_nv2a_tile_layout PRIVATE ${XLLE_INCLUDES})
  add_test(NAME nv2a_tile_layout COMMAND test_nv2a_tile_layout)
  add_executable(test_nv2a_tile_commands tests/test_nv2a_tile_commands.cpp)
  target_link_libraries(test_nv2a_tile_commands PRIVATE xlle_machine xlle_host)
  add_test(NAME nv2a_tile_commands COMMAND test_nv2a_tile_commands)
  add_executable(test_nv2a_vertex_colors tests/test_nv2a_vertex_colors.cpp)
  target_link_libraries(test_nv2a_vertex_colors PRIVATE xlle_machine xlle_host)
  add_test(NAME nv2a_vertex_colors COMMAND test_nv2a_vertex_colors)
  add_executable(test_nv2a_zeta_compression tests/test_nv2a_zeta_compression.cpp)
  target_include_directories(test_nv2a_zeta_compression PRIVATE ${XLLE_INCLUDES})
  add_test(NAME nv2a_zeta_compression COMMAND test_nv2a_zeta_compression)
  add_executable(test_nv2a tests/test_nv2a.cpp src/machine/nv2a.cpp src/machine/pci.cpp)
  target_include_directories(test_nv2a PRIVATE ${XLLE_INCLUDES})
  target_link_libraries(test_nv2a PRIVATE nv2a_vsh_emulator xlle_host)
  if(XLLE_VERTEX_REUSE_DIAGNOSTICS)
    target_compile_definitions(test_nv2a PRIVATE XLLE_VERTEX_REUSE_DIAGNOSTICS=1)
  else()
    target_compile_definitions(test_nv2a PRIVATE XLLE_VERTEX_REUSE_DIAGNOSTICS=0)
  endif()
  if(XLLE_NATIVE_OPTIMIZATIONS AND
     CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    target_compile_options(test_nv2a PRIVATE -march=native)
  endif()
  add_test(NAME nv2a COMMAND test_nv2a)
  # Private original-code qualification bridge. Reuses the maintained hardware
  # implementation; it is neither installed nor part of the shipping pilot.
  add_library(xlle_nv2a_reference SHARED EXCLUDE_FROM_ALL tools/nv2a_reference.cpp)
  target_link_libraries(xlle_nv2a_reference PRIVATE xlle_machine xlle_host)
endif()
if(XLLE_RENDERER STREQUAL "HLE")
  add_executable(test_nv2a_hle_renderer tests/test_nv2a_hle_renderer.cpp)
  target_link_libraries(test_nv2a_hle_renderer PRIVATE xlle_host)
  add_test(NAME nv2a_hle_renderer COMMAND test_nv2a_hle_renderer)
endif()
add_executable(test_nv2a_xemu_psh tests/test_nv2a_xemu_psh.cpp)
target_link_libraries(test_nv2a_xemu_psh PRIVATE xlle_host)
add_test(NAME nv2a_xemu_psh COMMAND test_nv2a_xemu_psh)
set_tests_properties(nv2a_xemu_psh PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_apu tests/test_apu.cpp)
target_include_directories(test_apu PRIVATE ${XLLE_INCLUDES})
add_test(NAME apu COMMAND test_apu)
add_executable(test_apu_source tests/test_apu_source.cpp)
target_include_directories(test_apu_source PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_apu_source PRIVATE xlle_apu_adpcm)
add_test(NAME apu_source COMMAND test_apu_source)
add_executable(test_apu_headroom tests/test_apu_headroom.cpp)
target_include_directories(test_apu_headroom PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_apu_headroom PRIVATE xlle_apu_adpcm)
add_test(NAME apu_headroom COMMAND test_apu_headroom)
add_executable(test_apu_publication tests/test_apu_publication.cpp)
target_include_directories(test_apu_publication PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_apu_publication PRIVATE xlle_apu_adpcm)
add_test(NAME apu_publication COMMAND test_apu_publication)
add_executable(test_apu_input_sge tests/test_apu_input_sge.cpp)
target_include_directories(test_apu_input_sge PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_apu_input_sge PRIVATE xlle_apu_adpcm)
add_test(NAME apu_input_sge COMMAND test_apu_input_sge)
add_executable(test_apu_ssl tests/test_apu_ssl.cpp)
target_link_libraries(test_apu_ssl PRIVATE xlle_apu_rate)
add_test(NAME apu_ssl COMMAND test_apu_ssl)
add_executable(test_apu_rate tests/test_apu_rate.cpp)
target_link_libraries(test_apu_rate PRIVATE xlle_apu_rate)
add_test(NAME apu_rate COMMAND test_apu_rate)
add_executable(test_apu_envelope tests/test_apu_envelope.cpp)
target_link_libraries(test_apu_envelope PRIVATE xlle_apu_rate)
add_test(NAME apu_envelope COMMAND test_apu_envelope)
add_executable(test_apu_envelope_lifecycle tests/test_apu_envelope_lifecycle.cpp)
target_link_libraries(test_apu_envelope_lifecycle PRIVATE xlle_apu_rate)
add_test(NAME apu_envelope_lifecycle COMMAND test_apu_envelope_lifecycle)
add_executable(test_apu_gp_dma tests/test_apu_gp_dma.cpp)
target_link_libraries(test_apu_gp_dma PRIVATE xlle_apu_rate)
add_test(NAME apu_gp_dma COMMAND test_apu_gp_dma)
add_executable(test_apu_gp_fifo tests/test_apu_gp_fifo.cpp)
target_link_libraries(test_apu_gp_fifo PRIVATE xlle_apu_rate)
add_test(NAME apu_gp_fifo COMMAND test_apu_gp_fifo)
add_executable(test_apu_stop tests/test_apu_stop.cpp)
target_link_libraries(test_apu_stop PRIVATE xlle_apu_rate)
add_test(NAME apu_stop COMMAND test_apu_stop)
add_executable(test_apu_fe_fence tests/test_apu_fe_fence.cpp)
target_link_libraries(test_apu_fe_fence PRIVATE xlle_apu_rate)
add_test(NAME apu_fe_fence COMMAND test_apu_fe_fence)
add_executable(test_apu_completion tests/test_apu_completion.cpp)
target_link_libraries(test_apu_completion PRIVATE xlle_apu_rate)
add_test(NAME apu_completion COMMAND test_apu_completion)
add_executable(test_apu_voice_quantum tests/test_apu_voice_quantum.cpp)
target_link_libraries(test_apu_voice_quantum PRIVATE xlle_apu_rate)
add_test(NAME apu_voice_quantum COMMAND test_apu_voice_quantum)

add_library(xlle_apu_reference SHARED EXCLUDE_FROM_ALL tools/apu_reference.cpp src/machine/pic.cpp)
target_include_directories(xlle_apu_reference PRIVATE ${XLLE_INCLUDES})
target_link_libraries(xlle_apu_reference PRIVATE xlle_apu_adpcm)
target_link_libraries(xlle_apu_reference PRIVATE xlle_apu_rate)

add_executable(test_aci tests/test_aci.cpp)
target_include_directories(test_aci PRIVATE ${XLLE_INCLUDES})
add_test(NAME aci COMMAND test_aci)
add_executable(test_aci_spdif tests/test_aci_spdif.cpp)
target_include_directories(test_aci_spdif PRIVATE ${XLLE_INCLUDES})
add_test(NAME aci_spdif COMMAND test_aci_spdif)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/AciOutput.cmake")

add_executable(test_ide_security tests/test_ide_security.cpp src/machine/ide.cpp)
target_include_directories(test_ide_security PRIVATE ${XLLE_INCLUDES})
add_test(NAME ide_security COMMAND test_ide_security)
add_executable(test_ide_dvd tests/test_ide_dvd.cpp src/machine/ide.cpp src/machine/smbus.cpp)
target_include_directories(test_ide_dvd PRIVATE ${XLLE_INCLUDES})
add_test(NAME ide_dvd COMMAND test_ide_dvd)

# Private original-filesystem qualification, using the existing disk/PIC floor.
# Kept out of installed/shipping targets; owned images are opened read-only.
add_library(xlle_storage_reference SHARED EXCLUDE_FROM_ALL
  tools/storage_reference.cpp src/machine/ide.cpp src/machine/pic.cpp src/machine/pit.cpp)
target_include_directories(xlle_storage_reference PRIVATE ${XLLE_INCLUDES})
add_executable(test_pit tests/test_pit.cpp src/machine/pit.cpp src/machine/pic.cpp)
target_include_directories(test_pit PRIVATE ${XLLE_INCLUDES})
add_test(NAME pit COMMAND test_pit)

add_executable(test_rtc tests/test_rtc.cpp src/machine/rtc.cpp)
target_include_directories(test_rtc PRIVATE ${XLLE_INCLUDES})
add_test(NAME rtc COMMAND test_rtc)
add_library(xlle_rtc_reference SHARED EXCLUDE_FROM_ALL tools/rtc_reference.cpp src/machine/rtc.cpp)
target_include_directories(xlle_rtc_reference PRIVATE ${XLLE_INCLUDES})

add_executable(test_smc tests/test_smc.cpp src/machine/smbus.cpp)
target_include_directories(test_smc PRIVATE ${XLLE_INCLUDES})
add_test(NAME smc COMMAND test_smc)
add_executable(test_eeprom tests/test_eeprom.cpp src/machine/smbus.cpp)
target_include_directories(test_eeprom PRIVATE ${XLLE_INCLUDES})
add_test(NAME eeprom COMMAND test_eeprom)
add_executable(test_smbus_av tests/test_smbus_av.cpp src/machine/smbus.cpp)
target_include_directories(test_smbus_av PRIVATE ${XLLE_INCLUDES})
add_test(NAME smbus_av COMMAND test_smbus_av)

add_executable(test_cx25871_c4 tests/test_cx25871_c4.cpp)
target_include_directories(test_cx25871_c4 PRIVATE ${XLLE_INCLUDES})
add_test(NAME cx25871_c4 COMMAND test_cx25871_c4)

add_executable(test_ohci tests/test_ohci.cpp src/machine/ohci.cpp)
target_include_directories(test_ohci PRIVATE ${XLLE_INCLUDES})
add_test(NAME ohci COMMAND test_ohci)

add_executable(test_ohci_synthetic_hub tests/test_ohci_synthetic_hub.cpp src/machine/ohci.cpp)
target_include_directories(test_ohci_synthetic_hub PRIVATE ${XLLE_INCLUDES})
add_test(NAME ohci_synthetic_hub COMMAND test_ohci_synthetic_hub)

add_executable(test_xid_input tests/test_xid_input.cpp src/host/xid_input.cpp)
target_include_directories(test_xid_input PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_xid_input PRIVATE xlle_machine)
add_test(NAME xid_input COMMAND test_xid_input)

if(XLLE_WITH_SDL)
  add_executable(test_present tests/test_present.cpp)
  target_link_libraries(test_present PRIVATE xlle_host)
  add_test(NAME present COMMAND test_present)
endif()

add_executable(test_xid_script tests/test_xid_script.cpp)
target_include_directories(test_xid_script PRIVATE ${XLLE_INCLUDES})
add_test(NAME xid_script COMMAND test_xid_script)

add_executable(test_xid_down_a_script tests/test_xid_down_a_script.cpp)
target_include_directories(test_xid_down_a_script PRIVATE ${XLLE_INCLUDES})
add_test(NAME xid_down_a_script COMMAND test_xid_down_a_script)

add_executable(test_xid_generic_script tests/test_xid_generic_script.cpp)
target_include_directories(test_xid_generic_script PRIVATE ${XLLE_INCLUDES})
target_compile_definitions(test_xid_generic_script PRIVATE
    XLLE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME xid_generic_script COMMAND test_xid_generic_script)

add_executable(test_machine_config tests/test_machine_config.cpp)
target_include_directories(test_machine_config PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_machine_config PRIVATE xlle_machine)
add_test(NAME machine_config COMMAND test_machine_config)

add_executable(test_unicorn_floor tests/test_unicorn_floor.cpp)
target_include_directories(test_unicorn_floor PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_unicorn_floor PRIVATE xlle_cpu)
add_test(NAME unicorn_floor COMMAND test_unicorn_floor)

# Real pinned TLB policy with controlled timestamps; no Xbox inputs or sleeps.
foreach(tlb_variant IN ITEMS policy host_timed)
  add_executable(test_unicorn_tlb_${tlb_variant} tests/test_unicorn_tlb_policy.c)
  target_include_directories(test_unicorn_tlb_${tlb_variant} PRIVATE
      "$<TARGET_PROPERTY:x86_64-softmmu,INCLUDE_DIRECTORIES>"
      "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu")
  target_compile_options(test_unicorn_tlb_${tlb_variant} PRIVATE
      "$<TARGET_PROPERTY:x86_64-softmmu,COMPILE_OPTIONS>")
  target_compile_definitions(test_unicorn_tlb_${tlb_variant} PRIVATE
      "$<TARGET_PROPERTY:x86_64-softmmu,COMPILE_DEFINITIONS>")
  if(tlb_variant STREQUAL "host_timed")
    target_compile_definitions(test_unicorn_tlb_${tlb_variant} PRIVATE XLLE_TEST_HOST_TLB=1)
  elseif(NOT XLLE_HOST_TIMED_TLB)
    target_compile_definitions(test_unicorn_tlb_${tlb_variant} PRIVATE XLLE_TEST_EXPECT_FLUSH_CLOCK=1)
  endif()
  target_link_libraries(test_unicorn_tlb_${tlb_variant} PRIVATE unicorn)
  add_test(NAME unicorn_tlb_${tlb_variant} COMMAND test_unicorn_tlb_${tlb_variant})
endforeach()

add_executable(test_nvnet tests/test_nvnet.cpp src/machine/nvnet.cpp)
target_include_directories(test_nvnet PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_nvnet PRIVATE xlle_net)
add_test(NAME nvnet COMMAND test_nvnet)

# The fixture spike is itself an acceptance test: all fixtures must pass (exit 0).
add_test(NAME spike COMMAND xlle)

# Source-level guard tests (pure Python, no build artifacts required). Each one pins
# structural invariants of a perf change that a compiler cannot check:
#   perf_excl_guards            - the exclusive-time partition (beads-xbox.1.14.3)
#   dispatch_table_guards       - the NV097 jump table equals the old if-chain (beads-xbox.1.15.1)
#   gl_timing_guards            - every GL call routes through a timed wrapper (beads-xbox.1.14.6)
#   raster_specialization_guards- compile-time profile dispatch, gated hashes (beads-xbox.1.15.2)
#   seam_guards                 - the latch/execute seam and its anchors (beads-xbox.1.15.4)
#   draw_threads_guards         - the threaded draw consumer and its fences (beads-xbox.1.15.5)
#   span_raster_guards          - within-job destination-span parallel raster (beads-xbox.1.15.9)
#   cpu_blockhook_guards        - block-granularity CPU tier + guest TSC (beads-xbox.1.4.6)
#   idle_fast_forward_guards    - the idle-loop skip and its preconditions (beads-xbox.1.4.7)
#   gpu_profiles_guards         - the witnessed-profile GPU executor, its exact state
#                                 predicate, its residency transitions and its default-off
#                                 flag (beads-xbox.1.15.7)
#   vertex_pipeline_guards      - the parsed-vertex-program cache, the per-vertex hoists
#                                 and the always-on residency/vertex censuses that
#                                 re-ranked the work (beads-xbox.1.15.10 phase 5),
#                                 plus the pre-decoded compiled step form, its
#                                 read-before-write zero plan and its differential
#                                 verifier (beads-xbox.1.15.11)
#   hdd_cow_guards              - copy-on-write HDD attach + the determinism digests
#                                 (beads-xbox.1.14.11): without it the replay rewrites the
#                                 BRFR boot counter in its own image on every run
# They read src/ directly, so they run from the source dir, not the build dir.
find_package(Python3 COMPONENTS Interpreter)
set(XLLE_AOT_PRIVATE_INPUT "" CACHE FILEPATH "Private prepared .aot-private.json input; never distribute its generated code")
set(XLLE_AOT_PRIVATE_OPTIMIZATION "O2" CACHE STRING "Private generated-C validation setting (O2 or O0)")
if(XLLE_AOT_PRIVATE_INPUT)
  if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "Private AOT generation requires Python 3")
  endif()
  if(NOT XLLE_AOT_PRIVATE_INPUT MATCHES "\\.aot-private\\.json$")
    message(FATAL_ERROR "Private AOT input must have the guarded .aot-private.json suffix")
  endif()
  file(READ "${XLLE_AOT_PRIVATE_INPUT}" _xlle_private_json)
  string(JSON _xlle_private GET "${_xlle_private_json}" private_guest_input)
  if(NOT _xlle_private)
    message(FATAL_ERROR "Private AOT input must explicitly identify private_guest_input=true")
  endif()
  if(NOT XLLE_AOT_PRIVATE_OPTIMIZATION MATCHES "^O[02]$")
    message(FATAL_ERROR "Private generated C supports validation settings O2 and O0")
  endif()
  set(_xlle_private_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/private_aot")
  add_custom_command(
    OUTPUT "${_xlle_private_dir}/private_aot.c" "${_xlle_private_dir}/private_aot.h"
           "${_xlle_private_dir}/manifest.aot-private.json"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/aot_compile.py"
            --input "${XLLE_AOT_PRIVATE_INPUT}"
            --output-c "${_xlle_private_dir}/private_aot.c"
            --output-h "${_xlle_private_dir}/private_aot.h"
            --manifest "${_xlle_private_dir}/manifest.aot-private.json"
    DEPENDS tools/aot_compile.py "${XLLE_AOT_PRIVATE_INPUT}"
    VERBATIM)
  add_library(xlle_aot_private STATIC "${_xlle_private_dir}/private_aot.c")
  target_include_directories(xlle_aot_private PUBLIC include "${_xlle_private_dir}")
  set_target_properties(xlle_aot_private PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xlle_aot_private PRIVATE -Wall -Wextra -Werror -pedantic-errors
                           "-${XLLE_AOT_PRIVATE_OPTIMIZATION}")
  endif()
  target_link_libraries(xlle PRIVATE xlle_aot_private)
  target_compile_definitions(xlle PRIVATE XLLE_HAS_PRIVATE_AOT=1)
endif()
if(Python3_Interpreter_FOUND)
  # Synthetic AOT proof only: these generated guest programs are not linked into
  # xlle. Proprietary artifacts must use a private build directory/input path.
  # Compile the qualification adapter with the pinned target's private headers
  # and definitions. It is linked only into this test, never into xlle.
  foreach(probe_variant IN ITEMS optimized unoptimized)
    add_library(xlle_x87_probe_${probe_variant} STATIC tests/aot_x87_probe.c)
    target_include_directories(xlle_x87_probe_${probe_variant} PRIVATE
        $<TARGET_PROPERTY:x86_64-softmmu,INCLUDE_DIRECTORIES>)
    target_compile_options(xlle_x87_probe_${probe_variant} PRIVATE
        $<TARGET_PROPERTY:x86_64-softmmu,COMPILE_OPTIONS>)
    target_compile_definitions(xlle_x87_probe_${probe_variant} PRIVATE
        $<TARGET_PROPERTY:x86_64-softmmu,COMPILE_DEFINITIONS>)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      if(probe_variant STREQUAL "optimized")
        target_compile_options(xlle_x87_probe_${probe_variant} PRIVATE -O2)
      else()
        target_compile_options(xlle_x87_probe_${probe_variant} PRIVATE -O0)
      endif()
    endif()
    add_executable(test_aot_x87_${probe_variant} tests/test_aot_x87.cpp)
    target_include_directories(test_aot_x87_${probe_variant} PRIVATE "${unicorn_SOURCE_DIR}/include")
    target_link_libraries(test_aot_x87_${probe_variant} PRIVATE xlle_x87_probe_${probe_variant} unicorn)
    add_test(NAME aot_x87_helpers_${probe_variant} COMMAND test_aot_x87_${probe_variant})
  endforeach()
  set(XLLE_AOT_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/aot")
  add_executable(test_mmx_floor tests/test_mmx_floor.cpp)
  target_include_directories(test_mmx_floor PRIVATE "${unicorn_SOURCE_DIR}/include")
  target_link_libraries(test_mmx_floor PRIVATE xlle_x87_probe_optimized unicorn)
  add_test(NAME mmx_floor COMMAND test_mmx_floor)
  add_executable(test_x87_floor tests/test_x87_floor.cpp)
  target_include_directories(test_x87_floor PRIVATE "${unicorn_SOURCE_DIR}/include")
  target_link_libraries(test_x87_floor PRIVATE xlle_x87_probe_optimized unicorn)
  add_test(NAME x87_floor COMMAND test_x87_floor)
  add_executable(test_cpu_deadlines tests/test_cpu_deadlines.cpp)
  target_link_libraries(test_cpu_deadlines PRIVATE xlle_cpu)
  add_test(NAME cpu_deadlines COMMAND test_cpu_deadlines
           "${CMAKE_CURRENT_BINARY_DIR}/generated/test-deadlines.census.csv")
  add_executable(test_cmpxchg8b_floor tests/test_cmpxchg8b_floor.cpp)
  target_link_libraries(test_cmpxchg8b_floor PRIVATE xlle_cpu)
  add_test(NAME cmpxchg8b_floor COMMAND test_cmpxchg8b_floor)
  add_executable(test_idle_fast_forward tests/test_idle_fast_forward.cpp)
  target_link_libraries(test_idle_fast_forward PRIVATE xlle_machine)
  add_test(NAME idle_fast_forward COMMAND test_idle_fast_forward)
  set(XLLE_AOT_REGISTRY_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/aot-registry")
  add_custom_command(
    OUTPUT "${XLLE_AOT_REGISTRY_GENERATED}/registry.c"
           "${XLLE_AOT_REGISTRY_GENERATED}/registry.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_aot_registry_fixture.py"
            "${XLLE_AOT_REGISTRY_GENERATED}"
    DEPENDS tests/generate_aot_registry_fixture.py tools/aot_compile.py
    VERBATIM)
  add_custom_command(
    OUTPUT "${XLLE_AOT_GENERATED}/aot_programs.c"
           "${XLLE_AOT_GENERATED}/aot_programs.h"
           "${XLLE_AOT_GENERATED}/aot_manifest.json"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XLLE_AOT_GENERATED}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/aot_compile.py"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/aot_programs.json"
            --output-c "${XLLE_AOT_GENERATED}/aot_programs.c"
            --output-h "${XLLE_AOT_GENERATED}/aot_programs.h"
            --manifest "${XLLE_AOT_GENERATED}/aot_manifest.json"
    DEPENDS tools/aot_compile.py tests/aot_programs.json
    VERBATIM)
  foreach(aot_variant IN ITEMS optimized unoptimized)
    # Authored scalar fixtures; generated guest bytes never enter xlle itself.
    set(XLLE_AOT_SCALAR_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/aot-scalar")
    if(aot_variant STREQUAL "optimized")
      add_custom_command(
        OUTPUT "${XLLE_AOT_SCALAR_GENERATED}/scalar.c"
               "${XLLE_AOT_SCALAR_GENERATED}/scalar.h"
               "${XLLE_AOT_SCALAR_GENERATED}/scalar_cases.hpp"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_aot_scalar_fixture.py"
                "${XLLE_AOT_SCALAR_GENERATED}"
        DEPENDS tests/generate_aot_scalar_fixture.py tools/aot_compile.py
        VERBATIM)
    endif()
    add_library(xlle_aot_scalar_${aot_variant} STATIC "${XLLE_AOT_SCALAR_GENERATED}/scalar.c")
    target_include_directories(xlle_aot_scalar_${aot_variant} PUBLIC include "${XLLE_AOT_SCALAR_GENERATED}")
    set_target_properties(xlle_aot_scalar_${aot_variant} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(xlle_aot_scalar_${aot_variant} PRIVATE -Wall -Wextra -Werror -pedantic-errors)
      if(aot_variant STREQUAL "optimized")
        target_compile_options(xlle_aot_scalar_${aot_variant} PRIVATE -O3)
      else()
        target_compile_options(xlle_aot_scalar_${aot_variant} PRIVATE -O0)
      endif()
    endif()
    add_executable(test_aot_scalar_${aot_variant} tests/test_aot_scalar.cpp)
    target_link_libraries(test_aot_scalar_${aot_variant} PRIVATE xlle_aot_scalar_${aot_variant} xlle_machine)
    add_test(NAME aot_scalar_${aot_variant} COMMAND test_aot_scalar_${aot_variant})
    add_library(xlle_aot_registry_${aot_variant} STATIC
                "${XLLE_AOT_REGISTRY_GENERATED}/registry.c")
    target_include_directories(xlle_aot_registry_${aot_variant} PUBLIC
                               include "${XLLE_AOT_REGISTRY_GENERATED}")
    set_target_properties(xlle_aot_registry_${aot_variant} PROPERTIES
                         C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(xlle_aot_registry_${aot_variant} PRIVATE
                             -Wall -Wextra -Werror -pedantic-errors)
      if(aot_variant STREQUAL "optimized")
        target_compile_options(xlle_aot_registry_${aot_variant} PRIVATE -O2)
      else()
        target_compile_options(xlle_aot_registry_${aot_variant} PRIVATE -O0)
      endif()
    endif()
    add_executable(test_aot_registry_${aot_variant} tests/test_aot_registry.cpp)
    target_link_libraries(test_aot_registry_${aot_variant} PRIVATE
                          xlle_aot_registry_${aot_variant} xlle_machine)
    add_test(NAME aot_registry_${aot_variant} COMMAND test_aot_registry_${aot_variant})
    add_library(xlle_aot_fixture_${aot_variant} STATIC
                "${XLLE_AOT_GENERATED}/aot_programs.c")
    target_include_directories(xlle_aot_fixture_${aot_variant} PUBLIC
                               include "${XLLE_AOT_GENERATED}")
    set_target_properties(xlle_aot_fixture_${aot_variant} PROPERTIES
                           C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(xlle_aot_fixture_${aot_variant} PRIVATE
                             -Wall -Wextra -Werror -pedantic-errors)
      if(aot_variant STREQUAL "optimized")
        target_compile_options(xlle_aot_fixture_${aot_variant} PRIVATE -O3)
      else()
        target_compile_options(xlle_aot_fixture_${aot_variant} PRIVATE -O0)
      endif()
    endif()
    add_executable(test_aot_${aot_variant} tests/test_aot.cpp)
    add_executable(test_aot_x87_machine_${aot_variant} tests/test_aot_x87_machine.cpp)
    target_link_libraries(test_aot_x87_machine_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_machine)
    add_test(NAME aot_x87_machine_${aot_variant} COMMAND test_aot_x87_machine_${aot_variant})
    target_link_libraries(test_aot_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_cpu xlle_spike)
    add_test(NAME aot_${aot_variant} COMMAND test_aot_${aot_variant})
    add_executable(test_aot_mmx_${aot_variant} tests/test_aot_mmx.cpp)
    target_link_libraries(test_aot_mmx_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_machine)
    add_test(NAME aot_mmx_${aot_variant} COMMAND test_aot_mmx_${aot_variant})
    add_executable(test_aot_machine_${aot_variant} tests/test_aot_machine.cpp
                   tests/test_aot_machine_state.cpp tests/test_aot_irq_shadow.cpp
                   tests/test_aot_store_fault.cpp)
    target_link_libraries(test_aot_machine_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_machine)
    add_test(NAME aot_machine_${aot_variant} COMMAND test_aot_machine_${aot_variant})
    add_executable(test_aot_integer_transfer_${aot_variant} tests/test_aot_integer_transfer.cpp)
    target_link_libraries(test_aot_integer_transfer_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_machine)
    add_test(NAME aot_integer_transfer_${aot_variant} COMMAND test_aot_integer_transfer_${aot_variant})
    add_executable(test_aot_required_${aot_variant} tests/test_aot_required.cpp)
    target_link_libraries(test_aot_required_${aot_variant} PRIVATE
                          xlle_aot_fixture_${aot_variant} xlle_machine)
    add_test(NAME aot_required_${aot_variant} COMMAND test_aot_required_${aot_variant})
  endforeach()
  add_test(NAME aot_compile
           COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aot_compile.py")
  add_test(NAME aot_inventory
           COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aot_inventory.py")
  add_test(NAME cpu_block_census
           COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpu_block_census.py")
  add_test(NAME aot_private_input
           COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aot_private_input.py")
endif()
if(Python3_Interpreter_FOUND)
  if(WIN32)
    add_test(NAME host_sample_report COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_sample_report.py $<TARGET_FILE:host_sample_probe>)
  else()
    add_test(NAME host_sample_report COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_sample_report.py)
  endif()
  add_test(NAME eeprom_generation
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_eeprom_generation.py)
  add_test(NAME hdd_security_provisioning
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hdd_security_provisioning.py
                   $<TARGET_FILE:xlle>)
  add_test(NAME replay_observer_reports
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_replay_observer_reports.py)
  add_test(NAME native_audio_ingress
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_audio_ingress.py)
  set(XLLE_PYTHON_GUARD_TESTS
      perf_excl_guards
      dispatch_table_guards
      gl_timing_guards
      raster_specialization_guards
      seam_guards
      draw_threads_guards
      span_raster_guards
      cpu_blockhook_guards
      idle_fast_forward_guards
      hdd_cow_guards
      gpu_profiles_guards
      vertex_pipeline_guards)
  foreach(guard IN LISTS XLLE_PYTHON_GUARD_TESTS)
    add_test(NAME ${guard}
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${guard}.py)
    set_tests_properties(${guard} PROPERTIES
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()
  add_test(NAME product_window_summary
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_product_window_summary.py)
  add_test(NAME generated_uniform_report
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generated_uniform_report.py)
  add_test(NAME generated_submit_report
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generated_submit_report.py)
  add_test(NAME vsh_draw_overlap_report
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vsh_draw_overlap_report.py)
else()
  message(STATUS "Python3 interpreter not found; skipping source guard tests")
endif()

if(WIN32)
  find_program(XLLE_POWERSHELL NAMES powershell)
  if(XLLE_POWERSHELL)
    foreach(replay_test IN ITEMS replay_quiescence replay_pair_retry)
      add_test(NAME ${replay_test}
               COMMAND "${XLLE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                       -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${replay_test}.ps1")
    endforeach()
  endif()
endif()
