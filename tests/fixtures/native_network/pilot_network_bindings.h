#pragma once
#define PILOT_NETWORK_ABI_VERSION 1u
enum { PILOT_NETWORK_ACTION_GUARD, PILOT_NETWORK_ACTION_STARTUP, PILOT_NETWORK_ACTION_CLEANUP };
enum { PILOT_NETWORK_TEST_STARTUP, PILOT_NETWORK_TEST_CLEANUP, PILOT_NETWORK_TEST_GUARD, PILOT_NETWORK_COUNT };
static const struct { const char *name; unsigned address, action, stack_bytes; } pilot_network_calls[]={
    {"authored startup",0x1000,PILOT_NETWORK_ACTION_STARTUP,4},
    {"authored cleanup",0x1100,PILOT_NETWORK_ACTION_CLEANUP,0},
    {"authored unsupported",0x1200,PILOT_NETWORK_ACTION_GUARD,0}
};
