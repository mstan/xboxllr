/* Save updates set directory timestamps, including a 4-byte-aligned guest
 * FILE_BASIC_INFORMATION. Verify the host effect independently of NtSet. */
static void test_storage_metadata(const char *directory)
{
    const uint32_t out=0x1d000,iosb=0x1d010,info=0x1d024;
    uint32_t create[]={out,FILE_WRITE_ATTRIBUTES|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        0x1a000,iosb,0,FILE_ATTRIBUTE_DIRECTORY,3,2,0x21};
    test_media_name("\\Device\\Harddisk0\\Partition1\\save-metadata",0);
    call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
    const uint64_t created=132000000000000000ull,accessed=132100000000000000ull,
        written=132200000000000000ull;
    memset(memory+info-4,0xcc,48);memset(memory+info,0,40);
    MEM64(info)=created;MEM64(info+8)=accessed;MEM64(info+16)=written;
    unsigned char original[40];memcpy(original,memory+info,40);
    uint32_t set[]={file,iosb,info,39,4};
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    call_kernel(226,set,5);CHECK(g_eax==0xc0000004u);
    CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    set[3]=40;call_kernel(226,set,5);CHECK(!g_eax && !MEM32(iosb) && !MEM32(iosb+4));
    CHECK(!memcmp(memory+info,original,40) && MEM32(info-4)==0xcccccccc && MEM32(info+40)==0xcccccccc);
    call_kernel(187,&file,1);CHECK(!g_eax);
    char path[MAX_PATH];sprintf(path,"%s\\Partition1\\save-metadata",directory);
    HANDLE host=CreateFileA(path,FILE_READ_ATTRIBUTES,3,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    CHECK(host!=INVALID_HANDLE_VALUE);
    FILETIME ct,at,wt;CHECK(GetFileTime(host,&ct,&at,&wt));
    CHECK((((uint64_t)ct.dwHighDateTime<<32)|ct.dwLowDateTime)==created);
    CHECK((((uint64_t)at.dwHighDateTime<<32)|at.dwLowDateTime)==accessed);
    CHECK((((uint64_t)wt.dwHighDateTime<<32)|wt.dwLowDateTime)==written);
    CHECK(CloseHandle(host));
    create[1]=FILE_READ_ATTRIBUTES|SYNCHRONIZE;create[7]=1;
    call_kernel(190,create,9);CHECK(!g_eax);file=MEM32(out);set[0]=file;
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    call_kernel(226,set,5);CHECK(g_eax==0xc0000022u);
    CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678 && !memcmp(memory+info,original,40));
    call_kernel(187,&file,1);CHECK(!g_eax);
    CHECK(RemoveDirectoryA(path));
    puts("save metadata: real directory timestamps, guest alignment/bounds, short length and denied write passed");
}

/* Completed writes must be independently visible through ordinary host reads.
 * No provider replacement or artificial completion is used. */
