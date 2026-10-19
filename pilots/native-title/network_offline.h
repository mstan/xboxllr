#ifndef PILOT_NETWORK_OFFLINE_H
#define PILOT_NETWORK_OFFLINE_H
#include <stdint.h>

/* Portable lifecycle core. Lock callbacks serialize all backend and owner
 * operations. No original CXo/CXn object or guest pointer is represented here. */
typedef struct PilotNetworkOps {
    void *context;
    void (*lock)(void *);
    void (*unlock)(void *);
    int32_t (*startup)(void *);
    void (*cleanup)(void *);
    void *(*create_owner)(void *);
    void (*destroy_owner)(void *, void *);
} PilotNetworkOps;
typedef struct PilotNetworkOffline {
    const PilotNetworkOps *ops;
    void *owner;
    uint32_t references;
} PilotNetworkOffline;
typedef struct PilotNetworkResult {
    uint32_t hresult;
    uint32_t last_error;
    unsigned set_last_error;
    unsigned guard;
} PilotNetworkResult;
enum { PILOT_NETWORK_GUARD_UNINITIALIZED=1, PILOT_NETWORK_GUARD_PARAMETER,
       PILOT_NETWORK_GUARD_REFCOUNT, PILOT_NETWORK_GUARD_STATE };
int pilot_network_offline_initialize(PilotNetworkOffline *, const PilotNetworkOps *);
PilotNetworkResult pilot_network_offline_startup(PilotNetworkOffline *, uint32_t);
PilotNetworkResult pilot_network_offline_cleanup(PilotNetworkOffline *);
#endif
