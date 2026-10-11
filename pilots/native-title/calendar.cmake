# Private original-kernel execution; selected at build time, never a runtime
# HLE/LLE selector. The HLE build does not link the interpreter or kernel bytes.
set(PILOT_CALENDAR "HLE" CACHE STRING "Calendar implementation: HLE or LLE")
set_property(CACHE PILOT_CALENDAR PROPERTY STRINGS HLE LLE)
if(PILOT_CALENDAR STREQUAL "LLE")
  set(PILOT_CALENDAR_KERNEL "" CACHE FILEPATH "Owned flat original kernel image")
  set(PILOT_CALENDAR_UNICORN "" CACHE PATH "Unicorn package with include and lib")
  set(PILOT_CALENDAR_UNICORN_NOTICES "" CACHE PATH "Unicorn source notices directory")
  foreach(required "${PILOT_CALENDAR_KERNEL}"
      "${PILOT_CALENDAR_UNICORN}/include/unicorn/unicorn.h"
      "${PILOT_CALENDAR_UNICORN}/lib/unicorn.lib"
      "${PILOT_CALENDAR_UNICORN_NOTICES}/COPYING"
      "${PILOT_CALENDAR_UNICORN_NOTICES}/COPYING.LGPL2")
    if(NOT EXISTS "${required}")
      message(FATAL_ERROR "LLE calendar requires ${required}")
    endif()
  endforeach()
  file(SHA256 "${PILOT_CALENDAR_KERNEL}" calendar_sha)
  if(NOT calendar_sha STREQUAL "827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c")
    message(FATAL_ERROR "LLE calendar kernel revision has no reviewed contract")
  endif()
  file(SHA256 "${PILOT_CALENDAR_UNICORN}/lib/unicorn.lib" calendar_engine_sha)
  set(pilot_calendar_irq_boundary FALSE)
  if(EXISTS "${PILOT_CALENDAR_UNICORN}/engine.aot-private.json")
    file(READ "${PILOT_CALENDAR_UNICORN}/engine.aot-private.json" engine_identity)
    string(JSON engine_api GET "${engine_identity}" api)
    string(JSON engine_commit GET "${engine_identity}" source_commit)
    string(JSON engine_library_sha GET "${engine_identity}" library_sha256)
    string(JSON engine_helper_sha GET "${engine_identity}" helper_sha256)
    string(JSON engine_helper_header_sha GET "${engine_identity}" helper_header_sha256)
    string(JSON engine_fxstate_sha ERROR_VARIABLE engine_fxstate_error GET "${engine_identity}" fxstate_patch_sha256)
    string(JSON engine_fpu_sha ERROR_VARIABLE engine_fpu_error GET "${engine_identity}" fpu_compiled_sha256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/unicorn-engine/fxstate.cmake" current_fxstate_sha)
    if(engine_fxstate_error OR engine_fpu_error OR
       NOT engine_fxstate_sha STREQUAL current_fxstate_sha OR
       NOT EXISTS "${PILOT_CALENDAR_UNICORN}/source/fpu_helper.c")
      message(FATAL_ERROR "Native engine needs the reviewed FP state correction; rebuild its package")
    endif()
    file(SHA256 "${PILOT_CALENDAR_UNICORN}/source/fpu_helper.c" packaged_fpu_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpu/unicorn_irq_boundary.c" current_helper_sha)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpu/unicorn_irq_boundary.h" current_helper_header_sha)
    file(SHA256 "${PILOT_CALENDAR_UNICORN}/include/unicorn_irq_boundary.h" packaged_helper_header_sha)
    if(NOT engine_api STREQUAL "xlle-unicorn-irq-v1" OR
        NOT engine_commit STREQUAL "8028ec436f2d9376525352dd38ed9ed6b9f6be10" OR
        NOT engine_library_sha STREQUAL calendar_engine_sha OR
        NOT engine_helper_sha STREQUAL current_helper_sha OR
        NOT engine_helper_header_sha STREQUAL current_helper_header_sha OR
        NOT engine_fpu_sha STREQUAL packaged_fpu_sha OR
        NOT packaged_helper_header_sha STREQUAL current_helper_header_sha)
      message(FATAL_ERROR "Native engine package/source identities disagree; rebuild the reviewed engine")
    endif()
    string(JSON header_count LENGTH "${engine_identity}" public_headers)
    if(header_count LESS 1)
      message(FATAL_ERROR "Native engine package has no compiled public-header identities")
    endif()
    math(EXPR last_header "${header_count}-1")
    foreach(index RANGE 0 ${last_header})
      string(JSON name MEMBER "${engine_identity}" public_headers ${index})
      string(JSON expected_sha GET "${engine_identity}" public_headers "${name}")
      file(SHA256 "${PILOT_CALENDAR_UNICORN}/include/unicorn/${name}" actual_sha)
      if(NOT expected_sha STREQUAL actual_sha)
        message(FATAL_ERROR "Native engine header ${name} differs from its compiled package")
      endif()
    endforeach()
    set(pilot_calendar_irq_boundary TRUE)
  elseif(NOT calendar_engine_sha STREQUAL "60a95ffb6572942b7db1f57901d23e664ed22a54a7d92dfcad15fc3e3ef1f724")
    message(FATAL_ERROR "LLE calendar requires the qualified Unicorn 2.1.4 Windows package")
  endif()
  file(SHA256 "${PILOT_CALENDAR_UNICORN_NOTICES}/COPYING" calendar_notice_sha)
  if(NOT calendar_notice_sha STREQUAL "6f04ae8364d0079a192b14635f4b1da294ce18724c034c39a6a41d1b09df6100")
    message(FATAL_ERROR "LLE calendar requires the original Unicorn 2.1.4 COPYING")
  endif()
  file(READ "${PILOT_CALENDAR_KERNEL}" calendar_bytes HEX)
  string(REGEX REPLACE "(..)" "0x\\1," calendar_bytes "${calendar_bytes}")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/calendar-private")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/calendar-private/calendar_kernel.h"
    "static const unsigned char pilot_calendar_kernel[]={${calendar_bytes}};\n")
  add_library(pilot_calendar_lle STATIC calendar_lle.c)
  target_include_directories(pilot_calendar_lle PRIVATE
    "${PILOT_CALENDAR_UNICORN}/include" "${CMAKE_CURRENT_BINARY_DIR}/calendar-private")
  target_link_libraries(pilot_calendar_lle PUBLIC "${PILOT_CALENDAR_UNICORN}/lib/unicorn.lib")
  foreach(target xlle_native_title pilot_contract_test)
    target_compile_definitions(${target} PRIVATE PILOT_CALENDAR_LLE)
    target_link_libraries(${target} PRIVATE pilot_calendar_lle)
  endforeach()
  add_custom_command(TARGET pilot_notices POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xlle_native_title>/third_party/unicorn"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PILOT_CALENDAR_UNICORN_NOTICES}/COPYING"
      "$<TARGET_FILE_DIR:xlle_native_title>/third_party/unicorn/COPYING"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PILOT_CALENDAR_UNICORN_NOTICES}/COPYING.LGPL2"
      "$<TARGET_FILE_DIR:xlle_native_title>/third_party/unicorn/COPYING.LGPL2")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/calendar-private/provenance.txt"
    "kernel_sha256=${calendar_sha}\nunicorn_static_library_sha256=${calendar_engine_sha}\nunicorn_irq_boundary=${pilot_calendar_irq_boundary}\nmode=LLE\nordinals=304,305\n")
elseif(NOT PILOT_CALENDAR STREQUAL "HLE")
  message(FATAL_ERROR "PILOT_CALENDAR must be HLE or LLE")
endif()
