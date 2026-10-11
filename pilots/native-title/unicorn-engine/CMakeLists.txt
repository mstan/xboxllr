cmake_minimum_required(VERSION 3.20)
project(xlle_native_unicorn_engine C)
set(XLLE_UNICORN_SOURCE "" CACHE PATH "Unmodified pinned Unicorn 2.1.4 checkout")
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${XLLE_UNICORN_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE engine_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0 OR NOT engine_commit STREQUAL "8028ec436f2d9376525352dd38ed9ed6b9f6be10")
  message(FATAL_ERROR "Native engine requires the reviewed Unicorn 2.1.4 source commit")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${XLLE_UNICORN_SOURCE}" diff --quiet HEAD --
  RESULT_VARIABLE dirty_result)
if(NOT dirty_result EQUAL 0)
  message(FATAL_ERROR "Native engine source must match its pinned commit")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(UNICORN_ARCH x86 CACHE STRING "" FORCE)
set(UNICORN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)
set(UNICORN_LEGACY_STATIC_ARCHIVE OFF CACHE BOOL "" FORCE)
add_subdirectory("${XLLE_UNICORN_SOURCE}" unicorn)
get_filename_component(xlle_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include(fxstate.cmake)
target_sources(x86_64-softmmu PRIVATE "${xlle_root}/src/cpu/unicorn_irq_boundary.c")
# Bundle the three pinned x86 components without upstream's optional .o
# symlink, which requires an unnecessary privilege on Windows.
add_library(xlle_native_unicorn_package STATIC $<TARGET_OBJECTS:unicorn>
  $<TARGET_OBJECTS:unicorn-common> $<TARGET_OBJECTS:x86_64-softmmu>)
set_target_properties(xlle_native_unicorn_package PROPERTIES OUTPUT_NAME unicorn)
target_include_directories(xlle_native_unicorn_package PUBLIC "${XLLE_UNICORN_SOURCE}/include")
target_compile_definitions(xlle_native_unicorn_package PUBLIC UNICORN_STATIC)
add_custom_command(TARGET xlle_native_unicorn_package POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:xlle_native_unicorn_package>"
    "-DSOURCE=${XLLE_UNICORN_SOURCE}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/package"
    "-DHELPER_SOURCE=${xlle_root}/src/cpu/unicorn_irq_boundary.c"
    "-DHELPER_HEADER=${xlle_root}/src/cpu/unicorn_irq_boundary.h"
    "-DFXSTATE_PATCH_SHA=${fxstate_patch_sha}" "-DFPU_ORIGINAL_SHA=${fpu_original_sha}"
    "-DFPU_CORRECTED_SHA=${fpu_corrected_sha}" "-DFPU_CORRECTED=${fpu_corrected}"
    "-DTRANSLATOR_ORIGINAL_SHA=${translator_original_sha}"
    "-DTRANSLATOR_CORRECTED_SHA=${translator_corrected_sha}" "-DTRANSLATOR_CORRECTED=${translator_corrected}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/package.cmake")
add_executable(test_native_unicorn_irq "${xlle_root}/tests/test_native_unicorn_irq.c")
target_include_directories(test_native_unicorn_irq PRIVATE "${xlle_root}/src/cpu")
target_link_libraries(test_native_unicorn_irq PRIVATE xlle_native_unicorn_package)
enable_testing()
add_test(NAME native_unicorn_irq_boundary COMMAND test_native_unicorn_irq)
set_tests_properties(native_unicorn_irq_boundary PROPERTIES TIMEOUT 10)
add_executable(test_native_unicorn_fxstate "${xlle_root}/tests/test_native_unicorn_fxstate.c")
target_link_libraries(test_native_unicorn_fxstate PRIVATE xlle_native_unicorn_package)
add_test(NAME native_unicorn_fxstate COMMAND test_native_unicorn_fxstate)
set_tests_properties(native_unicorn_fxstate PROPERTIES TIMEOUT 10)
add_executable(test_native_unicorn_fp_metadata "${xlle_root}/tests/test_native_unicorn_fp_metadata.c")
target_link_libraries(test_native_unicorn_fp_metadata PRIVATE xlle_native_unicorn_package)
add_test(NAME native_unicorn_fp_metadata COMMAND test_native_unicorn_fp_metadata)
set_tests_properties(native_unicorn_fp_metadata PROPERTIES TIMEOUT 10)
