/* Authored host topology, production input.inc; no controller/hardware reads. */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[65536],before[65536];
static uint32_t g_esp,g_eax;
static unsigned connected=1,host_calls,expected_guard,host_buttons;
static uint64_t test_now=100000;
typedef void (*recomp_func_t)(void);
#define MEM8(a) (*(uint8_t *)(memory+(uint32_t)(a)))
#define MEM16(a) (*(uint16_t *)(memory+(uint32_t)(a)))
#define MEM32(a) (*(uint32_t *)(memory+(uint32_t)(a)))
#define PUSH32(sp,v) do{(sp)-=4;MEM32(sp)=(v);}while(0)
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint32_t guard_esp,guard_eax;static unsigned guard_host_calls;
static void stop(const char *reason,uint32_t value)
{
    (void)value;
    if(expected_guard && !memcmp(before,memory,sizeof(memory)) && g_esp==guard_esp &&
        g_eax==guard_eax && host_calls==guard_host_calls){fprintf(stderr,"guard:%s\n",reason);ExitProcess(42);}
    fprintf(stderr,"unexpected/dirty guard:%s\n",reason);ExitProcess(43);
}
static uint64_t dispatch_monotonic(void){return test_now;}
uint32_t pilot_input_host_state(unsigned port,uint8_t state[18])
{
    ++host_calls;if(port || !connected)return ERROR_DEVICE_NOT_CONNECTED;
    memset(state,0,18);state[0]=(uint8_t)host_buttons;return 0;
}
uint32_t pilot_input_host_caps(unsigned port,uint8_t state[18],uint8_t rumble[4])
{
    if(port || !connected)return ERROR_DEVICE_NOT_CONNECTED;
    memset(state,0,18);memset(rumble,0,4);return 0;
}
uint32_t pilot_input_host_rumble(unsigned port,uint16_t left,uint16_t right)
{
    (void)left;(void)right;return port || !connected?ERROR_DEVICE_NOT_CONNECTED:0;
}
#include "input.inc"
static void error_callback(void){g_esp+=8;}
static uint32_t invoke(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    g_esp=0xf000;g_eax=0x12345678;MEM32(g_esp)=0x87654321;
    MEM32(g_esp+4)=a;MEM32(g_esp+8)=b;MEM32(g_esp+12)=c;MEM32(g_esp+16)=d;
    if(expected_guard){memcpy(before,memory,sizeof(memory));guard_esp=g_esp;guard_eax=g_eax;guard_host_calls=host_calls;}
    pilot_input_dispatch(entry,error_callback);
    CHECK(!expected_guard && g_esp==0xf004+pilot_input_calls[entry].stack_bytes && MEM32(0xf000)==0x87654321);
    return g_eax;
}
int main(int argc,char **argv)
{
    const uint32_t keyboard=0x1040,out=0x2004;
    if(argc>1 && !strcmp(argv[1],"init-mutation")) {
        MEM32(keyboard+4)=1;expected_guard=1;invoke(PILOT_INPUT_XInitDevices,0,0,0,0);return 1;
    }
    CHECK(!invoke(PILOT_INPUT_XInitDevices,0,0,0,0));
    CHECK(invoke(PILOT_INPUT_XGetDevices,PILOT_INPUT_g_DeviceType_Gamepad,0,0,0)==1);
    if(argc>1) {
        if(!strcmp(argv[1],"unknown")){expected_guard=1;invoke(PILOT_INPUT_XGetDevices,0x1080,0,0,0);}
        if(!strcmp(argv[1],"open")){expected_guard=1;invoke(PILOT_INPUT_XInputOpen,keyboard,0,0,0);}
        if(!strcmp(argv[1],"legacy")){expected_guard=1;invoke(PILOT_INPUT_XGetDevices,keyboard,0,0,0);}
        if(!strcmp(argv[1],"mutation")){MEM32(keyboard)=1;expected_guard=1;invoke(PILOT_INPUT_XGetDeviceChanges,keyboard,out,out+4,0);}
        if(!strcmp(argv[1],"previous")){MEM32(keyboard+8)=1;expected_guard=1;invoke(PILOT_INPUT_XGetDevices,keyboard,0,0,0);}
        if(!strcmp(argv[1],"range")){expected_guard=1;invoke(PILOT_INPUT_XGetDeviceChanges,keyboard,0xfffffffdu,out,0);}
        if(!strcmp(argv[1],"prealloc")) {
            /* This entry runs before initialization in a fresh child. */
            input_initialized=0;MEM32(0x3000)=keyboard;MEM32(0x3004)=1;
            expected_guard=1;invoke(PILOT_INPUT_XInitDevices,1,0x3000,0,0);
        }
#ifdef PILOT_INPUT_HAS_POLL
        if(!strcmp(argv[1],"poll-closed")) {
            uint32_t handle=invoke(PILOT_INPUT_XInputOpen,PILOT_INPUT_g_DeviceType_Gamepad,0,0,0);
            invoke(PILOT_INPUT_XInputClose,handle,0,0,0);expected_guard=1;invoke(PILOT_INPUT_XInputPoll,handle,0,0,0);
        }
#endif
        return 1;
    }
#if PILOT_INPUT_ABSENT_CLASS_COUNT > 0
    unsigned calls=host_calls;
    for(unsigned repeat=0;repeat<3;++repeat) {
        memset(memory+out-4,0xcd,16);
        CHECK(!invoke(PILOT_INPUT_XGetDevices,keyboard,0,0,0));
        CHECK(!invoke(PILOT_INPUT_XGetDeviceChanges,keyboard,out,out+4,0));
        CHECK(!MEM32(out) && !MEM32(out+4) && MEM32(out-4)==0xcdcdcdcd && MEM32(out+8)==0xcdcdcdcd);
        CHECK(!MEM32(keyboard) && !MEM32(keyboard+4) && !MEM32(keyboard+8) && host_calls==calls);
    }
    CHECK(!invoke(PILOT_INPUT_XGetDeviceChanges,keyboard,out,out,0) && !MEM32(out));
#endif
    /* Connected gamepad acknowledgements and disconnect/reinsert changes stay
     * on the original existing host path; absence queries cannot consume them. */
    connected=0;test_now+=3000000;
    CHECK(invoke(PILOT_INPUT_XGetDeviceChanges,PILOT_INPUT_g_DeviceType_Gamepad,out,out+4,0)==1);
    CHECK(!MEM32(out) && MEM32(out+4)==1);
    connected=1;test_now+=3000000;
    CHECK(invoke(PILOT_INPUT_XGetDeviceChanges,PILOT_INPUT_g_DeviceType_Gamepad,out,out+4,0)==1);
    CHECK(MEM32(out)==1 && !MEM32(out+4));
#ifdef PILOT_INPUT_HAS_POLL
    uint32_t handle=invoke(PILOT_INPUT_XInputOpen,PILOT_INPUT_g_DeviceType_Gamepad,0,0,0);
    CHECK(handle);unsigned sampled=host_calls;
    invoke(PILOT_INPUT_XInputGetState,handle,out,0,0);uint32_t packet=MEM32(out);
    host_buttons=0x10;test_now+=10000;
    CHECK(!invoke(PILOT_INPUT_XInputPoll,handle,0,0,0) && host_calls==sampled);
    test_now+=80000;
    CHECK(!invoke(PILOT_INPUT_XInputPoll,handle,0,0,0) && host_calls==sampled+1);
    CHECK(!invoke(PILOT_INPUT_XInputGetState,handle,out,0,0) && MEM32(out)==packet+1 && MEM8(out+4)==0x10);
    connected=0;test_now+=80000;
    CHECK(invoke(PILOT_INPUT_XInputPoll,handle,0,0,0)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(!invoke(PILOT_INPUT_XInputClose,handle,0,0,0));
#endif
    const char *negative[]={"unknown",
#if PILOT_INPUT_ABSENT_CLASS_COUNT > 0
        "open","mutation","previous","range","prealloc","init-mutation","poll-closed"
#else
        "legacy"
#endif
    };
    for(unsigned i=0;i<sizeof(negative)/sizeof(negative[0]);++i) {
        char command[4096];snprintf(command,sizeof(command),"\"%s\" %s",argv[0],negative[i]);
        STARTUPINFOA si={0};PROCESS_INFORMATION pi;si.cb=sizeof(si);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);DWORD code;
        CHECK(GetExitCodeProcess(pi.hProcess,&code) && code==42);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    }
    puts("PASS: exact absent queries/ABI/canaries, unsupported-state guards, legacy headers and connected gamepad changes");
    return 0;
}
