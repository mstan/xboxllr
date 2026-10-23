/* Opt-in tester input boundaries. This records every actual host-controller
 * state/capability/rumble result, not just changes or wall-clock samples.
 * Native scheduling and clocks are NOT snapshotted; strict replay detects a
 * changed call order rather than silently delivering a different input.
 *
 * Capture never waits for disk after startup: a fixed 256 KiB queue feeds one
 * writer. A full queue or the 128 MiB file cap stops recording, not gameplay.
 * Abrupt termination can lose the pending tail (normally <=100 ms). */
#define INPUT_DIAGNOSTIC_QUEUE 4096u
#define INPUT_DIAGNOSTIC_MAX_RECORDS ((128u*1024u*1024u-16u)/64u)
static INIT_ONCE input_diagnostic_once=INIT_ONCE_STATIC_INIT;
static HANDLE input_diagnostic_file=INVALID_HANDLE_VALUE,input_diagnostic_event,input_diagnostic_worker;
static SRWLOCK input_diagnostic_lock=SRWLOCK_INIT;
static uint8_t input_diagnostic_queue[INPUT_DIAGNOSTIC_QUEUE][64];
static unsigned input_diagnostic_read,input_diagnostic_write;
static volatile LONG input_diagnostic_failed,input_diagnostic_done;
static DWORD input_diagnostic_worker_id;
static int input_diagnostic_record,input_diagnostic_replay;
static uint64_t input_diagnostic_sequence,input_diagnostic_started,input_diagnostic_previous_time;

static void input_diagnostic_u32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));}
static void input_diagnostic_u64(uint8_t *p,uint64_t value)
{for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(value>>(8*i));}
static uint32_t input_diagnostic_get32(const uint8_t *p)
{uint32_t value=0;for(unsigned i=0;i<4;++i)value|=(uint32_t)p[i]<<(8*i);return value;}
static uint64_t input_diagnostic_get64(const uint8_t *p)
{uint64_t value=0;for(unsigned i=0;i<8;++i)value|=(uint64_t)p[i]<<(8*i);return value;}
static void input_diagnostic_fail(const char *reason)
{
    if(!InterlockedCompareExchange(&input_diagnostic_failed,1,0)) {
        fprintf(stderr,"[DIAGNOSTIC INPUTS INCOMPLETE] %s; gameplay continues\n",reason);
        fflush(stderr);
    }
}
static DWORD WINAPI input_diagnostic_writer(LPVOID unused)
{
    (void)unused;
    uint8_t batch[64*256];
    for(;;) {
        WaitForSingleObject(input_diagnostic_event,100);
        for(;;) {
            unsigned count=0;int finished=0;
            AcquireSRWLockExclusive(&input_diagnostic_lock);
            while(input_diagnostic_read!=input_diagnostic_write && count<256) {
                memcpy(batch+64*count++,input_diagnostic_queue[input_diagnostic_read%INPUT_DIAGNOSTIC_QUEUE],64);
                ++input_diagnostic_read;
            }
            finished=InterlockedCompareExchange(&input_diagnostic_done,0,0) &&
                input_diagnostic_read==input_diagnostic_write;
            ReleaseSRWLockExclusive(&input_diagnostic_lock);
            DWORD written=0;
            if(count && (!WriteFile(input_diagnostic_file,batch,64*count,&written,NULL) || written!=64*count)) {
                input_diagnostic_fail("writer I/O failure");return 1;
            }
            if(finished) {
                if(!FlushFileBuffers(input_diagnostic_file)){input_diagnostic_fail("final file flush failed");return 1;}
                return 0;
            }
            if(!count)break;
        }
    }
}
static void input_diagnostic_finish(void)
{
    if(input_diagnostic_worker) {
        if(GetCurrentThreadId()==input_diagnostic_worker_id){input_diagnostic_fail("writer cannot drain itself");return;}
        InterlockedExchange(&input_diagnostic_done,1);SetEvent(input_diagnostic_event);
        /* Explicit terminal stops and normal CRT shutdown only, never an
         * asynchronous hardware exception observer or a writer callback. */
        DWORD waited=WaitForSingleObject(input_diagnostic_worker,2000);
        if(waited!=WAIT_OBJECT_0)
            input_diagnostic_fail("normal exit writer timeout");
        fprintf(stderr,"[DIAGNOSTIC INPUTS FINAL] records=%llu flushed=%u\n",
            (unsigned long long)input_diagnostic_sequence,waited==WAIT_OBJECT_0 && !input_diagnostic_failed?1u:0u);
        fflush(stderr);
    }
}
/* The title's intentional stop() uses ExitProcess, which skips CRT atexit.
 * No initializer runs here; a disabled diagnostic stop is just a null check. */
