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
  if(NOT calendar_engine_sha STREQUAL "60a95ffb6572942b7db1f57901d23e664ed22a54a7d92dfcad15fc3e3ef1f724")
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
    "kernel_sha256=${calendar_sha}\nunicorn_static_library_sha256=${calendar_engine_sha}\nmode=LLE\nordinals=304,305\n")
elseif(NOT PILOT_CALENDAR STREQUAL "HLE")
  message(FATAL_ERROR "PILOT_CALENDAR must be HLE or LLE")
endif()
