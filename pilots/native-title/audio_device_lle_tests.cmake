add_executable(pilot_audio_device_lle_test audio_device_lle_test.cpp audio_device_pcm_capture.cpp)
target_compile_definitions(pilot_audio_device_lle_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX PILOT_PCM_CAPTURE_TEST)
target_link_libraries(pilot_audio_device_lle_test PRIVATE pilot_audio_device_lle)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-capture-private")
foreach(case positive startup startup-width startup-active counter counter-unknown configuration reset-bootstrap reset-clock unknown unknown-read gap absent reject paused rollback invalid engine-fault capture-full capture-stamps capture-failed capture-closed capture-engine capture-rollback)
  pilot_add_test(NAME pilot_audio_device_${case} COMMAND pilot_audio_device_lle_test
    "${case}" "${PILOT_IO_DEVICE_SNAPSHOT}" "${CMAKE_CURRENT_BINARY_DIR}/audio-capture-private")
  set_tests_properties(pilot_audio_device_${case} PROPERTIES TIMEOUT 20)
endforeach()
