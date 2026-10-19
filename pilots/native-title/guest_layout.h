#pragma once
/* Native export backing is transport storage, outside RAM allocation and
 * device apertures. Keep it distinct from the E0000000..E3FFFFFF pool and the
 * E5000000/E6000000 original-kernel/clock transport stacks and E8000000
 * bounded guarded service frames. */
#define PILOT_KERNEL_DATA_BASE 0xe7000000u
#define PILOT_KERNEL_DATA_BYTES 0xa000u
/* Shared native kernel-owner storage. Signing keys retain page +0x6000;
 * the loader's image-name export retains page +0x7000. */
#define PILOT_KERNEL_SHUTDOWN_HEAD (PILOT_KERNEL_DATA_BASE+0x8000u)
#define PILOT_KERNEL_TICK_COUNT (PILOT_KERNEL_DATA_BASE+0x9000u)
