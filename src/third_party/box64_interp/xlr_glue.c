/*
 * xboxllr glue for the vendored box64 interpreter core.
 *
 * Owns the box64 process-level symbols the core links against, converts
 * between XlleX86State and box64's x64emu_t, and turns every guest-visible
 * event the core raises (signals, interrupts, CPUID, RDTSC) into either a
 * caller hook or an explicit XlleX86Result fault.  Nothing here is
 * native-title specific; the bridge that decides when to enter and leave the
 * interpreter lives with each runtime.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xlle/x86_interp.h"

#include "debug.h"
#include "box64stack.h"
#include "x64emu.h"
#include "box64cpu_util.h"
#include "x64emu_private.h"
#include "x87emu_private.h"
#include "x64run_private.h"
#include "dynarec/native_lock.h"
#include <fenv.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define XLR_HOST_SSE 1
#endif

#define XLR_INTERP_IDENTITY XLLE_X86_INTERP_IDENTITY_STRING

/* ---- box64 process-level symbols ----------------------------------------- */
uintptr_t xlr_guest_base;
box64env_t xlr_box64env = {
    0, /* cputype: Intel */
    0, /* dynarec_test */
    0, /* ignoreint3: INT3 is a guest event */
    0, /* sse_flushto0: never touch the host MXCSR */
    0, /* start_cnt */
    0, /* x87_no80bits: keep exact FLD/FSTP m80 round trips */
};
uint8_t box64_rdtsc_shift;
int box64_unittest_mode;
static box64context_t xlr_context;
box64context_t* my_context = &xlr_context;
XLR_TLS uint64_t xlr_lock_seen;
XLR_TLS uint64_t xlr_lock_seen_hi;

void xlr_mutex_lock(xlr_mutex_t* m)
{
    while (xlr_cas32((volatile void*)&m->v, 0, 1) != 0) {
    }
}

void xlr_mutex_unlock(xlr_mutex_t* m)
{
    (void)xlr_cas32((volatile void*)&m->v, 1, 0);
}

int GetTID(void) { return 0; }

int LeadingZeros16(uint16_t x)
{
    int n = 0;
    if (!x)
        return 16;
    while (!(x & 0x8000u)) {
        ++n;
        x = (uint16_t)(x << 1);
    }
    return n;
}

void xlr_interp_log(int level, const char* fmt, ...)
{
    /* The core logs only diagnostics (LOG_NONE is its "always" level). Guest
       faults are reported through XlleX86Result instead, so stay quiet unless
       a caller opts in. */
    (void)level;
    (void)fmt;
}

/* ---- per-run context ------------------------------------------------------ */
typedef struct XlrRun {
    x64emu_t* emu;
    const XlleX86Hooks* hooks;
    uint32_t return_esp;
    uint32_t stop_eip;
    uint32_t entry_eip;
    uint64_t max_insns;
    uint64_t retired;
    int first;
    XlleX86Result result;
} XlrRun;

static XLR_TLS XlrRun* t_run;

static void xlr_fault(x64emu_t* emu, int fault, uint32_t eip, int detail)
{
    XlrRun* run = t_run;
    if (run && run->result.stop != XLLE_X86_STOP_FAULT) {
        run->result.stop = XLLE_X86_STOP_FAULT;
        run->result.fault = fault;
        run->result.fault_eip = eip;
        run->result.fault_detail = detail;
    }
    emu->quit = 1;
}

/* Called at the head of every interpreted instruction (XLR_INSN_HOOK). */
int xlr_insn_check(x64emu_t* emu, uintptr_t addr)
{
    XlrRun* run = t_run;
    if (emu->quit)
        return 1;
    if (run->max_insns && run->retired >= run->max_insns) {
        run->result.stop = XLLE_X86_STOP_LIMIT;
        emu->quit = 1;
        return 1;
    }
    if ((run->stop_eip && (uint32_t)addr == run->stop_eip) || R_ESP > run->return_esp) {
        run->result.stop = XLLE_X86_STOP_RETURNED;
        emu->quit = 1;
        return 1;
    }
    if (!run->first && run->hooks->leave_at
        && run->hooks->leave_at(run->hooks->user, (uint32_t)addr)) {
        run->result.stop = XLLE_X86_STOP_LEAVE;
        emu->quit = 1;
        return 1;
    }
    run->first = 0;
    run->retired++;
    return 0;
}

/* ---- guest-visible events from the core ----------------------------------- */
void EmitSignal(x64emu_t* emu, int sig, void* addr, int code)
{
    int fault = XLLE_X86_FAULT_GP;
    switch (sig) {
    case X64_SIGILL: fault = XLLE_X86_FAULT_UD; break;
    case X64_SIGTRAP: fault = code == 128 ? XLLE_X86_FAULT_BP : XLLE_X86_FAULT_TRAP; break;
    case X64_SIGFPE: fault = XLLE_X86_FAULT_FP; break;
    default: fault = XLLE_X86_FAULT_GP; break;
    }
    xlr_fault(emu, fault, (uint32_t)(uintptr_t)addr, code);
}

