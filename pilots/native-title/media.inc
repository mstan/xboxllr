/* Installed-media service. The complete XAPI verification boundary returns
 * NTSTATUS with no arguments and no escaped guest resources. It validates a
 * real mounted directory/default.xbe; it does NOT synthesize drive mode pages.
 * Xbox ABI: nxdk. Host-file design reference: sp00nznet 1409a7d kernel_file.c
 * and kernel_path.c. Service bodies use the host NT API, not donor code. */
#define MEDIA_INVALID 0xc000000du
#define MEDIA_BAD_HANDLE 0xc0000008u
#define MEDIA_WRONG_VOLUME 0xc0000012u
#define MEDIA_NO_MEDIA 0xc0000013u
static const char *media_content_path;
static HANDLE media_root,media_identity;
static SRWLOCK media_lock=SRWLOCK_INIT;
static struct { HANDLE host; uint32_t id; } media_files[256];
static uint32_t media_next_id=0x50000001u;
typedef LONG (NTAPI *MediaOpen)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
typedef LONG (NTAPI *MediaRead)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,PLARGE_INTEGER,PULONG);
typedef LONG (NTAPI *MediaInfo)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
static MediaOpen media_nt_open;
static MediaRead media_nt_read;
static MediaInfo media_nt_query,media_nt_set;
static INIT_ONCE media_once=INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK media_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once; (void)parameter; (void)context;
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    media_nt_open=(MediaOpen)(void*)GetProcAddress(nt,"NtOpenFile");
    media_nt_read=(MediaRead)(void*)GetProcAddress(nt,"NtReadFile");
    media_nt_query=(MediaInfo)(void*)GetProcAddress(nt,"NtQueryInformationFile");
    media_nt_set=(MediaInfo)(void*)GetProcAddress(nt,"NtSetInformationFile");
    if (!media_nt_open || !media_nt_read || !media_nt_query || !media_nt_set)
        stop("native file services unavailable",0);
    return TRUE;
}
static uint32_t media_open_host(HANDLE parent,const WCHAR *path,uint32_t access,
                               uint32_t share,uint32_t options,HANDLE *out,IO_STATUS_BLOCK *io)
{
    UNICODE_STRING name={(USHORT)(wcslen(path)*2),(USHORT)(wcslen(path)*2),(PWSTR)path};
    OBJECT_ATTRIBUTES attr={sizeof(attr),parent,&name,0x1040,NULL,NULL}; /* case-insensitive; no reparse traversal */
    InitOnceExecuteOnce(&media_once,media_start,NULL,NULL);
    return (uint32_t)media_nt_open(out,access,&attr,io,share,options);
}
static uint32_t media_hash(HANDLE file,char hex[65])
{
    BCRYPT_HASH_HANDLE hash=NULL;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,NULL,0,NULL,0,0)<0)
        stop("media hash initialization failed",0);
    unsigned char bytes[65536],digest[32];
    LARGE_INTEGER offset; offset.QuadPart=0;
    uint32_t status=0;
    for (;;) {
        IO_STATUS_BLOCK io={0};
        status=(uint32_t)media_nt_read(file,NULL,NULL,NULL,&io,bytes,sizeof(bytes),&offset,NULL);
        if (status==0xc0000011u) { status=0; break; }
        if (status) break;
        if (!io.Information) break;
        if (BCryptHashData(hash,bytes,(ULONG)io.Information,0)<0) stop("media hashing failed",0);
        offset.QuadPart+=io.Information;
    }
    if (!status) {
        if (BCryptFinishHash(hash,digest,32,0)<0) stop("media hash finalization failed",0);
        for (unsigned i=0;i<32;++i) sprintf(hex+2*i,"%02x",digest[i]);
    }
    BCryptDestroyHash(hash);
    return status;
}
/* Lock held by caller. Successful mounts retain the root and identity handles
 * with write/delete sharing denied. Repeated verification rechecks the XBE. */