static void test_storage_async(const char *directory)
{
    const uint32_t out=0x1d000,iosb=0x1d010,offset=0x1d020,timeout=0x1d030,buffer=0x1e000;
    uint32_t event_create[]={out+4,0,0,1};call_kernel(189,event_create,4);CHECK(!g_eax);
    uint32_t event=MEM32(out+4),create[]={out,GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,0x1a000,iosb,0,0,3,5,0x48};
    test_media_name("\\Device\\Harddisk0\\Partition1\\async-record.bin",0);
    char path[MAX_PATH];sprintf(path,"%s\\Partition1\\async-record.bin",directory);
    unsigned char expected[8192],actual[8192];
    for(unsigned i=0;i<sizeof(expected);++i)expected[i]=(unsigned char)(i*29+(i>>8));
    memcpy(memory+buffer,expected,sizeof(expected));MEM64(timeout)=(uint64_t)-50000000;
    for(unsigned cycle=0;cycle<32;++cycle) {
        call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
        struct MediaFile *owner=media_owned(file);CHECK(owner && owner->asynchronous);
        /* Actual Blinx caller permits an OVERLAPPED with no event and writes
         * lengths44/6000, neither a native sector multiple. */
        unsigned length=cycle&1?6000:44;
        uint32_t write[]={file,cycle%3?event:0,0,iosb,iosb,buffer,length,offset};MEM64(offset)=0;
        MEM32(iosb-4)=0xaabbccdd;MEM32(iosb+8)=0x11223344;
        call_kernel(236,write,8);CHECK(!g_eax || g_eax==0x103);
        uint32_t wait[]={write[1]?event:file,0,timeout};
        call_kernel(233,wait,3);CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==length);
        if(write[1])CHECK(WaitForSingleObject(owner->async_signal,0)==WAIT_TIMEOUT);
        CHECK(MEM32(iosb-4)==0xaabbccdd && MEM32(iosb+8)==0x11223344 && !memcmp(memory+buffer,expected,sizeof(expected)));
        /* Explicit non-sector offset, extending EOF with a zero-filled gap. */
        MEM64(offset)=7003;write[1]=0;write[5]=buffer+7003;write[6]=37;
        call_kernel(236,write,8);CHECK(!g_eax || g_eax==0x103);
        wait[0]=file;call_kernel(233,wait,3);CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==37);
        uint32_t flush[]={file,iosb};call_kernel(198,flush,2);CHECK(!g_eax);
        call_kernel(187,&file,1);CHECK(!g_eax && !owner->host && !owner->pending && !owner->async_idle && !owner->async_signal);
        FILE *saved=fopen(path,"rb");CHECK(saved && fread(actual,1,7040,saved)==7040 && fgetc(saved)==EOF);fclose(saved);
        CHECK(!memcmp(actual,expected,length) && !memcmp(actual+7003,expected+7003,37));
        for(unsigned i=length;i<7003;++i)CHECK(actual[i]==0);
    }
    /* Two independent outstanding IOSBs and close-before-wait preserve both
     * operations and a retained event even after its guest handle is closed. */
    call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
    uint32_t status;HANDLE retained=event_duplicate(event,&status);CHECK(retained && !status);
    uint32_t write[]={file,event,0,0,iosb,buffer,4096,offset};MEM64(offset)=0;
    call_kernel(236,write,8);CHECK(!g_eax || g_eax==0x103);
    write[1]=0;write[4]=iosb+64;write[5]=buffer+4096;MEM64(offset)=4096;
    call_kernel(236,write,8);CHECK(!g_eax || g_eax==0x103);
    call_kernel(187,&event,1);CHECK(!g_eax);call_kernel(187,&file,1);CHECK(!g_eax);
    CHECK(WaitForSingleObject(retained,5000)==WAIT_OBJECT_0 && !MEM32(iosb) && !MEM32(iosb+64));
    CHECK(MEM32(iosb+4)==4096 && MEM32(iosb+68)==4096 && CloseHandle(retained));
    FILE *saved=fopen(path,"rb");CHECK(saved && fread(actual,1,sizeof(actual),saved)==sizeof(actual));fclose(saved);
    CHECK(!memcmp(actual,expected,sizeof(actual)));
    create[1]=FILE_READ_DATA;create[7]=1;
    call_kernel(190,create,9);CHECK(!g_eax);file=MEM32(out);write[0]=file;write[4]=iosb;MEM64(offset)=0;
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    call_kernel(236,write,8);CHECK(g_eax==0xc0000022u && MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    uint32_t wait[]={file,0,timeout};call_kernel(233,wait,3);CHECK(g_eax==0xc0000022u); /* no SYNCHRONIZE */
    call_kernel(187,&file,1);CHECK(!g_eax);CHECK(DeleteFileA(path));
    for(unsigned i=0;i<256;++i)CHECK(!media_files[i].host && !media_files[i].pending && !media_files[i].async_idle && !media_files[i].async_signal);
    CHECK(!event_handles);
    puts("async writes: real persisted bytes, optional events/file waits, exact EOF/offsets, retained completion, access and32 lifetimes passed");
}
