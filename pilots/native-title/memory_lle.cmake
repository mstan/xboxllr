set(PILOT_MEMORY "NATIVE" CACHE STRING "General virtual-memory services: NATIVE or LLE")
set_property(CACHE PILOT_MEMORY PROPERTY STRINGS NATIVE LLE)
if(PILOT_MEMORY STREQUAL "LLE")
  if(NOT PILOT_IO_DEVICE STREQUAL "LLE" OR NOT PILOT_USB STREQUAL "LLE")
    message(FATAL_ERROR "Memory LLE currently requires the shared original page/pool/contiguous bridge (PILOT_IO_DEVICE=LLE, PILOT_USB=LLE)")
  endif()
  # Selection is confined to runtime services. Generated title bodies do not
  # depend on it and need not be recompiled when this family is selected.
  set_property(SOURCE runtime.c APPEND PROPERTY COMPILE_DEFINITIONS PILOT_MEMORY_LLE)
  target_compile_definitions(pilot_contract_test PRIVATE PILOT_MEMORY_LLE)
elseif(NOT PILOT_MEMORY STREQUAL "NATIVE")
  message(FATAL_ERROR "PILOT_MEMORY must be NATIVE or LLE")
endif()
