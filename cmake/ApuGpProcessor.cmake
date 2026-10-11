# Optional maintained GP owner over the already prepared, guarded MIT DSP core.
# Including this file does not admit a DSP backend or alter the fast HLE path.
include("${CMAKE_CURRENT_LIST_DIR}/ApuRate.cmake")
if(NOT TARGET xlle_dsp_core)
  message(FATAL_ERROR "GP processor requires the existing guarded xlle_dsp_core target")
endif()
if(NOT TARGET xlle_apu_gp_processor)
  add_library(xlle_apu_gp_processor STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/machine/apu_gp_processor.cpp")
  target_include_directories(xlle_apu_gp_processor PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
  target_link_libraries(xlle_apu_gp_processor PUBLIC xlle_apu_rate xlle_dsp_core)
endif()
