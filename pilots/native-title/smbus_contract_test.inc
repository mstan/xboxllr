/* Actual HAL callers, shared maintained controller, original IRQ11/ISR/EOI.
 * The model owns completion; no event, status or return is supplied by host. */
static void test_smbus_smc_lle(void)
{
    install_fault_handler();
    uint64_t ticks=0;CHECK(!pilot_io_device_interrupt_time(memory,&ticks));
    uint32_t unbound_address=0x21,unbound_register=3,unbound_start=0x1a;
    CHECK(!pilot_usb_board_port(0xc004,1,1,&unbound_address));
    CHECK(!pilot_usb_board_port(0xc008,1,1,&unbound_register));
    CHECK(pilot_usb_board_port(0xc002,1,1,&unbound_start));
    uint32_t unbound_status=0;CHECK(!pilot_usb_board_port(0xc000,1,0,&unbound_status));
    CHECK(!(unbound_status&0x10) && pilot_usb_pic_vector(0)<0);
    CHECK(!pilot_io_device_files(memory));
    CHECK(pilot_usb_board_bind_tray(NULL));
    const uint32_t output=0x19c00;
    for(unsigned life=0;life<3;++life) {
        uint32_t args[4]={0x20,1,0,0};
        CHECK(!pilot_io_device_lle(50,memory,args,&g_eax));CHECK(!g_eax);
        for(unsigned i=0;i<3;++i) {
            MEM32(output)=0xdeadbeef;args[3]=output;
            CHECK(!pilot_io_device_lle(45,memory,args,&g_eax));CHECK(!g_eax);
            CHECK(MEM32(output)==(i==0?0x50:0x31));
        }
        args[1]=4;CHECK(!pilot_io_device_lle(45,memory,args,&g_eax));
        CHECK(!g_eax && MEM32(output)==6);
        args[1]=3;CHECK(!pilot_io_device_lle(45,memory,args,&g_eax));
        CHECK(!g_eax && MEM32(output)==0); // Actual installed-HDD IDE owner: no DVD attached.
        args[1]=0x1b;args[3]=0x39+life;
        CHECK(!pilot_io_device_lle(50,memory,args,&g_eax));CHECK(!g_eax);
        args[3]=output;CHECK(!pilot_io_device_lle(45,memory,args,&g_eax));
        CHECK(!g_eax && MEM32(output)==0x39+life);
        uint32_t status=0;CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
        CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
        uint32_t known=0,mode=0,sequence=0;
        args[1]=7;args[3]=0;CHECK(!pilot_io_device_lle(50,memory,args,&g_eax));CHECK(!g_eax);
        args[1]=8;args[3]=0xe1+life;CHECK(!pilot_io_device_lle(50,memory,args,&g_eax));CHECK(!g_eax);
        CHECK(!pilot_usb_board_smc_led_requests(&known,&mode,&sequence));
        CHECK(known==3 && mode==0 && sequence==0xe1+life);
        args[1]=7;args[3]=1;CHECK(!pilot_io_device_lle(50,memory,args,&g_eax));CHECK(!g_eax);
        CHECK(!pilot_usb_board_smc_led_requests(&known,&mode,&sequence));
        CHECK(known==3 && mode==1 && sequence==0xe1+life);
        CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
        CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    }
    // Admission fails before starting work without its settings owner.
    uint32_t address=0xa9,command=0x1a;
    CHECK(!pilot_usb_board_port(0xc004,1,1,&address));
    CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    uint32_t status=0;CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    address=0x20;CHECK(!pilot_usb_board_port(0xc004,1,1,&address));
    uint32_t smc_register=5;CHECK(!pilot_usb_board_port(0xc008,1,1,&smc_register));
    CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    smc_register=0x1b;CHECK(!pilot_usb_board_port(0xc008,1,1,&smc_register));
    command=0x1b;CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    uint32_t value=0x39;CHECK(pilot_usb_board_port(0xc009,1,0,&value) && value==0x39);
    CHECK(pilot_usb_board_port(0xc004,2,1,&value));

    for(unsigned i=0;i<256;++i)eeprom[i]=(uint8_t)(i^0x5a);
    have_eeprom=1;CHECK(!pilot_usb_board_bind_eeprom(shared_eeprom_access));
    CHECK(pilot_usb_board_bind_eeprom(shared_eeprom_access));
    for(unsigned i=0;i<256;++i) {
        uint32_t args[]={0xa8,i,0,output};MEM32(output)=0xdeadbeef;
        CHECK(!pilot_io_device_lle(45,memory,args,&g_eax));
        CHECK(!g_eax && MEM32(output)==(i^0x5a));
    }
    // Native-owner changes are visible to the original controller immediately.
    eeprom[0xff]=0x31;eeprom[0]=0x72;
    uint32_t read_wrap[]={0xa8,0xff,1,output};
    CHECK(!pilot_io_device_lle(45,memory,read_wrap,&g_eax));
    CHECK(!g_eax && MEM32(output)==0x7231);
    uint32_t write_wrap[]={0xa8,0xff,1,0x5a3c};
    CHECK(!pilot_io_device_lle(50,memory,write_wrap,&g_eax));
    CHECK(!g_eax && eeprom[0xff]==0x3c && eeprom[0]==0x5a);

    // SMBus writes change storage; the settings API keeps its original cache.
    memset(eeprom,0,sizeof(eeprom));
    uint32_t language=6,checksum=~language;
    memcpy(eeprom+0x90,&language,4);memcpy(eeprom+0x60,&checksum,4);
    uint32_t query[]={7,0x19100,0x19104,4,0x19108};call_kernel(24,query,5);
    CHECK(!g_eax && MEM32(0x19104)==6);
    uint32_t set_language[]={0xa8,0x90,0,7};
    CHECK(!pilot_io_device_lle(50,memory,set_language,&g_eax));CHECK(!g_eax);
    MEM32(0x19104)=0xdeadbeef;call_kernel(24,query,5);
    CHECK(!g_eax && MEM32(0x19104)==6 && eeprom[0x90]==7);
    uint32_t set_checksum[]={0xa8,0x60,1,0xfff8};
    CHECK(!pilot_io_device_lle(50,memory,set_checksum,&g_eax));CHECK(!g_eax);
    set_checksum[1]=0x62;set_checksum[3]=0xffff;
    CHECK(!pilot_io_device_lle(50,memory,set_checksum,&g_eax));CHECK(!g_eax);
    call_kernel(24,query,5);CHECK(!g_eax && MEM32(0x19104)==6);
    address=0xa9;command=0x19;CHECK(!pilot_usb_board_port(0xc004,1,1,&address));
    CHECK(pilot_usb_board_port(0xc002,1,1,&command)); // RECEIVE_BYTE remains outside HAL byte/word scope.
    CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    puts("[SMBus SMC] original HAL, IRQ11 ack, shared IDE tray and256-byte native EEPROM owner pass");
}
