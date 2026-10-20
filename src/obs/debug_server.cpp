// xlle — TCP debug server implementation. Windows (Winsock) + POSIX.
#include "obs/debug_server.hpp"

#include <sstream>
#include <vector>
#include <chrono>
#include <algorithm>

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
#include <fcntl.h>
#include <cerrno>
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
bool socket_ready(socket_t fd, bool writing, long milliseconds) {
    fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
    timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    return ::select(static_cast<int>(fd) + 1, writing ? nullptr : &set,
                    writing ? &set : nullptr, nullptr, &timeout) > 0;
}
bool nonblocking(socket_t fd) {
#if defined(_WIN32)
    u_long value = 1; return ioctlsocket(fd, FIONBIO, &value) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
bool would_block() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}
bool send_bounded(socket_t fd, const std::string& response, const std::atomic<bool>& running) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    size_t offset = 0;
    while (offset < response.size() && running.load()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        if (!socket_ready(fd, true, 100)) continue;
        // A bounded chunk avoids a single large blocking send on slow clients.
        const auto chunk = static_cast<int>(std::min<size_t>(response.size() - offset, 16384));
#if defined(MSG_NOSIGNAL)
        int sent = ::send(fd, response.data() + offset, chunk, MSG_NOSIGNAL);
#else
        int sent = ::send(fd, response.data() + offset, chunk, 0);
#endif
        if (sent < 0 && would_block()) continue;
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return offset == response.size();
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
#if defined(_WIN32)
    // Windows SO_REUSEADDR permits another listener to steal this endpoint.
    // Each title must either own its selected port or report bind failure.
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only; this is a debug port
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        last_error_ = "bind() failed on port " + std::to_string(port);
        close_sock(fd);
        return false;
    }
    if (::listen(fd, 4) != 0 || !nonblocking(fd)) {
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
    // Both accept and client receives have bounded readiness waits. Join before
    // closing the listener so no worker can race a recycled socket handle.
    if (thread_.joinable()) thread_.join();
    if (listen_fd_ >= 0) {
        close_sock(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
}

void DebugServer::accept_loop() {
    while (running_.load()) {
        if (!socket_ready(static_cast<socket_t>(listen_fd_), false, 100)) continue;
        socket_t client = ::accept(static_cast<socket_t>(listen_fd_), nullptr, nullptr);
        if (client == kInvalid) {
            if (!running_.load()) break;
            continue;
        }
        if (!nonblocking(client)) { close_sock(client); continue; }
        // One request line at a time; keep the client open for a session.
        std::string buf;
        char chunk[1024];
        while (running_.load()) {
            if (!socket_ready(client, false, 100)) continue;
            int n = ::recv(client, chunk, sizeof(chunk), 0);
            if (n < 0 && would_block()) continue;
            if (n <= 0) break;
            buf.append(chunk, n);
            if (buf.size() > 4096) break;
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                std::string resp = handle(trim(line));
                if (!resp.empty() && !send_bounded(client, resp, running_)) {
                    close_sock(client); client = kInvalid; break;
                }
            }
            if (client == kInvalid) break;
        }
        if (client != kInvalid) close_sock(client);
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
