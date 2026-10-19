/* Built only by the existing Windows native host. Winsock must precede windows.h
 * in this isolated translation unit; it never leaks into generated guest code. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include "network_host.h"

static SRWLOCK network_host_lock=SRWLOCK_INIT;
struct NetworkPrivateOwner { unsigned offline_policy; };
static void host_lock(void *context){(void)context;AcquireSRWLockExclusive(&network_host_lock);}
static void host_unlock(void *context){(void)context;ReleaseSRWLockExclusive(&network_host_lock);}
static int32_t host_startup(void *context)
{
    WSADATA data;(void)context;
    /* WSAStartup returns the failure code directly. WSAGetLastError is not
     * the error source for this API. No socket, receiver or NIC is created. */
    return (int32_t)WSAStartup(MAKEWORD(2,0),&data);
}
static void host_cleanup(void *context){(void)context;(void)WSACleanup();}
static void *host_create_owner(void *context)
{
    (void)context;struct NetworkPrivateOwner *owner=calloc(1,sizeof(*owner));
    if(owner)owner->offline_policy=1;return owner;
}
static void host_destroy_owner(void *context,void *owner){(void)context;free(owner);}
const PilotNetworkOps *pilot_network_host_operations(void)
{
    static const PilotNetworkOps ops={0,host_lock,host_unlock,host_startup,host_cleanup,
                                     host_create_owner,host_destroy_owner};
    return &ops;
}
