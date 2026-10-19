#ifndef PILOT_OPTICAL_EMPTY_H
#define PILOT_OPTICAL_EMPTY_H
#include <stdint.h>
/* Explicit immutable virtual-board policy, not sampled physical hardware.
 * Original HalReadSMCTrayState masks raw00 with70, then normalizes it to10. */
#define PILOT_OPTICAL_EMPTY_RAW UINT32_C(0)
#define PILOT_OPTICAL_EMPTY_STATE UINT32_C(0x10)
#define PILOT_OPTICAL_EMPTY_CHANGES UINT32_C(0)
#endif
