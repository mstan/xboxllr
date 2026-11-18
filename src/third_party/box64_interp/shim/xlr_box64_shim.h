/*
 * XLR shim for the vendored box64 interpreter core.
 *
 * box64's interpreter expects the rest of box64 (process context, ELF loader,
 * Linux signal emulation, dynarec, wrapped-library bridges).  xboxllr uses the
 * core only as a guest i386 interpreter inside a native-title process, so this
 * header supplies the small surface the core needs and routes every
 * guest-visible side effect (signals, CPUID, TSC, syscalls) to xboxllr hooks
 * declared in xlr_box64_hooks.h.
 *
 * Guest memory: native-title reserves the 4 GiB guest space at
 * g_xbox_mem_offset.  Every guest address the core dereferences goes through
 * XLR_G2H(), which adds that base after truncating to 32 bits.
 */
#ifndef XLR_BOX64_SHIM_H
#define XLR_BOX64_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xlr_box64_hooks.h"

typedef struct x64emu_s x64emu_t;

/* ---- guest memory --------------------------------------------------------- */
#define XLR_G2H(a) ((uintptr_t)xlr_guest_base + (uint32_t)(uintptr_t)(a))

typedef uint32_t ptr_t; /* box32 guest pointer */

/* ---- configuration (box64 BOX64ENV) -------------------------------------- */
typedef struct box64env_s {
    int cputype;        /* 0 = Intel behaviour for undefined-flag corners */
    int dynarec_test;
    int ignoreint3;
    int sse_flushto0;
    int start_cnt;
    int x87_no80bits;   /* 0: keep exact 80-bit images for FLD/FSTP m80 pairs */
} box64env_t;
extern box64env_t xlr_box64env;
#define BOX64ENV(name) (xlr_box64env.name)
extern uint8_t box64_rdtsc_shift;
extern int box64_unittest_mode;

/* ---- logging -------------------------------------------------------------- */
#define LOG_NONE 0
#define LOG_INFO 1
#define LOG_DEBUG 2
#define LOG_DUMP 3
#define printf_log(level, ...) xlr_interp_log((level), __VA_ARGS__)
#define dynarec_log(level, ...) ((void)0)
int GetTID(void);

/* ---- process context ------------------------------------------------------ */
typedef struct base_segment_s {
    uintptr_t base;
    uint64_t limit;
    uint8_t present;
    uint8_t is32bits;
} base_segment_t;

typedef struct xlr_mutex_s { volatile long v; } xlr_mutex_t;
void xlr_mutex_lock(xlr_mutex_t* m);
void xlr_mutex_unlock(xlr_mutex_t* m);
#define pthread_mutex_lock(m) xlr_mutex_lock(m)
#define pthread_mutex_unlock(m) xlr_mutex_unlock(m)
#define mutex_lock(m) xlr_mutex_lock(m)
#define mutex_unlock(m) xlr_mutex_unlock(m)

typedef struct box64context_s {
    xlr_mutex_t mutex_lock;
    xlr_mutex_t mutex_trace;
    uint32_t mutex_16b;
    base_segment_t seggdt[16];
    void* dec;
    void* dec32;
    int trace_dynarec;
    int trace_tid;
    int x64trace;
} box64context_t;
extern box64context_t* my_context;

typedef struct xlr_jumpbuff_s JUMPBUFF;
typedef struct tlsdatasize_s tlsdatasize_t;
typedef struct cleanup_s cleanup_t;
#define MAX_SIGNAL 64

/* ---- signals / exceptions ------------------------------------------------- */
#define X64_SIGILL 4
#define X64_SIGTRAP 5
#define X64_SIGBUS 7
#define X64_SIGFPE 8
#define X64_SIGSEGV 11
void EmitSignal(x64emu_t* emu, int sig, void* addr, int code);
void EmitInterruption(x64emu_t* emu, int num, void* addr);
void CheckExec(x64emu_t* emu, uintptr_t addr);

int Run(x64emu_t* emu, int step);
int xlr_insn_check(x64emu_t* emu, uintptr_t addr);
#define XLR_INSN_HOOK(emu, addr) xlr_insn_check((emu), (addr))

void xlr_mxcsr_apply(x64emu_t* emu);

/* ---- CPU identity / time / misc ------------------------------------------ */
void my_cpuid(x64emu_t* emu);
uint64_t ReadTSC(x64emu_t* emu);
uint32_t helper_getcpu(x64emu_t* emu);
uint32_t get_random32(void);
uint64_t get_random64(void);
void EmuX64Syscall(void* emu);
void EmuX86Syscall(void* emu);
void EmuInt3(void* emu, void* addr);
void* EmuFork(void* emu, int forktype);
static inline void* getAlternate(void* addr) { return addr; }
int LeadingZeros16(uint16_t x);

/* box64 bridge/trace headers expect these names to exist. */
#define HAVE_TRACE_DISABLED 1

#endif /* XLR_BOX64_SHIM_H */
