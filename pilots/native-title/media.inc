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
        uint32_t status=storage_create_host(root,name,FILE_LIST_DIRECTORY|FILE_TRAVERSE|SYNCHRONIZE,
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
    uint32_t status=MEDIA_BAD_HANDLE;
    AcquireSRWLockExclusive(&media_lock);
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
    return status;
}
static void media_iosb(uint32_t va,uint32_t status,ULONG_PTR count)
{
    MEM32(va)=status; MEM32(va+4)=(uint32_t)count;
}
#include "namespace.inc"
/* Returns a validated relative ASCII path. No host path, traversal, alternate
 * stream or drive-name interpretation is available to guest input. */
static uint32_t media_path(uint32_t attributes,WCHAR path[1024],HANDLE *parent,unsigned *partition)
{
    if (!attributes || !MEM32(attributes+4)) return MEDIA_INVALID;
    if (MEM32(attributes+8)&~0x40u) stop("file object attributes unsupported",MEM32(attributes+8));
    uint32_t string=MEM32(attributes+4),length=MEM16(string),buffer=MEM32(string+4);
    if (!length || length>=1024 || length>MEM16(string+2)) return 0xc0000033u;
    char name[1024]; memcpy(name,memory+buffer,length); name[length]=0;
    if (strlen(name)!=length) return 0xc0000033u;
    uint32_t root=MEM32(attributes);
    char resolved[1024]; const char *part=name;
    if (root && root!=0xfffffffdu) {
        *parent=media_handle(root);
        if (!*parent) return MEDIA_BAD_HANDLE;
        *partition=media_partition(root);
    } else {
        /* nxdk ObDosDevicesDirectory() is the object-directory pseudo handle
         * -3, not a file handle. Resolve relative names inside guest \??. */
        char dos_name[1024]; const char *input=name;
        if(root==0xfffffffdu) {
            if(name[0]=='\\' || length+4>=sizeof(dos_name))return 0xc0000033u;
            snprintf(dos_name,sizeof(dos_name),"\\??\\%s",name);input=dos_name;
        }
        uint32_t status=namespace_resolve(input,resolved);
        if (status) return status;
        part=resolved;
        *parent=media_root;
        if (!_strnicmp(part,"\\Device\\CdRom0\\",15)) part+=15;
        else if (!_strnicmp(part,"\\Device\\Harddisk0\\Partition",27)) {
            unsigned number=(unsigned)(part[27]-'0');
            if (number<1 || number>5 || (part[28] && part[28]!='\\'))
                stop("unmounted raw/extended HDD partition",202);
            *parent=storage_roots[number]; *partition=number;
            if (!*parent) stop("HDD access requires a mounted local storage directory",number);
            part+=28; if (*part=='\\') ++part;
        }
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
    if (!status && !partition && ((access&(GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|0x116u)) ||
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
#include "directory.inc"
#include "file_async.inc"
static void media_kernel(unsigned ordinal)
{
    fsc_ensure();
    int transfer=ordinal==219 || ordinal==236;
    unsigned count=ordinal==207 ? 10 : ordinal==190 ? 9 : ordinal==202 ? 6 : transfer ? 8 : ordinal==198 ? 2 : 5;
    uint32_t a[10]; for (unsigned i=0;i<count;++i) a[i]=MEM32(g_esp+4+i*4);
    int opening=ordinal==190 || ordinal==202;
    if (opening) AcquireSRWLockExclusive(&media_lock);
    else AcquireSRWLockShared(&media_lock);
    if (opening) g_eax=media_open(a,ordinal==190);
    else {
        struct MediaFile *owned=media_owned(a[0]);
        HANDLE file=owned ? owned->host : NULL; IO_STATUS_BLOCK io={0};
        uint32_t iosb=transfer || ordinal==207 ? a[4] : a[1];int async_delivery=0;
        g_eax=MEDIA_BAD_HANDLE;
        if (!iosb) g_eax=MEDIA_INVALID;
        else if (file) {
            AcquireSRWLockExclusive(&owned->io_lock);
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
                if (ordinal==219) g_eax=fsc_read(owned,&io,memory+a[5],a[6],a[7]?&offset:NULL);
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
                else if (!size || (ordinal==226 && a[4]!=14 && a[4]!=19 && a[4]!=20 && a[4]!=13) ||
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
        if (transfer && !async_delivery) fprintf(stderr,"[FILE TRANSFER] ordinal=%u handle=%08x requested=%u completed=%llu status=%08x\n",
                              ordinal,a[0],a[6],(unsigned long long)io.Information,g_eax);
    }
    if (opening) ReleaseSRWLockExclusive(&media_lock);
    else ReleaseSRWLockShared(&media_lock);
    if (g_eax&0x80000000u) fprintf(stderr,"[FILE FAILURE] ordinal=%u status=%08x\n",ordinal,g_eax);
    g_esp+=4+count*4;
}
