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
static HANDLE media_root,media_identity,storage_roots[6],storage_parent;
static WCHAR storage_path[32768];
static SRWLOCK media_lock=SRWLOCK_INIT;
struct MediaFile { HANDLE host,async_idle,async_signal; uint32_t id,access; unsigned partition,cacheable,asynchronous,pending; SRWLOCK io_lock; };
static struct MediaFile media_files[256];
static uint32_t media_next_id=0x50000001u;

/* Optional sparse host-file diagnosis. These observations retain actual file
 * calls, lock ownership and completion; slow wall time alone is not I/O cost.
 * At most 256 records are written, after releasing the media owner lock. */
struct MediaReadProfile {uint64_t begin,end,cpu;unsigned called,cache_hit,cpu_available;};
struct MediaProfile {
    uint64_t begin,cpu,io_wait,transfer_log;
    unsigned cpu_available,io_sampled,log_sampled;
    struct MediaReadProfile read;
};
static uint64_t media_profile_cpu(unsigned *available)
{
    *available=0;
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    if(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) {
        *available=1;
        return ((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
            ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
    }
#endif
    return 0;
}
static struct MediaProfile media_profile_begin(void)
{
    static LONG enabled;
    LONG setting=InterlockedCompareExchange(&enabled,0,0);
    if(!setting) {
        const char *value=getenv("XLLE_PILOT_FILE_PROFILE");
        LONG selected=value && !strcmp(value,"1")?2:1;
        InterlockedCompareExchange(&enabled,selected,0);setting=selected;
    }
    struct MediaProfile profile={0};
    if(setting==2) {
        profile.cpu=media_profile_cpu(&profile.cpu_available);
        profile.begin=dispatch_monotonic();
    }
    return profile;
}
static void media_profile_end(const char *operation,unsigned ordinal,uint32_t handle,uint32_t bytes,
    uint32_t status,struct MediaProfile profile,uint64_t owner_wait)
{
    if(!profile.begin)return;
    uint64_t end=dispatch_monotonic();
    static LONG records;
    if(end-profile.begin<=500000 || InterlockedIncrement(&records)>256)return;
    unsigned available;uint64_t cpu=media_profile_cpu(&available);
    available=available && profile.cpu_available && cpu>=profile.cpu;
    fprintf(stderr,"[FILE SLOW] thread=%lu operation=%s ordinal=%u handle=%08x bytes=%u status=%08x begin=%llu end=%llu elapsed_ms=%.3f owner_lock_ms=%.3f thread_cpu_ms=%.3f cpu_available=%u io_lock_ms=%.3f io_lock_sampled=%u transfer_log_ms=%.3f transfer_log_sampled=%u nt_read_called=%u cache_hit=%u nt_read_begin=%llu nt_read_end=%llu nt_read_ms=%.3f nt_read_thread_cpu_ms=%.3f nt_read_cpu_available=%u\n",
        GetCurrentThreadId(),operation,ordinal,handle,bytes,status,
        (unsigned long long)profile.begin,(unsigned long long)end,(end-profile.begin)/10000.0,
        owner_wait/10000.0,available?(cpu-profile.cpu)/10000.0:0.0,available,
        profile.io_wait/10000.0,profile.io_sampled,profile.transfer_log/10000.0,profile.log_sampled,
        profile.read.called,profile.read.cache_hit,(unsigned long long)profile.read.begin,
        (unsigned long long)profile.read.end,(profile.read.end-profile.read.begin)/10000.0,
        profile.read.cpu/10000.0,profile.read.cpu_available);
}
typedef LONG (NTAPI *MediaOpen)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
typedef LONG (NTAPI *MediaRead)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,PLARGE_INTEGER,PULONG);
typedef LONG (NTAPI *MediaInfo)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
typedef LONG (NTAPI *MediaCreate)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
typedef LONG (NTAPI *MediaFlush)(HANDLE,PIO_STATUS_BLOCK);
typedef LONG (NTAPI *MediaDirectory)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG,BOOLEAN,PUNICODE_STRING,BOOLEAN);
static MediaOpen media_nt_open;
static MediaRead media_nt_read,media_nt_write;
static MediaInfo media_nt_query,media_nt_set,media_nt_volume;
static MediaCreate media_nt_create;
static MediaFlush media_nt_flush;
static MediaDirectory media_nt_directory;
static INIT_ONCE media_once=INIT_ONCE_STATIC_INIT;
#include "file_cache.inc"

