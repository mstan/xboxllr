add_executable(pilot_audio_device_lle_test audio_device_lle_test.cpp)
target_compile_definitions(pilot_audio_device_lle_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(pilot_audio_device_lle_test PRIVATE pilot_audio_device_lle)
foreach(case positive unknown gap absent reject paused rollback invalid engine-fault)
  pilot_add_test(NAME pilot_audio_device_${case} COMMAND pilot_audio_device_lle_test
    "${case}" "${PILOT_IO_DEVICE_SNAPSHOT}")
  set_tests_properties(pilot_audio_device_${case} PROPERTIES TIMEOUT 20)
endforeach()
