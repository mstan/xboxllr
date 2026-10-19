#ifndef PILOT_NETWORK_HOST_H
#define PILOT_NETWORK_HOST_H
#include "network_offline.h"
/* Deliberately no Winsock/Windows types: runtime.c already includes windows.h. */
const PilotNetworkOps *pilot_network_host_operations(void);
#endif
