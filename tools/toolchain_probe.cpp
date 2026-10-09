// Offline build-environment probe. No authentication or network operations.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <array>
#include <iostream>
#include <span>
#include <stop_token>

static_assert(sizeof(SOCKET) == sizeof(void*));

int main() {
    std::array<unsigned char, 4> synthetic{192, 0, 2, 1};
    std::span<const unsigned char> bytes{synthetic};
    std::stop_source cancellation;
    cancellation.request_stop();
    std::cout << "C++20 / Windows SDK probe: OK\n"
              << "MSVC=" << _MSC_VER << ", SOCKET bytes=" << sizeof(SOCKET)
              << ", synthetic bytes=" << bytes.size()
              << ", IP_UNICAST_IF=" << IP_UNICAST_IF
              << ", resume event=" << PBT_APMRESUMEAUTOMATIC
              << ", cancellation=" << cancellation.stop_requested() << '\n';
    return 0;
}
