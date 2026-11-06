#include "native_debug.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
static int read_guest_memory(uint32_t address,uint32_t bytes,unsigned char *output) {
    static const std::array<unsigned char,16> memory={42,0,0,0,255,255,255,255,1,2,3,4,5,6,7,8};
    if(address<0x12000 || uint64_t(address)+bytes>0x12000+memory.size())return 0;
    std::memcpy(output,memory.data()+address-0x12000,bytes);return 1;
}
int main(int argc,char **argv) {
    bool coherent=argc!=3 || std::strcmp(argv[2],"incoherent");
    if((argc!=2 && argc!=3) || pilot_debug_active() || pilot_debug_capture_requested())return 1;
    std::array<uint8_t,18> pad{};if(pilot_debug_input(pad.data()))return 1;
    if(!pilot_debug_start("invalid") || pilot_debug_active())return 1;
    if(pilot_debug_start(nullptr) || pilot_debug_active() || pilot_debug_start(argv[1]))return 1;
    pilot_debug_bind_guest(read_guest_memory);
    std::atomic<bool> running{true},observed{false},resized{false},second{false};
    std::thread producer([&] {
        const std::array<uint8_t,18> expected={1,0,255,0,0,0,0,0,0,0,0xc7,0xcf,0x39,0x30,0,0,0,0};
        std::vector<unsigned char> image(640*480*4);
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x) {
            auto p=&image[(y*640+x)*4];p[0]=x&255;p[1]=y&255;p[2]=0x65;p[3]=255;
        }
        while(running.load()) {
            PilotDebugGuest guest{};guest.pc=0x11000;guest.register_mask=255;
            guest.registers[0]=42;guest.registers[4]=0x20000;guest.coherent=coherent;
            guest.xbe_sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            guest.xmm[0][0]=0x3e800000;
            pilot_debug_guest_checkpoint(&guest);
            uint32_t size[2];
            if(pilot_debug_take_client_size(size) && size[0]==1680 && size[1]==720)resized.store(true);
            if(pilot_debug_input(pad.data()) && pad==expected)observed.store(true);
            std::array<uint8_t,18> other{};
            if(pilot_debug_input_port(1,other.data()) && other[3]==255 && other[2]==0 &&
               !pilot_debug_input_port(0,pad.data()) && pilot_debug_connected(1) && !pilot_debug_connected(2))second.store(true);
            auto request=pilot_debug_capture_requested();
            if(request)pilot_debug_capture_complete(request,image.data(),640,480,0);
            pilot_debug_frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::string command;std::getline(std::cin,command);
    running.store(false);producer.join();
    return !coherent || (observed.load() && resized.load() && second.load())?0:1;
}
