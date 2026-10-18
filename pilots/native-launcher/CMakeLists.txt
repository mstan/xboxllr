cmake_minimum_required(VERSION 3.20)
project(xlle_native_launcher LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(XLLE_UI_SOURCE "" CACHE PATH "Prepared, pinned Xbox recomp-ui")
if(NOT EXISTS "${XLLE_UI_SOURCE}/xboxlle-ui.json")
  message(FATAL_ERROR "Prepare the reviewed UI with tools/native_launcher_ui.py first")
endif()
file(READ "${XLLE_UI_SOURCE}/xboxlle-ui.json" ui_manifest)
string(JSON ui_revision GET "${ui_manifest}" revision)
if(NOT ui_revision STREQUAL "ccc186e097b362555292b157ffd303abcea26fbf")
  message(FATAL_ERROR "Unreviewed recomp-ui revision")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/native_launcher_ui.py"
  --verify --output "${XLLE_UI_SOURCE}" COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
find_package(SDL2 CONFIG REQUIRED)
set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL "Native title mod provider" FORCE)
set(RECOMP_UI_SDL3 OFF CACHE BOOL "Pinned SDL2 UI" FORCE)
include("${XLLE_UI_SOURCE}/recomp_ui.cmake")
add_executable(xlle_launcher launcher.cpp ../native-title/visual_mods.c)
target_include_directories(xlle_launcher PRIVATE ../native-title)
target_link_libraries(xlle_launcher PRIVATE SDL2::SDL2)
set(XLLE_LAUNCHER_BOXART "" CACHE FILEPATH "Title-supplied launcher artwork")
set(XLLE_LAUNCHER_ARTWORK_NOTICE "" CACHE FILEPATH "Title artwork attribution")
if(XLLE_LAUNCHER_BOXART)
  if(NOT EXISTS "${XLLE_LAUNCHER_BOXART}")
    message(FATAL_ERROR "Missing title artwork")
  endif()
  recomp_target_launcher_ui(xlle_launcher CONSOLE xbox BOXART "${XLLE_LAUNCHER_BOXART}" BOXART_NAME boxart.png)
  target_compile_definitions(xlle_launcher PRIVATE XLLE_LAUNCHER_HAS_BOXART=1)
else()
  recomp_target_launcher_ui(xlle_launcher CONSOLE xbox)
endif()
if(XLLE_LAUNCHER_ARTWORK_NOTICE)
  add_custom_command(TARGET xlle_launcher POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${XLLE_LAUNCHER_ARTWORK_NOTICE}" "$<TARGET_FILE_DIR:xlle_launcher>/ARTWORK-NOTICE.md")
endif()
add_custom_command(TARGET xlle_launcher POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xlle_launcher>/assets/img"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XLLE_UI_SOURCE}/assets/consoles/xbox/img/pad_xbox.jpg"
    "$<TARGET_FILE_DIR:xlle_launcher>/assets/img/pad_xbox.jpg"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.md"
    "$<TARGET_FILE_DIR:xlle_launcher>/XBOX-CONTROLLER-ARTWORK.md")
set(ui_notices LICENSE src/third_party/imgui/LICENSE.txt
  src/third_party/stb_image.h src/third_party/stb_image_write.h src/third_party/tinyfiledialogs.c
  assets/common/fonts/NOTICE.md assets/common/img/NOTICE.md xboxlle-ui.json)
file(GLOB_RECURSE ui_asset_notices RELATIVE "${XLLE_UI_SOURCE}" "${XLLE_UI_SOURCE}/assets/*LICENSE*" "${XLLE_UI_SOURCE}/assets/*OFL*")
list(APPEND ui_notices ${ui_asset_notices})
foreach(notice IN LISTS ui_notices)
  if(EXISTS "${XLLE_UI_SOURCE}/${notice}")
    get_filename_component(notice_directory "${notice}" DIRECTORY)
    add_custom_command(TARGET xlle_launcher POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xlle_launcher>/third_party/recomp-ui/${notice_directory}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XLLE_UI_SOURCE}/${notice}" "$<TARGET_FILE_DIR:xlle_launcher>/third_party/recomp-ui/${notice}")
  endif()
endforeach()
if(WIN32)
  add_custom_command(TARGET xlle_launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:xlle_launcher>")
  if(EXISTS "${SDL2_DIR}/../LICENSE.txt")
    add_custom_command(TARGET xlle_launcher POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:xlle_launcher>/third_party/SDL2"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SDL2_DIR}/../LICENSE.txt" "$<TARGET_FILE_DIR:xlle_launcher>/third_party/SDL2/LICENSE.txt")
  endif()
endif()