static BOOL CALLBACK media_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once; (void)parameter; (void)context;
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    media_nt_open=(MediaOpen)(void*)GetProcAddress(nt,"NtOpenFile");
    media_nt_read=(MediaRead)(void*)GetProcAddress(nt,"NtReadFile");
    media_nt_query=(MediaInfo)(void*)GetProcAddress(nt,"NtQueryInformationFile");
    media_nt_set=(MediaInfo)(void*)GetProcAddress(nt,"NtSetInformationFile");
    media_nt_volume=(MediaInfo)(void*)GetProcAddress(nt,"NtQueryVolumeInformationFile");
    media_nt_write=(MediaRead)(void*)GetProcAddress(nt,"NtWriteFile");
    media_nt_create=(MediaCreate)(void*)GetProcAddress(nt,"NtCreateFile");
    media_nt_flush=(MediaFlush)(void*)GetProcAddress(nt,"NtFlushBuffersFile");
    media_nt_directory=(MediaDirectory)(void*)GetProcAddress(nt,"NtQueryDirectoryFile");
    if (!media_nt_open || !media_nt_read || !media_nt_query || !media_nt_set ||
        !media_nt_volume || !media_nt_write || !media_nt_create || !media_nt_flush || !media_nt_directory)
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
static uint32_t storage_create_host(HANDLE parent,const WCHAR *path,uint32_t access,
    uint32_t share,uint32_t options,uint32_t disposition,uint32_t attributes,
    LARGE_INTEGER *allocation,HANDLE *out,IO_STATUS_BLOCK *io)
{
    UNICODE_STRING name={(USHORT)(wcslen(path)*2),(USHORT)(wcslen(path)*2),(PWSTR)path};
    OBJECT_ATTRIBUTES attr={sizeof(attr),parent,&name,0x1040,NULL,NULL};
    InitOnceExecuteOnce(&media_once,media_start,NULL,NULL);
    return (uint32_t)media_nt_create(out,access,&attr,io,allocation,attributes,share,disposition,options,NULL,0);
}
static void storage_mount(const char *directory)
{
    WCHAR path[32768];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,directory,-1,path,32768)) stop("invalid storage directory",0);
    HANDLE root=CreateFileW(path,FILE_LIST_DIRECTORY|FILE_ADD_SUBDIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (root==INVALID_HANDLE_VALUE) stop("cannot open local storage directory",GetLastError());
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(root,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) stop("storage root must be an ordinary directory",0);
    WCHAR resolved[32768];
    DWORD length=GetFinalPathNameByHandleW(root,resolved,32768,FILE_NAME_NORMALIZED);
    if (!length || length>=32768 || !_wcsnicmp(resolved,L"\\\\?\\UNC\\",8)) stop("storage root must be local",0);
    /* Reject using installed content as the writable root or its ancestor.
     * Both already exist; use final handle paths rather than textual aliases. */
    if (media_content_path) {
        WCHAR content[32768];
        if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,media_content_path,-1,content,32768)) stop("invalid content directory",0);
        HANDLE check=CreateFileW(content,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
        if (check!=INVALID_HANDLE_VALUE) {
            DWORD count=GetFinalPathNameByHandleW(check,content,32768,FILE_NAME_NORMALIZED); CloseHandle(check);
            if (!count || count>=32768) stop("cannot resolve content directory",0);
            size_t common=count<length ? count : length;
            if (!_wcsnicmp(content,resolved,common) &&
                (content[common]==0 || content[common]=='\\') && (resolved[common]==0 || resolved[common]=='\\'))
                stop("writable storage overlaps installed content",0);
        }
    }
    for (unsigned i=1;i<=5;++i) {
        WCHAR name[16]; swprintf(name,16,L"Partition%u",i); IO_STATUS_BLOCK io={0};
        uint32_t status=storage_create_host(root,name,FILE_LIST_DIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
             FILE_SHARE_READ|FILE_SHARE_WRITE,0x21,3,FILE_ATTRIBUTE_DIRECTORY,NULL,&storage_roots[i],&io);
        if (status) stop("cannot mount native HDD partition",status);
    }
    storage_parent=root; wcscpy(storage_path,resolved);
    fprintf(stderr,"[STORAGE] five local native partitions; host-backed capacity in 16 KiB logical units\n");
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
        WCHAR image_name[256];
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,PILOT_TITLE_IMAGE,-1,image_name,256))
            stop("invalid title content image name",0);
        status=media_open_host(root,image_name,GENERIC_READ|SYNCHRONIZE,FILE_SHARE_READ,0x60,&identity,&io);
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
#ifdef PILOT_FILES_LLE
static void files_verify_abi(const char *expected);
void pilot_verify_installed_media(void) { files_verify_abi(PILOT_XBE_SHA256); }
#else
void pilot_verify_installed_media(void) { media_verify_abi(PILOT_XBE_SHA256); }
#endif
static struct MediaFile *media_owned(uint32_t id)
{
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].id==id) return &media_files[i];
    return NULL;
}
static HANDLE media_handle(uint32_t id)
{
    struct MediaFile *file=media_owned(id); return file ? file->host : NULL;
}
/* The guest file signal follows publication of its guest IOSB, not the earlier
 * native kernel signal. It signals each completed request; idle drains all. */
