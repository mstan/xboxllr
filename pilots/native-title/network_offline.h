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
/* Raw namespace status, not a guessed HRESULT. Only exact name/path absence
 * qualifies an empty view. The runtime owns mounted-handle confinement. */
typedef uint32_t (*PilotNetworkCacheProbe)(void *,unsigned,const char *);
typedef int (*PilotNetworkOutputClear)(void *,unsigned);
/* stage must preflight readable+writable output and snapshot without writes;
 * commit cannot fail or call guest code after successful staging. */
typedef int (*PilotNetworkRandomStage)(void *,unsigned,unsigned char *);
typedef void (*PilotNetworkRandomCommit)(void *,unsigned,const unsigned char *);
typedef int (*PilotNetworkEntropy)(void *,unsigned,unsigned char *);
enum { PILOT_NETWORK_GUARD_UNINITIALIZED=1, PILOT_NETWORK_GUARD_PARAMETER,
       PILOT_NETWORK_GUARD_REFCOUNT, PILOT_NETWORK_GUARD_STATE,
       PILOT_NETWORK_GUARD_OUTPUT, PILOT_NETWORK_GUARD_CACHE,
       PILOT_NETWORK_GUARD_RANDOM_SIZE, PILOT_NETWORK_GUARD_ENTROPY };
enum { PILOT_NETWORK_CACHED_UPDATE_BYTES=156 };
enum { PILOT_NETWORK_RANDOM_MAX_BYTES=64 };
int pilot_network_offline_initialize(PilotNetworkOffline *, const PilotNetworkOps *);
PilotNetworkResult pilot_network_offline_startup(PilotNetworkOffline *, uint32_t);
PilotNetworkResult pilot_network_offline_cleanup(PilotNetworkOffline *);
PilotNetworkResult pilot_network_offline_cached_update(PilotNetworkOffline *,PilotNetworkOutputClear,void *,
                                                      PilotNetworkCacheProbe,void *,unsigned,const char *);
PilotNetworkResult pilot_network_offline_random(PilotNetworkOffline *,uint32_t,PilotNetworkRandomStage,
    PilotNetworkRandomCommit,void *,PilotNetworkEntropy,void *);
#endif
