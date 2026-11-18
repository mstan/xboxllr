/*
 * Guest i386 interpreter (Pentium III user/kernel integer, x87, MMX, SSE).
 *
 * Portable C, built from the vendored box64 interpreter core (MIT, see
 * src/third_party/box64_interp).  It executes guest code in place, in the same
 * flat 4 GiB guest space generated native-title code uses, so the two can hand
 * a thread back and forth at call boundaries.
 *
 * The interpreter never decides on its own what is "native".  The caller's
 * leave_at() hook is consulted before every instruction and can stop
 * execution at any EIP (a translated function entry, a kernel thunk, a
 * sentinel); the caller then services that address and resumes.
 */
#ifndef XLLE_X86_INTERP_H
#define XLLE_X86_INTERP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { XLLE_X86_EAX, XLLE_X86_ECX, XLLE_X86_EDX, XLLE_X86_EBX,
       XLLE_X86_ESP, XLLE_X86_EBP, XLLE_X86_ESI, XLLE_X86_EDI };

/* Architectural guest state exchanged with the interpreter. */
typedef struct XlleX86State {
    uint32_t gpr[8];        /* indexed by XLLE_X86_* (hardware encoding order) */
    uint32_t eip;
    uint32_t eflags;        /* architectural EFLAGS image */
    uint32_t fs_base;       /* guest linear base of FS (KPCR/TIB) */
    double fp[8];           /* physical x87 registers; ST(i) = fp[(fp_top + i) & 7] */
    uint32_t fp_top;        /* 0..7 */
    uint16_t fpu_cw;        /* x87 control word */
    uint16_t fpu_sw;        /* x87 status word (TOP field is taken from fp_top) */
    uint16_t fpu_tag;       /* abridged x87 tag word: bit i set = fp[i] valid */
    uint16_t reserved;
    uint8_t xmm[8][16];
    uint32_t mxcsr;
    uint64_t mm[8];         /* MMX registers (kept separate from the x87 file) */
} XlleX86State;

typedef struct XlleX86Hooks {
    void* user;
    /* Nonzero: stop BEFORE executing the instruction at eip.  Not consulted
       for the very first instruction of a run (the entry the caller chose). */
    int (*leave_at)(void* user, uint32_t eip);
    /* Required: guest CPUID / RDTSC as the configured Xbox CPU reports them. */
    void (*cpuid)(void* user, uint32_t leaf, uint32_t subleaf, uint32_t out[4]);
    uint64_t (*rdtsc)(void* user);
} XlleX86Hooks;

typedef enum XlleX86Stop {
    XLLE_X86_STOP_LEAVE = 1,  /* leave_at() asked to stop at state->eip */
    XLLE_X86_STOP_RETURNED,   /* reached stop_eip, or ESP rose above return_esp */
    XLLE_X86_STOP_LIMIT,      /* max_insns retired */
    XLLE_X86_STOP_FAULT       /* exception the interpreter cannot deliver; see fault_* */
} XlleX86Stop;

/* Fault classes (XlleX86Result.fault). */
enum {
    XLLE_X86_FAULT_NONE = 0,
    XLLE_X86_FAULT_UD,        /* invalid / unimplemented opcode */
    XLLE_X86_FAULT_GP,        /* privileged or protection fault */
    XLLE_X86_FAULT_DE,        /* divide error */
    XLLE_X86_FAULT_BP,        /* INT3 */
    XLLE_X86_FAULT_INT,       /* INT n / SYSENTER / SYSCALL */
    XLLE_X86_FAULT_TRAP,      /* single-step (TF) */
    XLLE_X86_FAULT_FP         /* unmasked FP exception */
};

typedef struct XlleX86Result {
    XlleX86Stop stop;
    uint64_t retired;         /* instructions retired in this call */
    int fault;                /* XLLE_X86_FAULT_* when stop == XLLE_X86_STOP_FAULT */
    uint32_t fault_eip;       /* EIP of the faulting instruction */
    int fault_detail;         /* interrupt vector / signal code */
} XlleX86Result;

/*
 * Run from state->eip until a stop condition.  guest_base is the host address
 * of guest address 0.  stop_eip: stop before executing an instruction at this
 * address (the entry's return address; checked on every instruction including
 * the first after a resume); 0 disables it.  return_esp: stop once
 * ESP > return_esp; 0xFFFFFFFF disables it.  max_insns == 0 means unlimited.
 * state is updated in place.
 */
XlleX86Result xlle_x86_run(XlleX86State* state, const XlleX86Hooks* hooks,
                           uintptr_t guest_base, uint32_t stop_eip,
                           uint32_t return_esp, uint64_t max_insns);

/* Short identity of the interpreter core (vendored revision + XLR patch level),
   recorded in compatibility/netplay profiles. */
#define XLLE_X86_INTERP_IDENTITY_STRING "box64-27c8275d8295-xlr1"
const char* xlle_x86_interp_identity(void);

#ifdef __cplusplus
}
#endif
#endif
