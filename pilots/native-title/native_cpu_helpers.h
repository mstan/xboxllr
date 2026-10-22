/* Authored compatibility surface for the attributed MIT CPU helper template.
 * Appended after the template's types; this file contains no guest code. */
#ifndef PILOT_NATIVE_CPU_HELPERS_H
#define PILOT_NATIVE_CPU_HELPERS_H
#define PILOT_NATIVE_CPU_HELPERS_VERSION 1

extern RECOMP_TLS uint32_t g_icall_saved_esp, g_icall_dispatch_form;
void recomp_int3_reached(uint32_t address);
void recomp_cxx_throw(uint32_t object, uint32_t information);
void recomp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t result[4]);
uint32_t recomp_eflags_push(void);
void recomp_eflags_pop(uint32_t flags);
typedef void (*recomp_foreign_longjmp_fn)(jmp_buf *destination, int value,
                                        uintptr_t owner_stack);
void recomp_set_foreign_longjmp(recomp_foreign_longjmp_fn callback);

#define MEM64(address) (*(volatile uint64_t *)XBOX_PTR(address))

static inline void pilot_recomp_indirect_failure(uint32_t target, unsigned kind)
{
    g_icall_saved_esp = g_esp;
    g_icall_dispatch_form = kind;
    recomp_icall_fail_log(target);
}

/* A known RET N must restore SP by exactly the return slot plus N, while the
 * three nonvolatile register cells retain their caller values. Normal builds
 * still emit a direct native call, with no additional register bookkeeping. */
#ifdef RECOMP_ABI_CHECK
void recomp_abi_pop_violation_log(uint32_t target, uint32_t ebx_before,
                                uint32_t esi_before, uint32_t edi_before,
                                uint32_t stack_before, uint32_t cleanup_bytes);
#define RECOMP_ABI_CALL_POP(target, function, cleanup) do { \
    const uint32_t _pilot_call_saved[4] = {g_ebx, g_esi, g_edi, g_esp}; \
    const uint32_t _pilot_call_cleanup = (uint32_t)(cleanup); \
    (function)(); \
    if (g_esp != _pilot_call_saved[3] + 4u + _pilot_call_cleanup || \
        g_ebx != _pilot_call_saved[0] || g_esi != _pilot_call_saved[1] || \
        g_edi != _pilot_call_saved[2]) \
        recomp_abi_pop_violation_log((target), _pilot_call_saved[0], \
            _pilot_call_saved[1], _pilot_call_saved[2], _pilot_call_saved[3], \
            _pilot_call_cleanup); \
} while (0)
#else
#define RECOMP_ABI_CALL_POP(target, function, cleanup) (function)()
#endif
#endif
