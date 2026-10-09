#include "windows_network.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cstring>

namespace jlu {
unsigned long processIntegrity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<Byte> storage(size);
    DWORD level = 0;
    if (size && GetTokenInformation(token, TokenIntegrityLevel, storage.data(), size, &size)) {
        const auto label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(storage.data());
        const auto sid = label->Label.Sid;
        if (IsValidSid(sid)) level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }
    CloseHandle(token); return level;
}
std::optional<TrafficCounters> trafficCounters(std::uint32_t interfaceIndex) {
    if (!interfaceIndex) return {};
    MIB_IF_ROW2 row{}; row.InterfaceIndex = interfaceIndex;
    if (GetIfEntry2(&row) != NO_ERROR || row.OperStatus != IfOperStatusUp ||
        !row.InterfaceAndOperStatusFlags.HardwareInterface) return {};
    return TrafficCounters{row.InOctets, row.OutOctets};
}
namespace {
std::string utf8(const wchar_t* p) {
    if (!p) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, p, -1, s.data(), n, nullptr, nullptr); s.pop_back(); return s;
}
sockaddr_in destination() {
    sockaddr_in d{}; d.sin_family = AF_INET; d.sin_port = htons(61440); InetPtonA(AF_INET, "10.100.61.3", &d.sin_addr); return d;
}
}
std::string ipText(Block<4> ip) {
    return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]);
}
std::vector<Adapter> adapters() {
    ULONG size = 16384; std::vector<Byte> storage(size);
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        storage.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    }
    std::vector<Adapter> out;
    if (result != NO_ERROR) return out;
    wchar_t hostname[256]{}; DWORD hostSize = 256; GetComputerNameW(hostname, &hostSize);
    auto hostText = utf8(hostname); if (hostText.size() > 32) hostText = "OpenNex";
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->PhysicalAddressLength != 6 ||
            (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) || a->TunnelType != TUNNEL_TYPE_NONE) continue;
        // HardwareInterface rejects Wintun/Hyper-V adapters even when their advertised type is Ethernet.
        MIB_IF_ROW2 row{}; row.InterfaceIndex = a->IfIndex;
        if (GetIfEntry2(&row) != NO_ERROR || !row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        for (auto* address = a->FirstUnicastAddress; address; address = address->Next) {
            if (address->Address.lpSockaddr->sa_family != AF_INET || address->DadState != IpDadStatePreferred) continue;
            const auto* v4 = reinterpret_cast<const sockaddr_in*>(address->Address.lpSockaddr);
            Host h; std::memcpy(h.ip.data(), &v4->sin_addr, 4);
            if (h.ip[0] == 127 || (h.ip[0] == 169 && h.ip[1] == 254)) continue;
            std::copy_n(a->PhysicalAddress, 6, h.mac.begin()); h.interfaceIndex = a->IfIndex; h.hostname = hostText;
            out.push_back({h, utf8(a->FriendlyName), utf8(a->Description), a->AdapterName}); break;
        }
    }
    return out;
}
RouteInfo authRoute() {
    SOCKADDR_INET d{}; d.Ipv4 = destination(); MIB_IPFORWARD_ROW2 row{}; SOCKADDR_INET source{};
    if (GetBestRoute2(nullptr, 0, nullptr, &d, 0, &row, &source) != NO_ERROR) return {};
    Block<4> next{}; std::memcpy(next.data(), &row.NextHop.Ipv4.sin_addr, 4);
    MIB_IF_ROW2 interfaceRow{}; interfaceRow.InterfaceIndex = row.InterfaceIndex;
    const auto name = GetIfEntry2(&interfaceRow) == NO_ERROR ? utf8(interfaceRow.Alias) : std::string{};
    return {row.InterfaceIndex, ipText(next), true, name};
}
UdpTransport::UdpTransport() { WSADATA data{}; winsock_ = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
UdpTransport::~UdpTransport() { close(); if (winsock_) WSACleanup(); }
void UdpTransport::close() { if (isOpen()) closesocket(socket_); socket_ = INVALID_SOCKET; }
bool UdpTransport::open(const Host& host) {
    close(); error_.clear();
    if (!winsock_) { error_ = "Winsock initialization failed"; return false; }
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!isOpen()) { error_ = "socket failed: " + std::to_string(WSAGetLastError()); return false; }
    const DWORD index = htonl(host.interfaceIndex);
    if (setsockopt(socket_, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index), sizeof(index)) != 0) {
        error_ = "adapter selection failed: " + std::to_string(WSAGetLastError()); close(); return false;
    }
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(61440); std::memcpy(&local.sin_addr, host.ip.data(), 4);
    // Exclusive binding prevents concurrent ownership with the official client.
    const BOOL exclusive = TRUE;
    if (setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0 ||
        bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        error_ = "UDP 61440 unavailable (another authentication client may own it): " + std::to_string(WSAGetLastError()); close(); return false;
    }
    u_long nonblocking = 1;
    if (ioctlsocket(socket_, FIONBIO, &nonblocking) != 0) { error_ = "nonblocking setup failed"; close(); return false; }
    return true;
}
bool UdpTransport::send(std::span<const Byte> p) {
    if (!isOpen()) return false;
    auto d = destination();
    const auto size = static_cast<int>(p.size());
    if (sendto(socket_, reinterpret_cast<const char*>(p.data()), size, 0, reinterpret_cast<sockaddr*>(&d), sizeof(d)) != size) {
        error_ = "UDP send failed: " + std::to_string(WSAGetLastError()); return false;
    }
    return true;
}
std::optional<Datagram> UdpTransport::receive() {
    if (!isOpen()) return {};
    Bytes p(2048); sockaddr_in source{}; int n = sizeof(source);
    const int received = recvfrom(socket_, reinterpret_cast<char*>(p.data()), static_cast<int>(p.size()), 0, reinterpret_cast<sockaddr*>(&source), &n);
    if (received == SOCKET_ERROR) {
        const int code = WSAGetLastError(); if (code != WSAEWOULDBLOCK) error_ = "UDP receive failed: " + std::to_string(code);
        return {};
    }
    p.resize(static_cast<std::size_t>(received)); const auto server = destination();
    return Datagram{std::move(p), source.sin_family == AF_INET && source.sin_addr.s_addr == server.sin_addr.s_addr && source.sin_port == server.sin_port};
}
}
