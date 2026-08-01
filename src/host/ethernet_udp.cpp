// xlle — nonblocking UDP Ethernet-frame transport.
#include "host/ethernet_udp.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xlle {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;

int socket_error() { return WSAGetLastError(); }
bool would_block(int error) { return error == WSAEWOULDBLOCK; }
void close_socket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;

int socket_error() { return errno; }
bool would_block(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
void close_socket(Socket socket) { ::close(socket); }
#endif

void format_socket_error(std::string& error, const char* operation, int code) {
    char message[128];
    std::snprintf(message, sizeof(message), "UDP Ethernet %s failed (socket error %d)",
                  operation, code);
    error = message;
}

}  // namespace

EthernetUdpTransport::~EthernetUdpTransport() {
    close();
}

bool EthernetUdpTransport::open() const {
    return static_cast<Socket>(socket_) != kInvalidSocket;
}

bool EthernetUdpTransport::bind(uint16_t port, std::string& error) {
    close();
    error.clear();

#ifdef _WIN32
    WSADATA data{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup != 0) {
        format_socket_error(error, "WSAStartup", startup);
        return false;
    }
    network_started_ = true;
#endif

    Socket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket) {
        format_socket_error(error, "socket", socket_error());
        close();
        return false;
    }
    socket_ = static_cast<long long>(socket);

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        format_socket_error(error, "bind", socket_error());
        close();
        return false;
    }

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
        format_socket_error(error, "nonblocking setup", socket_error());
        close();
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
        format_socket_error(error, "nonblocking setup", socket_error());
        close();
        return false;
    }
#endif

    sockaddr_in actual{};
#ifdef _WIN32
    int actual_size = sizeof(actual);
#else
    socklen_t actual_size = sizeof(actual);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&actual), &actual_size) != 0) {
        format_socket_error(error, "getsockname", socket_error());
        close();
        return false;
    }
    local_port_ = ntohs(actual.sin_port);
    return true;
}

bool EthernetUdpTransport::set_peer(const std::string& host, uint16_t port,
                                    std::string& error) {
    error.clear();
    if (!open()) {
        error = "UDP Ethernet peer configured before local bind";
        return false;
    }
    if (host.empty() || port == 0) {
        error = "UDP Ethernet peer requires a host and nonzero port";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    const std::string service = std::to_string(port);
    const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
    if (status != 0 || !result) {
        char message[192];
        std::snprintf(message, sizeof(message),
                      "UDP Ethernet could not resolve peer %s:%u (error %d)",
                      host.c_str(), static_cast<unsigned>(port), status);
        error = message;
        if (result) freeaddrinfo(result);
        return false;
    }

    const auto* peer = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    peer_address_ = peer->sin_addr.s_addr;
    peer_port_ = peer->sin_port;
    peer_configured_ = true;
    freeaddrinfo(result);
    return true;
}

bool EthernetUdpTransport::send(const uint8_t* data, size_t size,
                                std::string& error) {
    error.clear();
    if (!open() || !peer_configured_) {
        error = "UDP Ethernet send attempted without an open socket and peer";
        return false;
    }
    if ((!data && size != 0) ||
        size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "UDP Ethernet send received an invalid buffer";
        return false;
    }

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = peer_address_;
    peer.sin_port = peer_port_;
    const int sent = ::sendto(
        static_cast<Socket>(socket_), reinterpret_cast<const char*>(data),
        static_cast<int>(size), 0, reinterpret_cast<const sockaddr*>(&peer),
        sizeof(peer));
    if (sent < 0 || static_cast<size_t>(sent) != size) {
        format_socket_error(error, "sendto", socket_error());
        return false;
    }
    ++tx_packets_;
    tx_bytes_ += size;
    return true;
}

int EthernetUdpTransport::receive(uint8_t* data, size_t capacity,
                                  std::string& error) {
    error.clear();
    if (!open()) {
        error = "UDP Ethernet receive attempted without an open socket";
        return -1;
    }
    if (!data || capacity == 0 ||
        capacity > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "UDP Ethernet receive received an invalid buffer";
        return -1;
    }

    const int received = ::recvfrom(
        static_cast<Socket>(socket_), reinterpret_cast<char*>(data),
        static_cast<int>(capacity), 0, nullptr, nullptr);
    if (received < 0) {
        const int code = socket_error();
        if (would_block(code)) return 0;
        format_socket_error(error, "recvfrom", code);
        return -1;
    }
    ++rx_packets_;
    rx_bytes_ += static_cast<size_t>(received);
    return received;
}

void EthernetUdpTransport::close() {
    const Socket socket = static_cast<Socket>(socket_);
    if (socket != kInvalidSocket) close_socket(socket);
    socket_ = static_cast<long long>(kInvalidSocket);
    peer_address_ = 0;
    peer_port_ = 0;
    local_port_ = 0;
    peer_configured_ = false;
#ifdef _WIN32
    if (network_started_) WSACleanup();
#endif
    network_started_ = false;
}

}  // namespace xlle
