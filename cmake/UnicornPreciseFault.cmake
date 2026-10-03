# Restore architectural EIP/condition-code metadata on Unicorn's unhandled
# load/fetch errors. The upstream early-exit branches currently skip QEMU's
# restore helper and report TB-entry state. Tests exercise a preceding register
# effect followed by a failed load through both native and all-Unicorn paths.
#
# Derive a build-only copy of the pinned source. Retain its LGPL-2.1-or-later
# header verbatim; no third-party source is relicensed or edited in FetchContent.
set(_xlle_tlb_source "${unicorn_SOURCE_DIR}/qemu/accel/tcg/cputlb.c")
file(READ "${_xlle_tlb_source}" _xlle_tlb_text)
string(REPLACE "\r\n" "\n" _xlle_tlb_text "${_xlle_tlb_text}")
string(SHA256 _xlle_tlb_sha "${_xlle_tlb_text}")
if(NOT _xlle_tlb_sha STREQUAL "d5d5d8aff986d2c6f7f6a2d93bcb4ada75e2f66d62445590ed6534daf43af2df")
  message(FATAL_ERROR "Unicorn TLB source changed: requalify fault restoration and resize policy")
endif()
string(REPLACE "cpu_loop_exit(uc->cpu);"
               "cpu_loop_exit_restore(uc->cpu, retaddr);"
               _xlle_tlb_text "${_xlle_tlb_text}")
# Stores previously requested an exit but returned to translated instructions.
# Keep cpu_exit's outer-loop latch; restoration alone would retry the fault.
string(REGEX REPLACE "cpu_exit\\(uc->cpu\\);([ \t\r\n]+)return;"
               "cpu_exit(uc->cpu);\\1cpu_loop_exit_restore(uc->cpu, retaddr);\\1return;"
               _xlle_tlb_text "${_xlle_tlb_text}")
