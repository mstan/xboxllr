/*
 * Call-boundary bridge: generated guest code <-> guest i386 interpreter.
 * See recomp_interp_bridge.h for the contract.
 */
#include "recomp_interp_bridge.h"

#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* Same view of the register ABI as the native-title runtime (MMX cells live
   in the generated-code section of the header). */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h>
#define BRIDGE_HOST_SSE 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static uint32_t bridge_cas32(volatile uint32_t* p, uint32_t expect, uint32_t value)
{
    return (uint32_t)_InterlockedCompareExchange((volatile long*)p, (long)value, (long)expect);
}
static void bridge_add64(volatile uint64_t* p, uint64_t v) { _InterlockedExchangeAdd64((volatile long long*)p, (long long)v); }
static void bridge_add32(volatile uint32_t* p, uint32_t v) { _InterlockedExchangeAdd((volatile long*)p, (long)v); }
#else
static uint32_t bridge_cas32(volatile uint32_t* p, uint32_t expect, uint32_t value)
{
    __atomic_compare_exchange_n(p, &expect, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expect;
}
static void bridge_add64(volatile uint64_t* p, uint64_t v) { __atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
static void bridge_add32(volatile uint32_t* p, uint32_t v) { __atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
#endif

typedef char bridge_xmm_is_16_bytes[sizeof(RecompXmm) == 16 ? 1 : -1];
typedef char bridge_mmx_is_8_bytes[sizeof(RecompMmx) == 8 ? 1 : -1];

/* ---- always-on per-address record (heal manifest source) ------------------ */
#define BRIDGE_SLOTS 4096u
typedef struct BridgeSlot {
    volatile uint32_t va;          /* 0 = empty; guest address 0 is never code */
    uint32_t kind;
    uint32_t first_return;         /* guest return address of the first entry */
    volatile uint32_t count;
    volatile uint64_t instructions;
    volatile uint64_t host_ns;
} BridgeSlot;

static BridgeSlot g_slots[BRIDGE_SLOTS];
static volatile uint32_t g_distinct, g_dropped;
static volatile uint64_t g_entries, g_instructions, g_native_calls, g_host_ns;

/* Monotonic host clock for interpreter cost accounting. */
static uint64_t bridge_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / frequency.QuadPart) * 1000000000u +
           (uint64_t)(now.QuadPart % frequency.QuadPart) * 1000000000u / (uint64_t)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}
static RecompInterpBridgeConfig g_config;
static int g_configured;

static BridgeSlot* bridge_record(uint32_t va, unsigned kind, uint32_t return_address, int* fresh)
{
    *fresh = 0;
    uint32_t h = (va * 2654435761u) >> 20;
    for (uint32_t probe = 0; probe < BRIDGE_SLOTS; ++probe) {
        BridgeSlot* s = &g_slots[(h + probe) & (BRIDGE_SLOTS - 1)];
        uint32_t seen = s->va;
        if (seen == va) {
            bridge_add32(&s->count, 1);
            return s;
        }
        if (seen == 0) {
            seen = bridge_cas32(&s->va, 0, va);
            if (seen == 0) {
                s->kind = kind;
                s->first_return = return_address;
                bridge_add32(&s->count, 1);
                bridge_add32(&g_distinct, 1);
                *fresh = 1;
                return s;
            }
            if (seen == va) {
                bridge_add32(&s->count, 1);
                return s;
            }
        }
    }
    bridge_add32(&g_dropped, 1);
    return NULL;
}

/* ---- register file transfer ------------------------------------------------ */
static uint32_t host_mxcsr(void)
{
#ifdef BRIDGE_HOST_SSE
    return (uint32_t)_mm_getcsr();
#else
    return 0x1F80u;
#endif
}

static void bridge_load(XlleX86State* s)
{
    const RecompXmm* xmm[8] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3, &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
    const RecompMmx* mm[8] = { &g_mm0, &g_mm1, &g_mm2, &g_mm3, &g_mm4, &g_mm5, &g_mm6, &g_mm7 };
    memset(s, 0, sizeof(*s));
    s->gpr[XLLE_X86_EAX] = g_eax;
    s->gpr[XLLE_X86_ECX] = g_ecx;
    s->gpr[XLLE_X86_EDX] = g_edx;
    s->gpr[XLLE_X86_EBX] = g_ebx;
    s->gpr[XLLE_X86_ESP] = g_esp;
    /* EBP is a C local in generated code; callers publish it before calls. */
    s->gpr[XLLE_X86_EBP] = g_ebp;
    s->gpr[XLLE_X86_ESI] = g_esi;
    s->gpr[XLLE_X86_EDI] = g_edi;
    /* Arithmetic flags are dead across a call boundary; DF is modelled. */
    s->eflags = 0x202u | (g_df ? 0x400u : 0u);
    s->fs_base = g_fs_base;
    memcpy(s->fp, g_fp_stack, sizeof(s->fp));
    s->fp_top = (uint32_t)g_fp_top & 7u;
    s->fpu_cw = g_fp_control_word;
    s->fpu_sw = (uint16_t)((g_fp_cc & 0x4700u) | ((s->fp_top & 7u) << 11));
    s->fpu_tag = 0xFF; /* the generated model has no empty x87 registers */
    for (int i = 0; i < 8; ++i) {
        memcpy(s->xmm[i], xmm[i], 16);
        s->mm[i] = mm[i]->q;
    }
    s->mxcsr = host_mxcsr();
}

static void bridge_store(const XlleX86State* s)
{
    RecompXmm* xmm[8] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3, &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
    RecompMmx* mm[8] = { &g_mm0, &g_mm1, &g_mm2, &g_mm3, &g_mm4, &g_mm5, &g_mm6, &g_mm7 };
    uint16_t cc = (uint16_t)(s->fpu_sw & 0x4700u);
    g_eax = s->gpr[XLLE_X86_EAX];
    g_ecx = s->gpr[XLLE_X86_ECX];
    g_edx = s->gpr[XLLE_X86_EDX];
    g_ebx = s->gpr[XLLE_X86_EBX];
    g_esp = s->gpr[XLLE_X86_ESP];
    g_esi = s->gpr[XLLE_X86_ESI];
    g_edi = s->gpr[XLLE_X86_EDI];
    /* Publish the frame as generated code does before calls and tail jumps:
       frameless callees and the SEH helpers inherit it from these cells. */
    g_ebp = s->gpr[XLLE_X86_EBP];
    g_seh_ebp = s->gpr[XLLE_X86_EBP];
    g_df = (s->eflags >> 10) & 1u;
    g_fs_base = s->fs_base;
    memcpy(g_fp_stack, s->fp, sizeof(s->fp));
    g_fp_top = (int)(s->fp_top & 7u);
    g_fp_control_word = s->fpu_cw;
    g_fp_cc = cc;
    g_fp_cmp = cc == 0x4500u ? 2 : cc == 0x0100u ? -1 : cc == 0x4000u ? 0 : 1;
    for (int i = 0; i < 8; ++i) {
        memcpy(xmm[i], s->xmm[i], 16);
        mm[i]->q = s->mm[i];
    }
}

/* ---- interpreter hooks ------------------------------------------------------ */
static int bridge_leave_at(void* user, uint32_t eip)
{
    (void)user;
    if (g_config.interpret_only && g_config.interpret_only(eip))
        return 0;
    return g_config.lookup(eip) != NULL;
}

static void bridge_cpuid(void* user, uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    (void)user;
    g_config.cpuid(leaf, subleaf, out);
}

static uint64_t bridge_rdtsc(void* user)
{
    (void)user;
    return g_config.rdtsc();
}

void recomp_interp_bridge_configure(const RecompInterpBridgeConfig* config)
{
    g_config = *config;
    g_configured = 1;
}

int recomp_interp_bridge_call(uint32_t va, unsigned kind)
{
    XlleX86State state;
    XlleX86Hooks hooks;
    uint32_t entry_esp, entry_mxcsr, return_address;
    BridgeSlot* slot;
    uint64_t started;

    if (!g_configured || g_config.strict)
        return 0;
    started = bridge_now_ns();
    bridge_load(&state);
    entry_esp = state.gpr[XLLE_X86_ESP];
    entry_mxcsr = state.mxcsr;
    return_address = MEM32(entry_esp);
    {
        int fresh;
        slot = bridge_record(va, kind, return_address, &fresh);
        if (fresh && g_config.first_entry)
            g_config.first_entry(va, kind, return_address);
    }
    bridge_add64(&g_entries, 1);

    hooks.user = NULL;
    hooks.leave_at = bridge_leave_at;
    hooks.cpuid = bridge_cpuid;
    hooks.rdtsc = bridge_rdtsc;
    state.eip = va;

    for (;;) {
        /* The bridged function has returned when control reaches the return
           address its native caller pushed.  Interpreted code can only reach
           a native body's interior that way: calls and jumps to translated
           entries leave the interpreter.  (ESP is not a reliable signal:
           __SEH_prolog returns with ESP below its entry, having allocated its
           caller's frame.) */
        XlleX86Result r = xlle_x86_run(&state, &hooks, (uintptr_t)g_xbox_mem_offset, return_address, 0xFFFFFFFFu, 0);
        bridge_add64(&g_instructions, r.retired);
        if (slot)
            bridge_add64(&slot->instructions, r.retired);
        if (r.stop == XLLE_X86_STOP_RETURNED) {
            if ((state.mxcsr ^ entry_mxcsr) & 0xFFC0u)
                g_config.fatal("interpreted guest changed MXCSR controls (native profile admits no guest MXCSR writes)",
                               va, &r);
            bridge_store(&state);
            {
                uint64_t spent = bridge_now_ns() - started;
                bridge_add64(&g_host_ns, spent);
                if (slot) bridge_add64(&slot->host_ns, spent);
            }
            if (slot && g_config.progress) {
                uint32_t n = slot->count;
                if (n >= 2 && !(n & (n - 1)))
                    g_config.progress(va, n, slot->instructions, slot->host_ns);
            }
            return 1;
        }
        if (r.stop == XLLE_X86_STOP_LEAVE) {
            /* Interpreted code reached a translated function or kernel thunk
               by CALL or JMP; either way the top of stack is the address the
               callee returns to, and the callee pops it. */
            recomp_bridge_func_t fn = g_config.lookup(state.eip);
            uint32_t resume = MEM32(state.gpr[XLLE_X86_ESP]);
            uint32_t ebp = state.gpr[XLLE_X86_EBP];
            uint32_t target = state.eip;
            if ((state.mxcsr ^ entry_mxcsr) & 0xFFC0u)
                g_config.fatal("interpreted guest changed MXCSR controls (native profile admits no guest MXCSR writes)",
                               target, &r);
            bridge_store(&state);
            {
                /* Native callee time is not interpreter cost. */
                uint64_t before = bridge_now_ns();
                fn();
                started += bridge_now_ns() - before;
            }
            bridge_add64(&g_native_calls, 1);
            bridge_load(&state);
            state.gpr[XLLE_X86_EBP] = ebp; /* callee-saved; the C local is the caller's */
            state.eip = resume;
            continue;
        }
        g_config.fatal(r.stop == XLLE_X86_STOP_FAULT ? "interpreted guest fault" : "interpreter stopped unexpectedly",
                       r.fault_eip ? r.fault_eip : state.eip, &r);
        return 0;
    }
}

void recomp_interp_bridge_stats(RecompInterpBridgeStats* out)
{
    out->entries = g_entries;
    out->instructions = g_instructions;
    out->native_calls = g_native_calls;
    out->host_ns = g_host_ns;
    out->distinct = g_distinct;
    out->dropped = g_dropped;
}

void recomp_interp_bridge_write_manifest(FILE* out)
{
    static const char* kinds[] = { "unknown", "icall", "itail", "missing", "failed", "forced" };
    for (uint32_t i = 0; i < BRIDGE_SLOTS; ++i) {
        const BridgeSlot* s = &g_slots[i];
        if (!s->va)
            continue;
        fprintf(out,
                "{\"va\":\"0x%08X\",\"kind\":\"%s\",\"first_return\":\"0x%08X\",\"entries\":%u,\"instructions\":%llu,\"host_ns\":%llu,\"core\":\"%s\"}\n",
                s->va, kinds[s->kind < 6 ? s->kind : 0], s->first_return, s->count,
                (unsigned long long)s->instructions, (unsigned long long)s->host_ns, xlle_x86_interp_identity());
    }
}

void recomp_interp_bridge_coverage_line(char* buffer, size_t size)
{
    if (!g_distinct && !g_dropped)
        snprintf(buffer, size, "COVERAGE: FULLY STATIC (no interpreted entries)");
    else
        snprintf(buffer, size,
                 "COVERAGE: NOT FULLY STATIC - %u interpreted entry addresses%s, %llu entries, %llu instructions, %llu native calls from interpreted code, %.3f ms interpreting",
                 g_distinct + g_dropped, g_dropped ? " (table full)" : "", (unsigned long long)g_entries,
                 (unsigned long long)g_instructions, (unsigned long long)g_native_calls, (double)g_host_ns / 1e6);
}
