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
# Legacy protected-mode environment transport and documented control effects.
set(init_anchor "    env->fpus = 0;\n    env->fpstt = 0;\n    env->fpcs = 0;")
set(init_corrected "    env->fpus = 0;\n    env->fpstt = 0;\n    env->fpop = 0;\n    env->fpcs = 0;")
set(env_save_anchor "        cpu_stl_data_ra(env, ptr + 16, env->fpcs, retaddr); /* fpcs */")
set(env_save_corrected [=[        cpu_stl_data_ra(env, ptr + 16,
                        (env->hflags & HF_PE_MASK) && !(env->eflags & VM_MASK)
                        ? (uint16_t)env->fpcs | ((uint32_t)(env->fpop & 0x7ff) << 16)
                        : env->fpcs, retaddr); /* fpcs and protected-mode fop */]=])
set(env_load_anchor "        fptag = cpu_lduw_data_ra(env, ptr + 8, retaddr);")
set(env_load_corrected [=[        fptag = cpu_lduw_data_ra(env, ptr + 8, retaddr);
        if ((env->hflags & HF_PE_MASK) && !(env->eflags & VM_MASK)) {
            env->fpip = cpu_ldl_data_ra(env, ptr + 12, retaddr);
            env->fpcs = cpu_lduw_data_ra(env, ptr + 16, retaddr);
            env->fpop = cpu_lduw_data_ra(env, ptr + 18, retaddr) & 0x7ff;
            env->fpdp = cpu_ldl_data_ra(env, ptr + 20, retaddr);
            env->fpds = cpu_lduw_data_ra(env, ptr + 24, retaddr);
        }]=])
set(env_mask_anchor "    do_fstenv(env, ptr, data32, GETPC());\n}")
set(env_mask_corrected "    do_fstenv(env, ptr, data32, GETPC());\n    cpu_set_fpuc(env, env->fpuc | 0x3f);\n}")
foreach(kind init env_save env_load env_mask)
  string(FIND "${fpu_source}" "${${kind}_anchor}" position)
  if(position LESS 0)
    message(FATAL_ERROR "Reviewed environment anchor is missing: ${kind}")
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

# P6 FOP is the last non-control x87 opcode, independent of masked exceptions.
# Keep newer CPU models' opcode-generation policy outside this correction.
set(translator_original "${XLLE_UNICORN_SOURCE}/qemu/target/i386/translate.c")
file(SHA256 "${translator_original}" translator_original_sha)
if(NOT translator_original_sha STREQUAL "2d03db9a2b9632bcafb9d7f29b5949c6eccee629c200305576ad7cf0f68fa678")
  message(FATAL_ERROR "P6 opcode correction requires the reviewed translator")
endif()
file(READ "${translator_original}" translator_source)
set(opcode_anchor "            if (update_fip) {\n                tcg_gen_ld_i32")
set(opcode_corrected [=[            if (update_fip) {
                if (!s->code64 && s->uc->cpu_model == UC_CPU_X86_PENTIUM3) {
                    tcg_gen_st16_i32(tcg_ctx,
                        tcg_const_i32(tcg_ctx, ((b & 7) << 8) | modrm),
                        tcg_ctx->cpu_env, offsetof(CPUX86State, fpop));
                }
                tcg_gen_ld_i32]=])
string(FIND "${translator_source}" "${opcode_anchor}" position)
if(position LESS 0)
  message(FATAL_ERROR "Reviewed P6 opcode anchor is missing")
endif()
string(REPLACE "${opcode_anchor}" "${opcode_corrected}" translator_source "${translator_source}")
set(translator_corrected "${CMAKE_CURRENT_BINARY_DIR}/fxstate-private/translate.c")
file(WRITE "${translator_corrected}" "${translator_source}")
get_target_property(x86_sources x86_64-softmmu SOURCES)
list(LENGTH x86_sources original_source_count)
foreach(candidate IN LISTS x86_sources)
  if(candidate MATCHES "(^|/)translate[.]c$")
    get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${x86_source_directory}")
    if(NOT candidate_absolute STREQUAL translator_original)
      message(FATAL_ERROR "Compiled translator is outside the pinned source")
    endif()
    list(REMOVE_ITEM x86_sources "${candidate}")
  endif()
endforeach()
list(LENGTH x86_sources remaining_source_count)
math(EXPR removed_source_count "${original_source_count}-${remaining_source_count}")
if(NOT removed_source_count EQUAL 1)
  message(FATAL_ERROR "Expected one actual compiled translator to replace")
endif()
set_property(TARGET x86_64-softmmu PROPERTY SOURCES "${x86_sources}")
target_sources(x86_64-softmmu PRIVATE "${translator_corrected}")
file(SHA256 "${translator_corrected}" translator_corrected_sha)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" fxstate_patch_sha)
