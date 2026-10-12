add_executable(pilot_audio_compiled_mmio_test audio_compiled_mmio_test.cpp mmio_fault.c)
target_compile_features(pilot_audio_compiled_mmio_test PRIVATE cxx_std_17)
target_compile_definitions(pilot_audio_compiled_mmio_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(pilot_audio_compiled_mmio_test PRIVATE pilot_audio_device_lle)
foreach(case positive unknown callback-failure dma-failure)
  pilot_add_test(NAME pilot_audio_compiled_${case} COMMAND pilot_audio_compiled_mmio_test
    ${case} "${PILOT_IO_DEVICE_SNAPSHOT}")
  set_tests_properties(pilot_audio_compiled_${case} PROPERTIES TIMEOUT 30)
endforeach()
