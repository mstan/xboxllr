# Dependency-tracked notices, shared by every native title. No target-file
# expressions in OUTPUT: CMake requires configuration-only expressions there.
function(pilot_notices_begin output_directory)
  set_property(GLOBAL PROPERTY pilot_notice_directory "${output_directory}")
  set_property(GLOBAL PROPERTY pilot_notice_outputs "")
endfunction()

function(pilot_notice_group sources relative_destinations)
  if(NOT sources)
    return()
  endif()
  get_property(directory GLOBAL PROPERTY pilot_notice_directory)
  string(SHA256 key "${sources};${relative_destinations}")
  set(manifest "${CMAKE_CURRENT_BINARY_DIR}/notice-rules/${key}-$<CONFIG>.cmake")
  set(content "set(notice_sources [==[${sources}]==])\n")
  set(outputs "")
  foreach(relative IN LISTS relative_destinations)
    list(APPEND outputs "${directory}/${relative}")
  endforeach()
  string(APPEND content "set(notice_outputs [==[${outputs}]==])\n")
  file(GENERATE OUTPUT "${manifest}" CONTENT "${content}")
  # MSBuild needs a real primary input to retain custom-rule dependency tracking.
  # A copied reference-source tree also contains compiled inputs: never attach
  # a notice rule to one of those and displace its ordinary compilation rule.
  set(main_source "")
  foreach(source IN LISTS sources)
    string(TOLOWER "${source}" lower_source)
    if(NOT lower_source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm|rc|rs|asm|s|def|o|obj|a|lib)$")
      set(main_source "${source}")
      break()
    endif()
  endforeach()
  if(NOT main_source)
    message(FATAL_ERROR "A native notice group needs a non-code notice file as its primary dependency")
  endif()
  list(REMOVE_ITEM sources "${main_source}")
  add_custom_command(OUTPUT ${outputs}
    COMMAND "${CMAKE_COMMAND}" "-DPILOT_NOTICE_MANIFEST=${manifest}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_notices.cmake"
    MAIN_DEPENDENCY "${main_source}"
    DEPENDS ${sources} "${manifest}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_notices.cmake"
    COMMENT "Stage changed native-title notices"
    VERBATIM)
  set_property(GLOBAL APPEND PROPERTY pilot_notice_outputs ${outputs})
endfunction()

function(pilot_notice_file source relative_destination)
  pilot_notice_group("${source}" "${relative_destination}")
endfunction()

function(pilot_notice_tree source relative_directory)
  # Additions/removals cause regeneration; the updated rule depends on every
  # source and destination, so deleting a staged notice also restores it.
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${source}/*")
  set(destinations "")
  foreach(path IN LISTS sources)
    file(RELATIVE_PATH relative "${source}" "${path}")
    list(APPEND destinations "${relative_directory}/${relative}")
  endforeach()
  pilot_notice_group("${sources}" "${destinations}")
endfunction()

function(pilot_notices_finish)
  get_property(outputs GLOBAL PROPERTY pilot_notice_outputs)
  add_custom_target(pilot_notices ALL DEPENDS ${outputs})
endfunction()
