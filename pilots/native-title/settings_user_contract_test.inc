static void test_settings_user(void)
{
    WCHAR temp[32768],directory[32768];CHECK(GetTempPathW(32768,temp));
    CHECK(swprintf(directory,32768,L"%lsxllr-user-settings-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)GetTickCount64())>0);
    CHECK(CreateDirectoryW(directory,NULL));char utf8[32768];
    CHECK(WideCharToMultiByte(CP_UTF8,0,directory,-1,utf8,sizeof(utf8),NULL,NULL));
    memset(eeprom,0,sizeof(eeprom));for(unsigned i=0;i<256;++i)eeprom[i]=(unsigned char)(i*53+17);
    memset(eeprom+0x60,0,4);uint32_t sum=~settings_user_checksum(eeprom);memcpy(eeprom+0x60,&sum,4);
    unsigned char original[256];memcpy(original,eeprom,256);have_eeprom=1;settings_cache_valid=0;
    settings_region_bind_boot();storage_mount(utf8);settings_user_storage_bind();CHECK(settings_user_storage_bound);
    /* All public user fields, including previously misclassified DATE/DVD
     * DWORDs, go through the actual four-argument stdcall and file owner. */
    for(unsigned index=0;index<19;++index){
        MEM32(0x19000)=0x01020300u+index;uint32_t args[]={index,0xdeadbeefu,0x19000,4};
        call_kernel(29,args,4);if(g_eax)fprintf(stderr,"settings save index=%u status=%08x\n",index,g_eax);CHECK(!g_eax);
        uint32_t q[]={index,0x19100,0x19104,12,0x19120};call_kernel(24,q,5);
        CHECK(!g_eax && MEM32(0x19100)==((index==1||index==4)?3u:4u) && MEM32(0x19120)==4);
        CHECK(MEM32(0x19104)==0x01020300u+index && !MEM32(0x19108) && !MEM32(0x1910c));
        CHECK(settings_user_checksum(eeprom)==UINT32_MAX && !memcmp(eeprom,original,0x60) && !memcmp(eeprom+0xc0,original+0xc0,0x40));
    }
    /* Short writes zero-fill, ignored Type, whole-user section, zero-length
     * input without dereferencing NULL, failed writes leave both owners intact. */
    MEM32(0x19000)=0xaabbccdd;uint32_t args[]={8,0,0x19000,1};call_kernel(29,args,4);CHECK(!g_eax);
    uint32_t q[]={8,0x19100,0x19104,4,0};call_kernel(24,q,5);CHECK(!g_eax && MEM32(0x19104)==0xdd);
    args[3]=0;args[2]=0;call_kernel(29,args,4);CHECK(!g_eax);call_kernel(24,q,5);CHECK(!g_eax && !MEM32(0x19104));
    unsigned char saved[256];memcpy(saved,eeprom,256);
    const uint32_t forbidden[]={19,0x100,0x103,0x104,0xfffe,0xffff};
    for(unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i){args[0]=forbidden[i];args[2]=0;args[3]=0;
        call_kernel(29,args,4);CHECK(g_eax==0xc0000034u && !memcmp(saved,eeprom,256) && !memcmp(saved,settings_cache,256));}
    args[0]=8;args[2]=UINT32_MAX;args[3]=5;call_kernel(29,args,4);CHECK(g_eax==0xc000000du && !memcmp(saved,eeprom,256));
    settings_user_storage_bound=0;args[2]=0x19000;args[3]=4;call_kernel(29,args,4);CHECK(g_eax==0xc0000184u && !memcmp(saved,eeprom,256));
    settings_user_storage_bound=1;
    memcpy(memory+0x19200,saved+0x60,96);args[0]=0xff;args[2]=0x19200;args[3]=96;call_kernel(29,args,4);
    CHECK(!g_eax && !memcmp(saved,eeprom,256));
    /* A new process begins with unchanged private input, then restores exactly
     * the saved user section using its immutable source identity. */
    memcpy(eeprom,original,256);settings_cache_valid=0;settings_user_storage_bound=0;settings_user_storage_bind();
    CHECK(!memcmp(saved,eeprom,256));
    for(unsigned i=1;i<=5;++i){CloseHandle(storage_roots[i]);storage_roots[i]=NULL;}
    CloseHandle(storage_parent);storage_parent=NULL;
    WCHAR path[32768];swprintf(path,32768,L"%ls\\user-settings.aot-private.bin",directory);CHECK(DeleteFileW(path));
    for(unsigned i=1;i<=5;++i){swprintf(path,32768,L"%ls\\Partition%u",directory,i);CHECK(RemoveDirectoryW(path));}
    CHECK(RemoveDirectoryW(directory));
    puts("PASS: all19 user fields/typed queries, actual ordinal29 ABI, zero/short/all writes, checksums, atomic persisted reload and failure isolation; supplied EEPROM preserved");
}
