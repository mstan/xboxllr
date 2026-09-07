#include "unicorn_metrics.h"

// uc_priv.h belongs to the exact Unicorn revision pinned by CMakeLists.txt. Keeping
// this version-specific inspection in a C bridge avoids exposing private QEMU/GLib
// types to the rest of xlle.
#include "uc_priv.h"

static uint64_t region_count(struct hook* h) {
    return h && h->hooked_regions ? (uint64_t)g_hash_table_size(h->hooked_regions)
                                  : 0;
}

void xlle_unicorn_hook_region_counts(void* engine, void* code_hook,
                                     void* block_hook,
                                     uint64_t* code_regions,
                                     uint64_t* count_regions,
                                     uint64_t* block_regions) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    struct hook* count =
        uc && uc->count_hook ? (struct hook*)uc->count_hook : NULL;
    *code_regions = region_count((struct hook*)code_hook);
    *count_regions = region_count(count);
    *block_regions = region_count((struct hook*)block_hook);
}

// ---- guest TSC -------------------------------------------------------------
// qemu/hw/i386/x86.c's definition is renamed out of the way by a per-file
// -Dcpu_get_tsc_x86_64=... in CMakeLists.txt, so this is the definition every RDTSC and
// TSC MSR read in the pinned Unicorn resolves to. The parameter is CPUX86State* on the
// caller's side; it is unused here and typed void* so this bridge needs no target headers.
uint64_t xlle_guest_tsc_ticks = 0;
uint64_t xlle_guest_tsc_reads = 0;
uint32_t xlle_guest_tsc_prefix = 0;
int xlle_guest_tsc_block_clock = 1;
int xlle_guest_tsc_deterministic = 1;  /* default: the emulator owns guest time */

uint64_t xlle_unused_host_cpu_get_tsc(void* env);  /* the renamed original */
uint64_t cpu_get_tsc_x86_64(void* env);

uint64_t cpu_get_tsc_x86_64(void* env) {
    xlle_guest_tsc_reads++;
    if (xlle_guest_tsc_deterministic)
        return xlle_guest_tsc_ticks + (xlle_guest_tsc_block_clock ? xlle_guest_tsc_prefix : 0);
    return xlle_unused_host_cpu_get_tsc(env);
}
