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
    }
    // Admission fails before starting unconnected EEPROM/encoder work.
    uint32_t address=0xa9,command=0x1a;
    CHECK(!pilot_usb_board_port(0xc004,1,1,&address));
    CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    uint32_t status=0;CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    address=0x20;CHECK(!pilot_usb_board_port(0xc004,1,1,&address));
    uint32_t smc_register=7;CHECK(!pilot_usb_board_port(0xc008,1,1,&smc_register));
    CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    CHECK(!pilot_usb_board_port(0xc000,1,0,&status));
    CHECK(!(status&0x10) && pilot_usb_pic_vector(0)<0);
    smc_register=0x1b;CHECK(!pilot_usb_board_port(0xc008,1,1,&smc_register));
    command=0x1b;CHECK(pilot_usb_board_port(0xc002,1,1,&command));
    uint32_t value=0x39;CHECK(pilot_usb_board_port(0xc009,1,0,&value) && value==0x39);
    CHECK(pilot_usb_board_port(0xc004,2,1,&value));
    puts("[SMBus SMC] original HAL reads/writes, three call rounds, IRQ11 ack and endpoint guards pass");
}
