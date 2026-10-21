#include "native_debug.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
int main(int argc,char **argv) {
    if(argc!=2 || pilot_debug_active() || pilot_debug_capture_requested())return 1;
    std::array<uint8_t,18> pad{};if(pilot_debug_input(pad.data()))return 1;
    if(!pilot_debug_start("invalid") || pilot_debug_active())return 1;
    if(pilot_debug_start(nullptr) || pilot_debug_active() || pilot_debug_start(argv[1]))return 1;
    std::atomic<bool> running{true},observed{false},resized{false};
    std::thread producer([&] {
        const std::array<uint8_t,18> expected={1,0,255,0,0,0,0,0,0,0,0xc7,0xcf,0x39,0x30,0,0,0,0};
        std::vector<unsigned char> image(640*480*4);
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x) {
            auto p=&image[(y*640+x)*4];p[0]=x&255;p[1]=y&255;p[2]=0x65;p[3]=255;
        }
        while(running.load()) {
            uint32_t size[2];
            if(pilot_debug_take_client_size(size) && size[0]==1680 && size[1]==720)resized.store(true);
            if(pilot_debug_input(pad.data()) && pad==expected)observed.store(true);
            auto request=pilot_debug_capture_requested();
            if(request)pilot_debug_capture_complete(request,image.data(),640,480,0);
            pilot_debug_frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::string command;std::getline(std::cin,command);
    running.store(false);producer.join();
    return observed.load() && resized.load()?0:1;
}
