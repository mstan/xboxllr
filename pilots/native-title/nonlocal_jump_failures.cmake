set(kinds missing returned corrupt bypass unwind exhausted)
set(reasons
  "native jump buffer is not armed on this thread"
  "native jump buffer is not armed on this thread"
  "guest jump buffer no longer matches native frame"
  "native jump requires original CRT completion"
  "guest CRT did not restore the jump contract"
  "native jump slots exhausted")
foreach(index RANGE 0 5)
  list(GET kinds ${index} kind)
  list(GET reasons ${index} reason)
  execute_process(COMMAND "${PROGRAM}" "jump-${kind}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
  string(FIND "${error}" "[PILOT STOP] ${reason}" position)
  if(NOT result STREQUAL "2" OR position EQUAL -1)
    message(FATAL_ERROR "jump-${kind}: expected deliberate stop '${reason}', got ${result}: ${output}${error}")
  endif()
endforeach()
