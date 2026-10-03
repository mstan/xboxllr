# Applied only after UnicornPreciseFault.cmake verifies the pinned translate.c
# hash. Derive the build copy; retain upstream license and FetchContent source.
# Intel SDM Vol.1 9.6.2 and Vol.3A 11.5: MMX writes set the destination exponent
# to all ones, TOP/tags change only on successful execution, and pending x87
# exceptions precede memory effects. Scope: MOVQ, MOVNTQ and EMMS, as exercised
# by the dashboard census and tests/test_mmx_floor.cpp.
function(xlle_mmx_replace old new)
  string(FIND "${_xlle_translate_text}" "${old}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "Unicorn MMX patch anchor changed; requalify")
  endif()
  string(REPLACE "${old}" "${new}" _updated "${_xlle_translate_text}")
  set(_xlle_translate_text "${_updated}" PARENT_SCOPE)
endfunction()

set(_xlle_mmx_commit [=[static void xlle_gen_mmx_commit(DisasContext *s, int destination)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_enter_mmx(tcg_ctx, tcg_ctx->cpu_env);
    if (destination >= 0) {
        TCGv_i32 exponent = tcg_const_i32(tcg_ctx, 0xffff);
        tcg_gen_st16_i32(tcg_ctx, exponent, tcg_ctx->cpu_env,
                        offsetof(CPUX86State, fpregs[destination].d.high));
        tcg_temp_free_i32(tcg_ctx, exponent);
    }
}

]=])
xlle_mmx_replace("static void gen_sse(CPUX86State *env"
  "${_xlle_mmx_commit}static void gen_sse(CPUX86State *env")

# These instructions use the same fixed FWAIT helper as the x87 path. It raises
# the actual pending exception with precise restored PC; it executes no guest code.
# MOVNTQ legality must be established before #MF. Its memory-operand check and
# FWAIT therefore live together in the decoded case below.
set(_xlle_mmx_wait [=[    if (!is_xmm && (b == 0x6f || b == 0x7f || b == 0xe7 || b == 0x77)) {
        if (!(s->cpuid_features & CPUID_MMX) ||
            (b == 0xe7 && !(s->cpuid_features & CPUID_SSE))) {
            goto illegal_op;
        }
        if (b != 0xe7) gen_helper_fwait(tcg_ctx, tcg_ctx->cpu_env);
    }
]=])
xlle_mmx_replace("    if (b == 0x77) {"
  "${_xlle_mmx_wait}    if (b == 0x77) {")
xlle_mmx_replace([=[    if (!is_xmm) {
        gen_helper_enter_mmx(tcg_ctx, tcg_ctx->cpu_env);
    }

    modrm =]=] [=[    if (!is_xmm && b != 0x6f && b != 0x7f && b != 0xe7) {
        gen_helper_enter_mmx(tcg_ctx, tcg_ctx->cpu_env);
    }

    modrm =]=])

xlle_mmx_replace([=[        case 0x0e7: /* movntq */
            if (mod == 3) {
                goto illegal_op;
            }
            gen_lea_modrm(env, s, modrm);
            gen_stq_env_A0(s, offsetof(CPUX86State, fpregs[reg].mmx));
            break;]=] [=[        case 0x0e7: /* movntq */
            if (mod == 3) {
                goto illegal_op;
            }
            gen_helper_fwait(tcg_ctx, tcg_ctx->cpu_env);
            gen_lea_modrm(env, s, modrm);
            gen_stq_env_A0(s, offsetof(CPUX86State, fpregs[reg].mmx));
            xlle_gen_mmx_commit(s, -1);
            break;]=])

xlle_mmx_replace([=[                               offsetof(CPUX86State,fpregs[reg].mmx));
            }
            break;
        case 0x010:]=] [=[                               offsetof(CPUX86State,fpregs[reg].mmx));
            }
            xlle_gen_mmx_commit(s, reg);
            break;
        case 0x010:]=])

xlle_mmx_replace([=[                            offsetof(CPUX86State,fpregs[reg].mmx));
            }
            break;
        case 0x011:]=] [=[                            offsetof(CPUX86State,fpregs[reg].mmx));
            }
            xlle_gen_mmx_commit(s, mod == 3 ? rm : -1);
            break;
        case 0x011:]=])