static uint32_t media_verify(const char *expected)
{
    if (!media_content_path || !*media_content_path) return MEDIA_NO_MEDIA;
    HANDLE root=media_root,identity=media_identity;
    uint32_t status=0;
    if (!root) {
        WCHAR path[32768];
        if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,media_content_path,-1,path,32768)) return MEDIA_INVALID;
        root=CreateFileW(path,FILE_LIST_DIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
                         FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
        if (root==INVALID_HANDLE_VALUE) {
            DWORD error=GetLastError();
            return error==ERROR_ACCESS_DENIED ? 0xc0000022u :
                   error==ERROR_SHARING_VIOLATION ? 0xc0000043u : 0xc000003au;
        }
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(root,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            CloseHandle(root); return 0xc0000103u;
        }
        IO_STATUS_BLOCK io={0};
        status=media_open_host(root,L"default.xbe",GENERIC_READ|SYNCHRONIZE,FILE_SHARE_READ,0x60,&identity,&io);
    }
    char hex[65];
    if (!status) status=media_hash(identity,hex);
    if (!status && strcmp(hex,expected)) status=MEDIA_WRONG_VOLUME;
    if (status) {
        if (!media_root) { if (identity) CloseHandle(identity); CloseHandle(root); }
        return status;
    }
    media_root=root; media_identity=identity;
    return 0;
}
static void media_verify_abi(const char *expected)
{
    AcquireSRWLockExclusive(&media_lock);
    g_eax=media_verify(expected);
    ReleaseSRWLockExclusive(&media_lock);
    fprintf(stderr,"[MEDIA VERIFY] installed default.xbe identity/status=%08x\n",g_eax);
    g_esp+=4; /* original no-argument RET; all nonvolatile guest registers intact */
}
void pilot_verify_installed_media(void) { media_verify_abi(PILOT_XBE_SHA256); }
static HANDLE media_handle(uint32_t id)
{
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].id==id) return media_files[i].host;
    return NULL;
}
static uint32_t media_close(uint32_t id)
{
    uint32_t status=MEDIA_BAD_HANDLE;
    AcquireSRWLockExclusive(&media_lock);
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].id==id) {
        if (!CloseHandle(media_files[i].host)) stop("owned media handle close failed",id);
        media_files[i].host=NULL; status=0; break;
    }
    ReleaseSRWLockExclusive(&media_lock);
    return status;
}
static void media_iosb(uint32_t va,uint32_t status,ULONG_PTR count)
{
    MEM32(va)=status; MEM32(va+4)=(uint32_t)count;
}
/* Returns a validated relative ASCII path. No host path, traversal, alternate
 * stream or drive-name interpretation is available to guest input. */
