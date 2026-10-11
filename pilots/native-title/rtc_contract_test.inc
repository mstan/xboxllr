/* The compiled CPU port helpers and original engine share board-owned CMOS.
 * Time/status/interrupt/NMI accesses must fail before returning fixed values. */
static void test_rtc_cmos_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    for(unsigned index=0x10;index<256;++index)if((index<0x70) || index>=0x80) {
        pilot_cpu_out8(0x72,(uint8_t)index);
        uint8_t expected=(index&1)?0xaa:0x55;
        CHECK(pilot_cpu_in8(0x71)==expected && pilot_cpu_in8(0x73)==expected);
    }
    pilot_cpu_out8(0x72,0xff);pilot_cpu_out8(0x73,0x36);
    CHECK(pilot_cpu_in8(0x71)==0x36);
    pilot_cpu_out8(0x71,0xaa);CHECK(pilot_cpu_in8(0x73)==0xaa);
    pilot_cpu_out8(0x72,0x90);pilot_cpu_out8(0x71,0x27);
    pilot_cpu_out8(0x70,0x10);CHECK(pilot_cpu_in8(0x73)==0x55);
    pilot_cpu_out8(0x72,0x90);CHECK(pilot_cpu_in8(0x71)==0x27);
    pilot_cpu_out8(0x71,0x55);
    uint32_t value=0x91;
    CHECK(pilot_usb_board_port(0x72,2,1,&value));
    CHECK(pilot_cpu_in8(0x71)==0x55);
    value=0x100;CHECK(pilot_usb_board_port(0x72,1,1,&value));
    value=0x90;CHECK(pilot_usb_board_port(0x70,1,1,&value));
    CHECK(pilot_cpu_in8(0x71)==0x55);
    for(unsigned index=0;index<128;++index)if(index<0x10 || index>=0x70) {
        pilot_cpu_out8(0x72,(uint8_t)index);value=0x39;
        CHECK(pilot_usb_board_port(0x71,1,0,&value) && value==0x39);
        CHECK(pilot_usb_board_port(0x73,1,1,&value));
    }
    pilot_cpu_out8(0x72,0x80);CHECK(pilot_cpu_in8(0x71)==0x55);
    puts("[RTC CMOS] compiled port aliases, high-bank independence and clock/NMI/width guards pass");
}
