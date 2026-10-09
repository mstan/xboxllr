// Bounded clock/PIC contract, based on Intel 8254 231164-005 and original
// Xbox programming observed by native_timer_expiry_reference.py.
#include "machine/pit.hpp"
#include "machine/pic.hpp"
#include <cstdio>
#include <cstdlib>
static void check(bool value,const char* why) {
    if(!value){std::fprintf(stderr,"PIT: %s\n",why);std::exit(1);}
}
int main() {
    xlle::Pit8254 pit; xlle::DualPic pic; unsigned pulses=0;
    pic.io_write(0x20,1,0x11);pic.io_write(0x21,1,0x30);
    pic.io_write(0x21,1,4);pic.io_write(0x21,1,1);pic.io_write(0x21,1,0xfe);
    pit.set_irq0([&]{++pulses;pic.raise(0,true);pic.raise(0,false);});
    pit.set_time(100);
    check(!pulses && !pit.next_irq0_tick(),"no qualified clock before programming");
    pit.io_write(0x43,1,0x34);pit.io_write(0x40,1,0x65);
    check(!pit.next_irq0_tick(),"incomplete two-byte count");
    pit.io_write(0x40,1,4);
    check(pit.io_read(0x40,1)==0x65 && pit.io_read(0x40,1)==4,"LSB then MSB");
    pit.set_time(224);pit.io_write(0x43,1,0);
    check(pit.io_read(0x40,1)==0xe9,"latched count 1001 low byte");
    pit.set_time(500);pit.io_write(0x43,1,0);
    check(pit.io_read(0x40,1)==3,"second latch cannot overwrite unread high byte");
    pit.set_time(1224);check(pic.peek()<0,"no early IRQ");
    for(unsigned i=1;i<=64;++i) {
        pit.set_time(100+1125*i);
        check(pulses==i && pic.acknowledge()==0x30,"each serviced 1ms IRQ");
        check(pic.peek()<0,"acknowledgment consumes request");pic.io_write(0x20,1,0x20);
        check(pit.next_irq0_tick()==100+1125*(i+1),"next deadline");
    }
    pic.io_write(0x21,1,0xff);pit.set_time(100+1125*70);
    check(pic.peek()<0,"PIC mask suppresses delivery");pic.io_write(0x21,1,0xfe);
    check(pic.acknowledge()==0x30,"masked request retained");pic.io_write(0x20,1,0x20);
    check(pic.peek()<0,"late clock has one pending PIC request, not a replay queue");
    pit.io_write(0x43,1,0x30);pit.io_write(0x40,1,3);pit.io_write(0x40,1,0);
    auto next=pit.next_irq0_tick();pit.set_time(next);
    check(pic.acknowledge()==0x30 && !pit.next_irq0_tick(),"one-shot terminal count");
    pic.io_write(0x20,1,0x20);pit.set_time(next+100);
    check(pic.peek()<0,"one-shot must not repeat");
    pit.io_write(0x43,1,0x3c);pit.io_write(0x40,1,0);pit.io_write(0x40,1,0);
    check(pit.next_irq0_tick()==next+100+65536,"mode 6 alias and zero count");
    std::puts("PASS: PIT programmed mode-2 clock, latch, PIC delivery/mask/coalescing and one-shot");
}
