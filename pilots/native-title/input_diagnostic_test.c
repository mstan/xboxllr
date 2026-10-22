/* Authored host input samples; no title, original executable, controller or
 * screen/audio output. Modes run in separate processes for INIT_ONCE state. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile LONG raw_calls;
static unsigned failures;
static uint32_t input_host_state_raw(unsigned port,uint8_t pad[18])
{
    InterlockedIncrement(&raw_calls);if(port)return ERROR_DEVICE_NOT_CONNECTED;
    memset(pad,0,18);pad[2]=255;pad[10]=0x34;pad[11]=0x12;return ERROR_SUCCESS;
}
static uint32_t input_host_caps_raw(unsigned port,uint8_t pad[18],uint8_t rumble[4])
{
    (void)port;InterlockedIncrement(&raw_calls);memset(pad,0xff,18);memset(rumble,0xab,4);return ERROR_SUCCESS;
}
static uint32_t input_host_rumble_raw(unsigned port,uint16_t left,uint16_t right)
{(void)port;(void)left;(void)right;InterlockedIncrement(&raw_calls);return ERROR_NOT_SUPPORTED;}
#include "input_diagnostic.inc"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);++failures;}}while(0)
static DWORD WINAPI concurrent_inputs(LPVOID unused)
{
    (void)unused;uint8_t pad[18],rumble[4];
    for(unsigned i=0;i<200;++i) {
        if(pilot_input_host_state(0,pad) || pad[2]!=255)return 1;
        if(pilot_input_host_caps(0,pad,rumble) || rumble[3]!=0xab)return 1;
        if(pilot_input_host_rumble(0,0x56,0x1234)!=ERROR_NOT_SUPPORTED)return 1;
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=3)return 9;
    wchar_t path[4096];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[2],-1,path,4096))return 9;
    int fatal=!strcmp(argv[1],"fatal");
    int concurrent=!strcmp(argv[1],"concurrent");
    int capture=!strcmp(argv[1],"capture") || !strcmp(argv[1],"overflow") || fatal || concurrent;
    SetEnvironmentVariableW(capture?L"XLLE_PILOT_DIAGNOSTIC_RECORD":L"XLLE_PILOT_DIAGNOSTIC_REPLAY",path);
    uint8_t pad[18]={0},rumble[4]={0};
    if(!strcmp(argv[1],"diverge")) {
        pilot_input_host_caps(0,pad,rumble);return 8;
    }
    CHECK(!pilot_input_host_state(0,pad));CHECK(pad[2]==255 && pad[10]==0x34 && pad[11]==0x12);
    if(concurrent) {
        HANDLE workers[4];
        for(unsigned i=0;i<4;++i){workers[i]=CreateThread(NULL,0,concurrent_inputs,NULL,0,NULL);CHECK(workers[i]!=NULL);}
        CHECK(WaitForMultipleObjects(4,workers,TRUE,5000)==WAIT_OBJECT_0);
        for(unsigned i=0;i<4;++i) {DWORD result=9;CHECK(GetExitCodeThread(workers[i],&result) && !result);CloseHandle(workers[i]);}
        pilot_input_diagnostic_flush();CHECK(raw_calls==2401);return failures?1:0;
    }
    if(!strcmp(argv[1],"overflow")) {
        CHECK(SuspendThread(input_diagnostic_worker)!=0xffffffffu);
        for(unsigned i=0;i<INPUT_DIAGNOSTIC_QUEUE+10;++i)CHECK(!pilot_input_host_state(0,pad));
        CHECK(input_diagnostic_failed && raw_calls==(LONG)INPUT_DIAGNOSTIC_QUEUE+11);
        ResumeThread(input_diagnostic_worker);input_diagnostic_finish();return failures?1:0;
    }
    CHECK(!pilot_input_host_caps(0,pad,rumble));CHECK(pad[17]==255 && rumble[3]==0xab);
    CHECK(pilot_input_host_rumble(0,0x56,0x1234)==ERROR_NOT_SUPPORTED);
    memset(pad,0xcc,18);CHECK(pilot_input_host_state(3,pad)==ERROR_DEVICE_NOT_CONNECTED);
    for(unsigned i=0;i<18;++i)CHECK(pad[i]==0xcc);
    if(fatal) {
        CHECK(raw_calls==4);pilot_input_diagnostic_flush();
        fprintf(stderr,"[PILOT STOP] authored fixture stop at 00000000\n");fflush(stderr);ExitProcess(failures?1:2);
    }
    if(capture){CHECK(raw_calls==4);input_diagnostic_finish();return failures?1:0;}
    CHECK(raw_calls==0);if(failures)return 1;
    pilot_input_host_state(0,pad); /* An exhausted recording exits 4, never success. */
    return 8;
}
