execute_process(COMMAND "${PROGRAM}" calendar-host-fault
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 5)
if(NOT result STREQUAL "2" OR NOT errors MATCHES "unmapped guest access or native lowering fault")
  message(FATAL_ERROR "Real unmapped access did not reach the fault guard: ${result}\n${output}${errors}")
endif()