static HANDLE media_wait_duplicate(uint32_t id,uint32_t *status)
{
    HANDLE host=NULL;*status=MEDIA_BAD_HANDLE;AcquireSRWLockShared(&media_lock);
    struct MediaFile *file=media_owned(id);
    if(file) {
        *status=0xc0000022u;
        if(file->access&SYNCHRONIZE)
            *status=DuplicateHandle(GetCurrentProcess(),file->asynchronous?file->async_signal:file->host,
                GetCurrentProcess(),&host,0,FALSE,DUPLICATE_SAME_ACCESS)?0:0xc000009au;
    }
    ReleaseSRWLockShared(&media_lock);return host;
}
static unsigned media_partition(uint32_t id)
{
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].id==id) return media_files[i].partition;
    return 0;
}
static uint32_t media_close(uint32_t id)
{
    struct MediaProfile profile=media_profile_begin();
    uint32_t status=MEDIA_BAD_HANDLE;
    uint64_t owner_begin=profile.begin?dispatch_monotonic():0;
    AcquireSRWLockExclusive(&media_lock);
    uint64_t owner_wait=owner_begin?dispatch_monotonic()-owner_begin:0;
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].id==id) {
        if(media_files[i].async_idle) {
            if(WaitForSingleObject(media_files[i].async_idle,INFINITE)!=WAIT_OBJECT_0)stop("file close completion drain failed",id);
        }
        /* The final callback signals idle while still holding io_lock. Join
         * that release before closing/reusing this slot and its SRW lock. */
        AcquireSRWLockExclusive(&media_files[i].io_lock);
        if(media_files[i].async_idle) {
            if(!CloseHandle(media_files[i].async_idle) || !CloseHandle(media_files[i].async_signal))stop("file completion event close failed",id);
            media_files[i].async_idle=media_files[i].async_signal=NULL;
        }
        if (!CloseHandle(media_files[i].host)) stop("owned media handle close failed",id);
        fsc_forget(id);
        media_files[i].host=NULL; status=0;
        ReleaseSRWLockExclusive(&media_files[i].io_lock);break;
    }
    ReleaseSRWLockExclusive(&media_lock);
    media_profile_end("close",0,id,0,status,profile,owner_wait);
    return status;
}
static void media_iosb(uint32_t va,uint32_t status,ULONG_PTR count)
{
    MEM32(va)=status; MEM32(va+4)=(uint32_t)count;
}
#include "namespace.inc"
#if defined(PILOT_OPTICAL_EMPTY_NATIVE) && PILOT_OPTICAL_EMPTY_NATIVE
#include "media_optical.inc"
#endif
#include "media_path.inc"
static uint32_t media_open(uint32_t *args,int create)
{
    uint32_t out=args[0],access=args[1],attr=args[2],iosb=args[3];
    uint32_t share=args[create?6:4],options=args[create?8:5];
    if (!out || !iosb) return MEDIA_INVALID;
    if (attr && MEM32(attr+4)) {
        uint32_t name=MEM32(attr+4),length=MEM16(name);
        fprintf(stderr,"[FILE OPEN] root=%08x name=%.*s access=%08x options=%08x time=%llu\n",
                MEM32(attr),(int)(length>1024?1024:length),(char*)(memory+MEM32(name+4)),access,options,
                (unsigned long long)dispatch_monotonic());
    }
    /* Preserve native asynchronous/nonbuffered open semantics. Reads require
     * the separate completion-delivery contract below. */
    if (options&~0x80586fu)stop("file open requires unsupported IO options",options);
    if (share&~7u) return MEDIA_INVALID;
    WCHAR path[1024]; HANDLE parent=NULL,handle=NULL; IO_STATUS_BLOCK io={0};
    unsigned partition=0;
    uint32_t status=media_path(attr,path,&parent,&partition);
    if (!status && !partition && ((access&(GENERIC_WRITE|GENERIC_ALL|MAXIMUM_ALLOWED|DELETE|WRITE_DAC|WRITE_OWNER|0x116u)) ||
        (options&0x1000) || (create && args[7]!=1))) status=0xc00000a2u;
    if (!status && !partition && create && args[4] && MEM64(args[4])) stop("allocation size on read-only FILE_OPEN",190);
    unsigned slot=0;
    while (slot<256 && media_files[slot].host) ++slot;
    if (!status && (slot==256 || media_next_id==0x60000000u)) status=0xc000009au;
    /* Xbox startup queries network-open metadata through GENERIC_WRITE-only
     * handles. Desktop NT requires FILE_READ_ATTRIBUTES for that operation.
     * Supply metadata access internally, without adding FILE_READ_DATA. */
    uint32_t host_access=access|FILE_READ_ATTRIBUTES;
    /* Installed HDD files use native buffering, following Phobos's mapping.
     * The original XAPI interface submits byte-counted records with option0x48;
     * desktop NO_BUFFERING would impose a different device's sector rules.
     * Retain explicit write-through/flush and actual byte counts/completion. */
    uint32_t host_options=partition && !(options&1)?options&~8u:options;
    if (!status && create) {
        LARGE_INTEGER allocation; if (args[4]) allocation.QuadPart=(int64_t)MEM64(args[4]);
        status=storage_create_host(parent,path,host_access,share,host_options,args[7],args[5],args[4]?&allocation:NULL,&handle,&io);
    } else if (!status) status=media_open_host(parent,path,host_access,share,host_options,&handle,&io);
    uint32_t granted_access=0;
    if(!status) {
        IO_STATUS_BLOCK access_io={0};
        status=(uint32_t)media_nt_query(handle,&access_io,&granted_access,sizeof(granted_access),8);
        if(status)CloseHandle(handle);
    }
    if (!status) {
        media_files[slot].async_idle=media_files[slot].async_signal=NULL;media_files[slot].pending=0;
        if(!(options&0x30)) {
            media_files[slot].async_idle=CreateEventW(NULL,TRUE,TRUE,NULL);
            media_files[slot].async_signal=CreateEventW(NULL,TRUE,TRUE,NULL);
            if(!media_files[slot].async_idle || !media_files[slot].async_signal) {
                if(media_files[slot].async_idle)CloseHandle(media_files[slot].async_idle);
                if(media_files[slot].async_signal)CloseHandle(media_files[slot].async_signal);
                media_files[slot].async_idle=media_files[slot].async_signal=NULL;
                CloseHandle(handle);return 0xc000009au;
            }
        }
        media_files[slot].host=handle; media_files[slot].id=media_next_id++;media_files[slot].access=granted_access;
        media_files[slot].partition=partition;
        media_files[slot].asynchronous=!(options&0x30);
        media_files[slot].cacheable=!media_files[slot].asynchronous && !(share&FILE_SHARE_WRITE) && !(options&8) &&
            !!(access&(FILE_READ_DATA|GENERIC_READ|GENERIC_ALL));
        InitializeSRWLock(&media_files[slot].io_lock);
        MEM32(out)=media_files[slot].id;
    }
    /* Original Nt open/create rejection preserves caller IOSB and output
     * handle. Only publish after a real file object has been acquired. */
    if(!status)media_iosb(iosb,0,io.Information);
    return status;
}
/* NtQueryFullAttributesFile: OBJECT_ATTRIBUTES + FILE_NETWORK_OPEN_INFORMATION,
 * NTSTATUS/RET8. Use the same confined namespace and no-reparse open policy as
 * ordinary media files, without allocating a guest handle or granting writes.
 * Original FATX reports the actual EOF in both size fields and zeroes the ABI
 * padding. Host timestamps/EOF/attributes come only from the real query. */
