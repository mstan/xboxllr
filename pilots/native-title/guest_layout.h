#pragma once
/* Native export backing is transport storage, outside RAM allocation and
 * device apertures. Keep it distinct from the E0000000..E3FFFFFF pool and the
 * E5000000/E6000000 original-kernel/clock transport stacks and E8000000
 * bounded guarded service frames. */
#define PILOT_KERNEL_DATA_BASE 0xe7000000u
#define PILOT_KERNEL_DATA_BYTES 0x6000u