static uint32_t media_path(uint32_t attributes,WCHAR path[1024],HANDLE *parent)
{
    if (!attributes || !MEM32(attributes+4)) return MEDIA_INVALID;
    if (MEM32(attributes+8)&~0x40u) stop("file object attributes unsupported",MEM32(attributes+8));
    uint32_t string=MEM32(attributes+4),length=MEM16(string),buffer=MEM32(string+4);
    if (!length || length>=1024 || length>MEM16(string+2)) return 0xc0000033u;
    char name[1024]; memcpy(name,memory+buffer,length); name[length]=0;
    if (strlen(name)!=length) return 0xc0000033u;
    uint32_t root=MEM32(attributes);
    const char *part=name;
    if (root) {
        *parent=media_handle(root);
        if (!*parent) return MEDIA_BAD_HANDLE;
    } else {
        *parent=media_root;
        if (!_strnicmp(part,"\\??\\",4)) part+=4;
        if (!_strnicmp(part,"D:\\",3)) part+=3;
        else if (!_strnicmp(part,"\\Device\\CdRom0\\",15)) part+=15;
        else if (!_stricmp(part,"\\Device\\CdRom0"))
            stop("raw optical device requires optical media/authentication model",202);
        else return 0xc000003au;
        if (!*parent) return MEDIA_NO_MEDIA;
    }
    const char *component=part;
    for (unsigned i=0;;++i) {
        unsigned char c=(unsigned char)part[i];
        if (c>=128) stop("non-ASCII guest media path needs Xbox character mapping",202);
        if (c=='/' || c==':' || c=='*' || c=='?' || (c && c<32)) return 0xc0000033u;
        if (!c || c=='\\') {
            size_t n=part+i-component;
            if ((n==1 && component[0]=='.') || (n==2 && component[0]=='.' && component[1]=='.') ||
                (!n && c) || (n && (component[n-1]=='.' || component[n-1]==' '))) return 0xc0000033u;
            component=part+i+1;
        }
        path[i]=c;
        if (!c) break;
    }
    return 0;
}
static uint32_t media_open(uint32_t *args,int create)
{
    uint32_t out=args[0],access=args[1],attr=args[2],iosb=args[3];
    uint32_t share=args[create?6:4],options=args[create?8:5];
    if (!out || !iosb) return MEDIA_INVALID;
    if (attr && MEM32(attr+4)) {
        uint32_t name=MEM32(attr+4),length=MEM16(name);
        fprintf(stderr,"[FILE OPEN] root=%08x name=%.*s access=%08x options=%08x\n",
                MEM32(attr),(int)(length>1024?1024:length),(char*)(memory+MEM32(name+4)),access,options);
    }
    /* Mounted title content is immutable. Only FILE_OPEN is implemented here;
     * saves/cache must have distinct writable mounts, never mutate the source. */
    if ((access&(GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|0x116u)) ||
        (create && args[7]!=1)) { media_iosb(iosb,0xc00000a2u,0); return 0xc00000a2u; }
    if (!(options&0x20) || (options&~0x865u)) stop("file open requires unsupported IO mode/options",options);
    if (create && args[4] && MEM64(args[4])) stop("allocation size on read-only FILE_OPEN",190);
    if (share&~7u) return MEDIA_INVALID;
    WCHAR path[1024]; HANDLE parent=NULL,handle=NULL; IO_STATUS_BLOCK io={0};
    uint32_t status=media_path(attr,path,&parent);
    unsigned slot=0;
    while (slot<256 && media_files[slot].host) ++slot;
    if (!status && (slot==256 || media_next_id==0x60000000u)) status=0xc000009au;
    if (!status) status=media_open_host(parent,path,access,share,options,&handle,&io);
    if (!status) {
        media_files[slot].host=handle; media_files[slot].id=media_next_id++;
        MEM32(out)=media_files[slot].id;
    }
    media_iosb(iosb,status,status ? 0 : io.Information);
    return status;
}
static void media_kernel(unsigned ordinal)
{
    unsigned count=ordinal==190 ? 9 : ordinal==202 ? 6 : ordinal==219 ? 8 : 5;
    uint32_t a[9]; for (unsigned i=0;i<count;++i) a[i]=MEM32(g_esp+4+i*4);
    int opening=ordinal==190 || ordinal==202;
    if (opening) AcquireSRWLockExclusive(&media_lock);
    else AcquireSRWLockShared(&media_lock);
    if (opening) g_eax=media_open(a,ordinal==190);
    else {
        HANDLE file=media_handle(a[0]); IO_STATUS_BLOCK io={0};
        uint32_t iosb=ordinal==219 ? a[4] : a[1];
        g_eax=MEDIA_BAD_HANDLE;
        if (!iosb) g_eax=MEDIA_INVALID;
        else if (file) {
            if (ordinal==219) {
                if (a[1] || a[2] || a[3]) stop("file event/APC delivery unsupported",219);
                LARGE_INTEGER offset; if (a[7]) offset.QuadPart=(int64_t)MEM64(a[7]);
                g_eax=(uint32_t)media_nt_read(file,NULL,NULL,NULL,&io,memory+a[5],a[6],a[7]?&offset:NULL,NULL);
            } else {
                /* Fixed-layout classes shared by the Xbox and host NT ABIs:
                 * Standard=24 bytes; Position=8 bytes. No host pointers. */
                unsigned size=a[4]==5 ? 24 : a[4]==14 ? 8 : 0;
                if (!size || (ordinal==226 && a[4]!=14)) stop("file information class unsupported",a[4]);
                if (a[3]<size) g_eax=0xc0000004u;
                else g_eax=(uint32_t)(ordinal==226 ? media_nt_set : media_nt_query)(file,&io,memory+a[2],size,a[4]);
            }
            if (g_eax==0x103u) stop("synchronous file operation unexpectedly pending",ordinal);
        }
        if (iosb) media_iosb(iosb,g_eax,io.Information);
    }
    if (opening) ReleaseSRWLockExclusive(&media_lock);
    else ReleaseSRWLockShared(&media_lock);
    g_esp+=4+count*4;
}
