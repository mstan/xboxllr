// Independent native-title adapter over xboxllr's shared TCP transport.
// PSX workflow reference: press/set_input/clear_input and presented screenshots.
#include "native_debug.h"
#include "obs/debug_server.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::atomic<bool> active{false};
std::atomic<uint64_t> frames{0},wanted{0};
std::atomic<uint64_t> requested_size{0};
std::mutex state_mutex;
std::condition_variable captured;
std::array<std::array<uint8_t,18>,4> input{};
std::array<Clock::time_point,4> input_until{};
std::array<uint64_t,4> input_sequence{},input_delivered{};
std::atomic<unsigned> connected_mask{1};
uint64_t sequence=0,delivered=0,capture_sequence=0,completed=0,capture_frame=0;
unsigned image_width=0,image_height=0;
int image_status=0;
std::vector<unsigned char> image;
std::unique_ptr<xlle::DebugServer> server; // Destroy before its mailbox owners.
std::string failure(const char *message) {return std::string("{\"ok\":false,\"error\":\"")+message+"\"}\n";}
bool number(const std::string& text,unsigned& value) {
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return !text.empty() && result.ec==std::errc{} && result.ptr==text.data()+text.size();
}
int hex(char c) {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;}
std::string status(const std::string& args) {
    if(!args.empty())return failure("status takes no arguments");
    std::lock_guard<std::mutex> lock(state_mutex);
    return "{\"ok\":true,\"protocol\":\"xboxllr.native-debug.v1\",\"frames\":"+std::to_string(frames.load())+
        ",\"input_sequence\":"+std::to_string(sequence)+",\"delivered_sequence\":"+std::to_string(delivered)+
        ",\"connected_mask\":"+std::to_string(connected_mask.load())+
        ",\"input_active\":"+(std::any_of(input_until.begin(),input_until.end(),[](auto t){return Clock::now()<t;})?"true":"false")+"}\n";
}
std::string set_port_input(const std::string& args,unsigned port) {
    std::istringstream stream(args);std::string pad,lease,extra;stream>>pad>>lease;
    unsigned milliseconds=0;
    if(stream>>extra || pad.size()!=36 || !number(lease,milliseconds) || !milliseconds || milliseconds>5000)
        return failure("set_input needs 36 hex digits and lease_ms in 1..5000");
    std::array<uint8_t,18> next{};
    for(unsigned i=0;i<18;++i) {
        int high=hex(pad[i*2]),low=hex(pad[i*2+1]);
        if(high<0 || low<0)return failure("invalid gamepad hex");
        next[i]=static_cast<uint8_t>((high<<4)|low);
    }
    if(next[1])return failure("reserved digital button bits");
    std::lock_guard<std::mutex> lock(state_mutex);input[port]=next;
    input_until[port]=Clock::now()+std::chrono::milliseconds(milliseconds);input_sequence[port]=++sequence;
    connected_mask.fetch_or(1u<<port);
    return "{\"ok\":true,\"sequence\":"+std::to_string(sequence)+"}\n";
}
std::string set_input(const std::string& args) {return set_port_input(args,0);}
std::string set_input_port(const std::string& args) {
    auto space=args.find(' ');unsigned port=0;
    if(space==std::string::npos || !number(args.substr(0,space),port) || port>3)return failure("set_input_port needs port 0..3, gamepad hex and lease_ms");
    return set_port_input(args.substr(space+1),port);
}
std::string connect_input(const std::string& args) {
    unsigned mask=0;if(!number(args,mask) || mask>15 || !(mask&1))return failure("connect_input needs port mask 1..15 including Player 1");
    std::lock_guard<std::mutex> lock(state_mutex);
    for(unsigned port=0;port<4;port++)if(!(mask&(1u<<port))) {input[port].fill(0);input_until[port]={};}
    connected_mask.store(mask);return "{\"ok\":true}\n";
}
std::string clear_input(const std::string& args) {
    if(!args.empty())return failure("clear_input takes no arguments");
    std::lock_guard<std::mutex> lock(state_mutex);for(auto &pad:input)pad.fill(0);input_until={};
    return "{\"ok\":true}\n";
}
std::string resize(const std::string& args) {
    std::istringstream stream(args);std::string x,y,extra;stream>>x>>y;
    unsigned width=0,height=0;
    if(stream>>extra || !number(x,width) || !number(y,height) || width<64 || height<64 ||
       width>8192 || height>8192 || uint64_t(width)*height>16*1024*1024)
        return failure("resize needs client width/height in 64..8192, at most 16M pixels");
    requested_size.store((uint64_t(width)<<32)|height);
    return "{\"ok\":true,\"queued\":true}\n";
}
std::string base64(const std::vector<unsigned char>& bytes) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;result.reserve(((bytes.size()+2)/3)*4);
    for(size_t i=0;i<bytes.size();i+=3) {
        unsigned value=unsigned(bytes[i])<<16;
        if(i+1<bytes.size())value|=unsigned(bytes[i+1])<<8;
        if(i+2<bytes.size())value|=bytes[i+2];
        result+=alphabet[value>>18];result+=alphabet[(value>>12)&63];
        result+=i+1<bytes.size()?alphabet[(value>>6)&63]:'=';
        result+=i+2<bytes.size()?alphabet[value&63]:'=';
    }
    return result;
}
std::string screenshot(const std::string& args) {
    if(!args.empty())return failure("screenshot takes no arguments");
    std::unique_lock<std::mutex> lock(state_mutex);
    const auto request=++capture_sequence;wanted.store(request);
    if(!captured.wait_for(lock,std::chrono::seconds(3),[&]{return completed==request;})) {
        wanted.store(0);return failure("no presented frame within 3 seconds");
    }
    if(image_status)return failure("presented GPU readback failed or unsupported");
    auto pixels=std::move(image);unsigned width=image_width,height=image_height;auto frame=capture_frame;
    lock.unlock();
    return "{\"ok\":true,\"surface\":\"presented\",\"format\":\"rgba8\",\"width\":"+std::to_string(width)+
        ",\"height\":"+std::to_string(height)+",\"frame\":"+std::to_string(frame)+",\"data\":\""+base64(pixels)+"\"}\n";
}
}
extern "C" int pilot_debug_start(const char *port_text) {
    if(!port_text || !*port_text)return 0;
    unsigned port=0;if(!number(port_text,port) || !port || port>65535 || active.load())return 1;
    server=std::make_unique<xlle::DebugServer>();
    server->register_command("status",status);server->register_command("set_input",set_input);
    server->register_command("set_input_port",set_input_port);server->register_command("connect_input",connect_input);
    server->register_command("clear_input",clear_input);server->register_command("screenshot",screenshot);
    server->register_command("resize",resize);
    if(!server->start(static_cast<uint16_t>(port))) {
        std::fprintf(stderr,"[NATIVE TCP] %s\n",server->last_error().c_str());server.reset();return 1;
    }
    active.store(true);std::fprintf(stderr,"[NATIVE TCP] listening on 127.0.0.1:%u; leased input and on-demand presented GPU screenshots\n",port);
    return 0;
}
extern "C" int pilot_debug_active(void) {return active.load();}
extern "C" int pilot_debug_connected(unsigned port) {return active.load() && port<4 && (connected_mask.load()&(1u<<port));}
extern "C" int pilot_debug_take_client_size(uint32_t size[2]) {
    if(!size || !active.load())return 0;
    const auto requested=requested_size.exchange(0);
    if(!requested)return 0;
    size[0]=uint32_t(requested>>32);size[1]=uint32_t(requested);return 1;
}
extern "C" int pilot_debug_input(uint8_t pad[18]) {return pilot_debug_input_port(0,pad);}
extern "C" int pilot_debug_input_port(unsigned port,uint8_t pad[18]) {
    if(!pilot_debug_connected(port) || !pad)return 0;
    std::lock_guard<std::mutex> lock(state_mutex);
    if(Clock::now()>=input_until[port])return 0;
    std::memcpy(pad,input[port].data(),input[port].size());
    if(input_delivered[port]!=input_sequence[port]) {
        input_delivered[port]=input_sequence[port];delivered=std::max(delivered,input_sequence[port]);
        std::fprintf(stderr,"[NATIVE TCP INPUT] delivered=%llu port=%u pad=",static_cast<unsigned long long>(input_delivered[port]),port);
        for(auto byte:input[port])std::fprintf(stderr,"%02x",byte);std::fputc('\n',stderr);
    }
    return 1;
}
extern "C" void pilot_debug_frame(void) {if(active.load())++frames;}
extern "C" uint64_t pilot_debug_capture_requested(void) {return wanted.load();}
extern "C" void pilot_debug_capture_complete(uint64_t request,const unsigned char *rgba,unsigned width,unsigned height,int result) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if(!request || wanted.load()!=request)return;
    const uint64_t bytes=uint64_t(width)*height*4;
    if(!result && (!rgba || !width || !height || bytes>64*1024*1024))result=1;
    image.clear();if(!result)image.assign(rgba,rgba+static_cast<size_t>(bytes));
    image_width=width;image_height=height;image_status=result;capture_frame=frames.load()+1;
    completed=request;wanted.store(0);captured.notify_one();
}
