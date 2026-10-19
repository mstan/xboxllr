#ifndef PILOT_KERNEL_SHUTDOWN_H
#define PILOT_KERNEL_SHUTDOWN_H
#include <stdint.h>
enum { PILOT_SHUTDOWN_REGISTRATION_BYTES=16, PILOT_SHUTDOWN_LIMIT=256 };
/* nxdk HAL_SHUTDOWN_REGISTRATION x86 layout: routine, signed priority, links. */
typedef struct PilotShutdownRegistration {
    uint32_t address,routine;
    int32_t priority;
} PilotShutdownRegistration;
#endif
