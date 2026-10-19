/* Authored header fixture for current subset and pre-profile legacy ABI. */
#pragma once
enum PilotInputEntry {
    PILOT_INPUT_XInitDevices,PILOT_INPUT_XGetDevices,PILOT_INPUT_XGetDeviceChanges,
    PILOT_INPUT_XInputOpen,PILOT_INPUT_XInputClose,PILOT_INPUT_XInputGetState,
#ifdef PILOT_INPUT_TEST_LEGACY
    PILOT_INPUT_XInputGetCapabilities,PILOT_INPUT_XInputSetState,
#endif
    PILOT_INPUT_COUNT
};
static const struct {const char *name;unsigned stack_bytes;} pilot_input_calls[]={
    {"XInitDevices",8},{"XGetDevices",4},{"XGetDeviceChanges",12},
    {"XInputOpen",16},{"XInputClose",4},{"XInputGetState",8},
#ifdef PILOT_INPUT_TEST_LEGACY
    {"XInputGetCapabilities",8},{"XInputSetState",8},
#endif
};
#define PILOT_INPUT_g_DeviceType_Gamepad 0x1000u
#define PILOT_INPUT_g_DeviceType_MU 0x1010u
#define PILOT_INPUT_g_DeviceType_IRDongle 0x1020u
#ifndef PILOT_INPUT_TEST_LEGACY
#define PILOT_INPUT_ABI_VERSION 1u
#define PILOT_INPUT_STATE_BYTES 22u
#define PILOT_INPUT_DEFAULT_POLLING_0 1u
#define PILOT_INPUT_DEFAULT_POLLING_1 8u
#define PILOT_INPUT_DEFAULT_POLLING_2 0u
#define PILOT_INPUT_DEFAULT_POLLING_3 0u
#define PILOT_INPUT_HAS_CAPABILITIES 0
#define PILOT_INPUT_HAS_FEEDBACK 0
#define PILOT_INPUT_HAS_ENUMERATION_STATUS 0
#define PILOT_INPUT_HAS_DEVICE_DESCRIPTION 0
#define PILOT_INPUT_ABSENT_CLASS_ABI_VERSION 1u
#define PILOT_INPUT_ABSENT_CLASS_COUNT 1u
static const struct {unsigned address;const char *name;} pilot_input_absent_classes[]={
    {0x1040u,"g_DeviceType_Keyboard"}
};
#endif
