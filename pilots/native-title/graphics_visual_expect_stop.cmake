execute_process(COMMAND "${BINARY}" --visual-depth-mismatch
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT status EQUAL 2 OR NOT error MATCHES "enhanced target and depth use incompatible raster grids")
  message(FATAL_ERROR "Expected the explicit mixed-grid guard; got ${status}\n${output}\n${error}")
endif()
