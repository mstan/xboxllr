// Authored byte/word and shared-owner contract; no private EEPROM bytes.
#include "machine/smbus.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void check(bool value,const char* why) {
    if(!value){std::fprintf(stderr,"EEPROM: %s\n",why);std::exit(1);}
}
int main() {
    for(unsigned life=0;life<3;++life) {
        std::array<uint8_t,256> backing;
        for(unsigned i=0;i<256;++i)backing[i]=(uint8_t)(i^(0x39+life));
        xlle::XboxEeprom owned(std::vector<uint8_t>(backing.begin(),backing.end()));
        unsigned word_reads=0,word_writes=0;
        xlle::XboxEeprom shared([&](uint8_t offset,unsigned width,bool write,uint16_t& value) {
            check(width==1 || width==2,"backend width");
            if(width==2){if(write)++word_writes;else ++word_reads;}
            if(write) {
                backing[offset]=(uint8_t)value;
                if(width==2)backing[(uint8_t)(offset+1)]=(uint8_t)(value>>8);
            } else value=backing[offset]|(width==2?uint16_t(backing[(uint8_t)(offset+1)])<<8:0);
            return true;
        });
        for(unsigned i=0;i<256;++i) {
            uint8_t value=0;
            check(owned.read_byte((uint8_t)i,value) && value==(uint8_t)(i^(0x39+life)),"owned original profile");
            check(shared.read_byte((uint8_t)i,value) && value==(uint8_t)(i^(0x39+life)),"shared supplied bytes");
        }
        for(auto* device:{&owned,&shared}) {
            check(device->write_word(0xff,0x5a3c),"write wrapping word");
            uint16_t word=0;check(device->read_word(0xff,word) && word==0x5a3c,"read wrapping word");
            uint8_t value=0;check(device->receive_byte(value) && value==(uint8_t)(1^(0x39+life)),"serial offset advances by two");
            check(device->write_byte(0x80,0x72) && device->read_byte(0x80,value) && value==0x72,"byte write/read");
        }
        check(backing[0xff]==0x3c && backing[0]==0x5a && backing[0x80]==0x72,"shared owner sees writes");
        check(word_reads==1 && word_writes==1,"one atomic owner callback per word");
        backing[0x80]=0x26;uint8_t value=0;
        check(shared.read_byte(0x80,value) && value==0x26,"owner changes have no stale device copy");
        xlle::XboxEeprom unavailable([](uint8_t,unsigned,bool,uint16_t&){return false;});
        value=0x39;check(!unavailable.read_byte(0,value) && value==0x39,"unavailable owner does not supply data");
    }
    std::puts("EEPROM: three owner lifetimes, byte/word wrap, atomic access and no duplicate backing pass.");
}
