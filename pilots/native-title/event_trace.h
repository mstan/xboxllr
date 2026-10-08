#ifndef PILOT_EVENT_TRACE_H
#define PILOT_EVENT_TRACE_H
#include <windows.h>
#include <stdlib.h>
#include <string.h>
/* Startup diagnostic selection. Never serialize rendering, DPC delivery or
 * streaming on stderr unless event tracing was explicitly requested. Errors,
 * file opens and explicit capture/profile diagnostics remain independent. */
static BOOL CALLBACK pilot_event_trace_init(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)context;
    const char *value=getenv("XLLE_PILOT_EVENT_TRACE");
    *(int*)parameter=value && !strcmp(value,"1");return TRUE;
}
static int pilot_event_trace_enabled(void)
{
    static INIT_ONCE once=INIT_ONCE_STATIC_INIT;static int enabled;
    InitOnceExecuteOnce(&once,pilot_event_trace_init,&enabled,NULL);return enabled;
}
#endif
