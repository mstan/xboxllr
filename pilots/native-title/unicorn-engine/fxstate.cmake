# Authored transformation of the pinned engine, retaining its source/license.
# Intel SDM Vol2A FXRSTOR/FXSAVE legacy image includes FOP/FIP/FCS/FDP/FDS.
# Correct 32-bit transport; 64-bit behavior retains the upstream implementation.
set(fpu_original "${XLLE_UNICORN_SOURCE}/qemu/target/i386/fpu_helper.c")
file(SHA256 "${fpu_original}" fpu_original_sha)
if(NOT fpu_original_sha STREQUAL "809e3d8e0affb71cdf76e52da57d519ab227c223a5443039f2d41a8342979e33")
  message(FATAL_ERROR "FP state correction requires the reviewed upstream helper")
endif()
file(READ "${fpu_original}" fpu_source)
set(save_anchor [=[    cpu_stq_data_ra(env, ptr + XO(legacy.fpip), env->fpip, ra); /* eip+sel; rip */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), 0, ra); /* edp+sel; rdp */]=])
set(save_corrected [=[    if (env->hflags & HF_CS64_MASK) {
        cpu_stq_data_ra(env, ptr + XO(legacy.fpip), env->fpip, ra);
        cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), 0, ra);
    } else {
        cpu_stw_data_ra(env, ptr + XO(legacy.fpop), env->fpop & 0x7ff, ra);
        cpu_stq_data_ra(env, ptr + XO(legacy.fpip),
                       (uint32_t)env->fpip | (uint64_t)env->fpcs << 32, ra);
        cpu_stq_data_ra(env, ptr + XO(legacy.fpdp),
                       (uint32_t)env->fpdp | (uint64_t)env->fpds << 32, ra);
    }]=])
set(restore_anchor [=[    fptag = cpu_lduw_data_ra(env, ptr + XO(legacy.ftw), ra);]=])
set(restore_corrected [=[    fptag = cpu_lduw_data_ra(env, ptr + XO(legacy.ftw), ra);
    if (!(env->hflags & HF_CS64_MASK)) {
        env->fpop = cpu_lduw_data_ra(env, ptr + XO(legacy.fpop), ra) & 0x7ff;
        env->fpip = cpu_ldl_data_ra(env, ptr + XO(legacy.fpip), ra);
        env->fpcs = cpu_lduw_data_ra(env, ptr + XO(legacy.fpip) + 4, ra);
        env->fpdp = cpu_ldl_data_ra(env, ptr + XO(legacy.fpdp), ra);
        env->fpds = cpu_lduw_data_ra(env, ptr + XO(legacy.fpdp) + 4, ra);
    }]=])
foreach(kind save restore)
  string(FIND "${fpu_source}" "${${kind}_anchor}" position)
  if(position LESS 0)
    message(FATAL_ERROR "Reviewed FP state anchor is missing: ${kind}")
  endif()
  string(REPLACE "${${kind}_anchor}" "${${kind}_corrected}" fpu_source "${fpu_source}")
endforeach()
set(fpu_corrected "${CMAKE_CURRENT_BINARY_DIR}/fxstate-private/fpu_helper.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fxstate-private")
file(WRITE "${fpu_corrected}" "${fpu_source}")
get_target_property(x86_sources x86_64-softmmu SOURCES)
list(LENGTH x86_sources original_source_count)
get_target_property(x86_source_directory x86_64-softmmu SOURCE_DIR)
foreach(candidate IN LISTS x86_sources)
  if(candidate MATCHES "(^|/)fpu_helper[.]c$")
    get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${x86_source_directory}")
    if(NOT candidate_absolute STREQUAL fpu_original)
      message(FATAL_ERROR "Compiled FP helper is outside the pinned source: ${candidate_absolute}")
    endif()
    list(REMOVE_ITEM x86_sources "${candidate}")
  endif()
endforeach()
list(LENGTH x86_sources remaining_source_count)
math(EXPR removed_source_count "${original_source_count}-${remaining_source_count}")
if(NOT removed_source_count EQUAL 1)
  message(FATAL_ERROR "Expected one actual compiled FP helper to replace")
endif()
set_property(TARGET x86_64-softmmu PROPERTY SOURCES "${x86_sources}")
target_sources(x86_64-softmmu PRIVATE "${fpu_corrected}")
file(SHA256 "${fpu_corrected}" fpu_corrected_sha)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" fxstate_patch_sha)
