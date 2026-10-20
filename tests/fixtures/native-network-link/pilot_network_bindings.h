/* Authored service fixture: no original SDK data. */
#pragma once
#define PILOT_NETWORK_ABI_VERSION 1u
#define PILOT_NETWORK_HAS_LINK 1
#define PILOT_NETWORK_LINK_ATTACHMENT 1u
enum {PILOT_NETWORK_ACTION_GUARD,PILOT_NETWORK_ACTION_STARTUP,PILOT_NETWORK_ACTION_CLEANUP,PILOT_NETWORK_ACTION_LINK};
enum {PILOT_NETWORK_TEST_STARTUP,PILOT_NETWORK_TEST_CLEANUP,PILOT_NETWORK_TEST_LINK,PILOT_NETWORK_COUNT};
static const struct {const char *name;unsigned address,action,stack_bytes;} pilot_network_calls[]={
    {"authored_startup",0x1000,PILOT_NETWORK_ACTION_STARTUP,4},
    {"authored_cleanup",0x1100,PILOT_NETWORK_ACTION_CLEANUP,0},
    {"authored_link",0x1200,PILOT_NETWORK_ACTION_LINK,0}
};
