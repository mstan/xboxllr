/* Complete XMountUtilityDrive service. The reviewed original owns cache
 * selection/format/geometry validation and a Z: link, returning BOOL and
 * setting thread LastError on failure. No raw disk/authentication responses.
 * This private native manifest is deliberately not an Xbox disk sector. */
#define CACHE_CORRUPT 0xc0000102u
struct CacheSlot { uint32_t title,valid; uint64_t used; uint32_t unit,reserved; };
struct CacheState { char magic[8]; uint64_t sequence; struct CacheSlot slot[3]; unsigned char hash[32]; };
_Static_assert(sizeof(struct CacheState)==120,"native cache format size");
static HANDLE cache_manifest;
static struct CacheState cache_ram_state;
typedef LONG (NTAPI *CacheDirectory)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG,BOOLEAN,PUNICODE_STRING,BOOLEAN);

static uint32_t cache_digest(struct CacheState *state,unsigned char digest[32])
{
    return (uint32_t)BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)state,
                              (ULONG)offsetof(struct CacheState,hash),digest,32);
}
static uint32_t cache_save(struct CacheState *state)
{
    uint32_t status=cache_digest(state,state->hash); IO_STATUS_BLOCK io={0};
    if(storage_ram_enabled()) {if(!status)cache_ram_state=*state;return status;}
    LARGE_INTEGER offset; offset.QuadPart=0;
    if (!status) status=(uint32_t)media_nt_write(cache_manifest,NULL,NULL,NULL,&io,state,sizeof(*state),&offset,NULL);
    if (!status && io.Information!=sizeof(*state)) status=0xc0000185u;
    if (!status) status=(uint32_t)media_nt_flush(cache_manifest,&io);
    return status;
}
static uint32_t cache_load(struct CacheState *state)
{
    if (!storage_parent) return MEDIA_NO_MEDIA;
    if(storage_ram_enabled()) {
        if(!cache_ram_state.magic[0])memcpy(cache_ram_state.magic,"XLLECCH1",8);
        *state=cache_ram_state;return 0;
    }
    IO_STATUS_BLOCK io={0}; uint32_t status=0;
    if (!cache_manifest) {
        status=storage_create_host(storage_parent,L"native-cache-v1.bin",GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,
                                   0,0x60,3,FILE_ATTRIBUTE_NORMAL,NULL,&cache_manifest,&io);
        if (status) { cache_manifest=NULL; return status; }
        if (io.Information==2) { /* FILE_CREATED, never initialize an existing/corrupt file */
            memset(state,0,sizeof(*state)); memcpy(state->magic,"XLLECCH1",8);
            return cache_save(state);
        }
    }
    LARGE_INTEGER size,offset; offset.QuadPart=0;
    if (!GetFileSizeEx(cache_manifest,&size)) return 0xc0000185u;
    if (size.QuadPart!=sizeof(*state)) return CACHE_CORRUPT;
    status=(uint32_t)media_nt_read(cache_manifest,NULL,NULL,NULL,&io,state,sizeof(*state),&offset,NULL);
    if (status) return status;
    if (io.Information!=sizeof(*state) || memcmp(state->magic,"XLLECCH1",8)) return CACHE_CORRUPT;
    unsigned char digest[32]; status=cache_digest(state,digest);
    if (status) return status;
    if (memcmp(digest,state->hash,32)) return CACHE_CORRUPT;
    for (unsigned i=0;i<3;++i) {
        struct CacheSlot *slot=&state->slot[i];
        if (slot->valid>1 || slot->reserved || slot->used>state->sequence ||
            (slot->valid && slot->unit!=16384)) return CACHE_CORRUPT;
        for (unsigned j=0;j<i;++j) if (slot->valid && state->slot[j].valid &&
            slot->title==state->slot[j].title) return CACHE_CORRUPT;
    }
    return 0;
}
/* Handle-relative traversal with OBJ_DONT_REPARSE and no delete sharing pins
 * every opened child. Only called after validating the exact absolute root.
 * NT directory enumeration: Microsoft NtQueryDirectoryFile documentation. */
