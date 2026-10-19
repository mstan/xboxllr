#ifndef PILOT_KERNEL_RWLOCK_H
#define PILOT_KERNEL_RWLOCK_H
#include <stdint.h>
/* nxdk ERWLOCK x86 layout, independently checked against owned kernel exports.
 * Dispatcher header reserved bytes are not a separately modeled ABI receipt. */
enum { PILOT_ERWLOCK_BYTES=52, PILOT_ERWLOCK_MAX=256, PILOT_ERWLOCK_OWNERS=64 };
typedef struct PilotRwOwner { uint64_t thread; uint32_t readers; int waiting; } PilotRwOwner;
typedef struct PilotRwLock {
    uint32_t va;
    int32_t count;
    uint32_t writers_waiting, readers_waiting, readers_entered;
    uint64_t writer;
    HANDLE writer_event, reader_semaphore;
    PilotRwOwner owners[PILOT_ERWLOCK_OWNERS];
} PilotRwLock;
#endif
