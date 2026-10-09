# Maintained source-rate stage. The BSD-2-Clause donor implements interpolation;
# the Xbox adapter owns actual DMA, cursor, lifecycle and finite-stream behavior.
include("${CMAKE_CURRENT_LIST_DIR}/ApuAdpcm.cmake")
if(NOT TARGET xlle_apu_rate)
  include(FetchContent)
  function(xlle_prepare_samplerate)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(LIBSAMPLERATE_EXAMPLES OFF)
    set(LIBSAMPLERATE_INSTALL OFF)
    FetchContent_Declare(xlle_samplerate
      GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
      GIT_TAG c96f5e3de9c4488f4e6c97f59f5245f22fda22f7)
    FetchContent_MakeAvailable(xlle_samplerate)
    set_target_properties(samplerate PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endfunction()
  xlle_prepare_samplerate()
  add_library(xlle_apu_rate STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/machine/apu_rate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/machine/apu_envelope.cpp")
  set_target_properties(xlle_apu_rate PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(xlle_apu_rate PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
  target_link_libraries(xlle_apu_rate PUBLIC xlle_apu_adpcm PRIVATE SampleRate::samplerate)
  add_custom_target(xlle_apu_rate_notices
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xlle_apu_rate>/third_party/libsamplerate"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_LIST_DIR}/../LICENSES/libsamplerate-BSD.txt"
      "${CMAKE_CURRENT_LIST_DIR}/../src/third_party/libsamplerate/README.md"
      "$<TARGET_FILE_DIR:xlle_apu_rate>/third_party/libsamplerate")
  add_dependencies(xlle_apu_rate xlle_apu_rate_notices)
endif()
