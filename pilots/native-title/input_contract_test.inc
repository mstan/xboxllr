/* Test-only host API provider. Production links Windows XInput directly.
 * Exercise the actual donor-derived conversion as well as the guest ABI. */
#include <xinput.h>
static XINPUT_STATE input_test_states[4];
static XINPUT_CAPABILITIES input_test_caps[4];
static DWORD input_test_presence[4]={0,1167,1167,1167};
static DWORD input_test_caps_status,input_test_rumble_status;
static unsigned input_test_state_calls,input_test_rumble_calls;
static XINPUT_VIBRATION input_test_vibration;
static DWORD WINAPI input_test_get_state(DWORD port,XINPUT_STATE *state)
{
    CHECK(port<4); ++input_test_state_calls;
    if (input_test_presence[port]) return input_test_presence[port];
    *state=input_test_states[port]; return 0;
}
static DWORD WINAPI input_test_get_caps(DWORD port,DWORD flags,XINPUT_CAPABILITIES *caps)
{
    CHECK(port<4 && flags==0);
    if (input_test_caps_status) return input_test_caps_status;
    if (input_test_presence[port]) return input_test_presence[port];
    *caps=input_test_caps[port]; return 0;
}
static DWORD WINAPI input_test_set_state(DWORD port,XINPUT_VIBRATION *vibration)
{
    CHECK(port<4); ++input_test_rumble_calls; input_test_vibration=*vibration;
    return input_test_presence[port] ? input_test_presence[port] : input_test_rumble_status;
}
#define XInputGetState input_test_get_state
#define XInputGetCapabilities input_test_get_caps
#define XInputSetState input_test_set_state
/* A failed host API must not admit a marker even if its error slot is zero.
 * Normal calls retain actual Windows file semantics; CloseHandle fault control
 * closes the owned test handle first, so rejection does not leak it. */
static unsigned input_test_marker_api_failure;
static BOOL WINAPI input_test_marker_size(HANDLE file,PLARGE_INTEGER size)
{
    if(input_test_marker_api_failure==1){SetLastError(0);return FALSE;}
    return GetFileSizeEx(file,size);
}
static BOOL WINAPI input_test_marker_read(HANDLE file,LPVOID buffer,DWORD bytes,LPDWORD got,LPOVERLAPPED overlap)
{
    if(input_test_marker_api_failure==2){SetLastError(0);return FALSE;}
    return ReadFile(file,buffer,bytes,got,overlap);
}
static BOOL WINAPI input_test_marker_close(HANDLE file)
{
    const BOOL result=CloseHandle(file);
    if(input_test_marker_api_failure==3){CHECK(result);SetLastError(0);return FALSE;}
    return result;
}
#define GetFileSizeEx input_test_marker_size
#define ReadFile input_test_marker_read
#define CloseHandle input_test_marker_close
#include "input_host.c"
#undef XInputGetState
#undef XInputGetCapabilities
#undef XInputSetState
#undef GetFileSizeEx
#undef ReadFile
#undef CloseHandle

