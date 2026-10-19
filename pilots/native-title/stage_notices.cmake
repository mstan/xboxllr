if(NOT DEFINED PILOT_NOTICE_MANIFEST)
  message(FATAL_ERROR "A generated native-title notice manifest is required")
endif()
include("${PILOT_NOTICE_MANIFEST}")
list(LENGTH notice_sources count)
list(LENGTH notice_outputs output_count)
if(NOT count EQUAL output_count OR count EQUAL 0)
  message(FATAL_ERROR "Invalid native-title notice manifest")
endif()
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  list(GET notice_sources ${index} source)
  list(GET notice_outputs ${index} output)
  # COPYONLY preserves content byte-for-byte, only writing changed content.
  configure_file("${source}" "${output}" COPYONLY)
  # Also settle a source whose timestamp changed without a content change.
  # This command itself runs only when a dependency is newer or absent.
  file(TOUCH "${output}")
endforeach()
