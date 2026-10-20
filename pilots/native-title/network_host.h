#ifndef PILOT_NETWORK_HOST_H
#define PILOT_NETWORK_HOST_H
#include "network_offline.h"
/* Deliberately no Winsock/Windows types: runtime.c already includes windows.h. */
const PilotNetworkOps *pilot_network_host_operations(void);
int pilot_network_host_entropy(void *,unsigned,unsigned char *);
enum {PILOT_NETWORK_ATTACHMENT_UNATTACHED=1};
typedef struct PilotNetworkLinkPolicy {uint32_t attachment;} PilotNetworkLinkPolicy;
int pilot_network_host_link(void *,uint32_t *);
#endif
