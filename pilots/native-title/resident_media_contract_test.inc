static void test_media_resident(const char *directory,const unsigned char *content,unsigned length)
{
    CHECK(media_resident_configure(1,NULL,0));media_resident_prepare();CHECK(media_resident_ready());
    char path[MAX_PATH];snprintf(path,sizeof(path),"%s\\asset.bin",directory);
    HANDLE writer=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    CHECK(writer==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
    const uint32_t out=0x1b000,iosb=0x1b010,offset=0x1b060,buffer=0x1b100;
    uint32_t open[]={out,GENERIC_READ|SYNCHRONIZE,0x1a000,iosb,0,0x60};
    test_media_name("D:\\asset.bin",0);call_kernel(202,open,6);CHECK(!g_eax);
    uint32_t exclusive=MEM32(out);CHECK(media_owned(exclusive)->resident && !media_owned(exclusive)->share);
    MEM32(out)=0x12345678;MEM32(iosb)=0xabcdef12;MEM32(iosb+4)=0x98765432;
    open[4]=FILE_SHARE_READ;call_kernel(202,open,6);
    CHECK(g_eax==0xc0000043u && MEM32(out)==0x12345678 && MEM32(iosb)==0xabcdef12 && MEM32(iosb+4)==0x98765432);
    call_kernel(187,&exclusive,1);CHECK(!g_eax);
    call_kernel(202,open,6);CHECK(!g_eax);uint32_t first=MEM32(out);
    call_kernel(202,open,6);CHECK(!g_eax);uint32_t second=MEM32(out);
    open[4]=0;call_kernel(202,open,6);CHECK(g_eax==0xc0000043u);
    uint32_t read[]={first,0,0,0,iosb,buffer,4,offset};MEM64(offset)=0;
    uint64_t hits=(uint64_t)resident_hits;call_kernel(219,read,8);CHECK(!g_eax && !memcmp(memory+buffer,content,4));
    read[0]=second;MEM64(offset)=5;call_kernel(219,read,8);CHECK(!g_eax && !memcmp(memory+buffer,content+5,4));
    CHECK((uint64_t)resident_hits>=hits+2);
    LARGE_INTEGER zero={0},position;
    CHECK(SetFilePointerEx(media_handle(first),zero,&position,FILE_CURRENT) && position.QuadPart==4);
    CHECK(SetFilePointerEx(media_handle(second),zero,&position,FILE_CURRENT) && position.QuadPart==9);
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
    if(media_owned(first)->resident->blob.mapped) {
        const PilotResidentBlob *blob=&media_owned(first)->resident->blob;
        DWORD previous;
        CHECK(VirtualProtect(blob->bytes,(SIZE_T)blob->size,PAGE_NOACCESS,&previous));
        /* An actual mapped-source fault must fall through to NtReadFile with
         * the original NULL/-2/explicit position, bytes and IOSB preserved. */
        const uint64_t offsets[]={0,UINT64_MAX-1,5};
        for(unsigned i=0;i<3;++i) {
            LARGE_INTEGER start;start.QuadPart=3;
            CHECK(SetFilePointerEx(media_handle(first),start,NULL,FILE_BEGIN));
            fsc_forget(first);
            read[0]=first;read[7]=i?offset:0;MEM64(offset)=offsets[i];
            memset(memory+buffer,0xcc,8);MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
            uint64_t before=(uint64_t)resident_hits;
            call_kernel(219,read,8);
            CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==4);
            CHECK(!memcmp(memory+buffer,content+(i==2?5:3),4) && MEM8(buffer+4)==0xcc);
            CHECK(SetFilePointerEx(media_handle(first),zero,&position,FILE_CURRENT) && position.QuadPart==(i==2?9:7));
            CHECK((uint64_t)resident_hits==before);
        }
        CHECK(VirtualProtect(blob->bytes,(SIZE_T)blob->size,previous,&previous));
    }
#endif
    call_kernel(187,&first,1);CHECK(!g_eax);call_kernel(187,&second,1);CHECK(!g_eax);
    /* Reuse the actual async/APC/event/EOF and close-drain suite on resident
     * bytes too, including its original nonbuffered read fallback. */
    test_media_async(content,length);media_resident_close();CHECK(media_resident_configure(0,NULL,0));
    fprintf(stderr,"PASS: immutable resident pins, original exclusive/shared opens, independent file positions and resident async/APC completion\n");
}
