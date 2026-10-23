#pragma once
#include <stdint.h>

/* Existing admitted later-XDK reports suppress light analog-button pressure.
 * An exact original copy-only report proof explicitly selects zero instead.
 * This is report conversion, independent of optional camera/controller mods. */
#ifndef PILOT_INPUT_ANALOG_BUTTON_FLOOR
#define PILOT_INPUT_ANALOG_BUTTON_FLOOR 32
#endif
#if PILOT_INPUT_ANALOG_BUTTON_FLOOR != 0 && PILOT_INPUT_ANALOG_BUTTON_FLOOR != 32
#error Unsupported original analog-button report policy
#endif
static inline void pilot_input_report_filter(uint8_t report[18])
{
#if PILOT_INPUT_ANALOG_BUTTON_FLOOR != 0
    for(unsigned i=2;i<10;++i)
        if(report[i]<PILOT_INPUT_ANALOG_BUTTON_FLOOR)report[i]=0;
#else
    (void)report;
#endif
}
