/* Test-only adapter to existing pinned QEMU helpers. No copied donor runtime.
 * The selected helpers perform arithmetic on CPUX86State, without guest memory,
 * GETPC, translated-code fault restoration, or guest instruction execution.
 */
#include "aot_x87_probe.h"
#include "uc_priv.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

void xlle_probe_x87_snapshot(void* engine, xlle_x87_snapshot* out) {
    CPUX86State* env = ((struct uc_struct*)engine)->cpu->env_ptr;
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < 8; ++i) {
        out->significand[i] = env->fpregs[i].d.low;
        out->exponent[i] = env->fpregs[i].d.high;
        out->tags[i] = env->fptags[i];
    }
    out->top = env->fpstt; out->control = env->fpuc; out->status = env->fpus;
    out->ip = env->fpip; out->dp = env->fpdp; out->cs = env->fpcs; out->ds = env->fpds;
    out->opcode = env->fpop;
    out->arithmetic_exceptions = get_float_exception_flags(&env->fp_status);
}

int xlle_probe_x87_step(void* engine, unsigned operation, uint64_t input,
                       uint32_t pc, uint32_t address, int commit, uint64_t* output) {
    struct uc_struct* uc = engine;
    if (!uc || uc->arch != UC_ARCH_X86 || !(uc->mode & UC_MODE_32) || !output ||
        operation > XLLE_PROBE_FSTP64) return 0;
    CPUX86State* live = uc->cpu->env_ptr;
    // Unmasked exceptions, disabled FPU and stack faults need full instruction
    // integration; this bounded qualification explicitly declines them.
    if ((live->fpuc & 0x3f) != 0x3f || (live->fpus & 0x80) ||
        (live->cr[0] & (CR0_EM_MASK | CR0_TS_MASK))) return 0;
    if (operation == XLLE_PROBE_FLD64) {
        if (!live->fptags[(live->fpstt - 1) & 7]) return 0;
    } else if (live->fptags[live->fpstt]) return 0;
    CPUX86State staged = *live;
    CPUX86State* env = &staged;
    uint64_t result = 0;
    switch (operation) {
    case XLLE_PROBE_FLD64: helper_fldl_ST0(env, input); break;
    case XLLE_PROBE_FADD64:
        helper_fldl_FT0(env, input); helper_fadd_ST0_FT0(env); break;
    case XLLE_PROBE_FSUB64:
        helper_fldl_FT0(env, input); helper_fsub_ST0_FT0(env); break;
    case XLLE_PROBE_FSTP64:
        result = helper_fstl_ST0(env); helper_fpop(env); break;
    }
    env->fpip = pc; env->fpdp = address;
    static const uint16_t opcodes[] = {0x505, 0x405, 0x425, 0x51d};
    env->fpop = opcodes[operation];
    env->fpcs = env->segs[R_CS].selector; env->fpds = env->segs[R_DS].selector;
    if (commit) {
        // Publish only FPU state, never overwrite unrelated machine state.
        live->fpstt = env->fpstt; live->fpus = env->fpus;
        memcpy(live->fptags, env->fptags, sizeof(live->fptags));
        memcpy(live->fpregs, env->fpregs, sizeof(live->fpregs));
        live->fp_status = env->fp_status; live->ft0 = env->ft0;
        live->fpip = env->fpip; live->fpdp = env->fpdp;
        live->fpcs = env->fpcs; live->fpds = env->fpds;
        live->fpop = env->fpop;
    }
    *output = result;
    return 1;
}
