/* Authored host input samples; no title, original executable, controller or
 * screen/audio output. Modes run in separate processes for INIT_ONCE state. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session_replay.h"
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
#define input_host_state_mapped input_host_state_raw
#include "input_diagnostic.inc"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);++failures;}}while(0)
static uint64_t fixture_cpu_100ns(void)
{
    FILETIME created,ended,kernel,user;
    CHECK(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user));
    return ((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
        ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
}
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
    if(!strcmp(argv[1],"bulk-timed")) {
        SetEnvironmentVariableA("XLLE_PILOT_DIAGNOSTIC_TIMED","1");
        for(unsigned block=0;block<256;++block) {
            if(block)input_timed_start=GetTickCount64()-(uint64_t)block*10000;
            CHECK(!pilot_input_host_state(0,pad));
            unsigned last=(block+1)*1024-1;
            CHECK(pad[0]==(uint8_t)last && pad[1]==(uint8_t)(last>>8) && pad[2]==255);
            CHECK(input_diagnostic_sequence==(uint64_t)(block+1)*1024);
            if(!block){printf("TIMED REPLAY STEP block=0 records=1024\n");fflush(stdout);}
        }
        CHECK(input_timed_eof && raw_calls==0);return failures?1:0;
    }
    if(!strcmp(argv[1],"bulk") || !strcmp(argv[1],"bulk-eof")) {
        LARGE_INTEGER frequency,begin,end,refill_begin={0},refill_end={0};IO_COUNTERS io_begin,io_end;
        uint64_t refill_ticks=0,refill_max=0;
        CHECK(QueryPerformanceFrequency(&frequency));CHECK(GetProcessIoCounters(GetCurrentProcess(),&io_begin));
        uint64_t cpu=fixture_cpu_100ns();CHECK(QueryPerformanceCounter(&begin));
        for(unsigned i=0;i<262144;++i) {
            if(!(i&1023))CHECK(QueryPerformanceCounter(&refill_begin));
            CHECK(!pilot_input_host_state(0,pad));
            if(!(i&1023)) {
                CHECK(QueryPerformanceCounter(&refill_end));
                uint64_t ticks=(uint64_t)(refill_end.QuadPart-refill_begin.QuadPart);
                refill_ticks+=ticks;if(ticks>refill_max)refill_max=ticks;
            }
            CHECK(pad[0]==(uint8_t)i && pad[1]==(uint8_t)(i>>8) && pad[2]==255 && pad[10]==0x34 && pad[11]==0x12);
        }
        CHECK(QueryPerformanceCounter(&end));cpu=fixture_cpu_100ns()-cpu;
        CHECK(GetProcessIoCounters(GetCurrentProcess(),&io_end));
        CHECK(raw_calls==0 && input_diagnostic_sequence==262144);
        printf("INPUT REPLAY BENCH records=262144 seconds=%.9f cpu_100ns=%llu read_operations=%llu read_bytes=%llu\n",
            (double)(end.QuadPart-begin.QuadPart)/(double)frequency.QuadPart,(unsigned long long)cpu,
            (unsigned long long)(io_end.ReadOperationCount-io_begin.ReadOperationCount),
            (unsigned long long)(io_end.ReadTransferCount-io_begin.ReadTransferCount));fflush(stdout);
        printf("INPUT REPLAY REFILLS positions=256 mean_seconds=%.9f max_seconds=%.9f\n",
            (double)refill_ticks/(256.0*(double)frequency.QuadPart),(double)refill_max/(double)frequency.QuadPart);fflush(stdout);
        if(!strcmp(argv[1],"bulk-eof"))pilot_input_host_state(0,pad);
        return failures?1:0;
    }
    if(!strcmp(argv[1],"timed")) {
        SetEnvironmentVariableA("XLLE_PILOT_DIAGNOSTIC_TIMED","1");
        CHECK(!pilot_input_host_caps(0,pad,rumble));
        for(unsigned i=0;i<100;++i){CHECK(!pilot_input_host_state(0,pad));CHECK(pad[2]==255 && !pad[3]);}
        input_timed_start-=150;CHECK(!pilot_input_host_state(0,pad));CHECK(!pad[2] && !pad[3]);
        CHECK(pilot_input_host_rumble(0,123,456)==ERROR_NOT_SUPPORTED);
        input_timed_start-=100;CHECK(!pilot_input_host_state(0,pad));CHECK(!pad[2] && pad[3]==255);
        input_timed_start-=100;CHECK(pilot_input_host_state(0,pad)==ERROR_DEVICE_NOT_CONNECTED);
        CHECK(input_timed_eof && raw_calls==0);return failures?1:0;
    }

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
