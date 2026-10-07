#pragma once
#include <stdint.h>
/* Packed Xbox gamepad payload, excluding its USB report ID/size header.
 * Guest XAPI structures are serialized separately, never sizeof(host struct). */
uint32_t pilot_input_host_state(unsigned port, uint8_t gamepad[18]);
uint32_t pilot_input_host_caps(unsigned port, uint8_t gamepad[18], uint8_t rumble[4]);
uint32_t pilot_input_host_rumble(unsigned port, uint16_t left, uint16_t right);
