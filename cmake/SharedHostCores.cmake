include_guard(GLOBAL)
# Select only the pinned C profiles used by the native host. Xbox keeps its
# native state, journal, controller and image formats under their own owners.
set(xboxllr_shared_root "${CMAKE_CURRENT_LIST_DIR}/../external/shared-cores")
foreach(core recomp-diagnostics-core recomp-persistence-core)
  if(NOT EXISTS "${xboxllr_shared_root}/${core}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize the pinned shared host core: ${core}")
  endif()
endforeach()
add_library(xboxllr_shared_diagnostics STATIC
  "${xboxllr_shared_root}/recomp-diagnostics-core/src/diagnostics_server.c")
target_include_directories(xboxllr_shared_diagnostics PUBLIC
  "${xboxllr_shared_root}/recomp-diagnostics-core/include")
if(WIN32)
  target_link_libraries(xboxllr_shared_diagnostics PUBLIC ws2_32)
endif()
add_library(xboxllr_shared_atomic STATIC
  "${xboxllr_shared_root}/recomp-persistence-core/src/atomic_file.c")
target_include_directories(xboxllr_shared_atomic PUBLIC
  "${xboxllr_shared_root}/recomp-persistence-core/include")