set(_xlle_tlb_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/cputlb.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn")
# Keep the fault-corrected legacy policy available to the controlled test. It
# compiles this copy in its own object, never into the product alongside cputlb.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/cputlb-host-timed.c" "${_xlle_tlb_text}")
if(NOT XLLE_HOST_TIMED_TLB)
  # The upstream 100 ms host window changes software cache residency (and thus
  # AOT eligibility) under host load. Retain adaptive growth/shrink thresholds,
  # but age each MMU mode's window by resize opportunities (including selected
  # modes and large-page invalidations that flush an entire mode). No guest
  # access, permission check, invalidation, allocation limit or hot path changes.
  # Reuse the private window field as a bounded counter; no new engine ABI.
  string(REPLACE "    desc->window_begin_ns = ns;"
    "    (void)ns;\n    desc->window_begin_ns = 0; /* xlle: per-mode resize opportunity counter */"
    _xlle_tlb_text "${_xlle_tlb_text}")
  set(_xlle_old_window "    int64_t window_len_ms = 100;\n    int64_t window_len_ns = window_len_ms * 1000 * 1000;\n    bool window_expired = now > desc->window_begin_ns + window_len_ns;")
  set(_xlle_new_window "    (void)now;\n    bool window_expired = ++desc->window_begin_ns >= XLLE_TLB_RESIZE_FLUSH_WINDOW;")
  string(FIND "${_xlle_tlb_text}" "${_xlle_old_window}" _xlle_window_position)
  if(_xlle_window_position LESS 0)
    message(FATAL_ERROR "Unicorn resize window changed: requalify deterministic TLB policy")
  endif()
  string(REPLACE "${_xlle_old_window}" "${_xlle_new_window}" _xlle_tlb_text "${_xlle_tlb_text}")
  string(REPLACE "get_clock_realtime()" "0 /* xlle: no host clock in TLB policy */"
    _xlle_tlb_text "${_xlle_tlb_text}")
  set(_xlle_tlb_text "#define XLLE_TLB_RESIZE_FLUSH_WINDOW 100\n${_xlle_tlb_text}")
endif()
file(WRITE "${_xlle_tlb_derived}" "${_xlle_tlb_text}")

# QEMU already decodes the exact instruction index while restoring a fault.
# Publish that cold-path result so our block clock can remove the unexecuted
# suffix. This introduces no per-instruction or per-block callback.
file(READ "${unicorn_SOURCE_DIR}/qemu/accel/tcg/translate-all.c" _xlle_restore_text)
string(REPLACE "\r\n" "\n" _xlle_restore_text "${_xlle_restore_text}")
string(SHA256 _xlle_restore_sha "${_xlle_restore_text}")
if(NOT _xlle_restore_sha STREQUAL "aad6d750b4494b8221aaafcc40bc762835cd4895e56d65f1d637fd61417ab2d4")
  message(FATAL_ERROR "Unicorn restore source changed: requalify fault accounting")
endif()
string(REPLACE "    restore_state_to_opc(env, tb, data);"
  "    xlle_unicorn_record_restore(cpu->uc, tb->pc, tb->size, tb->icount, i, reset_icount);\n    restore_state_to_opc(env, tb, data);"
  _xlle_restore_text "${_xlle_restore_text}")
set(_xlle_restore_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/translate-all.c")
string(REPLACE "        tlb_reset_dirty_by_vaddr(cpu, pc & TARGET_PAGE_MASK,\n                                (pc & ~TARGET_PAGE_MASK) + tb->size);"
  "        xlle_unicorn_protect_code_aliases(cpu->uc, phys_pc, phys_page2);"
  _xlle_restore_text "${_xlle_restore_text}")
file(WRITE "${_xlle_restore_derived}" "#include \"unicorn_aot_bridge.h\"\n${_xlle_restore_text}")

# A tier boundary must not change an instruction's TSC value. Publish the
# instruction's translation-time index only at the four TSC read/write opcodes;
# ordinary instructions get no additional runtime work.
file(READ "${unicorn_SOURCE_DIR}/qemu/target/i386/translate.c" _xlle_translate_text)
string(REPLACE "\r\n" "\n" _xlle_translate_text "${_xlle_translate_text}")
string(SHA256 _xlle_translate_sha "${_xlle_translate_text}")
if(NOT _xlle_translate_sha STREQUAL "793ffab4b7e9f4186be04f4ae2f527dc72d3cc594345fd823e41f9a6689c5fd7")
  message(FATAL_ERROR "Unicorn x86 translator changed: requalify instruction TSC")
endif()
set(_xlle_tsc_prefix [=[{
            TCGv_i32 xlle_prefix = tcg_const_i32(tcg_ctx, s->base.num_insns - 1);
            TCGv_ptr xlle_clock = tcg_const_ptr(tcg_ctx, &xlle_guest_tsc_prefix);
            tcg_gen_st_i32(tcg_ctx, xlle_prefix, xlle_clock, 0);
            tcg_temp_free_i32(tcg_ctx, xlle_prefix);
            tcg_temp_free_ptr(tcg_ctx, xlle_clock);
        }
        ]=])
foreach(_xlle_tsc_opcode rdtsc rdtscp rdmsr wrmsr)
  string(REPLACE "gen_helper_${_xlle_tsc_opcode}(tcg_ctx, tcg_ctx->cpu_env);"
    "${_xlle_tsc_prefix}gen_helper_${_xlle_tsc_opcode}(tcg_ctx, tcg_ctx->cpu_env);"
    _xlle_translate_text "${_xlle_translate_text}")
endforeach()
set(_xlle_translate_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/translate.c")
include("${CMAKE_CURRENT_LIST_DIR}/UnicornMmx.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/UnicornX87.cmake")
# CMPXCHG8B's helper computes the preserved flags from env->cc_op. Publish
# the preceding lazy arithmetic operation before calling it. Otherwise an
# ADD operand can become a literal EFLAGS image when the old operation is
# CC_OP_EFLAGS, including control bits such as TF. No flags are canonicalized.
set(_xlle_cmpxchg_old "        case 1: /* CMPXCHG8, CMPXCHG16 */\n            if (mod == 3) {\n                goto illegal_op;\n            }\n")
string(FIND "${_xlle_translate_text}" "${_xlle_cmpxchg_old}" _xlle_cmpxchg_position)
if(_xlle_cmpxchg_position LESS 0)
  message(FATAL_ERROR "Unicorn CMPXCHG helper anchor changed: requalify lazy flags")
endif()
string(REPLACE "${_xlle_cmpxchg_old}"
  "${_xlle_cmpxchg_old}            gen_update_cc_op(s);\n"
  _xlle_translate_text "${_xlle_translate_text}")
file(WRITE "${_xlle_translate_derived}" "#include \"unicorn_metrics.h\"\n${_xlle_translate_text}")

# The pinned Unicorn omitted IA32_TIME_STAMP_COUNTER from RDMSR/WRMSR. Bind
# both to the same clock as RDTSC. Xbox's P6 WRMSR writes the low 32 bits and
# clears the high half (Intel SDM vol. 3B, Time-Stamp Counter).
file(READ "${unicorn_SOURCE_DIR}/qemu/target/i386/misc_helper.c" _xlle_misc_text)
string(REPLACE "\r\n" "\n" _xlle_misc_text "${_xlle_misc_text}")
string(SHA256 _xlle_misc_sha "${_xlle_misc_text}")
if(NOT _xlle_misc_sha STREQUAL "1fbc3899970491050aceca85a248cc15665ec13ad8e1dbd1cdd29ff00c21ce60")
  message(FATAL_ERROR "Unicorn x86 MSR helpers changed: requalify TSC ownership")
endif()
string(REPLACE "    case MSR_IA32_SYSENTER_CS:\n        env->sysenter_cs = val & 0xffff;"
  "    case 0x10: /* IA32_TIME_STAMP_COUNTER, P6 write semantics */\n        env->tsc_offset = (uint64_t)(uint32_t)val - cpu_get_tsc(env);\n        break;\n    case MSR_IA32_SYSENTER_CS:\n        env->sysenter_cs = val & 0xffff;"
  _xlle_misc_text "${_xlle_misc_text}")
string(REPLACE "    case MSR_IA32_SYSENTER_CS:\n        val = env->sysenter_cs;"
  "    case 0x10: /* IA32_TIME_STAMP_COUNTER */\n        val = cpu_get_tsc(env) + env->tsc_offset;\n        break;\n    case MSR_IA32_SYSENTER_CS:\n        val = env->sysenter_cs;"
  _xlle_misc_text "${_xlle_misc_text}")
set(_xlle_misc_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/misc_helper.c")
string(REGEX MATCHALL "case 0x10: /\\* IA32_TIME_STAMP_COUNTER" _xlle_tsc_cases "${_xlle_misc_text}")
list(LENGTH _xlle_tsc_cases _xlle_tsc_case_count)
if(NOT _xlle_tsc_case_count EQUAL 2)
  message(FATAL_ERROR "TSC MSR integration did not install both read and write cases")
endif()
file(WRITE "${_xlle_misc_derived}" "${_xlle_misc_text}")
include("${CMAKE_CURRENT_LIST_DIR}/UnicornDeadlines.cmake")
get_target_property(_xlle_x86_sources x86_64-softmmu SOURCES)
set(_xlle_x86_updated)
foreach(_xlle_source IN LISTS _xlle_x86_sources)
  if(_xlle_source MATCHES "(^|/)qemu/accel/tcg/cputlb\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_tlb_derived}")
  elseif(_xlle_source MATCHES "(^|/)qemu/accel/tcg/cpu-exec\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_exec_derived}")
  elseif(_xlle_source MATCHES "(^|/)qemu/accel/tcg/translate-all\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_restore_derived}")
  elseif(_xlle_source MATCHES "(^|/)qemu/target/i386/translate\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_translate_derived}")
  elseif(_xlle_source MATCHES "(^|/)qemu/target/i386/misc_helper\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_misc_derived}")
  elseif(_xlle_source MATCHES "(^|/)qemu/target/i386/fpu_helper\\.c$")
    list(APPEND _xlle_x86_updated "${_xlle_fpu_derived}")
  elseif(IS_ABSOLUTE "${_xlle_source}")
    list(APPEND _xlle_x86_updated "${_xlle_source}")
  else()
    list(APPEND _xlle_x86_updated "${unicorn_SOURCE_DIR}/${_xlle_source}")
  endif()
endforeach()
set_property(TARGET x86_64-softmmu PROPERTY SOURCES "${_xlle_x86_updated}")
target_include_directories(x86_64-softmmu PRIVATE "${unicorn_SOURCE_DIR}/qemu/accel/tcg"
                           "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu")
