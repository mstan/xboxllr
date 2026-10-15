execute_process(COMMAND "${PROGRAM}" --warm-ep-read RESULT_VARIABLE status
  OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
if(NOT status EQUAL 2 OR NOT err MATCHES "expected warm EP counter boundary")
  message(FATAL_ERROR "Warm EP counter guard missing: ${status} ${out}${err}")
endif()
