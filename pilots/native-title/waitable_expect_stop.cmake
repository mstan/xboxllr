if(NOT DEFINED BINARY OR NOT DEFINED CASE)
  message(FATAL_ERROR "Missing native waitable fixture input")
endif()
execute_process(COMMAND "${BINARY}" "${CASE}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT status STREQUAL "2" OR NOT error MATCHES "native mutant wait context unqualified")
  message(FATAL_ERROR "Expected bounded mutant wait guard, got ${status}: ${output}${error}")
endif()
