static void test_media_name(const char *name,uint32_t root)
{
    uint32_t length=(uint32_t)strlen(name);
    MEM32(0x1a000)=root; MEM32(0x1a004)=0x1a010; MEM32(0x1a008)=0x40;
    MEM16(0x1a010)=(uint16_t)length; MEM16(0x1a012)=(uint16_t)length;
    MEM32(0x1a014)=0x1a100; memcpy(memory+0x1a100,name,length);
}
#include "directory_contract_test.inc"
#include "file_async_contract_test.inc"
static void test_media(void)
{
    char temp[MAX_PATH],directory[MAX_PATH],path[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH,temp));
    CHECK(GetTempFileNameA(temp,"xlm",0,directory));
    CHECK(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    const unsigned char content[]={0,1,2,3,4,5,6,7,8,9,0xff,0,12,13,14,15,16,17,18,19};
    sprintf(path,"%s\\default.xbe",directory);
    FILE *file=fopen(path,"wb"); CHECK(file && fwrite(content,1,sizeof(content),file)==sizeof(content)); fclose(file);
    sprintf(path,"%s\\asset.bin",directory);
    file=fopen(path,"wb"); CHECK(file && fwrite(content,1,sizeof(content),file)==sizeof(content)); fclose(file);
    sprintf(path,"%s\\Plain",directory);
    file=fopen(path,"wb"); CHECK(file && fwrite(content,1,sizeof(content),file)==sizeof(content)); fclose(file);
    unsigned char digest[32]; char expected[65];
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)content,sizeof(content),digest,32)>=0);
    for (unsigned i=0;i<32;++i) sprintf(expected+2*i,"%02x",digest[i]);
    CHECK(media_verify(expected)==MEDIA_NO_MEDIA && !media_root && !media_identity);
    media_content_path=directory;
    char wrong[65]; memset(wrong,'0',64); wrong[64]=0;
    CHECK(media_verify(wrong)==MEDIA_WRONG_VOLUME && !media_root && !media_identity);
    g_ebx=0x12345678; g_esi=0x87654321; g_edi=0xaabbccdd; g_ebp=0x100000;
    uint32_t stack=g_esp;
    media_verify_abi(expected);
    CHECK(!g_eax && g_esp==stack+4 && g_ebx==0x12345678 && g_esi==0x87654321 &&
          g_edi==0xaabbccdd && g_ebp==0x100000 && media_root && media_identity);
    HANDLE identity=media_identity,root=media_root;
    CHECK(!namespace_create("\\??\\D:","\\Device\\CdRom0"));
    for (unsigned i=0;i<4;++i) CHECK(media_verify(expected)==0 && media_root==root && media_identity==identity);
    CHECK(media_verify(wrong)==MEDIA_WRONG_VOLUME); /* never return cached success for a different identity */
    sprintf(path,"%s\\default.xbe",directory);
    HANDLE denied=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(denied==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);

    const uint32_t out=0x1b000,iosb=0x1b010,info=0x1b020,offset=0x1b060,buffer=0x1b100;
    uint32_t open[]={out,GENERIC_READ|SYNCHRONIZE,0x1a000,iosb,1,0x60};
    test_media_name("\\Device\\CdRom0\\ASSET.BIN",0);
    MEM32(out)=0xfeed1234;
    call_kernel(202,open,6); CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==1);
    uint32_t handle=MEM32(out);
    uint32_t read[]={handle,0,0,0,iosb,buffer,8,offset};
    MEM64(offset)=3; memset(memory+buffer,0xcc,32);
    read[3]=iosb; /* ReadFile OVERLAPPED context without APC/port registration. */
    call_kernel(219,read,8);
    CHECK(!g_eax && MEM32(iosb+4)==8 && !memcmp(memory+buffer,content+3,8) && MEM8(buffer+8)==0xcc);
    uint32_t query[]={handle,iosb,info,8,14};
    call_kernel(211,query,5); CHECK(!g_eax && MEM64(info)==11 && MEM32(iosb+4)==8);
    read[7]=0; read[6]=16;
    call_kernel(219,read,8); CHECK(!g_eax && MEM32(iosb+4)==9 && !memcmp(memory+buffer,content+11,9));
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    call_kernel(219,read,8); CHECK(g_eax==0xc0000011u && MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    MEM64(info)=5;
    call_kernel(226,query,5); CHECK(!g_eax);
    read[6]=2; call_kernel(219,read,8); CHECK(!g_eax && !memcmp(memory+buffer,content+5,2));
    query[3]=64; query[4]=34;memset(memory+info,0xcc,68);
    call_kernel(211,query,5); CHECK(!g_eax && MEM64(info+32)==sizeof(content) && MEM64(info+40)==sizeof(content));
    CHECK(MEM32(iosb+4)==56 && !MEM64(info+56) && MEM32(info+64)==0xcccccccc);
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    for(unsigned kind=4;kind<=5;++kind) {
        query[3]=64;query[4]=kind;memset(memory+info,0xcc,68);
        call_kernel(211,query,5);CHECK(g_eax==MEDIA_INVALID);
        for(unsigned i=0;i<64;++i)CHECK(!MEM8(info+i));
        CHECK(MEM32(info+64)==0xcccccccc && MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    }
    query[4]=999;memset(memory+info,0xcc,64);
    call_kernel(211,query,5);CHECK(g_eax==0xc0000003u && MEM32(info)==0xcccccccc);
    query[4]=5;
    query[3]=23; MEM64(info)=0xaabbccdd;
    call_kernel(211,query,5); CHECK(g_eax==0xc0000004u && MEM64(info)==0xaabbccdd && MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    query[3]=55;query[4]=34;
    call_kernel(211,query,5);CHECK(g_eax==0xc0000004u && MEM64(info)==0xaabbccdd);
    query[3]=7;query[4]=14;
    call_kernel(226,query,5);CHECK(g_eax==0xc0000004u && MEM64(info)==0xaabbccdd);
    query[3]=8;MEM64(info)=UINT64_MAX;
    call_kernel(226,query,5);CHECK(g_eax==MEDIA_INVALID && MEM64(info)==UINT64_MAX);
    read[7]=offset;MEM64(offset)=UINT64_MAX;memset(memory+buffer,0xcc,32);
    call_kernel(219,read,8);CHECK(g_eax==MEDIA_INVALID && MEM8(buffer)==0xcc);
    CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    call_kernel(187,&handle,1); CHECK(!g_eax);
    call_kernel(187,&handle,1); CHECK(g_eax==MEDIA_BAD_HANDLE);
    call_kernel(219,read,8); CHECK(g_eax==MEDIA_BAD_HANDLE && MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);

    test_media_name("D:\\absent",0); MEM32(out)=0xfeed1234;
    MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
    call_kernel(202,open,6); CHECK(g_eax==0xc0000034u && MEM32(out)==0xfeed1234 &&
                                MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    test_media_name("D:\\..\\asset.bin",0);
    call_kernel(202,open,6); CHECK(g_eax==0xc0000033u && MEM32(out)==0xfeed1234);
    test_media_name("D:\\asset.bin",0); open[1]=GENERIC_WRITE|SYNCHRONIZE;
    call_kernel(202,open,6); CHECK(g_eax==0xc00000a2u && MEM32(out)==0xfeed1234);
    open[1]=GENERIC_READ|SYNCHRONIZE;
    test_media_name("D:\\",0); open[5]=0x21;
    call_kernel(202,open,6); CHECK(!g_eax);
    uint32_t guest_root=MEM32(out);
    test_directory(guest_root,iosb,buffer);
    test_media_name("asset.bin",guest_root); open[5]=0x60;
    call_kernel(202,open,6); CHECK(!g_eax);
    uint32_t child=MEM32(out);
    call_kernel(187,&guest_root,1); CHECK(!g_eax);
    read[0]=child; read[7]=offset; MEM64(offset)=0;
    call_kernel(219,read,8); CHECK(!g_eax && !memcmp(memory+buffer,content,2));
    call_kernel(187,&child,1); CHECK(!g_eax);
    test_media_name("\\??\\D:\\asset.bin",0);
    DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for (unsigned i=0;i<300;++i) {
        call_kernel(202,open,6); CHECK(!g_eax && MEM32(out)!=handle);
        uint32_t current=MEM32(out); call_kernel(187,&current,1); CHECK(!g_eax);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    uint32_t create[]={out,GENERIC_READ|SYNCHRONIZE,0x1a000,iosb,0,0,1,1,0x60};
    call_kernel(190,create,9); CHECK(!g_eax);
    child=MEM32(out); call_kernel(187,&child,1); CHECK(!g_eax);
    test_media_name("d:\\ASSET.BIN",0xfffffffdu);
    call_kernel(190,create,9); CHECK(!g_eax);
    child=MEM32(out);read[0]=child;read[6]=sizeof(content);read[7]=offset;MEM64(offset)=0;
    call_kernel(219,read,8);CHECK(!g_eax && MEM32(iosb+4)==sizeof(content) && !memcmp(memory+buffer,content,sizeof(content)));
    call_kernel(187,&child,1);CHECK(!g_eax);
    create[8]=0x848;call_kernel(190,create,9);CHECK(!g_eax);
    child=MEM32(out);CHECK(media_owned(child)->asynchronous && !media_owned(child)->cacheable);
    struct {uint32_t mode;} native_mode;IO_STATUS_BLOCK native_io={0};
    CHECK(media_nt_query(media_handle(child),&native_io,&native_mode,sizeof(native_mode),16)==0);
    CHECK(!(native_mode.mode&0x30) && (native_mode.mode&8));
    call_kernel(187,&child,1);CHECK(!g_eax);create[8]=0x60;
    test_media_async(content,sizeof(content));
    test_media_name("Q:\\asset.bin",0xfffffffdu);MEM32(out)=0xfeed1234;
    call_kernel(190,create,9);CHECK(g_eax==0xc000003au && MEM32(out)==0xfeed1234);
    test_media_name("D:\\..\\asset.bin",0xfffffffdu);
    call_kernel(190,create,9);CHECK(g_eax==0xc0000033u);
    CloseHandle(media_identity); CloseHandle(media_root); media_identity=media_root=NULL;
    sprintf(path,"%s\\asset.bin",directory); CHECK(DeleteFileA(path));
    sprintf(path,"%s\\Plain",directory); CHECK(DeleteFileA(path));
    sprintf(path,"%s\\default.xbe",directory); CHECK(DeleteFileA(path));
    CHECK(media_verify(expected)==0xc0000034u && !media_root && !media_identity);
    CHECK(RemoveDirectoryA(directory)); media_content_path=NULL;
    fprintf(stderr,"PASS: installed identity/missing/wrong media, ABI, immutable mounts, real file bytes/EOF/position/query, relative paths, ownership and 300 leak-free open/close cycles\n");
}
