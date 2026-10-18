/* Elective, local automation transport. Only controller reports cross this
 * boundary; it cannot read/write title memory or select guest control flow.
 * The writer owns a named 64-byte mapping, publishes odd/even sequence numbers
 * with full barriers, and uses GetTickCount64 for a <=5s lease. A torn, absent,
 * or expired publication is neutral, so a dead test driver cannot hold a stick.
 * Windows input_host.c already owns the platform boundary. */
struct InputControlReport {
    char magic[8];
    uint32_t sequence,lease_ms;
    uint64_t issued_ms;
    uint8_t pad[18],reserved[22];
};
typedef char input_control_size_check[sizeof(struct InputControlReport)==64?1:-1];
static const volatile struct InputControlReport *input_control_view;
static HANDLE input_control_mapping;
static uint32_t input_control_reported;

static uint32_t input_control_sample(const volatile struct InputControlReport *view,
                                     uint64_t now,uint8_t pad[18])
{
    memset(pad,0,18);
    const uint32_t sequence=view->sequence;
    if(!sequence || (sequence&1))return ERROR_SUCCESS;
    MemoryBarrier();
    struct InputControlReport report;
    memcpy(&report,(const void *)view,sizeof(report));
    MemoryBarrier();
    if(view->sequence!=sequence || report.sequence!=sequence)return ERROR_SUCCESS;
    if(memcmp(report.magic,"XLLEPAD1",8) || !report.lease_ms || report.lease_ms>5000 ||
       report.pad[1] || report.issued_ms>now)return ERROR_INVALID_DATA;
    for(unsigned i=0;i<sizeof(report.reserved);++i)
        if(report.reserved[i])return ERROR_INVALID_DATA;
    if(now-report.issued_ms>=report.lease_ms)return ERROR_SUCCESS;
    memcpy(pad,report.pad,18);
    if(input_control_reported!=sequence) {
        fprintf(stderr,"[INPUT CONTROL DELIVERED] sequence=%u issued_ms=%llu lease_ms=%u pad=",
                sequence,(unsigned long long)report.issued_ms,report.lease_ms);
        for(unsigned i=0;i<18;++i)fprintf(stderr,"%02x",pad[i]);
        fputc('\n',stderr);input_control_reported=sequence;
    }
    return ERROR_SUCCESS;
}
static uint32_t input_control_open(const char *name)
{
    const char prefix[]="Local\\xlle-input-";
    if(!name || strncmp(name,prefix,sizeof(prefix)-1) || !name[sizeof(prefix)-1] || strlen(name)>200)
        return ERROR_INVALID_PARAMETER;
    HANDLE mapping=OpenFileMappingA(FILE_MAP_READ,FALSE,name);
    if(!mapping)return GetLastError();
    const volatile struct InputControlReport *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,64);
    if(!view){DWORD error=GetLastError();CloseHandle(mapping);return error;}
    if(memcmp((const void *)view->magic,"XLLEPAD1",8)) {
        UnmapViewOfFile((const void *)view);CloseHandle(mapping);return ERROR_INVALID_DATA;
    }
    input_control_mapping=mapping;input_control_view=view;return ERROR_SUCCESS;
}