static uint32_t input_test_last_error;
static void input_test_trigger_file(const char *path,const char *data,DWORD bytes)
{
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;CHECK(WriteFile(file,data,bytes,&written,NULL) && written==bytes);
    CHECK(CloseHandle(file));
}
static uint32_t input_test_trigger_poll(uint64_t now,uint8_t pad[18])
{
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    CHECK(!pilot_input_host_reference_time(now));
    return pilot_input_host_state(0,pad);
#else
    /* Same source poll with an explicitly authored clock value. The separate
     * reference-clock selection below exercises publication + actual host API. */
    return input_replay_poll(now,pad);
#endif
}
static void input_test_trigger_failure(uint64_t now,uint32_t expected)
{
    uint8_t pad[18];memset(pad,0xcc,sizeof(pad));
    const ULONGLONG start=input_replay_start;const int clock=input_replay_clock_started,reported=input_replay_reported;
    const struct InputReplayTrigger before=input_replay_trigger;
    const struct InputReplayTrigger before2=input_replay_trigger_2;
    const unsigned generation=input_replay_generation;
    CHECK(input_test_trigger_poll(now,pad)==expected);
    for(unsigned i=0;i<18;++i)CHECK(pad[i]==0xcc);
    CHECK(input_replay_start==start && input_replay_clock_started==clock && input_replay_reported==reported);
    CHECK(input_replay_trigger.enabled==before.enabled && input_replay_trigger.consumed==before.consumed &&
          !strcmp(input_replay_trigger.path,before.path));
    CHECK(!memcmp(&input_replay_trigger_2,&before2,sizeof(before2)) && input_replay_generation==generation);
}
static void test_input_replay_trigger(void)
{
    input_host_is_keyboard();input_host_keyboard=0;
    FILE *file=tmpfile();CHECK(file);
    CHECK(fputs("0 000000000000000000000000000000000000\n20 100000000000000000000000000000000000\n220 000000000000000000000000000000000000\n",file)>=0);
    rewind(file);CHECK(input_replay_load(file) && !fclose(file));
    char directory[MAX_PATH],path[MAX_PATH];
    DWORD length=GetTempPathA(sizeof(directory),directory);CHECK(length && length<sizeof(directory));
    if(!length || length>=sizeof(directory))return;
    UINT temporary=GetTempFileNameA(directory,"irp",0,path);CHECK(temporary);
    if(!temporary)return;
    CHECK(DeleteFileA(path));
    CHECK(input_replay_trigger_configure("relative-trigger")==ERROR_INVALID_PARAMETER);
    CHECK(!input_replay_trigger.enabled && !input_replay_clock_started && input_replay_reported==-1);
    CHECK(!input_replay_trigger_configure(path));
    uint64_t base=1000;
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    base+=input_host_reference_ms;
#endif
    uint8_t pad[18];
    for(unsigned n=0;n<3;++n) {
        memset(pad,0xcc,sizeof(pad));CHECK(!input_test_trigger_poll(base+n*3000,pad));
        for(unsigned i=0;i<18;++i)CHECK(!pad[i]);
        CHECK(!input_replay_clock_started && !input_replay_trigger.consumed && input_replay_reported==-1);
    }
    input_test_trigger_file(path,"start-v1\r\n",10);
    input_test_trigger_failure(base+7000,ERROR_INVALID_DATA);CHECK(DeleteFileA(path));
    input_test_trigger_file(path,"start-v2\n",9);
    input_test_trigger_failure(base+7100,ERROR_INVALID_DATA);CHECK(DeleteFileA(path));
    HANDLE locked=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(locked!=INVALID_HANDLE_VALUE);
    input_test_trigger_failure(base+7200,ERROR_SHARING_VIOLATION);
    CHECK(CloseHandle(locked));CHECK(DeleteFileA(path));
    CHECK(CreateDirectoryA(path,NULL));input_test_trigger_failure(base+7300,ERROR_ACCESS_DENIED);
    CHECK(RemoveDirectoryA(path));
    input_test_trigger_file(path,"start-v1\n",9);
    for(unsigned api=1;api<=3;++api) {
        input_test_marker_api_failure=api;
        input_test_trigger_failure(base+7400+api,ERROR_ACCESS_DENIED);
    }
    input_test_marker_api_failure=0;
    const ULONGLONG old_start=input_replay_start;
    CHECK(input_replay_trigger_configure(path)==ERROR_ALREADY_EXISTS);
    CHECK(input_replay_trigger.enabled && !input_replay_trigger.consumed &&
          !input_replay_clock_started && input_replay_start==old_start && input_replay_reported==-1);
    /* A completed marker may still have a rename handle open. Namespace access
     * cannot modify its bytes; a content writer must still reject unchanged. */
    HANDLE writer=CreateFileA(path,GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(writer!=INVALID_HANDLE_VALUE);
    input_test_trigger_failure(base+7500,ERROR_SHARING_VIOLATION);
    CHECK(CloseHandle(writer));
    char staged[MAX_PATH];
    CHECK(GetTempFileNameA(directory,"ipb",0,staged) && DeleteFileA(staged));
    CHECK(MoveFileExA(path,staged,0));
    HANDLE publisher=CreateFileA(staged,DELETE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(publisher!=INVALID_HANDLE_VALUE && MoveFileExA(staged,path,0));
    HANDLE old_reader=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,
                                OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(old_reader==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
    if(old_reader!=INVALID_HANDLE_VALUE)CHECK(CloseHandle(old_reader));
    CHECK(!input_test_trigger_poll(base+8000,pad) && !pad[0]);
    CHECK(input_replay_trigger.consumed && input_replay_clock_started && input_replay_start==base+8000 && input_replay_reported==0);
    CHECK(CloseHandle(publisher));
    CHECK(DeleteFileA(path)); // consumption is sticky, not the file's lifetime
    CHECK(!input_test_trigger_poll(base+8019,pad) && !pad[0]);
    CHECK(!input_test_trigger_poll(base+8020,pad) && pad[0]==0x10 && input_replay_reported==1);
    input_test_trigger_file(path,"invalid",7); // never probe a consumed trigger again
    CHECK(!input_test_trigger_poll(base+8219,pad) && pad[0]==0x10);
    CHECK(!input_test_trigger_poll(base+8220,pad) && !pad[0] && input_replay_reported==2);
    CHECK(DeleteFileA(path));
    /* Configuration rejects both a disconnected row0 and any nonneutral axis
     * or button before arming, leaving the old trigger/epoch untouched. */
    for(unsigned kind=0;kind<2;++kind) {
        file=tmpfile();CHECK(file);
        CHECK(fputs(kind?"0 000000000000000000000100000000000000\n":"0 disconnected\n",file)>=0);
        rewind(file);CHECK(input_replay_load(file) && !fclose(file));
        CHECK(input_replay_trigger_configure(path)==(kind?ERROR_INVALID_DATA:ERROR_INVALID_PARAMETER));
        CHECK(!input_replay_trigger.enabled && !input_replay_clock_started && input_replay_reported==-1);
    }
    input_replay_count=0;
    puts("input replay trigger: neutral wait, selected-clock epoch, ordered press/release, sticky consumption and nonmutating invalid/stale/inaccessible failures passed");
}
static void input_test_load_start_replay(void)
{
    FILE *file=tmpfile();CHECK(file);
    CHECK(fputs("0 000000000000000000000000000000000000\n20 100000000000000000000000000000000000\n220 000000000000000000000000000000000000\n",file)>=0);
    rewind(file);CHECK(input_replay_load(file) && !fclose(file));
}
static void test_input_replay_two_triggers(void)
{
    input_host_is_keyboard();input_host_keyboard=0;input_test_load_start_replay();
    char directory[MAX_PATH],first[MAX_PATH],second[MAX_PATH],alias[MAX_PATH];
    DWORD length=GetTempPathA(sizeof(directory),directory);CHECK(length && length<sizeof(directory));
    CHECK(GetTempFileNameA(directory,"ir1",0,first) && DeleteFileA(first));
    CHECK(GetTempFileNameA(directory,"ir2",0,second) && DeleteFileA(second));
    CHECK(input_replay_triggers_configure(NULL,second)==ERROR_INVALID_PARAMETER);
    CHECK(input_replay_triggers_configure(first,"relative-second")==ERROR_INVALID_PARAMETER);
    CHECK(input_replay_triggers_configure(first,first)==ERROR_INVALID_PARAMETER);
    strcpy_s(alias,sizeof(alias),first);
    for(char *at=alias;*at;++at) {
        if(*at=='\\')*at='/';
        else if(*at>='a' && *at<='z')*at-=('a'-'A');
    }
    CHECK(input_replay_triggers_configure(first,alias)==ERROR_INVALID_PARAMETER);
    CHECK(!input_replay_trigger.enabled && !input_replay_trigger_2.enabled);
    CHECK(!input_replay_trigger_configure(first));
    input_test_trigger_file(second,"start-v1\n",9);
    CHECK(input_replay_triggers_configure(first,second)==ERROR_ALREADY_EXISTS);
    CHECK(input_replay_trigger.enabled && !input_replay_trigger_2.enabled && !input_replay_clock_started);
    CHECK(DeleteFileA(second));
    /* A replay with a final held button or disconnect cannot arm a second run. */
    for(unsigned kind=0;kind<2;++kind) {
        FILE *file=tmpfile();CHECK(file);
        CHECK(fputs(kind?"0 000000000000000000000000000000000000\n20 disconnected\n":
            "0 000000000000000000000000000000000000\n20 100000000000000000000000000000000000\n",file)>=0);
        rewind(file);CHECK(input_replay_load(file) && !fclose(file));
        CHECK(input_replay_triggers_configure(first,second)==ERROR_INVALID_PARAMETER);
        CHECK(!input_replay_trigger.enabled && !input_replay_trigger_2.enabled);
    }
    input_test_load_start_replay();CHECK(!input_replay_triggers_configure(first,second));
    uint64_t base=1000;
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    base+=input_host_reference_ms;
#endif
    uint8_t pad[18];CHECK(!input_test_trigger_poll(base,pad) && !pad[0]);
    CHECK(!input_replay_clock_started && !input_replay_generation);
    input_test_trigger_file(first,"start-v1\n",9);
    CHECK(!input_test_trigger_poll(base+10,pad) && !pad[0] && input_replay_generation==1);
    input_test_trigger_file(second,"invalid",7);
    CHECK(!input_test_trigger_poll(base+30,pad) && pad[0]==0x10);
    CHECK(!input_test_trigger_poll(base+229,pad) && pad[0]==0x10);
    CHECK(!input_replay_trigger_2.consumed);
    CHECK(!input_test_trigger_poll(base+230,pad) && !pad[0] && input_replay_reported==2);
    CHECK(!input_replay_trigger_2.consumed); /* release returned before any probe */
    input_test_trigger_failure(base+231,ERROR_INVALID_DATA);CHECK(DeleteFileA(second));
    HANDLE locked=CreateFileA(second,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(locked!=INVALID_HANDLE_VALUE);input_test_trigger_failure(base+232,ERROR_SHARING_VIOLATION);
    CHECK(CloseHandle(locked) && DeleteFileA(second));
    CHECK(!input_test_trigger_poll(base+500,pad) && !pad[0] && input_replay_generation==1);
    CHECK(input_replay_reported==2 && input_replay_start==base+10);
    CHECK(DeleteFileA(first));input_test_trigger_file(first,"invalid",7);
    input_test_trigger_file(second,"start-v1\n",9);
    CHECK(!input_test_trigger_poll(base+3000,pad) && !pad[0] && input_replay_generation==2);
    CHECK(input_replay_start==base+3000 && input_replay_reported==0);
    CHECK(!input_test_trigger_poll(base+3020,pad) && pad[0]==0x10);
    CHECK(!input_test_trigger_poll(base+3220,pad) && !pad[0] && input_replay_reported==2);
    CHECK(DeleteFileA(second));input_test_trigger_file(second,"invalid",7);
    CHECK(!input_test_trigger_poll(base+4000,pad) && !pad[0] && input_replay_generation==2);
    CHECK(input_replay_start==base+3000 && input_replay_trigger.consumed && input_replay_trigger_2.consumed);
    CHECK(DeleteFileA(first) && DeleteFileA(second));
    /* An early valid second marker still cannot conceal the first release. */
    input_test_load_start_replay();CHECK(!input_replay_triggers_configure(first,second));
    input_test_trigger_file(first,"start-v1\n",9);input_test_trigger_file(second,"start-v1\n",9);
    CHECK(!input_test_trigger_poll(base+5000,pad) && !pad[0] && input_replay_generation==1);
    CHECK(!input_test_trigger_poll(base+5020,pad) && pad[0]==0x10);
    CHECK(!input_test_trigger_poll(base+5220,pad) && !pad[0] && input_replay_generation==1);
    CHECK(!input_test_trigger_poll(base+5221,pad) && !pad[0] && input_replay_generation==2);
    CHECK(!input_test_trigger_poll(base+5241,pad) && pad[0]==0x10);
    CHECK(!input_test_trigger_poll(base+5441,pad) && !pad[0]);
    CHECK(DeleteFileA(first) && DeleteFileA(second));input_replay_count=0;
    puts("input replay two triggers: distinct atomic configuration, two real press/releases, neutral gap, deferred early marker, errors and sticky generations passed");
}
#ifdef PILOT_INPUT_REFERENCE_CLOCK
static void test_input_reference_clock(void)
{
    input_host_is_keyboard();input_host_keyboard=0;
    FILE *replay=tmpfile();CHECK(replay);
    CHECK(fputs("0 000000000000000000000000000000000000\n20 100000000000000000000000000000000000\n220 000000000000000000000000000000000000\n300 disconnected\n400 000000000000000000000000000000000000\n",replay)>=0);
    rewind(replay);CHECK(input_replay_load(replay) && !fclose(replay));
    uint8_t pad[18];CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
    Sleep(40);
    for(unsigned i=0;i<100;++i)CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
    CHECK(!pilot_input_host_reference_time(19));
    CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
    CHECK(!pilot_input_host_reference_time(20));
    CHECK(!pilot_input_host_state(0,pad) && pad[0]==0x10);
    Sleep(40);CHECK(!pilot_input_host_state(0,pad) && pad[0]==0x10);
    CHECK(pilot_input_host_reference_time(19)==ERROR_INVALID_PARAMETER);
    CHECK(!pilot_input_host_state(0,pad) && pad[0]==0x10);
    CHECK(!pilot_input_host_reference_time(20));
    CHECK(!pilot_input_host_reference_time(220));
    CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
    CHECK(!pilot_input_host_reference_time(300));memset(pad,0xcc,sizeof(pad));
    CHECK(pilot_input_host_state(0,pad)==ERROR_DEVICE_NOT_CONNECTED);
    for(unsigned i=0;i<18;++i)CHECK(pad[i]==0xcc);
    CHECK(!pilot_input_host_reference_time(400));
    CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
    CHECK(pilot_input_host_state(1,pad)==ERROR_DEVICE_NOT_CONNECTED);
    test_input_replay_trigger();
    test_input_replay_two_triggers();
    fprintf(stderr,"PASS reference input: Start20ms/release220ms/hotplug300/400ms on external time; 80ms host stalls and100 observer polls preserve state; backwards publication rejects before mutation\n");
}
#endif
#ifndef PILOT_INPUT_REPLAY_TEST_ONLY
static void input_test_error(void)
{
    input_test_last_error=MEM32(g_esp+4); g_esp+=8;
}
static uint32_t input_test_call(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    MEM32(g_esp+4)=a; MEM32(g_esp+8)=b; MEM32(g_esp+12)=c; MEM32(g_esp+16)=d;
    g_ebx=0x12345678; g_esi=0x23456789; g_edi=0x3456789a; g_ebp=0x456789ab;
    pilot_input_dispatch(entry,input_test_error);
    CHECK(g_esp==0x18004+pilot_input_calls[entry].stack_bytes && MEM32(0x18000)==0x12340);
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    return g_eax;
}
static void input_test_setup(void)
{
    /* Deterministic XInput provider independent of the user's launch choice. */
    input_host_is_keyboard();input_host_keyboard=0;input_replay_count=0;
    /* Exercise actual mapping lifetime and lease expiration independently of
     * keyboard focus, using the same reader as automated gameplay. */
    char control_name[100];snprintf(control_name,sizeof(control_name),"Local\\xlle-input-test-%lu",GetCurrentProcessId());
    CHECK(input_control_open("Global\\wrong")==ERROR_INVALID_PARAMETER);
    HANDLE control_map=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,64,control_name);
    CHECK(control_map && GetLastError()!=ERROR_ALREADY_EXISTS);
    struct InputControlReport *control=MapViewOfFile(control_map,FILE_MAP_WRITE,0,0,64);CHECK(control);
    memset(control,0,64);memcpy(control->magic,"XLLEPAD1",8);
    CHECK(!input_control_open(control_name));
    uint8_t controlled[18];memset(controlled,0xcc,18);
    CHECK(!input_control_sample(input_control_view,1000,controlled));
    for(unsigned i=0;i<18;++i)CHECK(!controlled[i]);
    control->sequence=1;control->issued_ms=1000;control->lease_ms=200;
    control->pad[0]=0x10;control->pad[2]=255;control->pad[12]=255;control->pad[13]=127;
    CHECK(!input_control_sample(input_control_view,1000,controlled) && !controlled[0]);
    MemoryBarrier();control->sequence=2;
    CHECK(!input_control_sample(input_control_view,1000,controlled) && controlled[0]==0x10 && controlled[2]==255 && controlled[13]==127);
    CHECK(!input_control_sample(input_control_view,1199,controlled) && controlled[0]==0x10);
    CHECK(!input_control_sample(input_control_view,1200,controlled));
    for(unsigned i=0;i<18;++i)CHECK(!controlled[i]);
    CHECK(input_control_sample(input_control_view,999,controlled)==ERROR_INVALID_DATA);
    control->lease_ms=5001;CHECK(input_control_sample(input_control_view,1000,controlled)==ERROR_INVALID_DATA);control->lease_ms=200;
    control->pad[1]=1;CHECK(input_control_sample(input_control_view,1000,controlled)==ERROR_INVALID_DATA);control->pad[1]=0;
    control->reserved[21]=1;CHECK(input_control_sample(input_control_view,1000,controlled)==ERROR_INVALID_DATA);control->reserved[21]=0;
    uint8_t controlled_caps[18],controlled_rumble[4];
    CHECK(!pilot_input_host_caps(0,controlled_caps,controlled_rumble));
    CHECK(controlled_caps[0]==255 && controlled_caps[1]==0 && controlled_caps[2]==255);
    for(unsigned i=10;i<18;++i)CHECK(controlled_caps[i]==255);
    CHECK(pilot_input_host_state(1,controlled)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(pilot_input_host_rumble(0,1,0)==ERROR_NOT_SUPPORTED);
    CHECK(!pilot_input_host_rumble(0,0,0));
    CHECK(UnmapViewOfFile((const void *)input_control_view) && CloseHandle(input_control_mapping));
    input_control_view=NULL;input_control_mapping=NULL;input_control_reported=0;
    CHECK(UnmapViewOfFile(control) && CloseHandle(control_map));
    FILE *replay=tmpfile();CHECK(replay);
    CHECK(fputs("0 000000000000000000000000000000000000\n20 1000ff00000000000000ff7f000000000000\n220 000000000000000000000000000000000000\n300 disconnected\n400 000000000000000000000000000000000000\n",replay)>=0);
    rewind(replay);CHECK(input_replay_load(replay) && input_replay_count==5);CHECK(!fclose(replay));
    uint8_t replay_pad[18];input_replay_sample(19,replay_pad);CHECK(!replay_pad[0] && !replay_pad[2]);
    input_replay_sample(20,replay_pad);CHECK(replay_pad[0]==0x10 && replay_pad[2]==255 && replay_pad[10]==255 && replay_pad[11]==127);
    input_replay_sample(219,replay_pad);CHECK(replay_pad[0]==0x10);
    input_replay_sample(220,replay_pad);CHECK(!replay_pad[0] && !replay_pad[2]);
    memset(replay_pad,0xcc,18);
    CHECK(input_replay_sample(300,replay_pad)==ERROR_DEVICE_NOT_CONNECTED);
    for(unsigned i=0;i<18;++i)CHECK(replay_pad[i]==0xcc);
    CHECK(input_replay_sample(399,replay_pad)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(input_replay_sample(400,replay_pad)==ERROR_SUCCESS);
    input_replay_sample(10000,replay_pad);CHECK(!replay_pad[0] && !replay_pad[10]);
    uint8_t replay_caps[18],replay_rumble[4];CHECK(!pilot_input_host_caps(0,replay_caps,replay_rumble) && !memcmp(replay_rumble,"\0\0\0\0",4));
    CHECK(replay_caps[0]==255 && replay_caps[1]==0 && replay_caps[2]==255 && replay_caps[9]==255);
    for(unsigned i=10;i<18;++i)CHECK(replay_caps[i]==255);
    CHECK(pilot_input_host_state(1,replay_pad)==ERROR_DEVICE_NOT_CONNECTED);
    replay=tmpfile();CHECK(replay && fputs("0 z00000000000000000000000000000000000\n",replay)>=0);
    rewind(replay);CHECK(!input_replay_load(replay));CHECK(!fclose(replay));input_replay_count=0;
    uint8_t keys[256]={0},packed[18];XINPUT_GAMEPAD keyboard;
    keys[VK_RETURN]=keys[VK_SPACE]=keys['W']=keys['D']=keys['Z']=1;
    input_host_keyboard_convert(keys,&keyboard);input_host_convert(&keyboard,packed);
    CHECK((packed[0]&0x10) && packed[2]==255 && packed[8]==255);
    CHECK(keyboard.sThumbLX==32767 && keyboard.sThumbLY==32767);
    keys['A']=keys['S']=1;input_host_keyboard_convert(keys,&keyboard);
    CHECK(!keyboard.sThumbLX && !keyboard.sThumbLY);
    memset(keys,0,sizeof(keys));input_host_keyboard_convert(keys,&keyboard);
    CHECK(!keyboard.wButtons && !keyboard.bLeftTrigger);
    commit(PILOT_INPUT_g_DeviceType_Gamepad&~4095u,4096);
    commit(PILOT_INPUT_g_DeviceType_MU&~4095u,4096);
    commit(PILOT_INPUT_g_DeviceType_IRDongle&~4095u,4096);
    input_test_states[0].dwPacketNumber=20;
    XINPUT_GAMEPAD *g=&input_test_states[0].Gamepad;
    g->wButtons=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_LEFT_SHOULDER;
    g->bLeftTrigger=17; g->bRightTrigger=241;
    g->sThumbLX=-32768; g->sThumbLY=32767; g->sThumbRX=-1234; g->sThumbRY=4321;
    for (unsigned i=0;i<4;++i) {
        XINPUT_CAPABILITIES *cap=&input_test_caps[i];
        cap->Type=1; cap->SubType=1; cap->Flags=0x1234; /* not Xbox ABI fields */
        cap->Gamepad.wButtons=0xffff; cap->Gamepad.bLeftTrigger=0xff; cap->Gamepad.bRightTrigger=0xff;
        cap->Gamepad.sThumbLX=cap->Gamepad.sThumbLY=cap->Gamepad.sThumbRX=cap->Gamepad.sThumbRY=(SHORT)0xffc0;
        cap->Vibration.wLeftMotorSpeed=0xffff; cap->Vibration.wRightMotorSpeed=0xff00;
    }
    MEM32(0x19000)=PILOT_INPUT_g_DeviceType_Gamepad; MEM32(0x19004)=4;
    MEM32(0x19008)=PILOT_INPUT_g_DeviceType_MU; MEM32(0x1900c)=2;
    input_test_call(PILOT_INPUT_XInitDevices,2,0x19000,0,0);
}
static void test_input(void)
{
    test_input_replay_trigger();
    test_input_replay_two_triggers();
    input_test_setup();
    uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,out=0x1a020;
    CHECK(input_test_call(PILOT_INPUT_XGetDevices,type,0,0,0)==1);
    CHECK(MEM32(type)==1 && MEM32(type+4)==0 && MEM32(type+8)==1);
    CHECK(input_test_call(PILOT_INPUT_XGetDevices,PILOT_INPUT_g_DeviceType_MU,0,0,0)==0);
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==0);
    CHECK(MEM32(out)==0 && MEM32(out+4)==0);
    uint32_t handle=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0); CHECK(handle!=0);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0)==0 && input_test_last_error==32);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,4,0,0)==0 && input_test_last_error==1167);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,PILOT_INPUT_g_DeviceType_MU,0,0,0)==0 && input_test_last_error==87);
    memset(memory+out-1,0xcc,72);
    input_ports[0].next_poll=UINT64_MAX;
    uint32_t packet=input_ports[0].packet;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0);
    const uint8_t expected_pad[18]={0x10,0,255,0,255,0,255,0,0,241,0,0x80,255,0x7f,0x2e,0xfb,0xe1,0x10};
    CHECK(packet && MEM32(out)==packet && !memcmp(memory+out+4,expected_pad,18) && MEM8(out-1)==0xcc && MEM8(out+22)==0xcc);
    unsigned calls=input_test_state_calls;
    input_ports[0].next_poll=UINT64_MAX;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && input_test_state_calls==calls);
    input_test_states[0].dwPacketNumber++; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM32(out)==++packet);
    input_test_states[0].Gamepad.wButtons|=XINPUT_GAMEPAD_B; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM32(out)==++packet && MEM8(out+7)==255);
    input_test_states[0].Gamepad.bLeftTrigger=31;input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM8(out+12)==0);
    input_test_states[0].Gamepad.bLeftTrigger=32;input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM8(out+12)==32);
    uint8_t last_state[22];memcpy(last_state,memory+out,22);
    input_ports[0].next_poll=UINT64_MAX;

    memset(memory+out-1,0xcc,28);
    CHECK(input_test_call(PILOT_INPUT_XInputGetCapabilities,handle,out,0,0)==0);
    const uint8_t expected_caps[25]={2,0,0,255,0,255,255,255,255,255,255,255,255,
                                    0xc0,255,0xc0,255,0xc0,255,0xc0,255,255,255,0,255};
    CHECK(!memcmp(memory+out,expected_caps,25) && MEM8(out-1)==0xcc && MEM8(out+25)==0xcc);
    input_test_caps_status=5; memset(memory+out,0xcc,25);
    CHECK(input_test_call(PILOT_INPUT_XInputGetCapabilities,handle,out,0,0)==5);
    CHECK(MEM8(out)==2 && MEM8(out+25)==0xcc);
    for (unsigned i=1;i<25;++i) CHECK(MEM8(out+i)==0);
    input_test_caps_status=0;

    memset(memory+out-1,0xa5,72); MEM32(out+4)=0;
    MEM16(out+66)=12345; MEM16(out+68)=54321;
    CHECK(input_test_call(PILOT_INPUT_XInputSetState,handle,out,0,0)==0);
    CHECK(input_test_vibration.wLeftMotorSpeed==12345 && input_test_vibration.wRightMotorSpeed==54321);
    CHECK(MEM32(out)==0 && MEM8(out+64)==0 && MEM8(out+65)==6 && MEM8(out+70)==0xa5 && MEM8(out-1)==0xa5);
    for (unsigned i=8;i<64;++i) CHECK(MEM8(out+i)==0xa5);
    input_test_rumble_status=5;
    CHECK(input_test_call(PILOT_INPUT_XInputSetState,handle,out,0,0)==5 && MEM32(out)==5);
    input_test_rumble_status=0;

    /* Removal/reinsertion observed between enumeration queries reports BOTH.
     * A stale handle must never silently acquire the replacement controller. */
    input_test_presence[0]=1167; input_ports[0].next_poll=0;
    memset(memory+out,0xcc,22);
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==1167 && !memcmp(memory+out,last_state,22));
    input_test_presence[0]=0; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==1167);
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==1);
    CHECK(MEM32(out)==1 && MEM32(out+4)==1);
    uint32_t replacement=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
    CHECK(replacement && replacement!=handle);
    unsigned rumble_calls=input_test_rumble_calls;
    input_test_call(PILOT_INPUT_XInputClose,handle,0,0,0);
    CHECK(input_test_rumble_calls==rumble_calls); /* don't touch replacement */
    input_test_call(PILOT_INPUT_XInputClose,replacement,0,0,0);
    CHECK(input_test_rumble_calls==rumble_calls+1 && input_test_vibration.wLeftMotorSpeed==0 && input_test_vibration.wRightMotorSpeed==0);
    for (unsigned i=0;i<100;++i) {
        uint32_t next=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
        CHECK(next>replacement); replacement=next;
        input_test_call(PILOT_INPUT_XInputClose,next,0,0,0);
    }
    for (unsigned i=0;i<4;++i) CHECK(!input_handles[i].id);
    input_test_presence[0]=1167; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==1 && MEM32(out)==0 && MEM32(out+4)==1);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0)==0 && input_test_last_error==1167);
    /* Original GetDeviceChanges observations, including remove/reinsert
     * between queries and independent changes on multiple ports. */
    const uint32_t changes[][5]={{0,0,0,0,0},{1,1,0,1,0},{0,1,1,0,1},
        {1,1,1,1,1},{3,2,1,2,0},{1,2,3,0,2},{5,7,6,5,6},{15,15,15,15,15}};
    for(unsigned i=0;i<4;++i)input_ports[i].next_poll=UINT64_MAX;
    for(unsigned i=0;i<sizeof(changes)/sizeof(changes[0]);++i) {
        const uint32_t *row=changes[i];memcpy(memory+type,row,12);
        CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==!!(row[3]|row[4]));
        CHECK(MEM32(out)==row[3] && MEM32(out+4)==row[4]);
        CHECK(MEM32(type)==row[0] && !MEM32(type+4) && MEM32(type+8)==row[0]);
    }
    MEM32(type)=5;MEM32(type+4)=15;MEM32(type+8)=10;
    CHECK(input_test_call(PILOT_INPUT_XGetDevices,type,0,0,0)==5);
    CHECK(MEM32(type)==5 && !MEM32(type+4) && MEM32(type+8)==5);
    puts("input contract: 22/25/70-byte ABI, mapping, poll cache, hotplug, stale handles, real feedback errors and 100 lifetimes passed");
}
static void test_input_failure(const char *kind)
{
    input_test_setup();
    uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,out=0x1a020;
    if (!strcmp(kind,"manual")) {
        MEM32(out)=0x800;
        input_test_call(PILOT_INPUT_XInputOpen,type,0,0,out);
    } else if (!strcmp(kind,"unknown")) input_test_call(PILOT_INPUT_XGetDevices,0x1a000,0,0,0);
    else {
        uint32_t h=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
        if (!strcmp(kind,"event")) {
            MEM32(out+4)=0x1234;
            input_test_call(PILOT_INPUT_XInputSetState,h,out,0,0);
        } else if (!strcmp(kind,"closed")) {
            input_test_call(PILOT_INPUT_XInputClose,h,0,0,0);
            input_test_call(PILOT_INPUT_XInputGetState,h,out,0,0);
        }
    }
    ExitProcess(0); /* WILL_FAIL tests require the intended terminal stop. */
}
#endif
