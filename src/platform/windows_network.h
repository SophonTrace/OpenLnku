#pragma once
#include "core/protocol.h"
#include "core/traffic.h"
#include <winsock2.h>
#include <optional>

namespace jlu {
struct Adapter { Host host; std::string name; std::string description; std::string id; };
std::vector<Adapter> adapters();
std::optional<TrafficCounters> trafficCounters(std::uint32_t interfaceIndex);
unsigned long processIntegrity();
struct RouteInfo { std::uint32_t interfaceIndex = 0; std::string nextHop; bool available = false; std::string interfaceName; };
RouteInfo authRoute();
std::string ipText(Block<4> ip);
struct Datagram { Bytes bytes; bool fromServer = false; };
class UdpTransport {
public:
    UdpTransport();
    ~UdpTransport();
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;
    bool open(const Host& host);
    void close();
    bool send(std::span<const Byte> packet);
    std::optional<Datagram> receive();
    bool isOpen() const { return socket_ != INVALID_SOCKET; }
    const std::string& error() const { return error_; }
private:
    SOCKET socket_ = INVALID_SOCKET;
    bool winsock_ = false;
    std::string error_;
};
}
