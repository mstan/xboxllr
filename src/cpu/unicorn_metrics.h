#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pinned-Unicorn diagnostic bridge. These are the live (start,length) entries
// retained for the xlle code hook and Unicorn's internal instruction-count hook.
void xlle_unicorn_hook_region_counts(void* engine, void* code_hook,
                                     uint64_t* code_regions,
                                     uint64_t* count_regions);

#ifdef __cplusplus
}
#endif
