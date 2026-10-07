static uint32_t cache_test_error;
static unsigned cache_test_error_calls;
static void cache_test_set_error(void)
{
    cache_test_error=MEM32(g_esp+4); ++cache_test_error_calls; g_esp+=8;
}
static void cache_test_unmount(void)
{
    int at=namespace_find("\\??\\Z:"); CHECK(at>=0); namespace_links[at].named=0;
}
static void cache_test_file(HANDLE directory,const WCHAR *name)
{
    HANDLE file=NULL; IO_STATUS_BLOCK io={0}; LARGE_INTEGER offset; offset.QuadPart=0;
    CHECK(!storage_create_host(directory,name,GENERIC_WRITE|SYNCHRONIZE,3,0x60,5,0,NULL,&file,&io));
    char data[]="real persisted cache bytes";
    CHECK(!media_nt_write(file,NULL,NULL,NULL,&io,data,sizeof(data),&offset,NULL) && io.Information==sizeof(data));
    CHECK(!media_nt_flush(file,&io)); CHECK(CloseHandle(file));
}
static int cache_test_exists(HANDLE parent,const WCHAR *name)
{
    HANDLE file=NULL; IO_STATUS_BLOCK io={0};
    uint32_t status=media_open_host(parent,name,FILE_READ_ATTRIBUTES|SYNCHRONIZE,3,0x20,&file,&io);
    if (!status) { CHECK(CloseHandle(file)); return 1; }
    CHECK(status==0xc0000034u || status==0xc000003au); return 0;
}
static void cache_test_abi(uint32_t clean,uint32_t expected_error)
{
    g_esp=0x18000; MEM32(g_esp)=0x12340; MEM32(g_esp+4)=clean;
    g_ebx=11; g_esi=22; g_edi=33; g_ebp=44;
    unsigned calls=cache_test_error_calls;
    pilot_mount_utility_drive(cache_test_set_error);
    CHECK(g_esp==0x18008 && g_ebx==11 && g_esi==22 && g_edi==33 && g_ebp==44);
    CHECK(g_eax==!expected_error && cache_test_error_calls==calls+!!expected_error);
    if (expected_error) CHECK(cache_test_error==expected_error);
}
static void test_cache(void)
{
    char temp[MAX_PATH],directory[MAX_PATH],path[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"xlc",0,directory));
    CHECK(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    storage_mount(directory);
    image_base=0x10000; header_bytes=0x200;
    uint32_t cert=0x10180,title=10;
    memcpy(image_bytes+0x118,&cert,4); memcpy(image_bytes+0x188,&title,4);
    cache_test_abi(0,0);
    CHECK(namespace_find("\\??\\Z:")>=0);
    char resolved[1024]; CHECK(!namespace_resolve("Z:\\payload.bin",resolved));
    CHECK(!strcmp(resolved,"\\Device\\Harddisk0\\Partition3\\payload.bin"));
    cache_test_abi(1,0xc0000035u); /* link collision must not format */
    const uint32_t out=0x1b000,iosb=0x1b010,buffer=0x1b100,offset=0x1b080;
    test_media_name("Z:\\payload.bin",0);
    uint32_t create[]={out,GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,0x1a000,iosb,0,0,3,2,0x60};
    call_kernel(190,create,9); CHECK(!g_eax); uint32_t file=MEM32(out);
    memcpy(memory+buffer,"cache payload",14); MEM64(offset)=0;
    uint32_t write[]={file,0,0,0,iosb,buffer,14,offset}; call_kernel(236,write,8); CHECK(!g_eax);
    memset(memory+buffer,0,14); call_kernel(219,write,8);
    CHECK(!g_eax && MEM32(iosb+4)==14 && !memcmp(memory+buffer,"cache payload",14));
    call_kernel(187,&file,1); CHECK(!g_eax);
    sprintf(path,"%s\\Partition3\\payload.bin",directory);
    FILE *saved=fopen(path,"rb"); char bytes[14];
    CHECK(saved && fread(bytes,1,14,saved)==14 && !memcmp(bytes,"cache payload",14)); fclose(saved);
    HANDLE second=NULL; IO_STATUS_BLOCK io={0};
    CHECK(storage_create_host(storage_parent,L"native-cache-v1.bin",GENERIC_READ|SYNCHRONIZE,3,0x60,1,0,NULL,&second,&io)==0xc0000043u);
    cache_test_unmount(); CHECK(CloseHandle(cache_manifest)); cache_manifest=NULL;
    cache_test_abi(0,0); CHECK(cache_test_exists(storage_roots[3],L"payload.bin"));
    cache_test_unmount();
    HANDLE nested=NULL;
    CHECK(!storage_create_host(storage_roots[3],L"nested",FILE_LIST_DIRECTORY|SYNCHRONIZE,3,0x21,2,FILE_ATTRIBUTE_DIRECTORY,NULL,&nested,&io));
    cache_test_file(nested,L"readonly.bin"); CHECK(CloseHandle(nested));
    /* Enumeration must keep progressing while many directory entries vanish. */
    for (unsigned i=0;i<80;++i) {
        WCHAR name[32]; swprintf(name,32,L"entry-%03u.bin",i); cache_test_file(storage_roots[3],name);
    }
    sprintf(path,"%s\\Partition3\\nested\\readonly.bin",directory); CHECK(SetFileAttributesA(path,FILE_ATTRIBUTE_READONLY));
    cache_test_file(storage_roots[1],L"preserve.bin"); cache_test_file(storage_roots[4],L"preserve.bin");
    cache_test_abi(1,0);
    CHECK(!cache_test_exists(storage_roots[3],L"payload.bin") && !cache_test_exists(storage_roots[3],L"nested"));
    CHECK(cache_test_exists(storage_roots[1],L"preserve.bin") && cache_test_exists(storage_roots[4],L"preserve.bin"));
    for (unsigned i=0;i<80;++i) {
        WCHAR name[32]; swprintf(name,32,L"entry-%03u.bin",i); CHECK(!cache_test_exists(storage_roots[3],name));
    }
    cache_test_unmount();
    /* Open guest files and host sharing failures must fail cleanup honestly. */
    test_media_name("\\Device\\Harddisk0\\Partition3\\busy.bin",0);
    call_kernel(190,create,9); CHECK(!g_eax); file=MEM32(out);
    cache_test_abi(1,0xc0000043u); CHECK(cache_test_exists(storage_roots[3],L"busy.bin"));
    call_kernel(187,&file,1); CHECK(!g_eax);
    cache_test_abi(0,0); /* dirty slot from failed clean is actually cleaned */
    CHECK(!cache_test_exists(storage_roots[3],L"busy.bin")); cache_test_unmount();
    cache_test_file(storage_roots[3],L"locked.bin"); HANDLE locked=NULL;
    CHECK(!media_open_host(storage_roots[3],L"locked.bin",GENERIC_READ|SYNCHRONIZE,1,0x60,&locked,&io));
    cache_test_abi(1,0xc0000043u); CHECK(CloseHandle(locked));
    cache_test_abi(0,0); CHECK(!cache_test_exists(storage_roots[3],L"locked.bin")); cache_test_unmount();
    /* Persistent three-slot ownership and eviction preserve the other slots. */
    unsigned partition=0; CHECK(!cache_mount(20,0,16384,&partition) && partition==4); cache_test_unmount();
    CHECK(!cache_mount(30,0,16384,&partition) && partition==5); cache_test_unmount();
    cache_test_file(storage_roots[3],L"ten.bin"); cache_test_file(storage_roots[4],L"twenty.bin"); cache_test_file(storage_roots[5],L"thirty.bin");
    CHECK(!cache_mount(10,0,16384,&partition) && partition==3); cache_test_unmount();
    CHECK(!cache_mount(40,0,16384,&partition) && partition==4); cache_test_unmount();
    CHECK(cache_test_exists(storage_roots[3],L"ten.bin") && !cache_test_exists(storage_roots[4],L"twenty.bin") && cache_test_exists(storage_roots[5],L"thirty.bin"));
    CHECK(cache_mount(50,0,32768,&partition)==0xc00000bbu && namespace_find("\\??\\Z:")<0);
    /* A checksum failure cannot implicitly reinitialize or erase storage. */
    struct CacheState state; CHECK(!cache_load(&state));
    unsigned char wrong=0; LARGE_INTEGER zero; zero.QuadPart=0;
    CHECK(!media_nt_write(cache_manifest,NULL,NULL,NULL,&io,&wrong,1,&zero,NULL));
    CHECK(cache_mount(10,1,16384,&partition)==CACHE_CORRUPT && cache_test_exists(storage_roots[3],L"ten.bin"));
    CHECK(!cache_save(&state));
    /* A directory link must never cause formatting to enter another partition. */
    char target[MAX_PATH]; sprintf(target,"%s\\Partition1",directory);
    sprintf(path,"%s\\Partition3\\escape",directory);
    if (CreateSymbolicLinkA(path,target,SYMBOLIC_LINK_FLAG_DIRECTORY|2)) {
        CHECK(cache_format(3)==0xc000050bu && cache_test_exists(storage_roots[1],L"preserve.bin"));
        CHECK(RemoveDirectoryA(path));
    } else {
        CHECK(GetLastError()==ERROR_PRIVILEGE_NOT_HELD);
        fprintf(stderr,"SKIP: symlink fixture requires Windows developer mode/privilege\n");
    }
    WCHAR original[32768]; wcscpy(original,storage_path); wcscat(storage_path,L"-outside");
    CHECK(cache_format(3)==0xc0000022u && cache_test_exists(storage_roots[3],L"ten.bin")); wcscpy(storage_path,original);
    DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for (unsigned i=0;i<100;++i) { CHECK(!cache_mount(10,0,16384,&partition)); cache_test_unmount(); }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    for (unsigned i=3;i<=5;++i) CHECK(!cache_format(i));
    CHECK(CloseHandle(cache_manifest)); cache_manifest=NULL;
    sprintf(path,"%s\\native-cache-v1.bin",directory); CHECK(DeleteFileA(path));
    sprintf(path,"%s\\Partition1\\preserve.bin",directory); CHECK(DeleteFileA(path));
    for (unsigned i=1;i<=5;++i) {
        CHECK(CloseHandle(storage_roots[i])); storage_roots[i]=NULL;
        sprintf(path,"%s\\Partition%u",directory,i); CHECK(RemoveDirectoryA(path));
    }
    CHECK(CloseHandle(storage_parent)); storage_parent=NULL; CHECK(RemoveDirectoryA(directory));
    fprintf(stderr,"PASS: utility-drive ABI/error callback, real Z: IO/persistence, clean recursive format, ownership/LRU, failure recovery, corruption rejection and stable handles\n");
}
