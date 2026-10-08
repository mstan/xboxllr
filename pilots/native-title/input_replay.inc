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
    input_replay_count=count;input_replay_clock_started=0;input_replay_reported=-1;return 1;
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
