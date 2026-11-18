/*
 * Call-boundary bridge between generated (recompiled) guest code and the
 * guest i386 interpreter (include/xlle/x86_interp.h).
 *
 * Generated code reaches the bridge only where it would otherwise stop: an
 * indirect call/jump with no translated target, or a direct call to a
 * function discovery missed or translation failed.  The caller has already
 * pushed the guest return address, exactly as for a translated callee.  The
 * bridge loads the per-thread register file (recomp_types.h g_* globals) into
 * the interpreter, runs until the entry frame returns, calls translated
 * functions and kernel thunks natively whenever interpreted code reaches one,
 * and writes the register file back.
 *
 * Bridging is honest (recomp-template PRINCIPLES.md "The interpreter may
 * bridge -- but only honestly"): every bridged entry is recorded per address
 * in an always-on table, can be written out as a heal manifest for the next
 * code generation, and a strict configuration restores stop-on-miss.
 */
#ifndef RECOMP_INTERP_BRIDGE_H
#define RECOMP_INTERP_BRIDGE_H

#include <stdint.h>
#include <stdio.h>
#include "xlle/x86_interp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*recomp_bridge_func_t)(void);

enum {
    RECOMP_BRIDGE_ICALL = 1,     /* indirect call with no translated target */
    RECOMP_BRIDGE_ITAIL = 2,     /* indirect tail jump with no translated target */
    RECOMP_BRIDGE_MISSING = 3,   /* direct call to an undiscovered function */
    RECOMP_BRIDGE_FAILED = 4,    /* direct call to a function translation failed on */
    RECOMP_BRIDGE_FORCED = 5     /* qualification build: function deliberately left untranslated */
};

typedef struct RecompInterpBridgeConfig {
    /* Translated function (generated, manual or kernel thunk) at va, or NULL. */
    recomp_bridge_func_t (*lookup)(uint32_t va);
    /* Nonzero: always interpret va even if translated.  Used for helpers that
       exchange the caller's EBP (MSVC __SEH_prolog/__SEH_epilog), which a
       native body can only do through the g_seh_ebp side channel. */
    int (*interpret_only)(uint32_t va);
    void (*cpuid)(uint32_t leaf, uint32_t subleaf, uint32_t out[4]);
    uint64_t (*rdtsc)(void);
    /* Fatal stop for a fault the interpreter cannot deliver; must not return. */
    void (*fatal)(const char* reason, uint32_t va, const XlleX86Result* result);
    /* Optional: called once per newly bridged entry address (loud, per address). */
    void (*first_entry)(uint32_t va, unsigned kind, uint32_t return_address);
    /* Optional: called when an address's entry count reaches 2, 4, 8, ...
       with the cumulative instructions interpreted for it. */
    void (*progress)(uint32_t va, uint32_t entries, uint64_t instructions, uint64_t host_ns);
    /* Nonzero: never interpret; every miss keeps today's stop behaviour. */
    int strict;
} RecompInterpBridgeConfig;

void recomp_interp_bridge_configure(const RecompInterpBridgeConfig* config);

/* Execute guest function va for the current thread.  Returns 1 when it ran to
   its return (register file and guest stack updated as a translated callee
   would leave them), 0 when bridging is disabled (strict). */
int recomp_interp_bridge_call(uint32_t va, unsigned kind);

typedef struct RecompInterpBridgeStats {
    uint64_t entries;          /* bridge calls that ran */
    uint64_t instructions;     /* guest instructions interpreted */
    uint64_t native_calls;     /* translated functions called from interpreted code */
    uint64_t host_ns;          /* host time inside the bridge, excluding native callees */
    uint32_t distinct;         /* distinct bridged entry addresses */
    uint32_t dropped;          /* entry addresses beyond the table capacity */
} RecompInterpBridgeStats;

void recomp_interp_bridge_stats(RecompInterpBridgeStats* out);

/* One JSON object per bridged entry address (heal manifest). */
void recomp_interp_bridge_write_manifest(FILE* out);

/* "COVERAGE: FULLY STATIC" or "COVERAGE: NOT FULLY STATIC - N interpreted ...". */
void recomp_interp_bridge_coverage_line(char* buffer, size_t size);

#ifdef __cplusplus
}
#endif
#endif
