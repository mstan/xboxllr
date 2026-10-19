#ifndef PILOT_KERNEL_STARTUP_H
#define PILOT_KERNEL_STARTUP_H
#include <stdint.h>
/* A native execution-domain owner, not a host GDT or x86 fault emulator. */
typedef struct PilotKernelStartup {
    uint64_t initial, current, restricted;
    uint32_t init_start;
    int initialized;
} PilotKernelStartup;

static inline int pilot_kernel_startup_descriptor(uint64_t descriptor)
{
    /* Only base-zero, present ring0, accessed readable code, 32bit/4KiB.
     * Limit fields are the only admitted variation. */
    const uint64_t limit_mask=UINT64_C(0x000f00000000ffff);
    return (descriptor&~limit_mask)==UINT64_C(0x00c09b0000000000);
}
static inline uint64_t pilot_kernel_startup_limit_descriptor(uint32_t start)
{
    uint32_t pages=start>>12;
    return UINT64_C(0x00c09b0000000000) | (pages&0xffffu) |
           ((uint64_t)(pages&0xf0000u)<<32);
}
static inline uint32_t pilot_kernel_startup_limit(uint64_t descriptor)
{
    return (uint32_t)(((descriptor&0xffffu)|((descriptor>>32)&0xf0000u))<<12)|0xfffu;
}
static inline int pilot_kernel_startup_initialize(PilotKernelStartup *state,
    uint64_t initial,uint32_t init_start,unsigned kernel_build,int export_only)
{
    if(!state || state->initialized || !export_only || kernel_build!=5838 ||
       init_start<0x80010000u || !pilot_kernel_startup_descriptor(initial) ||
       pilot_kernel_startup_limit(initial)<init_start)return 0;
    state->initial=state->current=initial;
    state->restricted=pilot_kernel_startup_limit_descriptor(init_start);
    state->init_start=init_start;state->initialized=1;return 1;
}
static inline int pilot_kernel_startup_exchange(PilotKernelStartup *state,
    uint64_t requested,uint64_t *previous)
{
    if(!state || !state->initialized || !previous ||
       (requested!=state->initial && requested!=state->restricted))return 0;
    *previous=state->current;state->current=requested;return 1;
}
#endif
