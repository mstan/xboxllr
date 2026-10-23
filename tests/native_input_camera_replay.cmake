foreach(horizontal 0 1)
  foreach(vertical 0 1)
    set(record "${WORK}/camera-${horizontal}-${vertical}.bin")
    file(REMOVE "${record}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
      "XLLE_PILOT_DIAGNOSTIC_REPLAY=" "XLLE_PILOT_DIAGNOSTIC_RECORD=${record}"
      "${PROGRAM}" ${horizontal} ${vertical}
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Camera capture failed: ${result}\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
      "XLLE_PILOT_DIAGNOSTIC_RECORD=" "XLLE_PILOT_DIAGNOSTIC_REPLAY=${record}"
      "${PROGRAM}" ${horizontal} ${vertical}
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Camera replay failed: ${result}\n${output}\n${error}")
    endif()
  endforeach()
endforeach()
message(STATUS "Camera inversion capture/replay passed in all four configurations; no double inversion")
