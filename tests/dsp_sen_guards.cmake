# Exercise both JIT entry paths without original title assets.
set(modes sp sc sz ep ssh-write ssl-write ssh-read ssl-read underflow enddo spill disable-occupied sz-overflow enable-unset-sz status-history)
set(messages "direct SP" "task/configuration" "task/configuration" "task/configuration"
  "explicit" "explicit" "explicit" "explicit" "underflow/refill"
  "explicit/early-abort" "spill boundary" "occupied mode transition" "SZ overflow" "unset SZ" "status/history")
foreach(execution single block)
  foreach(index RANGE 0 14)
    list(GET modes ${index} mode)
    list(GET messages ${index} expected)
    execute_process(COMMAND "${PROGRAM}" "${mode}" "${execution}"
      RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 10)
    if(status STREQUAL "0" OR NOT err MATCHES "Unqualified SEN" OR NOT err MATCHES "${expected}")
      message(FATAL_ERROR "${mode}/${execution}: missing SEN guard: ${status} ${out}${err}")
    endif()
  endforeach()
  execute_process(COMMAND "${PROGRAM}" empty-disable "${execution}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 10)
  if(NOT status STREQUAL "0" OR NOT out MATCHES "Empty SEN enable/disable preserved")
    message(FATAL_ERROR "${execution}: empty transition failed: ${status} ${out}${err}")
  endif()
endforeach()