static uint32_t cache_clear_children(HANDLE directory,unsigned depth)
{
    if (depth>128) return 0xc0000106u;
    CacheDirectory query=(CacheDirectory)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryDirectoryFile");
    if (!query) stop("native directory enumeration unavailable",0);
    union { uint64_t align; unsigned char bytes[1024]; } entry;
    IO_STATUS_BLOCK io={0}; BOOLEAN restart=TRUE;
    for (;;) {
        uint32_t status=(uint32_t)query(directory,NULL,NULL,NULL,&io,entry.bytes,sizeof(entry),1,TRUE,NULL,restart);
        restart=FALSE;
        if (status==0x80000006u) return 0; /* STATUS_NO_MORE_FILES */
        if (status) return status;
        if (io.Information<64) return CACHE_CORRUPT;
        uint32_t attributes,length;
        memcpy(&attributes,entry.bytes+56,4); memcpy(&length,entry.bytes+60,4);
        if (!length || length%2 || length>510 || length>io.Information-64) return CACHE_CORRUPT;
        WCHAR name[256]; memcpy(name,entry.bytes+64,length); name[length/2]=0;
        if (!wcscmp(name,L".") || !wcscmp(name,L"..")) continue;
        if (wcslen(name)!=length/2 || wcschr(name,L'\\') || wcschr(name,L'/') || wcschr(name,L':')) return CACHE_CORRUPT;
        if (attributes&FILE_ATTRIBUTE_REPARSE_POINT) return 0xc000050bu; /* never follow junctions */
        unsigned is_directory=!!(attributes&FILE_ATTRIBUTE_DIRECTORY);
        HANDLE child=NULL;
        status=media_open_host(directory,name,DELETE|FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES|SYNCHRONIZE|
            (is_directory?FILE_LIST_DIRECTORY:0),3,0x20|(is_directory?1:0x40),&child,&io);
        if (status) return status;
        if (is_directory) status=cache_clear_children(child,depth+1);
        if (!status && (attributes&FILE_ATTRIBUTE_READONLY)) {
            struct { LARGE_INTEGER times[4]; ULONG attributes; } basic={0};
            basic.attributes=(attributes&~FILE_ATTRIBUTE_READONLY)|FILE_ATTRIBUTE_NORMAL;
            status=(uint32_t)media_nt_set(child,&io,&basic,sizeof(basic),4);
        }
        if (!status) {
            BOOLEAN remove=TRUE;
            status=(uint32_t)media_nt_set(child,&io,&remove,sizeof(remove),13);
        }
        CloseHandle(child);
        if (status) return status;
    }
}
static uint32_t cache_format(unsigned partition)
{
    /* Verify the exact resolved absolute deletion target, then stay entirely
     * inside pinned, handle-relative ordinary directories. Root is retained. */
    WCHAR actual[32768],expected[32768];
    if (partition<3 || partition>5 || !storage_roots[partition]) return MEDIA_NO_MEDIA;
    struct StorageImageHandle *root=storage_image_find(storage_roots[partition]);
    if(root) {
        for(unsigned i=0;i<256;++i)if(media_files[i].host && media_files[i].partition==partition)return 0xc0000043u;
        return xllr_storage_clear_cache(root->owner,partition);
    }
    if (swprintf(expected,32768,L"%ls\\Partition%u",storage_path,partition)<0) return MEDIA_INVALID;
    DWORD count=GetFinalPathNameByHandleW(storage_roots[partition],actual,32768,FILE_NAME_NORMALIZED);
    if (!count || count>=32768 || _wcsicmp(actual,expected)) return 0xc0000022u;
    for (unsigned i=0;i<256;++i) if (media_files[i].host && media_files[i].partition==partition) return 0xc0000043u;
    return cache_clear_children(storage_roots[partition],0);
}
/* media_lock serializes namespace and guest file ownership throughout mount.
 * An exclusive manifest handle excludes another native VM using this state. */
static uint32_t cache_mount(uint32_t title,int clean,unsigned unit,unsigned *partition)
{
    if (unit!=16384) return 0xc00000bbu; /* no mismatched volume geometry */
    if (namespace_find("\\??\\Z:")>=0) return 0xc0000035u;
    unsigned free_link=0;
    while (free_link<64 && (namespace_links[free_link].named || namespace_links[free_link].refs)) ++free_link;
    if (free_link==64) return 0xc000009au;
    struct CacheState state;
    uint32_t status=cache_load(&state);
    if (status) return status;
    unsigned selected=3;
    for (unsigned i=0;i<3;++i) if (state.slot[i].valid && state.slot[i].title==title) { selected=i; break; }
    if (selected==3) {
        for (unsigned i=0;i<3;++i) if (!state.slot[i].valid) { selected=i; break; }
        if (selected==3) {
            selected=0;
            for (unsigned i=1;i<3;++i) if (state.slot[i].used<state.slot[selected].used) selected=i;
        }
        clean=1;
    }
    if (!storage_roots[selected+3]) return MEDIA_NO_MEDIA;
    if(storage_ram_enabled()) {
        for(unsigned i=0;i<256;++i)
            if(media_files[i].host && media_files[i].partition==selected+3)return 0xc0000043u;
        status=storage_ram_mount(selected+3);
        if(status)return status;
    }
    if (state.sequence==UINT64_MAX) return 0xc0000095u;
    if (clean) {
        /* Durable invalidation precedes destruction; interruption or partial
         * cleanup must never turn a half-formatted cache into a valid one. */
        state.slot[selected].valid=0;
        status=cache_save(&state);
        if (!status) status=cache_format(selected+3);
        if (status) return status;
    }
    state.slot[selected]=(struct CacheSlot){title,1,++state.sequence,unit,0};
    status=cache_save(&state);
    if (status) return status;
    char target[64]; snprintf(target,sizeof(target),"\\Device\\Harddisk0\\Partition%u",selected+3);
    status=namespace_create("\\??\\Z:",target);
    if (!status) *partition=selected+3;
    return status;
}
void pilot_mount_utility_drive(recomp_func_t set_last_nt_error)
{
    uint32_t certificate=file32(0x118);
    if (certificate<image_base || (uint64_t)certificate-image_base+12>header_bytes)
        stop("invalid title certificate for cache mount",certificate);
    uint32_t title=file32(certificate-image_base+8),clean=MEM32(g_esp+4);
    unsigned unit=16384u<<(file32(0x124)>>30),partition=0;
    AcquireSRWLockExclusive(&media_lock);
    uint32_t status=cache_mount(title,clean!=0,unit,&partition);
    ReleaseSRWLockExclusive(&media_lock);
    fprintf(stderr,"[UTILITY DRIVE] title=%08x clean=%u partition=%u status=%08x\n",title,clean,partition,status);
    if (status) {
        uint32_t original_stack=g_esp;
        PUSH32(g_esp,status); PUSH32(g_esp,0);
        set_last_nt_error(); /* original title TLS/error helper, no layout guess */
        if (g_esp!=original_stack) stop("cache LastError callback ABI mismatch",g_esp);
    }
    g_eax=status?0:1; g_esp+=8;
}
