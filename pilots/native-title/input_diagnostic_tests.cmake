# Small host-only fixture. Python drives capture/replay/malformed/overflow
# modes in separate processes; no game or generated original code is used.
add_executable(pilot_input_diagnostic_test "${CMAKE_CURRENT_LIST_DIR}/input_diagnostic_test.c")
target_link_libraries(pilot_input_diagnostic_test PRIVATE pilot_session_replay)
target_compile_definitions(pilot_input_diagnostic_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(pilot_input_diagnostic_test PRIVATE /W4 /WX)
endif()
