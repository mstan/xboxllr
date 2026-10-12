if(NOT EXISTS "${PROGRAM}" OR NOT OUTPUT)
  message(FATAL_ERROR "G03 guard fixture requires its owned executable/output")
endif()
# Preserve every captured branch. A new directory avoids overwrite/cleanup.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(capture "${OUTPUT}/guard-${suffix}")
file(MAKE_DIRECTORY "${capture}")
execute_process(COMMAND "${PROGRAM}" gpu-first-context-capture-guard "${capture}"
  RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error TIMEOUT 25)
file(WRITE "${capture}/fixture.log" "${log}${error}")
if(NOT result EQUAL 0 OR NOT "${log}${error}" MATCHES "PASS: actual raw-MMIO guard preserved")
  message(FATAL_ERROR "G03 authored capture guard failed (${result}); preserved at ${capture}\n${log}${error}")
endif()
message(STATUS "PASS: authored passive capture/real guard; preserved at ${capture}")
