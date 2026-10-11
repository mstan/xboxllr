# Maintained headless PCM descriptor consumer. No host playback backend or
# original DSP bootstrap/frame admission is selected by this focused check.
if(NOT TARGET test_aci_output)
  add_executable(test_aci_output "${CMAKE_CURRENT_LIST_DIR}/../tests/test_aci_output.cpp")
  target_include_directories(test_aci_output PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src")
  add_test(NAME aci_output COMMAND test_aci_output)
endif()
