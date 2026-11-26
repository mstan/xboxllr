// Xbox protocol and mailbox ownership over the shared nonblocking transport.
#include "obs/debug_server.hpp"
#include "diagnostics_server.h"
#include <sstream>
#include <chrono>

namespace xlle {
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}
}
DebugServer::~DebugServer() { stop(); }
bool DebugServer::start(uint16_t port) {
    if(running_.load()) { last_error_="server already running"; return false; }
    server_=diag_server_create(port);
    if(!server_) { last_error_="Could not bind debug loopback port "+std::to_string(port); return false; }
    if(!diag_server_protocol(server_,4096,64u*1024u*1024u,nullptr,nullptr) ||
       !diag_server_raw_handler(server_,[](void* user,DiagServer* server,const char* line) {
           auto* self=static_cast<DebugServer*>(user);
           auto reply=self->handle(trim(line));
           if(!reply.empty() && !diag_server_write(server,reply.data(),reply.size()))
               diag_server_disconnect(server);
       },this)) {
        diag_server_destroy(server_);server_=nullptr;last_error_="Could not configure debug transport";return false;
    }
    port_=diag_server_port(server_);
    running_.store(true);
    try { thread_=std::thread(&DebugServer::accept_loop,this); }
    catch(...) { running_.store(false);diag_server_destroy(server_);server_=nullptr;throw; }
    return true;
}
void DebugServer::stop() {
    if(!running_.exchange(false))return;
    if(thread_.joinable())thread_.join();
    diag_server_destroy(server_);server_=nullptr;
}
void DebugServer::accept_loop() {
    // Existing callbacks still execute on this transport thread and use the
    // established native mailboxes. No guest or renderer ownership changes.
    while(running_.load()) {
        diag_server_poll(server_);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string DebugServer::handle(const std::string& line) {
    if (line.empty()) return "";
    std::istringstream is(line);
    std::string cmd;
    is >> cmd;
    auto command = commands_.find(cmd);
    if (command != commands_.end()) {
        std::string arguments; std::getline(is, arguments);
        return command->second(trim(arguments));
    }
    if (cmd == "ping") return "pong\n";
    if (cmd == "rings") {
        std::string out;
        for (auto& kv : dumpers_) out += kv.first + "\n";
        return out.empty() ? "(none)\n" : out;
    }
    if (cmd == "query") {
        std::string name;
        size_t n = 64;
        is >> name >> n;
        auto it = dumpers_.find(name);
        if (it == dumpers_.end()) return "{\"error\":\"no such ring\"}\n";
        return it->second(n);
    }
    return "{\"error\":\"unknown command\"}\n";
}

}  // namespace xlle
