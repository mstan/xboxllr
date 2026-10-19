/* Read-only mounted-filesystem probe. Include after media.inc. Raw NTSTATUS
 * is intentional: only the query owner may interpret genuine name/path absence.
 * Parent case sensitivity is checked before lookup, never inferred from ENOENT.
 * No guest objects, file contents, creates, writes, or callbacks are involved. */
static void network_cache_probe_trace(const char *stage,unsigned partition,unsigned component,uint32_t value)
{
    static LONG records;
    if(InterlockedIncrement(&records)<=16)
        fprintf(stderr,"[CACHE PROBE] stage=%s partition=%u component=%u value=%08x\n",stage,partition,component,value);
}
static uint32_t network_cache_probe_metadata_status(uint32_t status)
{
    /* Only an actual component lookup may report qualified namespace absence.
     * A failed metadata query is unsupported even if it returns the same code.
     * This internal guard status is never projected as a guest query HRESULT. */
    return status==0xc0000034u || status==0xc000003au?0xc0000184u:status;
}
static uint32_t network_cache_probe_directory(HANDLE directory)
{
    IO_STATUS_BLOCK io={0};
    union { uint64_t alignment; unsigned char bytes[24]; } standard={0};
    uint32_t status=(uint32_t)media_nt_query(directory,&io,standard.bytes,24,5);
    if(status || !standard.bytes[21])network_cache_probe_trace("parent-standard",0,0,status?status:standard.bytes[21]);
    if(status)return network_cache_probe_metadata_status(status);
    if(!standard.bytes[21])return 0xc0000103u; /* STATUS_NOT_A_DIRECTORY */
    uint32_t flags=0;
    status=(uint32_t)media_nt_query(directory,&io,&flags,4,71);
    if(status || flags)network_cache_probe_trace("parent-case",0,0,status?status:flags);
    if(status)return network_cache_probe_metadata_status(status); /* unsupported filesystems fail closed */
    if(flags)return 0xc00000bbu; /* case-sensitive or unknown flags */
    return 0;
}
static uint32_t network_cache_probe(void *context,unsigned partition,const char *relative)
{
    WCHAR components[8][128];unsigned count=0,length=0;
    (void)context;
    if(!relative || !*relative)return 0xc000000du;
    for(const unsigned char *p=(const unsigned char *)relative;;++p) {
        unsigned c=*p;
        if(!c || c=='\\') {
            if(!length || count>=8 || components[count][length-1]=='.' ||
               components[count][length-1]==' ' ||
               (length==1 && components[count][0]=='.') ||
               (length==2 && components[count][0]=='.' && components[count][1]=='.'))return 0xc0000033u;
            components[count++][length]=0;length=0;
            if(!c)break;
        } else {
            if(count>=8 || length>=127 || c<32 || c>=127 || c=='/' || c==':' || c=='*' || c=='?')return 0xc0000033u;
            components[count][length++]=(WCHAR)c;
        }
    }
    InitOnceExecuteOnce(&media_once,media_start,NULL,NULL);
    network_cache_probe_trace("validated",partition,count,0);
    AcquireSRWLockShared(&media_lock);
    uint32_t status=0;
    HANDLE parent=partition>0 && partition<6?storage_roots[partition]:NULL;
    HANDLE owned=NULL;
    if(!parent)status=0xc0000008u;
    if(status)network_cache_probe_trace("mount",partition,0,status);
    for(unsigned i=0;!status && i<count;++i) {
        status=network_cache_probe_directory(parent);
        if(status)break;
        HANDLE child=NULL;IO_STATUS_BLOCK io={0};
        status=media_open_host(parent,components[i],FILE_READ_ATTRIBUTES|SYNCHRONIZE,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,i+1<count?0x21:0x20,&child,&io);
        network_cache_probe_trace("component-open",partition,i,status);
        if(status)break;
        if(!child || child==INVALID_HANDLE_VALUE) {status=0xc0000008u;break;}
        if(owned && !CloseHandle(owned))stop("cache probe directory release failed",GetLastError());
        owned=child;parent=child;
        if(i+1==count) {
            union {uint64_t alignment;unsigned char bytes[24];} info={0};
            status=(uint32_t)media_nt_query(child,&io,info.bytes,24,5);
            if(status || info.bytes[21])network_cache_probe_trace("final-standard",partition,i,status?status:info.bytes[21]);
            status=network_cache_probe_metadata_status(status);
            if(!status && info.bytes[21])status=0xc00000bau; /* file is a directory */
        }
    }
    if(owned && !CloseHandle(owned))stop("cache probe handle release failed",GetLastError());
    ReleaseSRWLockShared(&media_lock);
    network_cache_probe_trace("return",partition,count,status);
    return status;
}