void EmitInterruption(x64emu_t* emu, int num, void* addr)
{
    xlr_fault(emu, num == 3 ? XLLE_X86_FAULT_BP : XLLE_X86_FAULT_INT, (uint32_t)(uintptr_t)addr, num);
}

void EmitDiv0(x64emu_t* emu, void* addr, int code)
{
    xlr_fault(emu, XLLE_X86_FAULT_DE, (uint32_t)(uintptr_t)addr, code);
}

void CheckExec(x64emu_t* emu, uintptr_t addr)
{
    (void)emu;
    (void)addr;
}

void UnimpOpcode(x64emu_t* emu, int is32bits)
{
    (void)is32bits;
    xlr_fault(emu, XLLE_X86_FAULT_UD, (uint32_t)emu->old_ip, 0);
}

void EmuX64Syscall(void* e) { x64emu_t* emu = (x64emu_t*)e; xlr_fault(emu, XLLE_X86_FAULT_INT, (uint32_t)emu->old_ip, 0x100); }
void EmuX86Syscall(void* e) { x64emu_t* emu = (x64emu_t*)e; xlr_fault(emu, XLLE_X86_FAULT_INT, (uint32_t)emu->old_ip, 0x80); }
void EmuInt3(void* e, void* addr) { xlr_fault((x64emu_t*)e, XLLE_X86_FAULT_BP, (uint32_t)(uintptr_t)addr, 3); }
void* EmuFork(void* e, int forktype) { (void)forktype; xlr_fault((x64emu_t*)e, XLLE_X86_FAULT_GP, (uint32_t)((x64emu_t*)e)->old_ip, 0); return e; }

/* RDRAND/RDSEED/RDPID/RDTSCP do not exist on the Pentium III. */
static uint32_t xlr_missing_instruction(void)
{
    if (t_run)
        xlr_fault(t_run->emu, XLLE_X86_FAULT_UD, (uint32_t)t_run->emu->old_ip, 0);
    return 0;
}
uint32_t get_random32(void) { return xlr_missing_instruction(); }
uint64_t get_random64(void) { return xlr_missing_instruction(); }
uint32_t helper_getcpu(x64emu_t* emu) { (void)emu; return xlr_missing_instruction(); }

void my_cpuid(x64emu_t* emu)
{
    uint32_t out[4] = { 0, 0, 0, 0 };
    t_run->hooks->cpuid(t_run->hooks->user, R_EAX, R_ECX, out);
    R_RAX = out[0];
    R_RBX = out[1];
    R_RCX = out[2];
    R_RDX = out[3];
}

uint64_t ReadTSC(x64emu_t* emu)
{
    (void)emu;
    return t_run->hooks->rdtsc(t_run->hooks->user);
}

uintptr_t GetSegmentBaseEmu(x64emu_t* emu, int seg)
{
    /* Flat guest: only FS carries a base (the per-thread KPCR/TIB). */
    return seg == _FS ? emu->segs_offs[_FS] : 0;
}

/* VEX-encoded instructions do not exist on the Pentium III; in 32-bit mode
   C4/C5 with a register ModRM are LES/LDS with an invalid operand: #UD. */
uintptr_t RunAVX(x64emu_t* emu, vex_t vex, uintptr_t addr, int* step)
{
    (void)vex;
    (void)addr;
    (void)step;
    emu->error |= ERR_ILLEGAL;
    return 0;
}

/* ---- guest MXCSR on the host ------------------------------------------------
 * The core performs guest SSE (and x87, as doubles) arithmetic with host
 * floating-point operations, so the guest's rounding control and flush-to-zero
 * must be the host's while it runs, exactly as for generated native-title code
 * (which keeps the guest MXCSR in the host MXCSR).  Host exceptions stay
 * masked: an unmasked guest exception must never become a host trap. */
void xlr_mxcsr_apply(x64emu_t* emu)
{
#ifdef XLR_HOST_SSE
    _mm_setcsr((emu->mxcsr.x32 & 0xE040u) | 0x1F80u);
#else
    static const int modes[4] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    fesetround(modes[(emu->mxcsr.x32 >> 13) & 3u]);
#endif
}

/* ---- state transfer -------------------------------------------------------- */
static const uint32_t xlr_parity_tab[8] = {
    0x96696996, 0x69969669, 0x69969669, 0x96696996,
    0x69969669, 0x96696996, 0x96696996, 0x69969669,
};

