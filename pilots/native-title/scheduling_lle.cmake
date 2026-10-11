set(PILOT_SCHEDULING "NATIVE" CACHE STRING "Scheduling family: NATIVE or experimental original-kernel LLE")
set_property(CACHE PILOT_SCHEDULING PROPERTY STRINGS NATIVE LLE)
if(PILOT_SCHEDULING STREQUAL "LLE")
  if(NOT PILOT_FILES STREQUAL "LLE" OR NOT PILOT_MEMORY STREQUAL "LLE" OR NOT PILOT_USB STREQUAL "LLE")
    message(FATAL_ERROR "Scheduling LLE requires the original file, memory and USB/PIC namespace")
  endif()
  # Select once for the title. The mixed component-test executable continues
  # to exercise both implementations. Original thread bodies execute on the
  # sole original CPU; the normal title keeps its fast generated native bodies.
  set_property(SOURCE runtime.c APPEND PROPERTY COMPILE_DEFINITIONS PILOT_SCHEDULING_LLE)
elseif(NOT PILOT_SCHEDULING STREQUAL "NATIVE")
  message(FATAL_ERROR "PILOT_SCHEDULING must be NATIVE or LLE")
endif()
