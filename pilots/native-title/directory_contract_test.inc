static void test_directory(uint32_t root,uint32_t iosb,uint32_t output)
{
    uint32_t args[]={root,0,0,0,iosb,output,256,1,0x1b700,1};
    MEM16(0x1b700)=3;MEM16(0x1b702)=3;MEM32(0x1b704)=0x1b720;
    memcpy(memory+0x1b720,"*.*",3);
    memset(memory+output,0xcc,256);
    args[7]=2;call_kernel(207,args,10);CHECK(g_eax==0xc0000003u && !MEM32(iosb+4) && MEM8(output)==0xcc);
    args[7]=1;args[6]=63;call_kernel(207,args,10);CHECK(g_eax==0xc0000004u && MEM8(output)==0xcc);
    args[6]=256;
    unsigned found=0;char first[32]={0};
    for(unsigned i=0;i<2;++i) {
        memset(memory+output,0xcc,256);call_kernel(207,args,10);
        CHECK(!g_eax && !MEM32(iosb) && !MEM32(output));
        unsigned n=MEM32(output+60);CHECK(n<32 && MEM32(iosb+4)==64+n && MEM8(output+64+n)==0xcc);
        CHECK(MEM64(output+40)==20 && !(MEM32(output+56)&FILE_ATTRIBUTE_DIRECTORY));
        if(n==9 && !memcmp(memory+output+64,"asset.bin",9))found|=1;
        else if(n==11 && !memcmp(memory+output+64,"default.xbe",11))found|=2;
        else CHECK(0);
        if(!i)memcpy(first,memory+output+64,n);
        args[9]=0;
    }
    CHECK(found==3 && !memcmp(memory+0x1b720,"*.*",3));
    for(unsigned i=0;i<2;++i){call_kernel(207,args,10);CHECK(g_eax==0x80000006u && !MEM32(iosb+4));}
    args[9]=1;call_kernel(207,args,10);CHECK(!g_eax && !memcmp(memory+output+64,first,strlen(first)));
    /* Each open directory retains its own filter/cursor; no host search handle
     * escapes. Keep the previous cursor alive while testing the new one. */
    uint32_t open[]={0x1b800,FILE_LIST_DIRECTORY|SYNCHRONIZE,0x1a000,iosb,1,0x21};
    test_media_name("D:\\",0);call_kernel(202,open,6);CHECK(!g_eax);
    uint32_t other=MEM32(0x1b800);args[0]=other;args[9]=0;
    MEM16(0x1b700)=5;MEM16(0x1b702)=5;memcpy(memory+0x1b720,"*.bin",5);
    call_kernel(207,args,10);CHECK(!g_eax && MEM32(output+60)==9 && !memcmp(memory+output+64,"asset.bin",9));
    call_kernel(207,args,10);CHECK(g_eax==0x80000006u);
    args[9]=1;args[6]=68;memset(memory+output,0xcc,256);
    call_kernel(207,args,10);CHECK(g_eax==0x80000005u && MEM32(iosb+4)==68 && MEM32(output+60)==9);
    CHECK(!memcmp(memory+output+64,"asse",4) && MEM8(output+68)==0xcc);
    call_kernel(187,&other,1);CHECK(!g_eax);
    call_kernel(207,args,10);CHECK(g_eax==MEDIA_BAD_HANDLE && !MEM32(iosb+4));
    args[0]=root;args[6]=256;args[8]=0;args[9]=0;
    call_kernel(207,args,10);CHECK(!g_eax && memcmp(memory+output+64,first,strlen(first)));
    fprintf(stderr,"PASS: real directory entries/lengths, independent cursors, filters, restart, exhaustion, bounded overflow, unchanged counted mask and closed-handle errors\n");
}
