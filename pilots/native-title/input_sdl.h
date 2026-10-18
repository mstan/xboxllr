#pragma once
#include <stdint.h>
/* Optional host transport. Guest XAPI/USB layouts remain in their existing
 * adapters. No SDL types cross this boundary; builds without SDL keep XInput. */
uint32_t pilot_input_sdl_start(const char *guid);
uint32_t pilot_input_sdl_state(unsigned port,uint8_t report[18]);
uint32_t pilot_input_sdl_caps(unsigned port,uint8_t report[18],uint8_t rumble[4]);
uint32_t pilot_input_sdl_rumble(unsigned port,uint16_t left,uint16_t right);
void pilot_input_sdl_shutdown(void);
