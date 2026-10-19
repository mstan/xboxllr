# COPYONLY preserves the destination when contents are unchanged. Unlike an
# unconditional directory copy, relinking the executable need not rewrite the
# complete prepared DSP dependency notice tree. Works with CMake 3.20 too.
if(NOT IS_DIRECTORY "${SOURCE}" OR "${DESTINATION}" STREQUAL "")
  message(FATAL_ERROR "Notice copy needs a prepared source and destination")
endif()
file(GLOB_RECURSE _xlle_notices LIST_DIRECTORIES false "${SOURCE}/*")
foreach(_xlle_notice IN LISTS _xlle_notices)
  file(RELATIVE_PATH _xlle_relative "${SOURCE}" "${_xlle_notice}")
  get_filename_component(_xlle_parent "${DESTINATION}/${_xlle_relative}" DIRECTORY)
  file(MAKE_DIRECTORY "${_xlle_parent}")
  configure_file("${_xlle_notice}" "${DESTINATION}/${_xlle_relative}" COPYONLY)
endforeach()
