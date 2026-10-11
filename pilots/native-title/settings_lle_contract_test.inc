/* Unchanged original query/cache/HAL execution versus the native contract.
 * Authored settings and full output canaries provide independent inputs. */
static unsigned settings_word_reads;
static int settings_fixture_access(uint8_t offset,int size,int write,uint32_t *value)
{
    if(!write && size==2)++settings_word_reads;
    return shared_eeprom_access(offset,size,write,value);
}
static void settings_compare_query(uint32_t index,uint32_t length,uint32_t expected_status,
                                   uint32_t expected_type,const unsigned char *expected,uint32_t bytes)
{
    const uint32_t outputs[]={0x1c000,0x1c100};
    for(unsigned native=0;native<2;++native) {
        uint32_t base=outputs[native],args[]={index,base+4,base+16,length,base+8};
        memset(memory+base,0xa5,160);
        if(native)call_kernel(24,args,5);
        else CHECK(!pilot_io_device_lle(24,memory,args,&g_eax));
        CHECK(g_eax==expected_status);
        CHECK(MEM32(base)==0xa5a5a5a5u && MEM32(base+12)==0xa5a5a5a5u);
        if(expected_status) {
            for(unsigned i=0;i<160;++i)CHECK(MEM8(base+i)==0xa5);
        } else {
            CHECK(MEM32(base+4)==expected_type && MEM32(base+8)==bytes);
            CHECK(!memcmp(memory+base+16,expected,bytes));
            for(unsigned i=bytes;i<length;++i)CHECK(!MEM8(base+16+i));
            CHECK(MEM8(base+16+length)==0xa5);
        }
    }
    CHECK(!memcmp(memory+outputs[0],memory+outputs[1],160));
}
static void test_settings_lle(unsigned mode)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    for(unsigned i=0;i<256;++i)eeprom[i]=(uint8_t)(3+i*7);
    memset(eeprom+0x60,0,4);
    uint64_t total=0;
    for(unsigned i=0x64;i<0xc0;i+=4) {
        uint32_t word=0;memcpy(&word,eeprom+i,4);total+=word;
        total=(total&UINT32_MAX)+(total>>32);
    }
    uint32_t checksum=~(uint32_t)total;memcpy(eeprom+0x60,&checksum,4);
    if(mode==1)eeprom[0x90]^=1;
    if(mode==2)memset(eeprom,0xff,256);
    have_eeprom=1;CHECK(!pilot_usb_board_bind_eeprom(settings_fixture_access));
    unsigned char expected[256];memcpy(expected,eeprom,sizeof(expected));
    settings_compare_query(19,4,0xc0000034u,0,NULL,0);
    CHECK(!settings_word_reads); /* Unknown keys do not consume the owner. */
    if(mode) {
        uint32_t status=mode==1?0xc000009cu:0xc0000184u;
        settings_compare_query(7,4,status,0,NULL,0);
        settings_compare_query(7,3,status,0,NULL,0); /* Load/checksum failure precedes length. */
        CHECK(settings_word_reads==(mode==1?128u:256u));
        // Only an all-FF load leaves the cache invalid. Repair one factory
        // byte through actual HAL; the unchanged all-FF user checksum is valid.
        uint32_t write[]={0xa8,0,0,0};CHECK(!pilot_io_device_lle(50,memory,write,&g_eax));CHECK(!g_eax);
        if(mode==2) {
            settings_compare_query(7,4,0,4,expected+0x90,4);
            CHECK(settings_word_reads==384);
        } else {
            settings_compare_query(7,4,status,0,NULL,0);CHECK(settings_word_reads==128);
        }
    } else {
        static const unsigned offsets[]={4,8,24,40,12,28,44,48,52,56,60,64,68,72,76,80,84,88,92};
        for(unsigned index=0;index<19;++index) {
            uint32_t type=index==1 || index==2 || index==4 || index==5 || index==18?3:4;
            settings_compare_query(index,12,0,type,expected+0x60+offsets[index],4);
        }
        settings_compare_query(0xff,104,0,3,expected+0x60,96);
        settings_compare_query(7,3,0xc0000023u,0,NULL,0);
        settings_compare_query(0xff,95,0xc0000023u,0,NULL,0);
        uint32_t write[]={0xa8,0x90,0,expected[0x90]^1u};
        CHECK(!pilot_io_device_lle(50,memory,write,&g_eax));CHECK(!g_eax);
        CHECK(eeprom[0x90]!=(uint8_t)expected[0x90]);
        settings_compare_query(7,12,0,4,expected+0x90,4);
        CHECK(settings_word_reads==128); /* Actual cache survives raw bus writes. */
    }
    uint32_t status=0;CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    fprintf(stderr,"PASS: original/native settings mode%u, actual HAL word loads=%u, error ordering/canaries and boot-derived cache ownership\n",mode,settings_word_reads);
}
static void test_status_map_lle(const char *audit_path)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    const uint32_t statuses[]={0,0x102,0x103,0x105,0x107,
        0x80000005,0x80000006,0x8000001a,
        0xc0000001,0xc0000002,0xc0000003,0xc0000004,0xc0000005,0xc0000008,
        0xc000000d,0xc0000017,0xc0000022,0xc0000023,0xc0000034,0xc0000035,
        0xc000003a,0xc0000043,0xc0000054,0xc0000056,0xc000007f,0xc000009a,
        0xc000009c,0xc0000120,0xc0000184,0xc0000185,
        0x80070005,0xc0010017,0xd0000008,0xe1234567,0x12345678};
    unsigned failures=0,count=0;
    FILE *audit=audit_path?fopen(audit_path,"r"):NULL;
    if(audit_path)CHECK(audit);
    for(unsigned i=0;;++i) {
        uint32_t argument=0;
        if(audit) {if(fscanf(audit,"%x",&argument)!=1)break;}
        else {if(i>=sizeof(statuses)/sizeof(statuses[0]))break;argument=statuses[i];}
        uint32_t original=0;
        CHECK(!pilot_io_device_lle(301,memory,&argument,&original));
        call_kernel(301,&argument,1);
        if(audit)fprintf(stdout,"STATUS %08x %08x %08x\n",argument,original,g_eax);
        if(g_eax!=original)fprintf(stderr,"NT status mapping mismatch %08x: original=%u native=%u\n",argument,original,g_eax);
        failures+=g_eax!=original;++count;
    }
    if(audit)fclose(audit);
    if(!audit_path) {
        /* Native constants supply inputs, never expected results. Unchanged
         * original code independently supplies each expected EAX. The private
         * domain audit also checks every original lookup range for omissions. */
        for(unsigned i=0;i<sizeof(pilot_status_mappings)/sizeof(pilot_status_mappings[0]);++i) {
            for(unsigned variant=0;variant<4;++variant) {
                uint32_t argument=pilot_status_mappings[i].status;
                if(variant==0)--argument;
                if(variant==2)++argument;
                if(variant==3)argument|=0x10000000u;
                uint32_t original=0;CHECK(!pilot_io_device_lle(301,memory,&argument,&original));
                call_kernel(301,&argument,1);
                if(g_eax!=original)fprintf(stderr,"NT status mapping mismatch %08x: original=%u native=%u\n",argument,original,g_eax);
                failures+=g_eax!=original;++count;
            }
        }
    }
    CHECK(!failures);
    fprintf(stderr,"PASS: %u original/native NTSTATUS translations, service errors, facilities/customer bits and unknown status\n",count);
}
