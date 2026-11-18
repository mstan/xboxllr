# Guest i386 interpreter (vendored box64 core, MIT). Shared by the REFERENCE
# build (tests/oracle) and native-title builds (call-boundary fallback).
# Portable C: no host assembly, no dynarec, no host 80-bit long double, so the
# same guest code produces the same results on every supported host compiler.
if(NOT TARGET xlle_x86_interp)
  set(_xlr_root "${CMAKE_CURRENT_LIST_DIR}/../src/third_party/box64_interp")
  set(_xlr_up "${_xlr_root}/upstream")
  add_library(xlle_x86_interp STATIC
    "${_xlr_root}/xlr_glue.c"
    "${_xlr_up}/x64emu_flags.c"
    "${_xlr_up}/x64run.c"
    "${_xlr_up}/x64run0f.c"
    "${_xlr_up}/x64run66.c"
    "${_xlr_up}/x64run660f.c"
    "${_xlr_up}/x64run66d9.c"
    "${_xlr_up}/x64run66dd.c"
    "${_xlr_up}/x64run66f0.c"
    "${_xlr_up}/x64run66f20f.c"
    "${_xlr_up}/x64run66f30f.c"
    "${_xlr_up}/x64rund8.c"
    "${_xlr_up}/x64rund9.c"
    "${_xlr_up}/x64runda.c"
    "${_xlr_up}/x64rundb.c"
    "${_xlr_up}/x64rundc.c"
    "${_xlr_up}/x64rundd.c"
    "${_xlr_up}/x64runde.c"
    "${_xlr_up}/x64rundf.c"
    "${_xlr_up}/x64runf0.c"
    "${_xlr_up}/x64runf20f.c"
    "${_xlr_up}/x64runf30f.c"
    "${_xlr_up}/x64run_private.c"
    "${_xlr_up}/x64primop.c"
    "${_xlr_up}/x87emu_private.c"
    "${_xlr_up}/x64compstrings.c"
    "${_xlr_up}/x64shaext.c")
  target_include_directories(xlle_x86_interp
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include"
    PRIVATE "${_xlr_root}/shim" "${_xlr_up}" "${_xlr_root}")
  target_compile_definitions(xlle_x86_interp PRIVATE XLR_NATIVE_LOCK)
  set_target_properties(xlle_x86_interp PROPERTIES
    C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Vendored third-party code: keep its own warning profile out of ours.
    # Type-punning through register unions is pervasive in the core.
    target_compile_options(xlle_x86_interp PRIVATE -w -fno-strict-aliasing)
  elseif(MSVC)
    # unistd.h / sys/syscall.h placeholders; the core needs nothing from them.
    target_include_directories(xlle_x86_interp PRIVATE "${_xlr_root}/shim/msvc")
    target_compile_options(xlle_x86_interp PRIVATE /W0)
  endif()
endif()

# Call-boundary bridge between generated code (recomp_types.h register ABI)
# and the interpreter. Linked by native-title runtimes.
if(NOT TARGET xlle_recomp_interp_bridge)
  add_library(xlle_recomp_interp_bridge STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/cpu/recomp_interp_bridge.c")
  target_include_directories(xlle_recomp_interp_bridge PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../src/cpu"
    "${CMAKE_CURRENT_LIST_DIR}/../src/third_party/xboxrecomp_cpu")
  target_link_libraries(xlle_recomp_interp_bridge PUBLIC xlle_x86_interp)
  set_target_properties(xlle_recomp_interp_bridge PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endif()

function(xlle_add_recomp_interp_bridge_test)
  if(NOT TARGET test_recomp_interp_bridge)
    add_executable(test_recomp_interp_bridge "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/test_recomp_interp_bridge.c")
    target_link_libraries(test_recomp_interp_bridge PRIVATE xlle_recomp_interp_bridge)
    add_test(NAME recomp_interp_bridge COMMAND test_recomp_interp_bridge)
  endif()
endfunction()

# Unicorn-free qualification for builds that do not configure the reference CPU
# (native titles, every host compiler): the interpreter must reproduce the
# Unicorn-qualified build's per-template output digests bit-exactly.
function(xlle_add_x86_interp_replay_test)
  if(NOT TARGET test_x86_interp_replay)
    add_executable(test_x86_interp_replay "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/test_x86_interp.cpp")
    target_compile_definitions(test_x86_interp_replay PRIVATE XLR_X86_INTERP_REPLAY_ONLY)
    target_link_libraries(test_x86_interp_replay PRIVATE xlle_x86_interp)
    set_target_properties(test_x86_interp_replay PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME x86_interp_replay COMMAND test_x86_interp_replay 200 20261117
      "--check-digests=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/fixtures/x86_interp_digests.txt")
  endif()
endfunction()
