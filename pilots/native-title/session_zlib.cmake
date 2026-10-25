# Pinned, unmodified portable compressor. An offline checkout can supply
# FETCHCONTENT_SOURCE_DIR_XBOXLLR_ZLIB; no host-installed DLL is required.
include(FetchContent)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(xboxllr_zlib
  URL https://zlib.net/fossils/zlib-1.3.2.tar.gz
  URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(xboxllr_zlib)
if(UNIX)
  target_compile_definitions(zlibstatic PUBLIC _FILE_OFFSET_BITS=64)
endif()
