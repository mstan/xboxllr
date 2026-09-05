#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pinned-Unicorn diagnostic bridge. These are the live (start,length) entries
// retained for the xlle code hook, the xlle block hook, and Unicorn's internal
// instruction-count hook. Any of the hook handles may be NULL, in which case the
// corresponding count is reported as 0 (which is the truth: the hook is absent).
void xlle_unicorn_hook_region_counts(void* engine, void* code_hook,
                                     void* block_hook,
                                     uint64_t* code_regions,
                                     uint64_t* count_regions,
                                     uint64_t* block_regions);

// Guest time-stamp counter (beads-xbox.1.4.6). The pinned Unicorn resolves the guest's
// RDTSC to cpu_get_ticks() -> cpu_get_host_ticks() -> the HOST rdtsc
// (qemu/softmmu/cpus.c:55, qemu/include/qemu/timer.h), so guest behaviour depends on how
// fast the emulator happens to run. This bridge replaces that with a value the emulator
// owns. `xlle_guest_tsc_ticks` is published by the CPU floor at every translation-block
// boundary; `xlle_guest_tsc_reads` counts how often the guest actually asked, so the leak
// carries its own denominator.
extern uint64_t xlle_guest_tsc_ticks;
extern uint64_t xlle_guest_tsc_reads;
// 1 (default) = serve the published retired-instruction clock; 0 = pass through the host
// TSC, which is what every build before 2026-09-04 did. Kept only so a leg can be compared
// against an older binary; it makes the guest workload depend on how fast the host is.
extern int xlle_guest_tsc_deterministic;

#ifdef __cplusplus
}
#endif