static uint32_t media_query_full_attributes(uint32_t attributes,uint32_t output)
{
    if(!output || (uint64_t)output+56>UINT64_C(0x100000000) ||
       !attributes || (uint64_t)attributes+12>UINT64_C(0x100000000))return MEDIA_INVALID;
    uint32_t name=MEM32(attributes+4);
    if(name && ((uint64_t)name+8>UINT64_C(0x100000000) ||
        (uint64_t)MEM32(name+4)+MEM16(name)>UINT64_C(0x100000000)))return MEDIA_INVALID;
    WCHAR path[1024];HANDLE parent=NULL,file=NULL;unsigned partition=0;
    uint32_t status=media_path(attributes,path,&parent,&partition);
    if(status)return status;
    IO_STATUS_BLOCK io={0};
    status=media_open_host(parent,path,FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0x20,&file,&io);
    if(status)return status;
    union {uint64_t alignment;unsigned char bytes[56];} stage={0};
    status=(uint32_t)media_nt_query(file,&io,stage.bytes,sizeof(stage.bytes),34);
    if(status==0x103u)stop("synchronous named metadata unexpectedly pending",210);
    if(!CloseHandle(file))stop("named metadata handle release failed",210);
    if(!status) {
        memcpy(stage.bytes+32,stage.bytes+40,8); /* proved original FATX sizes */
        memset(stage.bytes+52,0,4);
        memcpy(memory+output,stage.bytes,sizeof(stage.bytes));
    }
    return status;
}
#include "directory.inc"
#include "file_async.inc"
static void media_kernel(unsigned ordinal)
{
#if defined(PILOT_OPTICAL_EMPTY_NATIVE) && PILOT_OPTICAL_EMPTY_NATIVE
    if(ordinal==91 && (uint64_t)g_esp+8>UINT64_C(0x100000000))
        stop("invalid dismount argument frame",g_esp);
#endif
    struct MediaProfile profile=media_profile_begin();
    fsc_ensure();
    int transfer=ordinal==219 || ordinal==236;
    unsigned count=ordinal==207 ? 10 : ordinal==190 ? 9 : ordinal==202 ? 6 : transfer ? 8 : ordinal==198 || ordinal==210 ? 2 : 5;
#if defined(PILOT_OPTICAL_EMPTY_NATIVE) && PILOT_OPTICAL_EMPTY_NATIVE
    if(ordinal==91)count=1;
#endif
    uint32_t a[10]; for (unsigned i=0;i<count;++i) a[i]=MEM32(g_esp+4+i*4);
    int opening=ordinal==190 || ordinal==202;
    uint64_t owner_begin=profile.begin?dispatch_monotonic():0;
    if (opening) AcquireSRWLockExclusive(&media_lock);
    else AcquireSRWLockShared(&media_lock);
    uint64_t owner_wait=owner_begin?dispatch_monotonic()-owner_begin:0;
    if (opening) g_eax=media_open(a,ordinal==190);
#if defined(PILOT_OPTICAL_EMPTY_NATIVE) && PILOT_OPTICAL_EMPTY_NATIVE
    else if(ordinal==91)g_eax=media_dismount_empty_optical(a[0]);
#endif
    else if(ordinal==210)g_eax=media_query_full_attributes(a[0],a[1]);
    else {
        struct MediaFile *owned=media_owned(a[0]);
        HANDLE file=owned ? owned->host : NULL; IO_STATUS_BLOCK io={0};
        uint32_t iosb=transfer || ordinal==207 ? a[4] : a[1];int async_delivery=0;
        g_eax=MEDIA_BAD_HANDLE;
        if (!iosb) g_eax=MEDIA_INVALID;
        else if (file) {
            uint64_t io_begin=profile.begin?dispatch_monotonic():0;
            AcquireSRWLockExclusive(&owned->io_lock);
            if(io_begin){profile.io_wait=dispatch_monotonic()-io_begin;profile.io_sampled=1;}
            if(ordinal==207) g_eax=media_directory(file,&io,a);
            else if (transfer) {
                if(ordinal==219 && !(owned->access&FILE_READ_DATA))g_eax=0xc0000022u;
                else if(ordinal==236 && !(owned->access&(FILE_WRITE_DATA|FILE_APPEND_DATA)))g_eax=0xc0000022u;
                else if(owned->asynchronous) {
                    if(ordinal==236)fsc_forget(owned->id);
                    async_delivery=1;g_eax=media_async_transfer(owned,a,ordinal==236);
                } else {
                /* A context alone requests no callback. XAPI ReadFile passes
                 * its OVERLAPPED pointer here even with no APC routine/event.
                 * Completion ports cannot be associated with our file owner. */
                if (a[1] || a[2]) {
                    fprintf(stderr,"[FILE DELIVERY] event=%08x routine=%08x context=%08x\n",a[1],a[2],a[3]);
                    stop("file event/APC delivery unsupported",ordinal);
                }
                LARGE_INTEGER offset; if (a[7]) offset.QuadPart=(int64_t)MEM64(a[7]);
                if (ordinal==219) g_eax=fsc_read(owned,&io,memory+a[5],a[6],a[7]?&offset:NULL,
                    profile.begin?&profile.read:NULL);
                else {
                    fsc_forget(owned->id);
                    g_eax=(uint32_t)media_nt_write(file,NULL,NULL,NULL,&io,memory+a[5],a[6],a[7]?&offset:NULL,NULL);
                }
                }
            } else if (ordinal==198) g_eax=(uint32_t)media_nt_flush(file,&io);
            else if (ordinal==218) {
                if (!media_partition(a[0]) || a[4]!=3) stop("volume information class/mount unsupported",a[4]);
                if (a[3]<24) g_eax=0xc0000004u;
                else {
                    struct { LARGE_INTEGER total,available; ULONG sectors,bytes; } geometry={0};
                    g_eax=(uint32_t)media_nt_volume(file,&io,&geometry,sizeof(geometry),3);
                    if (!g_eax) {
                        uint64_t unit=(uint64_t)geometry.sectors*geometry.bytes;
                        if (!unit || geometry.total.QuadPart<0 || geometry.available.QuadPart<0 ||
                            (uint64_t)geometry.total.QuadPart>UINT64_MAX/unit ||
                            (uint64_t)geometry.available.QuadPart>UINT64_MAX/unit) stop("invalid host volume capacity",218);
                        memset(memory+a[2],0,a[3]);
                        MEM64(a[2])=(uint64_t)geometry.total.QuadPart*unit/16384;
                        MEM64(a[2]+8)=(uint64_t)geometry.available.QuadPart*unit/16384;
                        MEM32(a[2]+16)=32; MEM32(a[2]+20)=512; io.Information=24;
                    }
                }
            } else {
                /* Original Nt/FATX qualification, not desktop NT behavior.
                 * In-range unqualified classes retain an explicit guard. */
                unsigned size=a[4]==34 ? 56 : a[4]==4 ? 40 : a[4]==5 ? 24 : a[4]==14 || a[4]==19 || a[4]==20 ? 8 : a[4]==13 ? 1 : 0;
                if (a[4]==0 || a[4]>=37) g_eax=0xc0000003u;
                else if (!size || (ordinal==226 && a[4]!=4 && a[4]!=14 && a[4]!=19 && a[4]!=20 && a[4]!=13) ||
                    (ordinal==211 && a[4]!=4 && a[4]!=5 && a[4]!=14 && a[4]!=34)) stop("file information class unsupported",a[4]);
                else if (a[3]<size) g_eax=0xc0000004u;
                else if (ordinal==211 && (a[4]==4 || a[4]==5)) {
                    memset(memory+a[2],0,a[3]);g_eax=MEDIA_INVALID;
                }
                else {
                    /* Desktop NT probes 8-byte alignment; Xbox callers only
                     * guarantee 4. Never pass the guest's stack struct as a
                     * native ABI struct, even when the field offsets match. */
                    union { uint64_t alignment; unsigned char bytes[56]; } stage={0};
                    if (ordinal==226 && a[4]!=14) fsc_forget(owned->id);
                    /* Basic information sets real file/directory timestamps
                     * and attributes. Blinx updates its existing save folder
                     * after Results; metadata retains the documented host
                     * semantics, including zero fields meaning unchanged. */
                    if (ordinal==226) memcpy(stage.bytes,memory+a[2],size);
                    g_eax=(uint32_t)(ordinal==226 ? media_nt_set : media_nt_query)(file,&io,stage.bytes,size,a[4]);
                    if (ordinal==211 && !g_eax) {
                        if(a[4]==34) {
                            memcpy(stage.bytes+32,stage.bytes+40,8);
                            memset(memory+a[2],0,a[3]);
                        }
                        memcpy(memory+a[2],stage.bytes,size);
                    }
                }
            }
            if (g_eax==0x103u && !async_delivery) stop("synchronous file operation unexpectedly pending",ordinal);
            ReleaseSRWLockExclusive(&owned->io_lock);
        }
        /* Original immediate errors preserve IOSB. Warning completions
         * (directory overflow/exhaustion) publish it. Accepted asynchronous
         * requests own their later success/failure publication separately. */
        if (iosb && !async_delivery && (g_eax&0xc0000000u)!=0xc0000000u)
            media_iosb(iosb,g_eax,io.Information);
        if (transfer && !async_delivery) {
            uint64_t log_begin=profile.begin?dispatch_monotonic():0;
            fprintf(stderr,"[FILE TRANSFER] ordinal=%u handle=%08x requested=%u completed=%llu status=%08x\n",
                ordinal,a[0],a[6],(unsigned long long)io.Information,g_eax);
            if(log_begin){profile.transfer_log=dispatch_monotonic()-log_begin;profile.log_sampled=1;}
        }
    }
    if (opening) ReleaseSRWLockExclusive(&media_lock);
    else ReleaseSRWLockShared(&media_lock);
    if(profile.begin)media_profile_end("service",ordinal,opening?(!g_eax?MEM32(a[0]):0):ordinal==210?0:a[0],
        transfer?a[6]:0,g_eax,profile,owner_wait);
    if (g_eax&0x80000000u) fprintf(stderr,"[FILE FAILURE] ordinal=%u status=%08x\n",ordinal,g_eax);
    g_esp+=4+count*4;
}
