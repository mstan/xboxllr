#include "unicorn_metrics.h"

// uc_priv.h belongs to the exact Unicorn revision pinned by CMakeLists.txt. Keeping
// this version-specific inspection in a C bridge avoids exposing private QEMU/GLib
// types to the rest of xlle.
#include "uc_priv.h"

void xlle_unicorn_hook_region_counts(void* engine, void* code_hook,
                                     uint64_t* code_regions,
                                     uint64_t* count_regions) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    struct hook* code = (struct hook*)code_hook;
    struct hook* count =
        uc && uc->count_hook ? (struct hook*)uc->count_hook : NULL;
    *code_regions = code && code->hooked_regions
        ? (uint64_t)g_hash_table_size(code->hooked_regions) : 0;
    *count_regions = count && count->hooked_regions
        ? (uint64_t)g_hash_table_size(count->hooked_regions) : 0;
}
