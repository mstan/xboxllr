// CMOS storage/alias contract. Clock advancement and RTC interrupt support
// are intentionally outside this test, matching the documented device scope.
#include "machine/rtc.hpp"
#include <cstdio>
#include <cstdlib>
static void check(bool value,const char* why) {
    if(!value){std::fprintf(stderr,"RTC: %s\n",why);std::exit(1);}
}
int main() {
    for(unsigned life=0;life<3;++life) {
        xlle::Mc146818Rtc rtc;
        check(rtc.owns(0x70) && rtc.owns(0x73) && !rtc.owns(0x6f) && !rtc.owns(0x74),"four-port decode");
        for(unsigned index=0;index<256;++index) {
            rtc.io_write(0x72,1,index);
            check(rtc.selected_index()==index,"extended eight-bit selection");
            check(rtc.io_read(0x70,1)==255 && rtc.io_read(0x72,1)==255,"index reads do not expose selector");
            check(rtc.selected_index()==index,"index reads leave selector intact");
            check(rtc.io_read(0x71,1)==rtc.io_read(0x73,1),"shared data alias");
            if(xlle::Mc146818Rtc::signature_ram((uint8_t)index)) {
                uint32_t expected=(index&1)?0xaa:0x55;
                check(rtc.io_read(0x71,1)==expected,"initialized kernel signature");
                uint32_t changed=index^0x39;
                rtc.io_write(0x73,1,changed);
                check(rtc.io_read(0x71,1)==changed,"extended data write visible through legacy alias");
                rtc.io_write(0x71,1,expected);
                check(rtc.io_read(0x73,1)==expected,"legacy data write visible through extended alias");
            }
        }
        // Distinguish the high bank from low registers and seven-bit selection.
        rtc.io_write(0x72,1,0x90);rtc.io_write(0x71,1,0x36);
        rtc.io_write(0x70,1,0x90);
        check(rtc.selected_index()==0x10 && rtc.io_read(0x73,1)==0x55,"legacy selector masks bit7");
        rtc.io_write(0x72,1,0x90);check(rtc.io_read(0x71,1)==0x36,"high bank does not alias low RAM");
        rtc.io_write(0x72,1,0x0d);rtc.io_write(0x73,1,0);
        check(rtc.io_read(0x71,1)==0x80,"read-only valid-RAM status");
    }
    std::puts("RTC CMOS storage: three lifetimes, full index range and both data aliases pass.");
}
