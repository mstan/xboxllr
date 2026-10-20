// xlle — TCP debug server (PRINCIPLES §7/§9; family port 4370).
//
// The single wire face for observability. Probes connect and QUERY ring windows over a
// line protocol; the engine, the xemu bridge, and the hardware probe all speak the same
// JSONL. Ring types are hidden behind registered "dumper" callbacks so the server never
// depends on any concrete ring.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <atomic>

namespace xlle {

class DebugServer {
public:
    // dumper(n) returns JSONL text for the most recent n entries of a named ring.
    using Dumper = std::function<std::string(size_t n)>;
    using Command = std::function<std::string(const std::string& arguments)>;

    DebugServer() = default;
    ~DebugServer();

    void register_ring(const std::string& name, Dumper d) { dumpers_[name] = std::move(d); }
    // Register before start. Handlers run on the transport thread; mutable
    // engine state must cross an owned mailbox, never a renderer/CPU callback.
    void register_command(const std::string& name, Command c) { commands_[name] = std::move(c); }

    // Starts the accept loop on a background thread. Returns false on bind/listen failure
    // (fault-stop: the reason is written to last_error()).
    bool start(uint16_t port = 4370);
    void stop();

    const std::string& last_error() const { return last_error_; }
    uint16_t port() const { return port_; }

private:
    void accept_loop();
    std::string handle(const std::string& line);

    std::map<std::string, Dumper> dumpers_;
    std::map<std::string, Command> commands_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    long long listen_fd_ = -1;  // SOCKET on Windows; fits in long long
    uint16_t port_ = 0;
    std::string last_error_;
};

}  // namespace xlle
