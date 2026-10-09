# Shared maintained APU decoder, also linked by private comparison tests.
if(NOT TARGET xlle_apu_adpcm)
  add_library(xlle_apu_adpcm STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/machine/apu_adpcm.c")
  set_target_properties(xlle_apu_adpcm PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(xlle_apu_adpcm PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
endif()
