// xlle — opt-in UDP transport for complete Ethernet frames.
//
// Each UDP datagram carries exactly one Ethernet frame. The transport is deliberately
// policy-free: NVNet owns filtering and descriptor DMA, while the CLI selects the local
// bind port and a fixed peer. Sockets are nonblocking so guest execution never waits on
// host networking.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xlle {

class EthernetUdpTransport {
public:
    EthernetUdpTransport() = default;
    ~EthernetUdpTransport();

    EthernetUdpTransport(const EthernetUdpTransport&) = delete;
    EthernetUdpTransport& operator=(const EthernetUdpTransport&) = delete;

    bool bind(uint16_t port, std::string& error);
    bool set_peer(const std::string& host, uint16_t port, std::string& error);
    bool send(const uint8_t* data, size_t size, std::string& error);
    // Returns a datagram length, zero when none is queued, or -1 on a socket error.
    int receive(uint8_t* data, size_t capacity, std::string& error);
    void close();

    bool open() const;
    bool peer_configured() const { return peer_configured_; }
    uint16_t local_port() const { return local_port_; }
    uint64_t tx_packets() const { return tx_packets_; }
    uint64_t tx_bytes() const { return tx_bytes_; }
    uint64_t rx_packets() const { return rx_packets_; }
    uint64_t rx_bytes() const { return rx_bytes_; }

private:
    long long socket_ = -1;
    uint32_t peer_address_ = 0;
    uint16_t peer_port_ = 0;
    uint16_t local_port_ = 0;
    bool peer_configured_ = false;
    bool network_started_ = false;
    uint64_t tx_packets_ = 0;
    uint64_t tx_bytes_ = 0;
    uint64_t rx_packets_ = 0;
    uint64_t rx_bytes_ = 0;
};

}  // namespace xlle
