#ifndef PILOT_GRAPHICS_COMMANDS_H
#define PILOT_GRAPHICS_COMMANDS_H
#include <stdint.h>

/* Linear, host-consumed Kelvin commands written by original inline XDK code.
 * Device addresses/layouts belong to the title profile, not this owner.
 * Callers serialize access and supply an immutable snapshot of pending words. */
enum PilotCommandError {
    PILOT_COMMAND_OK=0,PILOT_COMMAND_RANGE,PILOT_COMMAND_HEADER,
    PILOT_COMMAND_INCOMPLETE,PILOT_COMMAND_METHOD,PILOT_COMMAND_BACKEND,
    PILOT_COMMAND_FAULTED
};
struct PilotCommandArena {
    uint32_t base,bytes,consumed,margin,fault,detail;
    uint64_t methods,submissions;
};
/* Probe all methods before applying any. Apply failure latches the owner:
 * already submitted native work must never be silently replayed on a retry. */
typedef int (*PilotCommandMethod)(void *context,uint32_t method,uint32_t value,int apply);
int pilot_command_open(struct PilotCommandArena *state,uint32_t base,uint32_t bytes,uint32_t margin);
int pilot_command_flush(struct PilotCommandArena *state,const uint32_t *pending,uint32_t current,
                        PilotCommandMethod method,void *context);
int pilot_command_reserve(struct PilotCommandArena *state,const uint32_t *pending,uint32_t current,
                          uint32_t minimum,uint32_t window,PilotCommandMethod method,void *context,
                          uint32_t *next,uint32_t *limit);
#endif
