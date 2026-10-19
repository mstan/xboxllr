# Select an already prepared, guarded MIT DSP core. No firmware is fetched or
# embedded here. The source manifest and matching host ABI library are inputs.
include("${CMAKE_CURRENT_LIST_DIR}/ValidateDspSource.cmake")
function(xlle_add_dsp_core source library)
  if(TARGET xlle_dsp_core)
    return()
  endif()
  if(NOT EXISTS "${source}/donor.json" OR
     NOT EXISTS "${source}/crates/emu-ffi/include/dsp56300.h" OR
     NOT EXISTS "${source}/third_party/inventory.json" OR
     NOT EXISTS "${library}")
    message(FATAL_ERROR "DSP execution requires the prepared donor and host ABI library")
  endif()
  xlle_validate_dsp_source("${source}")
  add_library(xlle_dsp_core STATIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/machine/apu_dsp_core.c")
  target_include_directories(xlle_dsp_core PRIVATE "${source}/crates/emu-ffi/include")
  target_link_libraries(xlle_dsp_core PUBLIC "${library}")
  if(WIN32)
    target_link_libraries(xlle_dsp_core PUBLIC ntdll userenv ws2_32 bcrypt)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(xlle_dsp_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  endif()
endfunction()
