#pragma once
#include <stdint.h>

/* Executes the owned original kernel routine and its callees. NULL is success;
 * failure never publishes a partial result to the native caller. */
const char *pilot_calendar_lle(unsigned ordinal, unsigned char *guest,
    uint32_t source, uint32_t destination, uint32_t *result);
