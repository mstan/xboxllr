/*
 * Call-boundary bridge qualification: synthetic "translated" C functions that
 * follow generated-code conventions (callee pops its return address and any
 * RET n bytes from the guest stack, registers in the g_* cells) mixed with
 * guest code the bridge interprets.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same view of the register ABI as the native-title runtime (MMX cells live
   in the generated-code section of the header). */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "../src/cpu/recomp_interp_bridge.h"

/* recomp_types.h register file and memory base (normally the runtime's). */
ptrdiff_t g_xbox_mem_offset;
int g_force_return;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp, g_fs_base;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_df, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;

static unsigned char* guest;
static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL %s (line %d)\n", what, __LINE__); ++fails; } } while (0)

static void put(uint32_t va, const unsigned char* bytes, size_t n) { memcpy(guest + va, bytes, n); }

/* ---- synthetic translated functions --------------------------------------- */
/* 0x20000: stdcall(arg) -> eax = arg * 3; RET 4 */
static void native_triple(void) { uint32_t arg = MEM32(g_esp + 4); g_eax = arg * 3; g_esp += 8; }
/* 0xFE000004: kernel-thunk style stdcall(a, b) -> eax = a - b; RET 8 */
static void native_kernel_sub(void) { g_eax = MEM32(g_esp + 4) - MEM32(g_esp + 8); g_esp += 12; }
/* 0x21000: a "translated" SEH-style helper that must never run natively. */
static int seh_native_ran;
static void native_seh_helper(void) { seh_native_ran = 1; g_esp += 4; }
/* 0x23000: translated function that itself hits an unresolved target and
   bridges it, as generated code's RECOMP_ICALL miss path does. */
static void native_nested(void)
{
    PUSH32(g_esp, 0x23010u);                       /* return address of the call site */
    CHECK(recomp_interp_bridge_call(0x12000, RECOMP_BRIDGE_ICALL), "nested bridge ran");
    g_eax += 1000;
    g_esp += 4;                                    /* own RET */
}

static recomp_bridge_func_t lookup(uint32_t va)
{
    switch (va) {
    case 0x20000: return native_triple;
    case 0x21000: return native_seh_helper;
    case 0x23000: return native_nested;
    case 0xFE000004: return native_kernel_sub;
    default: return NULL;
    }
}
static int interpret_only(uint32_t va) { return va == 0x21000; }
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]) { (void)sub; out[0] = leaf ? 0x683 : 2; out[1] = out[2] = out[3] = 0; }
static uint64_t rdtsc(void) { return 1; }

static jmp_buf fatal_jump;
static const char* fatal_reason;
static XlleX86Result fatal_result;
static void fatal(const char* reason, uint32_t va, const XlleX86Result* r)
{
    (void)va;
    fatal_reason = reason;
    fatal_result = *r;
    longjmp(fatal_jump, 1);
}

static unsigned first_entries;
static void first_entry(uint32_t va, unsigned kind, uint32_t return_address)
{
    (void)va; (void)kind; (void)return_address;
    ++first_entries;
}

static void reset_regs(void)
{
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = 0;
    g_esp = 0x80000;
    g_ebp = g_seh_ebp = 0x1234;
    g_fs_base = 0x3000;
    g_fp_control_word = 0x027F;
    g_fp_top = 0;
    g_df = 0;
}

