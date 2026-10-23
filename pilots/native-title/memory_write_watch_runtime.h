#ifndef PILOT_MEMORY_WRITE_WATCH_RUNTIME_H
#define PILOT_MEMORY_WRITE_WATCH_RUNTIME_H
#include "memory_write_watch.h"
#ifdef PILOT_MEMORY_WRITE_WATCH
#include "memory_write_watch_win32.h"
/* All host policy changes relinquish internal RO ownership first. Internal
 * residency checks see logical RW; original guest protection stays original. */
#define VirtualAlloc pilot_write_watch_virtual_alloc
#define VirtualFree pilot_write_watch_virtual_free
#define VirtualProtect pilot_write_watch_virtual_protect
#define VirtualQuery pilot_write_watch_virtual_query
#endif
#endif