void pilot_input_diagnostic_flush(void) {input_diagnostic_finish();}
static BOOL CALLBACK input_diagnostic_initialize(PINIT_ONCE once,PVOID parameter,PVOID *context)
{
    (void)once;(void)parameter;(void)context;
    wchar_t record[4096],replay[4096];
    DWORD have_record=GetEnvironmentVariableW(L"XLLE_PILOT_DIAGNOSTIC_RECORD",record,4096);
    DWORD have_replay=GetEnvironmentVariableW(L"XLLE_PILOT_DIAGNOSTIC_REPLAY",replay,4096);
    if(!have_record && !have_replay)return TRUE;
    if(have_record>=4096 || have_replay>=4096 || (have_record && have_replay)) {
        fprintf(stderr,"[DIAGNOSTIC INPUT STOP] invalid capture/replay configuration\n");ExitProcess(3);
    }
    input_diagnostic_started=GetTickCount64();
    const uint8_t header[16]={'X','L','L','R','I','N','P','1',1,0,0,0,64,0,0,0};
    input_diagnostic_file=CreateFileW(have_record?record:replay,have_record?GENERIC_WRITE:GENERIC_READ,
        FILE_SHARE_READ,NULL,have_record?CREATE_NEW:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(input_diagnostic_file==INVALID_HANDLE_VALUE) {
        if(have_record){input_diagnostic_fail("exclusive file creation failed");return TRUE;}
        fprintf(stderr,"[DIAGNOSTIC INPUT STOP] replay file could not be opened\n");ExitProcess(3);
    }
    DWORD count=0;uint8_t actual[16];
    if(have_record) {
        if(!WriteFile(input_diagnostic_file,header,16,&count,NULL) || count!=16) {
            input_diagnostic_fail("header write failed");return TRUE;
        }
        input_diagnostic_event=CreateEventW(NULL,FALSE,FALSE,NULL);
        if(input_diagnostic_event)input_diagnostic_worker=CreateThread(NULL,0,input_diagnostic_writer,NULL,0,&input_diagnostic_worker_id);
        if(!input_diagnostic_worker){input_diagnostic_fail("writer could not start");return TRUE;}
        atexit(input_diagnostic_finish);input_diagnostic_record=1;
        fprintf(stderr,"[DIAGNOSTIC INPUTS] capture=1 queue_bytes=262144 limit_bytes=134217728 flush_ms=100\n");
    } else {
        LARGE_INTEGER size;
        if(!GetFileSizeEx(input_diagnostic_file,&size) || size.QuadPart<16 || size.QuadPart>128ll*1024*1024 ||
           (size.QuadPart-16)%64 || !ReadFile(input_diagnostic_file,actual,16,&count,NULL) || count!=16 || memcmp(actual,header,16)) {
            fprintf(stderr,"[DIAGNOSTIC INPUT STOP] invalid replay size/header\n");ExitProcess(3);
        }
        input_diagnostic_replay=1;
        fprintf(stderr,"[DIAGNOSTIC INPUTS] strict call-sequence replay; clocks/scheduler are not restored\n");
    }
    return TRUE;
}
static void input_diagnostic_publish(unsigned kind,unsigned port,uint32_t status,uint32_t argument,const uint8_t *payload,unsigned bytes)
{
    if(!input_diagnostic_record || input_diagnostic_failed || InterlockedCompareExchange(&input_diagnostic_done,0,0))return;
    uint8_t row[64]={0};
    input_diagnostic_u32(row+16,kind);input_diagnostic_u32(row+20,port);
    input_diagnostic_u32(row+24,status);input_diagnostic_u32(row+28,argument);
    if(payload && bytes)memcpy(row+32,payload,bytes);
    int full=0,budget=0,signal=0;
    AcquireSRWLockExclusive(&input_diagnostic_lock);
    if(InterlockedCompareExchange(&input_diagnostic_done,0,0)) {ReleaseSRWLockExclusive(&input_diagnostic_lock);return;}
    if(input_diagnostic_sequence>=INPUT_DIAGNOSTIC_MAX_RECORDS)budget=1;
    else if(input_diagnostic_write-input_diagnostic_read>=INPUT_DIAGNOSTIC_QUEUE)full=1;
    else {
        input_diagnostic_u64(row,input_diagnostic_sequence);
        input_diagnostic_u64(row+8,GetTickCount64()-input_diagnostic_started);
        memcpy(input_diagnostic_queue[input_diagnostic_write%INPUT_DIAGNOSTIC_QUEUE],row,64);
        ++input_diagnostic_write;++input_diagnostic_sequence;
        signal=input_diagnostic_write%256==0;
    }
    ReleaseSRWLockExclusive(&input_diagnostic_lock);
    if(budget)input_diagnostic_fail("128 MiB budget reached");
    else if(full)input_diagnostic_fail("bounded writer queue overflow");
    else if(signal)SetEvent(input_diagnostic_event);
}
static uint32_t input_diagnostic_deliver(unsigned kind,unsigned port,uint32_t argument,uint8_t *payload,unsigned bytes)
{
    uint8_t row[64];DWORD count=0;
    AcquireSRWLockExclusive(&input_diagnostic_lock);
    if(!ReadFile(input_diagnostic_file,row,64,&count,NULL)) {
        fprintf(stderr,"[DIAGNOSTIC INPUT STOP] replay read failure\n");ExitProcess(3);
    }
    if(!count) {
        fprintf(stderr,"[DIAGNOSTIC REPLAY EXHAUSTED] records=%llu; original outcome not established\n",(unsigned long long)input_diagnostic_sequence);
        fflush(stderr);ExitProcess(4);
    }
    int invalid=count!=64;
    if(!invalid) {
        invalid=input_diagnostic_get64(row)!=input_diagnostic_sequence ||
            input_diagnostic_get64(row+8)<input_diagnostic_previous_time ||
            input_diagnostic_get32(row+16)!=kind || input_diagnostic_get32(row+20)!=port || port>3 ||
            input_diagnostic_get32(row+28)!=argument;
        for(unsigned i=32+bytes;i<64;++i)if(row[i])invalid=1;
    }
    if(invalid) {
        fprintf(stderr,"[DIAGNOSTIC INPUT DIVERGENCE] sequence=%llu requested_kind=%u port=%u argument=%08x\n",
            (unsigned long long)input_diagnostic_sequence,kind,port,argument);
        fflush(stderr);ExitProcess(3);
    }
    input_diagnostic_previous_time=input_diagnostic_get64(row+8);++input_diagnostic_sequence;
    if(bytes)memcpy(payload,row+32,bytes);
    uint32_t status=input_diagnostic_get32(row+24);
    ReleaseSRWLockExclusive(&input_diagnostic_lock);return status;
}

uint32_t pilot_input_host_state(unsigned port,uint8_t gamepad[18])
{
    InitOnceExecuteOnce(&input_diagnostic_once,input_diagnostic_initialize,NULL,NULL);
    if(!input_diagnostic_record && !input_diagnostic_replay)return input_host_state_mapped(port,gamepad);
    uint8_t payload[18]={0};uint32_t status;
    if(input_diagnostic_replay)status=input_diagnostic_deliver(1,port,0,payload,18);
    else {status=input_host_state_mapped(port,payload);input_diagnostic_publish(1,port,status,0,payload,18);}
    if(!status)memcpy(gamepad,payload,18);
    return status;
}
uint32_t pilot_input_host_caps(unsigned port,uint8_t gamepad[18],uint8_t rumble[4])
{
    InitOnceExecuteOnce(&input_diagnostic_once,input_diagnostic_initialize,NULL,NULL);
    if(!input_diagnostic_record && !input_diagnostic_replay)return input_host_caps_raw(port,gamepad,rumble);
    uint8_t payload[22]={0};uint32_t status;
    if(input_diagnostic_replay)status=input_diagnostic_deliver(2,port,0,payload,22);
    else {status=input_host_caps_raw(port,payload,payload+18);input_diagnostic_publish(2,port,status,0,payload,22);}
    if(!status){memcpy(gamepad,payload,18);memcpy(rumble,payload+18,4);}return status;
}
uint32_t pilot_input_host_rumble(unsigned port,uint16_t left,uint16_t right)
{
    InitOnceExecuteOnce(&input_diagnostic_once,input_diagnostic_initialize,NULL,NULL);
    if(!input_diagnostic_record && !input_diagnostic_replay)return input_host_rumble_raw(port,left,right);
    uint32_t argument=(uint32_t)left|((uint32_t)right<<16);
    if(input_diagnostic_replay)return input_diagnostic_deliver(3,port,argument,NULL,0);
    uint32_t status=input_host_rumble_raw(port,left,right);
    input_diagnostic_publish(3,port,status,argument,NULL,0);return status;
}
