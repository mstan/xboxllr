execute_process(COMMAND "${PROGRAM}" "${CASE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT result STREQUAL "2" OR NOT errors MATCHES "${REASON}")
  message(FATAL_ERROR "${CASE} did not stop explicitly: ${result}\n${output}${errors}")
endif()
