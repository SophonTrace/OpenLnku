#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace jlu {
using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;
template<std::size_t N> using Block = std::array<Byte, N>;
struct Credentials { std::string username; std::string password; };
struct Host {
    Block<4> ip{};
    Block<6> mac{};
    std::string hostname = "JluConnect";
    std::uint32_t interfaceIndex = 0;
    bool operator==(const Host&) const = default;
};
// A named historical profile, pending verification against today's campus server.
struct Profile {
    Block<2> authVersion{0x6a, 0};
    Block<2> keepVersion{0xdc, 2};
    Block<4> dns{10, 10, 10, 10};
    std::string fingerprint = "1c210c99585fd22ad03d35c956911aeec1eb449b";
    std::uint64_t timeoutMs = 3000;
    std::uint64_t heartbeatMs = 20000;
};
class ProtocolError : public std::runtime_error { using std::runtime_error::runtime_error; };
Block<16> md5(std::span<const Byte> data);
Block<4> checksum(std::span<const Byte> data);
Block<4> heartbeatCrc(std::span<const Byte> data);
void validate(const Credentials& credentials, const Host& host);
Bytes challenge(Byte counter, Block<2> nonce);
struct ChallengeReply { Block<4> salt; Block<4> ip; };
ChallengeReply parseChallenge(std::span<const Byte> packet, Byte counter, Block<2> nonce);
struct LoginPacket { Bytes bytes; Block<16> digest; };
LoginPacket login(const Credentials&, const Host&, const Profile&, Block<4> salt, Block<2> nonce);
Block<16> parseLogin(std::span<const Byte> packet);
Bytes keep38(Block<16> digest, Block<16> tail, Block<2> nonce);
Block<2> parseKeep38(std::span<const Byte> packet, Block<2> nonce);
Bytes keep40(Byte sequence, Byte type, bool extra, Block<2> version, Block<2> nonce,
             Block<4> tail, Block<4> ip);
Block<4> parseKeep40(std::span<const Byte>, Byte sequence, Byte expectedType, Block<2> nonce);
Bytes logout(const Credentials&, const Host&, Block<4> salt, Block<16> tail);
void parseLogout(std::span<const Byte>);
}