static void xlr_state_to_emu(const XlleX86State* s, x64emu_t* emu)
{
    int i;
    memset(emu, 0, sizeof(*emu));
    emu->context = my_context;
    for (i = 0; i < 16; ++i)
        emu->sbiidx[i] = &emu->regs[i];
    emu->sbiidx[4] = &emu->zero;
    emu->x64emu_parity_tab = (uint32_t*)xlr_parity_tab;
    for (i = 0; i < 8; ++i)
        emu->regs[i].q[0] = s->gpr[i];
    R_RIP = s->eip;
    emu->eflags.x64 = (s->eflags & 0x003F7FD5u) | 0x2u;
    emu->df = d_none;
    emu->segs[_CS] = 0x23;
    emu->segs[_DS] = emu->segs[_ES] = emu->segs[_SS] = 0x2b;
    emu->segs[_FS] = 0x3b;
    emu->segs[_GS] = 0;
    emu->segs_offs[_FS] = s->fs_base;

    for (i = 0; i < 8; ++i) {
        emu->x87[i].d = s->fp[i];
        emu->fpu_ld[i].uref = FPU_LD80_INVALID_REF;
    }
    emu->top = s->fp_top & 7u;
    emu->cw.x16 = s->fpu_cw;
    emu->sw.x16 = (uint16_t)((s->fpu_sw & ~0x3800u) | ((emu->top & 7u) << 11));
    emu->fpu_tags = 0;
    emu->fpu_stack = 0;
    for (i = 0; i < 8; ++i) {
        unsigned phys = (emu->top + (unsigned)i) & 7u;
        if ((s->fpu_tag >> phys) & 1u)
            ++emu->fpu_stack;
        else
            emu->fpu_tags |= (uint64_t)3u << (i * 2);
    }
    for (i = 0; i < 8; ++i)
        memcpy(&emu->xmm[i], s->xmm[i], 16);
    emu->mxcsr.x32 = s->mxcsr;
    for (i = 0; i < 8; ++i)
        emu->mmx[i].q = s->mm[i];
}

static void xlr_emu_to_state(x64emu_t* emu, XlleX86State* s)
{
    int i;
    CHECK_FLAGS(emu);
    for (i = 0; i < 8; ++i)
        s->gpr[i] = emu->regs[i].dword[0];
    s->eip = (uint32_t)R_RIP;
    s->eflags = (uint32_t)emu->eflags.x64;
    s->fs_base = (uint32_t)emu->segs_offs[_FS];
    for (i = 0; i < 8; ++i)
        s->fp[i] = emu->x87[i].d;
    s->fp_top = emu->top & 7u;
    s->fpu_cw = emu->cw.x16;
    s->fpu_sw = (uint16_t)((emu->sw.x16 & ~0x3800u) | ((emu->top & 7u) << 11));
    s->fpu_tag = 0;
    for (i = 0; i < 8; ++i) {
        unsigned phys = (emu->top + (unsigned)i) & 7u;
        if (((emu->fpu_tags >> (i * 2)) & 3u) != 3u)
            s->fpu_tag |= (uint16_t)(1u << phys);
    }
    for (i = 0; i < 8; ++i)
        memcpy(s->xmm[i], &emu->xmm[i], 16);
    s->mxcsr = emu->mxcsr.x32;
    for (i = 0; i < 8; ++i)
        s->mm[i] = emu->mmx[i].q;
}

/* ---- public entry ---------------------------------------------------------- */
XlleX86Result xlle_x86_run(XlleX86State* state, const XlleX86Hooks* hooks,
                           uintptr_t guest_base, uint32_t stop_eip,
                           uint32_t return_esp, uint64_t max_insns)
{
    x64emu_t emu;
    XlrRun run;
    XlrRun* outer = t_run;

    memset(&run, 0, sizeof(run));
    run.emu = &emu;
    run.hooks = hooks;
    run.return_esp = return_esp;
    run.stop_eip = stop_eip;
    run.entry_eip = state->eip;
    run.max_insns = max_insns;
    run.first = 1;
    xlr_guest_base = guest_base;

    xlr_state_to_emu(state, &emu);
#ifdef XLR_HOST_SSE
    unsigned host_csr = _mm_getcsr();
#else
    int host_round = fegetround();
#endif
    xlr_mxcsr_apply(&emu);
    t_run = &run;
    Run(&emu, 0);
    t_run = outer;
#ifdef XLR_HOST_SSE
    _mm_setcsr(host_csr);
#else
    fesetround(host_round);
#endif
    xlr_emu_to_state(&emu, state);
    if (run.result.stop == 0) {
        /* The core returned without a recorded reason: treat as #UD at EIP. */
        run.result.stop = XLLE_X86_STOP_FAULT;
        run.result.fault = XLLE_X86_FAULT_UD;
        run.result.fault_eip = state->eip;
    }
    if (run.result.stop == XLLE_X86_STOP_FAULT && run.result.fault_eip == 0)
        run.result.fault_eip = (uint32_t)emu.old_ip;
    run.result.retired = run.retired;
    return run.result;
}

const char* xlle_x86_interp_identity(void)
{
    return XLR_INTERP_IDENTITY;
}
