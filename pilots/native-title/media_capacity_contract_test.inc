static void test_media_capacity(void)
{
    WCHAR temp[MAX_PATH],root[MAX_PATH],name[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp) && GetTempFileNameW(temp,L"xvc",0,root));
    CHECK(DeleteFileW(root) && CreateDirectoryW(root,NULL));
    const WCHAR *names[]={L"empty",L"one",L"exact",L"\x03bb.bin",L"nested\\one"};
    const DWORD sizes[]={0,1,2048,2049,1};unsigned char zeros[2049]={0};
    swprintf(name,MAX_PATH,L"%ls\\nested",root);CHECK(CreateDirectoryW(name,NULL));
    swprintf(name,MAX_PATH,L"%ls\\emptydir",root);CHECK(CreateDirectoryW(name,NULL));
    for(unsigned i=0;i<5;++i) {
        swprintf(name,MAX_PATH,L"%ls\\%ls",root,names[i]);
        HANDLE file=CreateFileW(name,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        CHECK(file!=INVALID_HANDLE_VALUE);DWORD written=0;
        CHECK(WriteFile(file,zeros,sizes[i],&written,NULL) && written==sizes[i] && CloseHandle(file));
    }
    HANDLE directory=CreateFileW(root,FILE_LIST_DIRECTORY|SYNCHRONIZE,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    CHECK(directory!=INVALID_HANDLE_VALUE);
    uint64_t sectors=0;CHECK(!media_capacity_walk(directory,0,&sectors));
    CHECK(sectors==8); /* 3 directories and 5 allocated file sectors. */
    uint64_t before=sectors;CHECK(media_capacity_walk(directory,64,&sectors)==0xc00000bbu);
    CHECK(sectors==before && CloseHandle(directory));
    for(unsigned i=0;i<5;++i){swprintf(name,MAX_PATH,L"%ls\\%ls",root,names[i]);CHECK(DeleteFileW(name));}
    swprintf(name,MAX_PATH,L"%ls\\nested",root);CHECK(RemoveDirectoryW(name));
    swprintf(name,MAX_PATH,L"%ls\\emptydir",root);CHECK(RemoveDirectoryW(name));CHECK(RemoveDirectoryW(root));

    const uint32_t out=0x1d080,iosb=0x1d090,info=0x1d0a0;
    uint32_t open[]={out,FILE_LIST_DIRECTORY|SYNCHRONIZE,0x1a000,iosb,3,0x21};
    test_media_name("D:\\",0);call_kernel(202,open,6);CHECK(!g_eax);uint32_t handle=MEM32(out);
    uint32_t volume[]={handle,iosb,info,28,3};memset(memory+info,0xcc,32);
    call_kernel(218,volume,5);CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==24);
    CHECK(MEM64(info)>0 && !MEM64(info+8) && MEM32(info+16)==1 && MEM32(info+20)==2048);
    CHECK(MEM32(info+24)==0xcccccccc && MEM32(info+28)==0xcccccccc);
    uint64_t cached=MEM64(info);call_kernel(218,volume,5);CHECK(!g_eax && MEM64(info)==cached);
    volume[3]=23;MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;MEM64(info)=0xabcdef;
    call_kernel(218,volume,5);CHECK(g_eax==0xc0000004u && MEM64(info)==0xabcdef);
    CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
    call_kernel(187,&handle,1);CHECK(!g_eax);
    fprintf(stderr,"PASS mounted content capacity: exact authored empty/partial/aligned/nested/Unicode extents, depth guard, cached read-only NT ABI/IOSB/length/canaries\n");
}
