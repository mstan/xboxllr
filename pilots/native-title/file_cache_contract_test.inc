static MediaRead fsc_test_native_read;
static LONG fsc_test_reads;
static LONG NTAPI fsc_test_read(HANDLE file,HANDLE event,PVOID apc,PVOID context,PIO_STATUS_BLOCK io,
                               PVOID buffer,ULONG length,PLARGE_INTEGER offset,PULONG key)
{
    InterlockedIncrement(&fsc_test_reads);
    return fsc_test_native_read(file,event,apc,context,io,buffer,length,offset,key);
}
static struct MediaFile *fsc_test_file;
static unsigned char fsc_test_payload[9000];
static DWORD WINAPI fsc_test_worker(void *unused)
{
    (void)unused;
    for (unsigned i=0;i<200;++i) {
        unsigned char bytes[600]; LARGE_INTEGER offset; offset.QuadPart=(i*37)%7000;
        IO_STATUS_BLOCK io={0}; AcquireSRWLockExclusive(&fsc_test_file->io_lock);
        uint32_t status=fsc_read(fsc_test_file,&io,bytes,sizeof(bytes),&offset);
        ReleaseSRWLockExclusive(&fsc_test_file->io_lock);
        CHECK(!status && io.Information==sizeof(bytes) && !memcmp(bytes,fsc_test_payload+offset.QuadPart,sizeof(bytes)));
    }
    return 0;
}
static void test_file_cache(void)
{
    char directory[MAX_PATH],temp[MAX_PATH],path[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"xlf",0,directory));
    CHECK(DeleteFileA(directory) && CreateDirectoryA(directory,NULL)); storage_mount(directory);
    sprintf(path,"%s\\Partition1\\cache.bin",directory);
    for (unsigned i=0;i<sizeof(fsc_test_payload);++i) fsc_test_payload[i]=(unsigned char)(i*73+(i>>8));
    FILE *host=fopen(path,"wb"); CHECK(host && fwrite(fsc_test_payload,1,sizeof(fsc_test_payload),host)==sizeof(fsc_test_payload)); fclose(host);
    call_kernel(35,NULL,0); CHECK(g_eax==16 && vm_service_bytes==65536);
    MEMORY_BASIC_INFORMATION backing; CHECK(VirtualQuery(fsc_bytes,&backing,sizeof(backing)) && backing.State==MEM_COMMIT);
    unsigned char *previous=fsc_bytes; uint32_t pages=2; call_kernel(37,&pages,1); CHECK(!g_eax && vm_service_bytes==8192);
    CHECK(VirtualQuery(previous,&backing,sizeof(backing)) && backing.State==MEM_FREE);
    call_kernel(35,NULL,0); CHECK(g_eax==2);
    const uint32_t out=0x1b000,iosb=0x1b010,info=0x1b020,offset=0x1b060,buffer=0x1b100;
    test_media_name("\\Device\\Harddisk0\\Partition1\\cache.bin",0);
    uint32_t open[]={out,GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,0x1a000,iosb,1,0x60};
    call_kernel(202,open,6); CHECK(!g_eax); uint32_t file=MEM32(out);
    fsc_test_native_read=media_nt_read; media_nt_read=fsc_test_read;
    uint32_t read[]={file,0,0,0,iosb,buffer,5000,offset}; MEM64(offset)=100;
    call_kernel(219,read,8); CHECK(!g_eax && MEM32(iosb+4)==5000 && !memcmp(memory+buffer,fsc_test_payload+100,5000));
    LONG misses=fsc_test_reads; memset(memory+buffer,0,5000);
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses && !memcmp(memory+buffer,fsc_test_payload+100,5000));
    uint32_t query[]={file,iosb,info,8,14}; call_kernel(211,query,5); CHECK(!g_eax && MEM64(info)==5100);
    MEM64(info)=300; call_kernel(226,query,5); CHECK(!g_eax);
    read[6]=20; read[7]=0;
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses && !memcmp(memory+buffer,fsc_test_payload+300,20));
    call_kernel(211,query,5); CHECK(!g_eax && MEM64(info)==320);
    /* FILE_USE_FILE_POINTER_POSITION and explicit subrange hits update cursor. */
    MEM64(offset)=(uint64_t)-2; read[7]=offset;
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses && !memcmp(memory+buffer,fsc_test_payload+320,20));
    call_kernel(36,NULL,0); MEM64(offset)=100;
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses+1);
    pages=2049; call_kernel(37,&pages,1); CHECK(g_eax==VM_INVALID);
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses+1);
    /* Failure preserves both configured size and cached data. The same budget
     * constrains virtual memory and real native cache allocations. */
    uint32_t base=0,available=(uint32_t)(64ull*1024*1024-vm_committed(0,0x100000000ull)-vm_service_bytes);
    uint32_t too_many=available+4096;
    CHECK(vm_allocate(&base,0,&too_many,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==VM_NOMEM);
#ifdef PILOT_MEMORY_LLE
    /* The original kernel already owns physical RAM, so allocating every
     * nominal console byte is not a valid fixture assumption. Inject shared
     * native-service budget pressure; original VM exhaustion is tested by the
     * memory contract. This never alters the production cache implementation. */
    uint64_t saved_services=vm_service_bytes;
    vm_service_bytes=64ull*1024*1024-vm_committed(0,0x100000000ull);
#else
    CHECK(!vm_allocate(&base,0,&available,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#endif
    pages=3; call_kernel(37,&pages,1); CHECK(g_eax==VM_NOMEM);
    call_kernel(35,NULL,0); CHECK(g_eax==2);
    call_kernel(219,read,8); CHECK(!g_eax && fsc_test_reads==misses+1);
#ifdef PILOT_MEMORY_LLE
    vm_service_bytes=saved_services;
#else
    uint32_t release=0; CHECK(!vm_free(&base,&release,MEM_RELEASE));
#endif
    call_kernel(37,&pages,1); CHECK(!g_eax && vm_service_bytes==12288);
    /* Resizing while real reads run cannot expose freed cache buffers. */
    fsc_test_file=media_owned(file);
    HANDLE workers[]={CreateThread(NULL,0,fsc_test_worker,NULL,0,NULL),CreateThread(NULL,0,fsc_test_worker,NULL,0,NULL)};
    CHECK(workers[0] && workers[1]);
    for (unsigned i=0;i<60;++i) CHECK(!fsc_resize(i%5));
    CHECK(WaitForMultipleObjects(2,workers,TRUE,5000)==WAIT_OBJECT_0);
    CHECK(CloseHandle(workers[0]) && CloseHandle(workers[1]));
    pages=2; call_kernel(37,&pages,1); CHECK(!g_eax);
    MEM64(offset)=0; read[6]=32; call_kernel(219,read,8); CHECK(!g_eax);
    memcpy(memory+buffer,"updated",7); read[6]=7;
    call_kernel(236,read,8); CHECK(!g_eax); misses=fsc_test_reads;
    read[6]=32; call_kernel(219,read,8);
    CHECK(!g_eax && fsc_test_reads==misses+1 && !memcmp(memory+buffer,"updated",7));
    MEM64(info)=5; query[4]=20; call_kernel(226,query,5); CHECK(!g_eax);
    call_kernel(219,read,8); CHECK(!g_eax && MEM32(iosb+4)==5 && !memcmp(memory+buffer,"updat",5));
    read[7]=0; call_kernel(219,read,8); CHECK(g_eax==0xc0000011u && !MEM32(iosb+4));
    MEM64(info)=20; call_kernel(226,query,5); CHECK(!g_eax);
    read[7]=offset; MEM64(offset)=5; read[6]=15; memset(memory+buffer,0xcc,16);
    call_kernel(219,read,8); CHECK(!g_eax && MEM32(iosb+4)==15 && MEM8(buffer+15)==0xcc);
    for (unsigned i=0;i<15;++i) CHECK(!MEM8(buffer+i));
    pages=0; call_kernel(37,&pages,1); CHECK(!g_eax && !fsc_bytes && !vm_service_bytes);
    misses=fsc_test_reads; call_kernel(219,read,8); CHECK(!g_eax); call_kernel(219,read,8);
    CHECK(!g_eax && fsc_test_reads==misses+2);
    call_kernel(187,&file,1); CHECK(!g_eax);
    /* Shared-write files must reflect an external writer, never cached bytes. */
    pages=2; call_kernel(37,&pages,1); CHECK(!g_eax);
    open[1]=GENERIC_READ|SYNCHRONIZE; open[4]=3; call_kernel(202,open,6); CHECK(!g_eax); file=MEM32(out);
    read[0]=file; read[6]=5; MEM64(offset)=0; call_kernel(219,read,8); CHECK(!g_eax);
    HANDLE external=CreateFileA(path,GENERIC_WRITE,3,NULL,OPEN_EXISTING,0,NULL); CHECK(external!=INVALID_HANDLE_VALUE);
    DWORD written; CHECK(WriteFile(external,"other",5,&written,NULL) && written==5 && FlushFileBuffers(external)); CHECK(CloseHandle(external));
    misses=fsc_test_reads; call_kernel(219,read,8);
    CHECK(!g_eax && fsc_test_reads==misses+1 && !memcmp(memory+buffer,"other",5));
    call_kernel(187,&file,1); CHECK(!g_eax);
    media_nt_read=fsc_test_native_read; CHECK(!fsc_resize(0));
    CHECK(DeleteFileA(path));
    for (unsigned i=1;i<=5;++i) {
        CHECK(CloseHandle(storage_roots[i])); storage_roots[i]=NULL;
        sprintf(path,"%s\\Partition%u",directory,i); CHECK(RemoveDirectoryA(path));
    }
    CHECK(CloseHandle(storage_parent)); storage_parent=NULL; CHECK(RemoveDirectoryA(directory));
    fprintf(stderr,"PASS: real file-cache storage/resizing/budget, native-read avoidance, byte/position/EOF contracts, concurrent resize/read, write/truncate/invalidate coherence and shared-writer bypass\n");
}
