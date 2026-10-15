# The selected original audio device requires the observed immutable EP image.
# Generate private build input; no ROM bytes belong in the source repository.
set(PILOT_AUDIO_EP_Y_LOOKUP "" CACHE FILEPATH "Owned verified8192-byte EP high-Y lookup image")
if(NOT EXISTS "${PILOT_AUDIO_EP_Y_LOOKUP}" OR IS_DIRECTORY "${PILOT_AUDIO_EP_Y_LOOKUP}")
  message(FATAL_ERROR "Original audio LLE requires PILOT_AUDIO_EP_Y_LOOKUP with the owned verified EP image")
endif()
file(SIZE "${PILOT_AUDIO_EP_Y_LOOKUP}" audio_ep_y_size)
if(NOT audio_ep_y_size EQUAL 8192)
  message(FATAL_ERROR "Original audio LLE requires an8192-byte EP lookup image")
endif()
set(audio_ep_y_expected "9f2cb3fdbb541a80f4b2b49de094949c88b4edcd4305d3c764ff0de2a852a414")
file(SHA256 "${PILOT_AUDIO_EP_Y_LOOKUP}" audio_ep_y_sha)
if(NOT audio_ep_y_sha STREQUAL audio_ep_y_expected)
  message(FATAL_ERROR "Unreviewed original audio EP lookup image")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PILOT_AUDIO_EP_Y_LOOKUP}")
file(READ "${PILOT_AUDIO_EP_Y_LOOKUP}" audio_ep_y_hex HEX)
# Explicit little-endian DWORD decoding produces host-independent constants.
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1u,\n" audio_ep_y_words "${audio_ep_y_hex}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-device-private")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/audio-device-private/audio_ep_y.aot-private.h"
  "#pragma once\n#include <cstdint>\nstatic constexpr uint32_t kPilotEpYLookup[]={\n${audio_ep_y_words}};\nstatic_assert(sizeof(kPilotEpYLookup)/sizeof(kPilotEpYLookup[0])==2048,\"EP lookup size\");\n")
