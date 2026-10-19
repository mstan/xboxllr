#ifndef PILOT_KERNEL_CLOCK_H
#define PILOT_KERNEL_CLOCK_H
#include <stdint.h>
#define PILOT_CLOCK_100NS_HZ UINT32_C(10000000)
#define PILOT_CLOCK_PM_HZ UINT32_C(3375000)
#define PILOT_CLOCK_TSC_HZ UINT32_C(733333333)
typedef struct PilotKernelClock {uint64_t origin,latest;int initialized;} PilotKernelClock;
static inline int pilot_kernel_clock_initialize(PilotKernelClock *clock,uint64_t origin)
{
    if(!clock || clock->initialized)return 0;
    clock->origin=clock->latest=origin;clock->initialized=1;return 1;
}
static inline uint64_t pilot_kernel_clock_sample(PilotKernelClock *clock,uint64_t now)
{
    if(now>clock->latest)clock->latest=now;
    return clock->latest-clock->origin;
}
static inline int pilot_kernel_clock_scale(uint64_t elapsed,uint32_t frequency,uint64_t *result)
{
    if(!frequency || !result)return 0;
    uint64_t seconds=elapsed/PILOT_CLOCK_100NS_HZ;
    uint64_t fraction=(elapsed%PILOT_CLOCK_100NS_HZ)*frequency/PILOT_CLOCK_100NS_HZ;
    if(seconds>UINT64_MAX/frequency)return 0;
    uint64_t whole=seconds*frequency;
    if(whole>UINT64_MAX-fraction)return 0;
    *result=whole+fraction;return 1;
}
static inline uint64_t pilot_kernel_clock_next_tick(const PilotKernelClock *clock)
{
    uint64_t remaining=10000-(clock->latest-clock->origin)%10000;
    return clock->latest>UINT64_MAX-remaining?UINT64_MAX:clock->latest+remaining;
}
#endif
