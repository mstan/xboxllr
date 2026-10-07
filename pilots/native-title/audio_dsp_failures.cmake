foreach(kind debug reset wait mode)
  execute_process(COMMAND "${PROGRAM}" "${kind}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 8)
  string(FIND "${error}" "Unqualified Xbox DSP" match)
  if(result EQUAL 0 OR match EQUAL -1)
    message(FATAL_ERROR "Expected linked DSP guard for ${kind}; got ${result}: ${output}${error}")
  endif()
endforeach()