int main(void)
{
    guest = calloc(1, 0x200000);
    g_xbox_mem_offset = (ptrdiff_t)guest;
    RecompInterpBridgeConfig cfg = { lookup, interpret_only, cpuid, rdtsc, fatal, first_entry, 0 };
    recomp_interp_bridge_configure(&cfg);

    /* 1. Interpreted stdcall function calling a translated function, then
          returning to its native caller with RET 4. */
    {
        const unsigned char f[] = {
            0x55,                         /* push ebp */
            0x89, 0xE5,                   /* mov ebp, esp */
            0x6A, 0x07,                   /* push 7 */
            0xE8, 0xF6, 0xFF, 0x00, 0x00, /* call 0x20000 (rel from 0x1000A) */
            0x03, 0x45, 0x08,             /* add eax, [ebp+8] */
            0x5D,                         /* pop ebp */
            0xC2, 0x04, 0x00 };           /* ret 4 */
        put(0x10000, f, sizeof f);
        reset_regs();
        PUSH32(g_esp, 5);                 /* argument */
        PUSH32(g_esp, 0x30000);           /* native call site's return address */
        CHECK(recomp_interp_bridge_call(0x10000, RECOMP_BRIDGE_ICALL), "bridge ran");
        CHECK(g_eax == 26, "interpreted + native result");
        CHECK(g_esp == 0x80000, "RET 4 cleaned argument and return address");
        CHECK(g_ebp == 0x1234 && g_seh_ebp == 0x1234, "caller frame published back");
    }
    /* 2. An interpret-only helper changes its caller's EBP (SEH prolog shape)
          and is never called natively. */
    {
        const unsigned char f[] = { 0xE8, 0xFB, 0xFF, 0x00, 0x00, /* call 0x21000 (from 0x11000) */
                                    0x89, 0xE8,                   /* mov eax, ebp */
                                    0xC3 };
        const unsigned char helper[] = { 0xBD, 0x42, 0x42, 0x00, 0x00, /* mov ebp, 0x4242 */
                                         0xC3 };
        put(0x11000, f, sizeof f);
        put(0x21000, helper, sizeof helper);
        reset_regs();
        PUSH32(g_esp, 0x30010);
        CHECK(recomp_interp_bridge_call(0x11000, RECOMP_BRIDGE_MISSING), "bridge ran");
        CHECK(!seh_native_ran, "interpret-only helper was not called natively");
        CHECK(g_eax == 0x4242 && g_ebp == 0x4242 && g_seh_ebp == 0x4242, "helper's EBP reached the caller");
    }
    /* 3. A bridged function that returns with ESP BELOW its entry (as
          __SEH_prolog does after allocating its caller's frame). */
    {
        const unsigned char f[] = { 0x59,             /* pop ecx (return address) */
                                    0x83, 0xEC, 0x08, /* sub esp, 8 */
                                    0x51,             /* push ecx */
                                    0xC3 };           /* ret */
        put(0x22000, f, sizeof f);
        reset_regs();
        PUSH32(g_esp, 0x30020);
        CHECK(recomp_interp_bridge_call(0x22000, RECOMP_BRIDGE_MISSING), "bridge ran");
        CHECK(g_esp == 0x80000 - 8, "returned with the frame it allocated");
    }
    /* 4. Interpreted -> translated -> bridged (nested) -> back, and a call
          through a kernel thunk. */
    {
        const unsigned char f[] = { 0xE8, 0xFB, 0xFF, 0x00, 0x00,       /* call 0x23000 (from 0x13000) */
                                    0x89, 0xC3,                         /* mov ebx, eax */
                                    0x6A, 0x02, 0x6A, 0x09,             /* push 2; push 9 */
                                    0xFF, 0x15, 0x00, 0x40, 0x01, 0x00, /* call [0x14000] */
                                    0x01, 0xD8,                         /* add eax, ebx */
                                    0xC3 };
        const unsigned char g[] = { 0xB8, 0x05, 0x00, 0x00, 0x00, 0xC3 }; /* 0x12000: mov eax,5; ret */
        put(0x13000, f, sizeof f);
        put(0x12000, g, sizeof g);
        uint32_t thunk = 0xFE000004u;
        memcpy(guest + 0x14000, &thunk, 4);
        reset_regs();
        PUSH32(g_esp, 0x30030);
        CHECK(recomp_interp_bridge_call(0x13000, RECOMP_BRIDGE_ICALL), "bridge ran");
        CHECK(g_eax == 1005 + 7, "nested bridge result plus kernel thunk result");
        CHECK(g_esp == 0x80000, "stack balanced across nested and thunk calls");
    }
    /* 5. A guest fault is fatal, with the faulting EIP. */
    {
        const unsigned char f[] = { 0x90, 0x0F, 0x0B };
        put(0x15000, f, sizeof f);
        reset_regs();
        PUSH32(g_esp, 0x30040);
        if (!setjmp(fatal_jump)) {
            recomp_interp_bridge_call(0x15000, RECOMP_BRIDGE_FAILED);
            CHECK(0, "fault must not return");
        } else {
            CHECK(fatal_result.stop == XLLE_X86_STOP_FAULT && fatal_result.fault == XLLE_X86_FAULT_UD &&
                  fatal_result.fault_eip == 0x15001, "UD2 reported at its address");
        }
    }
    /* 6. The heal manifest and coverage report name every bridged entry. */
    {
        RecompInterpBridgeStats st;
        char line[256];
        recomp_interp_bridge_stats(&st);
        CHECK(st.distinct == 6 && st.entries == 6 && st.native_calls == 3, "per-address accounting");
        CHECK(first_entries == 6, "one first-entry report per address");
        recomp_interp_bridge_coverage_line(line, sizeof line);
        CHECK(strstr(line, "NOT FULLY STATIC - 6 interpreted") != NULL, "coverage line");
        FILE* m = tmpfile();
        recomp_interp_bridge_write_manifest(m);
        rewind(m);
        char buf[4096] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, m);
        fclose(m);
        buf[n] = 0;
        CHECK(strstr(buf, "\"va\":\"0x00010000\",\"kind\":\"icall\",\"first_return\":\"0x00030000\"") != NULL &&
              strstr(buf, "\"va\":\"0x00012000\"") != NULL && strstr(buf, "\"kind\":\"failed\"") != NULL,
              "heal manifest entries");
    }
    /* 7. Strict mode never interprets. */
    {
        cfg.strict = 1;
        recomp_interp_bridge_configure(&cfg);
        reset_regs();
        PUSH32(g_esp, 0x30050);
        CHECK(!recomp_interp_bridge_call(0x10000, RECOMP_BRIDGE_ICALL) && g_esp == 0x80000 - 4, "strict declines");
    }
    printf("recomp interpreter bridge: %s\n", fails ? "FAIL" : "pass");
    return fails ? 1 : 0;
}
