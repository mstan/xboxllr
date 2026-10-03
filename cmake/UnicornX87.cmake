# Behavioral contract: tests/test_x87_floor.cpp. No modifications to FetchContent.
# The derived sources retain their original upstream licenses and provenance.
file(READ "${unicorn_SOURCE_DIR}/qemu/target/i386/fpu_helper.c" _xlle_fpu_text)
string(REPLACE "\r\n" "\n" _xlle_fpu_text "${_xlle_fpu_text}")
string(SHA256 _xlle_fpu_sha "${_xlle_fpu_text}")
if(NOT _xlle_fpu_sha STREQUAL "809e3d8e0affb71cdf76e52da57d519ab227c223a5443039f2d41a8342979e33")
  message(FATAL_ERROR "Unicorn FPU helper source changed; requalify x87 status and fault semantics")
endif()
foreach(_fn flds_FT0 fldl_FT0 flds_ST0 fldl_ST0 fsts_ST0 fstl_ST0
    fxchg_ST0_STN fmov_ST0_STN fmov_STN_ST0
    fadd_ST0_FT0 fmul_ST0_FT0 fsub_ST0_FT0 fsubr_ST0_FT0 fdiv_ST0_FT0 fdivr_ST0_FT0
    fadd_STN_ST0 fmul_STN_ST0 fsub_STN_ST0 fsubr_STN_ST0 fdiv_STN_ST0 fdivr_STN_ST0)
  string(REGEX REPLACE "(void|uint32_t|uint64_t) helper_${_fn}\\("
    "static inline \\1 xlle_raw_${_fn}(" _xlle_fpu_text "${_xlle_fpu_text}")
endforeach()
set(_xlle_fpu_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/fpu_helper.c")
file(WRITE "${_xlle_fpu_derived}" "${_xlle_fpu_text}\n#include \"x87_status.inc\"\n")

file(READ "${unicorn_SOURCE_DIR}/qemu/target/i386/helper.h" _xlle_helper_text)
string(REPLACE "\r\n" "\n" _xlle_helper_text "${_xlle_helper_text}")
string(SHA256 _xlle_helper_sha "${_xlle_helper_text}")
if(NOT _xlle_helper_sha STREQUAL "01e8427e0982c2c1faa13284726cf701df17f096a72b0de243e7002d957ba4f0")
  message(FATAL_ERROR "Unicorn helper declarations changed; requalify x87 store")
endif()
set(_xlle_x87_headers "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/x87-headers")
file(MAKE_DIRECTORY "${_xlle_x87_headers}")
file(WRITE "${_xlle_x87_headers}/helper.h"
  "${_xlle_helper_text}\nDEF_HELPER_3(xlle_fst, void, env, tl, i32)\nDEF_HELPER_2(xlle_fmul32, void, env, i32)\nDEF_HELPER_2(xlle_fmul64, void, env, i64)\n")
target_include_directories(x86_64-softmmu BEFORE PRIVATE "${_xlle_x87_headers}")

function(xlle_x87_replace old new)
  string(FIND "${_xlle_translate_text}" "${old}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "Unicorn x87 translator anchor changed; requalify")
  endif()
  string(LENGTH "${old}" _length)
  math(EXPR _after "${_at} + ${_length}")
  string(SUBSTRING "${_xlle_translate_text}" ${_after} -1 _tail)
  string(FIND "${_tail}" "${old}" _duplicate)
  if(NOT _duplicate LESS 0)
    message(FATAL_ERROR "Unicorn x87 translator anchor is not unique; requalify")
  endif()
  string(REPLACE "${old}" "${new}" _updated "${_xlle_translate_text}")
  set(_xlle_translate_text "${_updated}" PARENT_SCOPE)
endfunction()
# Add pending-exception checks only to decoded, valid arithmetic and transfer
# forms. Do not turn the non-waiting FN* controls or reserved forms into FWAIT.
set(_xlle_x87_decode "            op = ((b & 7) << 3) | ((modrm >> 3) & 7);\n")
set(_xlle_x87_wait [=[            if ((mod != 3 && (op < 8 || (op >= 0x20 && op <= 0x27) ||
                      op == 0x08 || op == 0x0a || op == 0x0b ||
                      op == 0x28 || op == 0x2a || op == 0x2b)) ||
                (mod == 3 && (b == 0xd8 || b == 0xdc || op == 0x08 || op == 0x09 || op == 0x28 || op == 0x2b ||
                      (b == 0xde && ((op & 7) < 2 || (op & 7) >= 4))))) {
                gen_helper_fwait(tcg_ctx, tcg_ctx->cpu_env);
            }
]=])
xlle_x87_replace("${_xlle_x87_decode}" "${_xlle_x87_decode}${_xlle_x87_wait}")
# Keep FMUL's memory conversion and arithmetic in one exception transaction.
# Other arithmetic families remain separately qualified work.
xlle_x87_replace("                            gen_helper_flds_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);"
  "                            if (op1 == 1) gen_helper_xlle_fmul32(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);\n                            else gen_helper_flds_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);")
xlle_x87_replace("                            gen_helper_fldl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64);"
  "                            if (op1 == 1) gen_helper_xlle_fmul64(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64);\n                            else gen_helper_fldl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64);")
xlle_x87_replace("\n                        gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1);"
  "\n                        if (op != 0x01 && op != 0x21) gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1);")
# The Xbox P6 retains the last non-control x87 instruction's 11-bit opcode.
xlle_x87_replace("            if (update_fip) {\n"
  "            if (update_fip) {\n                tcg_gen_movi_i32(tcg_ctx, s->tmp2_i32, ((b & 7) << 8) | modrm);\n                tcg_gen_st16_i32(tcg_ctx, s->tmp2_i32,\n                                tcg_ctx->cpu_env, offsetof(CPUX86State, fpop));\n")
xlle_x87_replace([=[                            gen_helper_fsts_ST0(tcg_ctx, s->tmp2_i32, tcg_ctx->cpu_env);
                            tcg_gen_qemu_st_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LEUL);]=]
  [=[                            tcg_gen_movi_i32(tcg_ctx, s->tmp2_i32, op & 1);
                            gen_helper_xlle_fst(tcg_ctx, tcg_ctx->cpu_env, s->A0,
                                                s->tmp2_i32);]=])
xlle_x87_replace([=[                            gen_helper_fstl_ST0(tcg_ctx, s->tmp1_i64, tcg_ctx->cpu_env);
                            tcg_gen_qemu_st_i64(tcg_ctx, s->tmp1_i64, s->A0,
                                                s->mem_index, MO_LEQ);]=]
  [=[                            tcg_gen_movi_i32(tcg_ctx, s->tmp2_i32, 2 | (op & 1));
                            gen_helper_xlle_fst(tcg_ctx, tcg_ctx->cpu_env, s->A0,
                                                s->tmp2_i32);]=])
xlle_x87_replace([=[                        if ((op & 7) == 3)
                            gen_helper_fpop(tcg_ctx, tcg_ctx->cpu_env);]=]
  [=[                        if ((op & 7) == 3 && (op >> 4) != 0 && (op >> 4) != 2)
                            gen_helper_fpop(tcg_ctx, tcg_ctx->cpu_env);]=])
