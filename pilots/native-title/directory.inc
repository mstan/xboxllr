/* Xbox class-1 directory ABI (nxdk). Phobos supplies the real-host-enumeration
 * approach; use our existing handle-relative NT service rather than reopen a
 * path. No donor bodies copied. A handle owns its host search cursor/mask.
 * Installed-media tolerance: host metadata/order, synchronous ASCII entries.
 * Original Nt/FATX evidence: native_filesystem_contract_reference.py.
 * Reject sub-header buffers instead of reproducing the original overflow. */
static uint32_t media_directory(HANDLE file,IO_STATUS_BLOCK *io,const uint32_t a[10])
{
    if(a[1] || a[2])stop("directory event/APC delivery outside qualified scope",207);
    if(a[7]!=1)return 0xc0000003u;
    if(!a[5])return MEDIA_INVALID;
    if(a[6]<64)return 0xc0000004u;
    WCHAR pattern[1024];UNICODE_STRING mask={0};
    if(a[8]) {
        unsigned n=MEM16(a[8]);uint32_t source=MEM32(a[8]+4);
        if(!source || n>MEM16(a[8]+2) || n>=1024)return MEDIA_INVALID;
        for(unsigned i=0;i<n;++i) {
            unsigned c=MEM8(source+i);
            if(c>=128)stop("directory mask needs Xbox character mapping",207);
            if(c<32 || c=='/' || c=='\\' || c==':')return 0xc0000033u;
            pattern[i]=(WCHAR)c;
        }
        /* Xbox's conventional all-files mask also includes extensionless
         * files/directories. Do not mutate the caller's counted string. */
        if(n==3 && pattern[0]=='*' && pattern[1]=='.' && pattern[2]=='*')n=1;
        mask.Length=mask.MaximumLength=(USHORT)(n*2);mask.Buffer=pattern;
    }
    union {uint64_t aligned;unsigned char bytes[64+1024*2];} result;
    uint32_t status;BOOLEAN restart=(BOOLEAN)a[9];
    unsigned length;WCHAR *name=(WCHAR*)(result.bytes+64);
    for(;;) {
        memset(&result,0,sizeof(result));io->Information=0;
        status=(uint32_t)media_nt_directory(file,NULL,NULL,NULL,io,result.bytes,sizeof(result),1,TRUE,a[8]?&mask:NULL,restart);
        if(!status)pilot_session_value(PILOT_SESSION_FILE_RESULT,UINT64_C(0x4000000000000000)|a[0],result.bytes,sizeof(result.bytes));
        restart=FALSE;
        if(status){io->Information=0;return status;}
        memcpy(&length,result.bytes+60,4);
        if(length&1 || length>2048 || io->Information<64+length)stop("invalid native directory entry",207);
        length/=2;
        if((length==1 && name[0]=='.') || (length==2 && name[0]=='.' && name[1]=='.'))continue;
        break;
    }
    uint32_t attrs;memcpy(&attrs,result.bytes+56,4);
    if(attrs&FILE_ATTRIBUTE_REPARSE_POINT)stop("directory reparse entry outside installed-media contract",207);
    for(unsigned i=0;i<length;++i)if(!name[i] || name[i]>=128)stop("directory entry needs Xbox character mapping",207);
    unsigned copied=length<a[6]-64?length:a[6]-64;
    memcpy(memory+a[5],result.bytes,64);MEM32(a[5])=0;MEM32(a[5]+4)=0;
    MEM64(a[5]+48)=MEM64(a[5]+40); /* FATX reports EOF as AllocationSize. */
    MEM32(a[5]+60)=length;
    for(unsigned i=0;i<copied;++i)MEM8(a[5]+64+i)=(unsigned char)name[i];
    io->Information=64+copied;
    status=copied==length?0:0x80000005u;
    fprintf(stderr,"[DIRECTORY] bytes=%u name=%.*s status=%08x\n",(unsigned)io->Information,(int)copied,(char*)(memory+a[5]+64),status);
    return status;
}
