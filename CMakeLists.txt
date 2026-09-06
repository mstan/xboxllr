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
add_library(xlle_cpu STATIC src/cpu/unicorn_floor.cpp)
target_include_directories(xlle_cpu PUBLIC ${XLLE_INCLUDES})
target_include_directories(xlle_cpu PRIVATE ${unicorn_SOURCE_DIR}/include)
target_link_libraries(xlle_cpu PUBLIC unicorn)

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
target_link_libraries(xlle_machine PUBLIC xlle_cpu nv2a_vsh_emulator)
if(XLLE_DISPATCH_MICRO)
  target_compile_definitions(xlle_machine PUBLIC XLLE_DISPATCH_MICRO=1)
endif()
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
  src/host/nv2a_opengl_renderer.cpp src/host/nv2a_combiner_glsl.cpp)
target_include_directories(xlle_host PUBLIC ${XLLE_INCLUDES})
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
enable_testing()
add_executable(test_ring tests/test_ring.cpp)
target_include_directories(test_ring PRIVATE ${XLLE_INCLUDES})
add_test(NAME ring COMMAND test_ring)

add_executable(test_jsonl tests/test_jsonl.cpp)
target_include_directories(test_jsonl PRIVATE ${XLLE_INCLUDES})
add_test(NAME jsonl COMMAND test_jsonl)

add_executable(test_nv2a tests/test_nv2a.cpp src/machine/nv2a.cpp)
target_include_directories(test_nv2a PRIVATE ${XLLE_INCLUDES})
target_link_libraries(test_nv2a PRIVATE nv2a_vsh_emulator xlle_host)
if(XLLE_NATIVE_OPTIMIZATIONS AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  target_compile_options(test_nv2a PRIVATE -march=native)
endif()
add_test(NAME nv2a COMMAND test_nv2a)

add_executable(test_apu tests/test_apu.cpp)
target_include_directories(test_apu PRIVATE ${XLLE_INCLUDES})
add_test(NAME apu COMMAND test_apu)

add_executable(test_aci tests/test_aci.cpp)
target_include_directories(test_aci PRIVATE ${XLLE_INCLUDES})
add_test(NAME aci COMMAND test_aci)

add_executable(test_smc tests/test_smc.cpp src/machine/smbus.cpp)
target_include_directories(test_smc PRIVATE ${XLLE_INCLUDES})
add_test(NAME smc COMMAND test_smc)

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
if(Python3_Interpreter_FOUND)
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
else()
  message(STATUS "Python3 interpreter not found; skipping source guard tests")
endif()
