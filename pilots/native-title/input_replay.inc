/* Deterministic host gamepad source, following Phobos input_host.c's timed
 * input-replay approach. Independent parser; no donor body copied. This feeds
 * real XAPI input handling, never title memory or control flow. Each line is
 * milliseconds since first host poll, then 36 hex digits of Xbox pad state
 * or the word disconnected. Connection changes enter the real selected path. */
struct InputReplayRow {uint64_t at;uint8_t pad[18];int connected;};
static struct InputReplayRow input_replay_rows[256];
static unsigned input_replay_count;
static ULONGLONG input_replay_start;
static int input_replay_clock_started;
static int input_replay_reported=-1;
static struct InputReplayTrigger {char path[4096];int enabled,consumed;} input_replay_trigger;
static int input_replay_absolute_path(const char *path)
{
    size_t length=strlen(path);
    if(length>=3 && ((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z')) &&
       path[1]==':' && (path[2]=='\\' || path[2]=='/'))return 1;
    /* A UNC path must contain nonempty server, share and marker components. */
    if(length<5 || path[0]!='\\' || path[1]!='\\')return 0;
    const char *server=path+2,*share=strchr(server,'\\');
    if(!share || share==server)return 0;
    const char *marker=strchr(share+1,'\\');
    return marker && marker>share+1 && marker[1];
}
/* Configuration is committed only after the loaded source and absent marker
 * have been checked. The marker is an elective host-input action, never a
 * guest-memory write or a replacement clock/publication opportunity. */
static uint32_t input_replay_trigger_configure(const char *path)
{
    struct InputReplayTrigger next={0};
    if(path && *path) {
        if(strlen(path)>=sizeof(next.path) || !input_replay_absolute_path(path) ||
           !input_replay_count || !input_replay_rows[0].connected || input_replay_rows[0].at)
            return ERROR_INVALID_PARAMETER;
        for(unsigned i=0;i<18;++i)if(input_replay_rows[0].pad[i])return ERROR_INVALID_DATA;
        DWORD attributes=GetFileAttributesA(path);
        if(attributes!=INVALID_FILE_ATTRIBUTES)return ERROR_ALREADY_EXISTS;
        DWORD error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND)return error?error:ERROR_ACCESS_DENIED;
        memcpy(next.path,path,strlen(path)+1);next.enabled=1;
    }
    input_replay_trigger=next;return ERROR_SUCCESS;
}
/* Read an immutable, complete marker without changing replay state. Missing
 * means wait; a missing parent, sharing violation, directory or bad contents
 * is an explicit error. Publish the marker by creating it privately, closing
 * it, then renaming into this absent path to avoid a partial writer race. */
static uint32_t input_replay_trigger_probe(int *found)
{
    DWORD attributes=GetFileAttributesA(input_replay_trigger.path);
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND){*found=0;return ERROR_SUCCESS;}
        return error?error:ERROR_ACCESS_DENIED;
    }
    if(attributes&FILE_ATTRIBUTE_DIRECTORY)return ERROR_ACCESS_DENIED;
    HANDLE file=CreateFileA(input_replay_trigger.path,GENERIC_READ,FILE_SHARE_READ,NULL,
                           OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND){*found=0;return ERROR_SUCCESS;}
        return error?error:ERROR_ACCESS_DENIED;
    }
    LARGE_INTEGER size;DWORD got=0,error=ERROR_SUCCESS;char marker[10];
    if(!GetFileSizeEx(file,&size))error=GetLastError();
    else if(size.QuadPart!=9)error=ERROR_INVALID_DATA;
    else if(!ReadFile(file,marker,sizeof(marker),&got,NULL))error=GetLastError();
    else if(got!=9 || memcmp(marker,"start-v1\n",9))error=ERROR_INVALID_DATA;
    if(!CloseHandle(file) && !error)error=GetLastError();
    if(error)return error;
    *found=1;return ERROR_SUCCESS;
}
static int input_replay_load(FILE *file)
{
    char line[160],hex[37],extra;unsigned count=0;unsigned long long at;
    input_replay_count=0;
    while(fgets(line,sizeof(line),file)) {
        if(count==256 || sscanf(line,"%llu %36s %c",&at,hex,&extra)!=2 ||
           (!count && at) || (count && at<=input_replay_rows[count-1].at))return 0;
        struct InputReplayRow row={0};row.at=at;
        if(!strcmp(hex,"disconnected")){input_replay_rows[count++]=row;continue;}
        if(strlen(hex)!=36)return 0;
        row.connected=1;
        for(unsigned i=0;i<36;++i) {
            unsigned char c=(unsigned char)hex[i];unsigned nibble;
            if(c>='0' && c<='9')nibble=c-'0';
            else if(c>='a' && c<='f')nibble=c-'a'+10;
            else if(c>='A' && c<='F')nibble=c-'A'+10;
            else return 0;
            row.pad[i/2]|=(uint8_t)(nibble<<((i&1)?0:4));
        }
        if(row.pad[1])return 0; /* only the eight defined digital buttons */
        input_replay_rows[count++]=row;
    }
    if(!count || ferror(file))return 0;
    input_replay_count=count;input_replay_clock_started=0;input_replay_reported=-1;
    memset(&input_replay_trigger,0,sizeof(input_replay_trigger));return 1;
}
static uint32_t input_replay_sample(uint64_t elapsed,uint8_t pad[18])
{
    unsigned row=0;
    while(row+1<input_replay_count && input_replay_rows[row+1].at<=elapsed)++row;
    if(input_replay_reported!=(int)row) {
        fprintf(stderr,"[INPUT REPLAY DELIVERED] row=%u scheduled_ms=%llu elapsed_ms=%llu digital=%02x%02x connected=%d\n",
            row,(unsigned long long)input_replay_rows[row].at,(unsigned long long)elapsed,
            input_replay_rows[row].pad[1],input_replay_rows[row].pad[0],input_replay_rows[row].connected);
        input_replay_reported=(int)row;
    }
    if(!input_replay_rows[row].connected)return ERROR_DEVICE_NOT_CONNECTED;
    memcpy(pad,input_replay_rows[row].pad,18);return ERROR_SUCCESS;
}
static uint32_t input_replay_poll(uint64_t now,uint8_t pad[18])
{
    if(input_replay_trigger.enabled && !input_replay_trigger.consumed) {
        int found=0;uint32_t error=input_replay_trigger_probe(&found);
        if(error)return error;
        if(!found){memset(pad,0,18);return ERROR_SUCCESS;}
        input_replay_trigger.consumed=1;
        input_replay_start=now;input_replay_clock_started=1;
        fprintf(stderr,"[INPUT REPLAY TRIGGER] consumed start_ms=%llu\n",(unsigned long long)now);
    }
    if(!input_replay_clock_started){input_replay_start=now;input_replay_clock_started=1;}
    if(now<input_replay_start)return ERROR_INVALID_PARAMETER;
    return input_replay_sample(now-input_replay_start,pad);
}
