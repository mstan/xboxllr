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
static struct InputReplayTrigger input_replay_trigger_2;
static unsigned input_replay_generation;
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
static uint32_t input_replay_trigger_validate(const char *path,struct InputReplayTrigger *result)
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
    *result=next;return ERROR_SUCCESS;
}
static int input_replay_row_neutral(const struct InputReplayRow *row)
{
    if(!row->connected)return 0;
    for(unsigned i=0;i<18;++i)if(row->pad[i])return 0;
    return 1;
}
static uint32_t input_replay_triggers_configure(const char *first,const char *second)
{
    struct InputReplayTrigger next={0},next2={0};
    uint32_t error=input_replay_trigger_validate(first,&next);
    if(error)return error;
    if(second && *second) {
        if(!next.enabled || input_replay_count<2 ||
           !input_replay_row_neutral(input_replay_rows+input_replay_count-1))return ERROR_INVALID_PARAMETER;
        error=input_replay_trigger_validate(second,&next2);if(error)return error;
        /* Compare normalized absolute names, including case/slash/dot aliases.
         * Both markers must be absent, so no existing file identity is needed. */
        char names[2][4096];
        for(unsigned i=0;i<2;++i) {
            DWORD length=GetFullPathNameA(i?second:first,sizeof(names[i]),names[i],NULL);
            if(!length || length>=sizeof(names[i]))return ERROR_INVALID_PARAMETER;
            for(char *at=names[i];*at;++at)if(*at=='/')*at='\\';
        }
        if(!_stricmp(names[0],names[1]))return ERROR_INVALID_PARAMETER;
    }
    input_replay_trigger=next;input_replay_trigger_2=next2;input_replay_generation=0;
    return ERROR_SUCCESS;
}
static uint32_t input_replay_trigger_configure(const char *path)
{
    return input_replay_triggers_configure(path,NULL);
}
static uint32_t input_replay_trigger_failure(const struct InputReplayTrigger *trigger,
                                           const char *operation,DWORD error)
{
    if(!error)error=ERROR_ACCESS_DENIED;
    fprintf(stderr,"[INPUT REPLAY TRIGGER ERROR] operation=%s error=%lu path=\"%s\"\n",
            operation,(unsigned long)error,trigger->path);
    return error;
}
/* Read an immutable, complete marker without changing replay state. Missing
 * means wait; a missing parent, sharing violation, directory or bad contents
 * is an explicit error. Publish the marker by creating it privately, closing
 * it, then renaming into this absent path to avoid a partial writer race.
 * The new name can be visible before the renamer closes its DELETE handle.
 * Share that namespace access; content writers remain excluded for the whole
 * size/read verification on our pinned handle. */
static uint32_t input_replay_trigger_probe(const struct InputReplayTrigger *trigger,int *found)
{
    DWORD attributes=GetFileAttributesA(trigger->path);
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND){*found=0;return ERROR_SUCCESS;}
        return input_replay_trigger_failure(trigger,"GetFileAttributesA",error);
    }
    if(attributes&FILE_ATTRIBUTE_DIRECTORY)return input_replay_trigger_failure(trigger,"directory",ERROR_ACCESS_DENIED);
    HANDLE file=CreateFileA(trigger->path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,
                           OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND){*found=0;return ERROR_SUCCESS;}
        return input_replay_trigger_failure(trigger,"CreateFileA",error);
    }
    LARGE_INTEGER size;DWORD got=0,error=ERROR_SUCCESS;char marker[10];
    const char *operation=NULL;
    if(!GetFileSizeEx(file,&size)){operation="GetFileSizeEx";error=GetLastError();}
    else if(size.QuadPart!=9){operation="size";error=ERROR_INVALID_DATA;}
    else if(!ReadFile(file,marker,sizeof(marker),&got,NULL)){operation="ReadFile";error=GetLastError();}
    else if(got!=9 || memcmp(marker,"start-v1\n",9)){operation="contents";error=ERROR_INVALID_DATA;}
    if(!CloseHandle(file) && !operation){operation="CloseHandle";error=GetLastError();}
    if(operation)return input_replay_trigger_failure(trigger,operation,error);
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
    memset(&input_replay_trigger,0,sizeof(input_replay_trigger));
    memset(&input_replay_trigger_2,0,sizeof(input_replay_trigger_2));input_replay_generation=0;return 1;
}
static uint32_t input_replay_sample(uint64_t elapsed,uint8_t pad[18])
{
    unsigned row=0;
    while(row+1<input_replay_count && input_replay_rows[row+1].at<=elapsed)++row;
    if(input_replay_reported!=(int)row) {
        fprintf(stderr,"[INPUT REPLAY DELIVERED] row=%u scheduled_ms=%llu elapsed_ms=%llu digital=%02x%02x connected=%d generation=%u start_ms=%llu\n",
            row,(unsigned long long)input_replay_rows[row].at,(unsigned long long)elapsed,
            input_replay_rows[row].pad[1],input_replay_rows[row].pad[0],input_replay_rows[row].connected,
            input_replay_generation,(unsigned long long)input_replay_start);
        input_replay_reported=(int)row;
    }
    if(!input_replay_rows[row].connected)return ERROR_DEVICE_NOT_CONNECTED;
    memcpy(pad,input_replay_rows[row].pad,18);return ERROR_SUCCESS;
}
static uint32_t input_replay_poll(uint64_t now,uint8_t pad[18])
{
    if(input_replay_clock_started && now<input_replay_start) {
        fprintf(stderr,"[INPUT REPLAY CLOCK ERROR] now_ms=%llu start_ms=%llu generation=%u\n",
            (unsigned long long)now,(unsigned long long)input_replay_start,input_replay_generation);
        return ERROR_INVALID_PARAMETER;
    }
    struct InputReplayTrigger *trigger=NULL;unsigned generation=0;
    if(input_replay_trigger.enabled && !input_replay_trigger.consumed) {
        trigger=&input_replay_trigger;generation=1;
    } else if(input_replay_generation==1 && input_replay_trigger_2.enabled &&
              !input_replay_trigger_2.consumed && input_replay_reported==(int)input_replay_count-1) {
        /* A separate poll must already have returned the final neutral row.
         * Never hide that release by consuming generation2 on the same poll. */
        trigger=&input_replay_trigger_2;generation=2;
    }
    if(trigger) {
        int found=0;uint32_t error=input_replay_trigger_probe(trigger,&found);
        if(error)return error;
        if(!found){memset(pad,0,18);return ERROR_SUCCESS;}
        trigger->consumed=1;input_replay_generation=generation;input_replay_reported=-1;
        input_replay_start=now;input_replay_clock_started=1;
        fprintf(stderr,"[INPUT REPLAY TRIGGER] consumed start_ms=%llu generation=%u path=\"%s\"\n",
            (unsigned long long)now,generation,trigger->path);
    }
    if(!input_replay_clock_started){input_replay_start=now;input_replay_clock_started=1;}
    if(now<input_replay_start)return ERROR_INVALID_PARAMETER;
    return input_replay_sample(now-input_replay_start,pad);
}
