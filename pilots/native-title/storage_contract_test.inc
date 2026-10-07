static void test_string(uint32_t va,uint32_t buffer,const char *value)
{
    strcpy((char*)(memory+buffer),value);
    uint32_t args[]={va,buffer}; call_kernel(289,args,2);
    CHECK(MEM16(va)==strlen(value) && MEM16(va+2)==strlen(value)+1 && MEM32(va+4)==buffer);
}
static void test_storage(void)
{
    char temp[MAX_PATH],directory[MAX_PATH],path[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"xls",0,directory));
    CHECK(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    storage_mount(directory);
    const uint32_t out=0x1b000,iosb=0x1b010,info=0x1b020,offset=0x1b060,buffer=0x1b100;
    uint32_t open[]={out,FILE_LIST_DIRECTORY|SYNCHRONIZE,0x1a000,iosb,3,0x800021};
    test_media_name("\\Device\\Harddisk0\\Partition1\\",0);
    call_kernel(202,open,6); CHECK(!g_eax);
    uint32_t root=MEM32(out);
    uint32_t volume[]={root,iosb,info,24,3};
    call_kernel(218,volume,5);
    CHECK(!g_eax && MEM32(iosb+4)==24 && MEM32(info+16)==32 && MEM32(info+20)==512);
    CHECK(MEM64(info)>0 && MEM64(info+8)<=MEM64(info));
    /* The advertised byte capacity is the actual root handle's native volume,
     * rounded down by less than one logical 16 KiB unit. */
    struct { LARGE_INTEGER total,available; ULONG sectors,bytes; } host={0}; IO_STATUS_BLOCK io={0};
    CHECK(!media_nt_volume(media_handle(root),&io,&host,sizeof(host),3));
    uint64_t native_bytes=(uint64_t)host.total.QuadPart*host.sectors*host.bytes;
    CHECK(MEM64(info)*16384<=native_bytes && native_bytes-MEM64(info)*16384<16384);
    volume[3]=23; MEM64(info)=0xaabbccdd;
    call_kernel(218,volume,5); CHECK(g_eax==0xc0000004u && MEM64(info)==0xaabbccdd && !MEM32(iosb+4));
    uint32_t create[]={out,GENERIC_READ|GENERIC_WRITE|DELETE|SYNCHRONIZE,0x1a000,iosb,0,0,3,2,0x60};
    test_media_name("payload.bin",root);
    call_kernel(190,create,9); CHECK(!g_eax && MEM32(iosb+4)==2);
    uint32_t file=MEM32(out); MEM32(out)=0xfeed;
    call_kernel(190,create,9); CHECK(g_eax==0xc0000035u && MEM32(out)==0xfeed);
    unsigned char data[4097]; for (unsigned i=0;i<sizeof(data);++i) data[i]=(unsigned char)(i*37+11);
    memcpy(memory+buffer,data,sizeof(data)); MEM64(offset)=0;
    uint32_t write[]={file,0,0,0,iosb,buffer,sizeof(data),offset};
    call_kernel(236,write,8); CHECK(!g_eax && MEM32(iosb+4)==sizeof(data));
    uint32_t flush[]={file,iosb}; call_kernel(198,flush,2); CHECK(!g_eax);
    call_kernel(187,&file,1); CHECK(!g_eax);
    sprintf(path,"%s\\Partition1\\payload.bin",directory);
    FILE *saved=fopen(path,"rb"); unsigned char loaded[4097];
    CHECK(saved && fread(loaded,1,sizeof(loaded),saved)==sizeof(loaded) && fgetc(saved)==EOF); fclose(saved);
    CHECK(!memcmp(loaded,data,sizeof(data))); /* independent host read after guest close/flush */
    HANDLE only_write=NULL; IO_STATUS_BLOCK query_io={0};
    CHECK(!storage_create_host(storage_roots[1],L"payload.bin",GENERIC_WRITE|SYNCHRONIZE,3,0x60,1,0,NULL,&only_write,&query_io));
    uint64_t aligned_metadata[7]={0};
    uint32_t metadata_status=(uint32_t)media_nt_query(only_write,&query_io,aligned_metadata,56,34);
    fprintf(stderr,"[HOST CONTRACT] query metadata on GENERIC_WRITE-only handle: %08x\n",metadata_status);
    CHECK(metadata_status==0xc0000022u);
    CHECK(CloseHandle(only_write));
    create[7]=1; create[1]=GENERIC_WRITE|SYNCHRONIZE;
    call_kernel(190,create,9); CHECK(!g_eax); file=MEM32(out);
    write[0]=file; call_kernel(219,write,8); CHECK(g_eax==0xc0000022u); /* metadata access is not READ_DATA */
    uint32_t set[]={file,iosb,info+4,8,20}; MEM64(info+4)=17;
    call_kernel(226,set,5); CHECK(!g_eax);
    set[2]=info;
    set[3]=24; set[4]=5; call_kernel(211,set,5); CHECK(!g_eax && MEM64(info+8)==17);
    set[3]=56; set[4]=34; MEM32(info+56)=0xfeed1234;
    call_kernel(211,set,5); CHECK(!g_eax && MEM64(info+40)==17 && MEM32(iosb+4)==56 && MEM32(info+56)==0xfeed1234);
    set[2]=info+4; MEM32(info+60)=0xfeed1234;
    call_kernel(211,set,5); CHECK(!g_eax && MEM64(info+44)==17 && MEM32(info+60)==0xfeed1234);
    call_kernel(187,&file,1); CHECK(!g_eax);
    CHECK(DeleteFileA(path));

    test_string(0x1c000,0x1c100,"\\??\\T:");
    test_string(0x1c010,0x1c200,"\\Device\\Harddisk0\\Partition1");
    test_string(0x1d000,0x1d100,"aBc"); test_string(0x1d010,0x1d200,"AbC");
    uint32_t equal[]={0x1d000,0x1d010,0}; call_kernel(279,equal,3); CHECK(!g_eax);
    equal[2]=1; call_kernel(279,equal,3); CHECK(g_eax==1);
    MEM8(0x1d101)=MEM8(0x1d201)=0; call_kernel(279,equal,3); CHECK(g_eax==1);
    MEM8(0x1d202)='d'; call_kernel(279,equal,3); CHECK(!g_eax); /* counted comparison past NUL */
    uint32_t link[]={0x1c000,0x1c010}; call_kernel(67,link,2); CHECK(!g_eax);
    call_kernel(67,link,2); CHECK(g_eax==0xc0000035u);
    MEM32(0x1c020)=0; MEM32(0x1c024)=0x1c000; MEM32(0x1c028)=0x40;
    uint32_t open_link[]={out,0x1c020}; call_kernel(203,open_link,2); CHECK(!g_eax);
    uint32_t handle=MEM32(out);
    MEM16(0x1c030)=0xbeef; MEM16(0x1c032)=2; MEM32(0x1c034)=0x1c300;
    MEM32(0x1c300)=0xcccccccc;
    uint32_t query[]={handle,0x1c030,0x1c040}; call_kernel(215,query,3);
    CHECK(g_eax==0xc0000023u && MEM32(0x1c040)==28 && MEM32(0x1c300)==0xcccccccc && MEM16(0x1c030)==0xbeef);
    MEM16(0x1c032)=28; call_kernel(215,query,3);
    CHECK(!g_eax && MEM16(0x1c030)==28 && !memcmp(memory+0x1c300,memory+0x1c200,28));
    test_media_name("T:\\",0); open[5]=0x21;
    call_kernel(202,open,6); CHECK(!g_eax); uint32_t through=MEM32(out);
    call_kernel(187,&through,1); CHECK(!g_eax);
    call_kernel(69,link,1); CHECK(!g_eax);
    for (unsigned i=0;i<300;++i) {
        call_kernel(67,link,2); CHECK(!g_eax);
        call_kernel(203,open_link,2); CHECK(!g_eax); uint32_t owned=MEM32(out);
        call_kernel(69,link,1); CHECK(!g_eax);
        call_kernel(187,&owned,1); CHECK(!g_eax);
    }
    CHECK(!namespace_create("\\??\\U:","\\Device\\Harddisk0\\Partition1\\"));
    char joined[1024]; CHECK(!namespace_resolve("U:\\file",joined));
    CHECK(!strcmp(joined,"\\Device\\Harddisk0\\Partition1\\file"));
    call_kernel(203,open_link,2); CHECK(g_eax==0xc0000034u);
    call_kernel(202,open,6); CHECK(g_eax==0xc000003au); /* deleted name no longer resolves */
    test_string(0x1c010,0x1c200,"\\Device\\Harddisk0\\Partition2");
    call_kernel(67,link,2); CHECK(!g_eax);
    call_kernel(215,query,3); CHECK(!g_eax && MEM8(0x1c300+27)=='1'); /* original object still alive */
    call_kernel(187,&handle,1); CHECK(!g_eax);
    call_kernel(215,query,3); CHECK(g_eax==MEDIA_BAD_HANDLE);
    query[0]=0; call_kernel(215,query,3); CHECK(g_eax==MEDIA_BAD_HANDLE);
    call_kernel(69,link,1); CHECK(!g_eax);
    CHECK(!namespace_create("\\??\\A:","\\??\\B:") && !namespace_create("\\??\\B:","\\??\\A:"));
    char resolved[1024]; CHECK(namespace_resolve("A:\\file",resolved)==0xc0000039u);
    uint32_t init[]={0x1c030,0}; call_kernel(289,init,2); CHECK(!MEM32(0x1c030) && !MEM32(0x1c034));
    call_kernel(187,&root,1); CHECK(!g_eax);
    volume[0]=root; volume[3]=24; call_kernel(218,volume,5); CHECK(g_eax==MEDIA_BAD_HANDLE);
    for (unsigned i=1;i<=5;++i) {
        CHECK(CloseHandle(storage_roots[i])); storage_roots[i]=NULL;
        sprintf(path,"%s\\Partition%u",directory,i); CHECK(RemoveDirectoryA(path));
    }
    CHECK(RemoveDirectoryA(directory));
    fprintf(stderr,"PASS: actual mounted volume capacity, HDD persistence/flush/truncate, create collisions, dynamic links, deletion/open-handle lifetime and cycle rejection\n");
}
