// xlle — TCP debug server implementation. Windows (Winsock) + POSIX.
#include "obs/debug_server.hpp"

#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalid = INVALID_SOCKET;
static int close_sock(socket_t s) { return closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalid = -1;
static int close_sock(socket_t s) { return ::close(s); }
#endif

namespace xlle {

namespace {
#if defined(_WIN32)
struct WsaInit {
    WsaInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsa;  // process-lifetime Winsock init
#endif

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}
}  // namespace

DebugServer::~DebugServer() { stop(); }

bool DebugServer::start(uint16_t port) {
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalid) {
        last_error_ = "socket() failed";
        return false;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only; this is a debug port
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        last_error_ = "bind() failed on port " + std::to_string(port);
        close_sock(fd);
        return false;
    }
    if (::listen(fd, 4) != 0) {
        last_error_ = "listen() failed";
        close_sock(fd);
        return false;
    }
    listen_fd_ = static_cast<long long>(fd);
    port_ = port;
    running_.store(true);
    thread_ = std::thread(&DebugServer::accept_loop, this);
    return true;
}

void DebugServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        close_sock(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void DebugServer::accept_loop() {
    while (running_.load()) {
        socket_t client = ::accept(static_cast<socket_t>(listen_fd_), nullptr, nullptr);
        if (client == kInvalid) {
            if (!running_.load()) break;
            continue;
        }
        // One request line at a time; keep the client open for a session.
        std::string buf;
        char chunk[1024];
        while (running_.load()) {
            int n = ::recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.append(chunk, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                std::string resp = handle(trim(line));
                if (!resp.empty()) ::send(client, resp.data(), static_cast<int>(resp.size()), 0);
            }
        }
        close_sock(client);
    }
}

std::string DebugServer::handle(const std::string& line) {
    if (line.empty()) return "";
    std::istringstream is(line);
    std::string cmd;
    is >> cmd;
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
