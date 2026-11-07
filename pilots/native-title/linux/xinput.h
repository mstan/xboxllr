/* SPDX-License-Identifier: GPL-3.0-or-later
 * Report vocabulary shared with the existing keyboard conversion. Physical
 * Linux controllers use input_sdl.c, with no XInput runtime dependency.
 */
#pragma once
#include "windows.h"
typedef struct {WORD wButtons;BYTE bLeftTrigger,bRightTrigger;SHORT sThumbLX,sThumbLY,sThumbRX,sThumbRY;} XINPUT_GAMEPAD;
#define XINPUT_GAMEPAD_A 0x1000u
#define XINPUT_GAMEPAD_B 0x2000u
#define XINPUT_GAMEPAD_X 0x4000u
#define XINPUT_GAMEPAD_Y 0x8000u
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x100u
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x200u
