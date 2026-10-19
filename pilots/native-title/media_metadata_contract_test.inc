/* Real confined host metadata; no invented timestamp/size or success fixture. */
static uint32_t metadata_test_call(uint32_t attributes,uint32_t output)
{
    uint32_t saved_ebx=g_ebx,saved_esi=g_esi,saved_edi=g_edi,saved_ebp=g_ebp;
    g_esp=0x18000;MEM32(g_esp)=0x12340;
    MEM32(g_esp+4)=attributes;MEM32(g_esp+8)=output;
    media_kernel(210);
    CHECK(g_esp==0x1800c && MEM32(0x18000)==0x12340);
    CHECK(g_ebx==saved_ebx && g_esi==saved_esi && g_edi==saved_edi && g_ebp==saved_ebp);
    return g_eax;
}
static LONG NTAPI metadata_test_query_failure(HANDLE file,PIO_STATUS_BLOCK io,PVOID output,ULONG bytes,ULONG kind)
{
    (void)file;(void)io;CHECK(bytes==56 && kind==34);
    memset(output,0xab,bytes);return (LONG)0xc0000022u;
}
static void metadata_test_unchanged(uint32_t output,uint32_t status)
{
    memset(memory+output-4,0xcc,64);
    CHECK(metadata_test_call(0x1a000,output)==status);
    for(unsigned i=0;i<64;++i)CHECK(MEM8(output-4+i)==0xcc);
}
static void test_media_metadata(const char *directory,uint32_t guest_root)
{
    const uint32_t output=0x1c004; /* Xbox only guarantees four-byte alignment. */
    char path[MAX_PATH];snprintf(path,sizeof(path),"%s\\asset.bin",directory);
    HANDLE reference=CreateFileA(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,0,NULL);CHECK(reference!=INVALID_HANDLE_VALUE);
    union {uint64_t alignment;unsigned char bytes[56];} expected={0};IO_STATUS_BLOCK io={0};
    CHECK(!media_nt_query(reference,&io,expected.bytes,sizeof(expected.bytes),34));
    CHECK(CloseHandle(reference));
    memcpy(expected.bytes+32,expected.bytes+40,8);memset(expected.bytes+52,0,4);
    uint32_t next_handle=media_next_id;
    const char *names[]={"D:\\asset.bin","d:\\ASSET.BIN","\\??\\D:\\asset.bin",
        "\\Device\\CdRom0\\asset.bin","asset.bin","d:\\ASSET.BIN"};
    const uint32_t roots[]={0,0,0,0,guest_root,0xfffffffdu};
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);++i) {
        test_media_name(names[i],roots[i]);memset(memory+output-4,0xcc,64);
        CHECK(!metadata_test_call(0x1a000,output));
        CHECK(!memcmp(memory+output,expected.bytes,56));
        CHECK(MEM32(output-4)==0xcccccccc && MEM32(output+56)==0xcccccccc);
    }
    test_media_name("D:\\",0);memset(memory+output,0xcc,56);
    CHECK(!metadata_test_call(0x1a000,output) && (MEM32(output+48)&FILE_ATTRIBUTE_DIRECTORY));
    CHECK(media_next_id==next_handle); /* Temporary query handles never escape. */
    test_media_name("D:\\asset.bin.absent",0);metadata_test_unchanged(output,0xc0000034u);
    test_media_name("D:\\absent-parent\\asset.bin",0);metadata_test_unchanged(output,0xc000003au);
    test_media_name("D:\\..\\asset.bin",0);metadata_test_unchanged(output,0xc0000033u);
    test_media_name("D:\\asset.bin:stream",0);metadata_test_unchanged(output,0xc0000033u);
    test_media_name("Q:\\asset.bin",0xfffffffdu);metadata_test_unchanged(output,0xc000003au);
    test_media_name("asset.bin",0xdeadbeefu);metadata_test_unchanged(output,MEDIA_BAD_HANDLE);
    test_media_name("D:\\asset.bin",0);
    memset(memory+output-4,0xcc,64);
    CHECK(metadata_test_call(0,output)==MEDIA_INVALID);
    CHECK(metadata_test_call(0xfffffffcu,output)==MEDIA_INVALID);
    CHECK(metadata_test_call(0x1a000,0)==MEDIA_INVALID);
    CHECK(metadata_test_call(0x1a000,0xfffffff0u)==MEDIA_INVALID);
    for(unsigned i=0;i<64;++i)CHECK(MEM8(output-4+i)==0xcc);
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    MediaInfo saved=media_nt_query;media_nt_query=metadata_test_query_failure;
    metadata_test_unchanged(output,0xc0000022u);media_nt_query=saved;
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    for(unsigned i=0;i<32;++i)CHECK(!metadata_test_call(0x1a000,output));
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    fprintf(stderr,"PASS: named metadata original56-byte ABI, real host times/EOF/attributes, case/root paths, failure staging and leak-free read-only queries\n");
}
