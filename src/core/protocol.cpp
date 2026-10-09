// Protocol research: drcoms/jlu-drcom-client (AGPL-3.0), especially Java protocol notes.
#include "protocol.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <limits>

namespace jlu {
namespace {
void put(Bytes& out, std::size_t offset, std::span<const Byte> value) {
    if (offset > out.size() || value.size() > out.size() - offset) throw ProtocolError("packet write out of bounds");
    std::copy(value.begin(), value.end(), out.begin() + static_cast<std::ptrdiff_t>(offset));
}
void text(Bytes& out, std::size_t offset, const std::string& value) {
    put(out, offset, {reinterpret_cast<const Byte*>(value.data()), value.size()});
}
template<std::size_t N> Block<N> slice(std::span<const Byte> p, std::size_t offset) {
    if (offset > p.size() || N > p.size() - offset) throw ProtocolError("truncated response");
    Block<N> result{};
    std::copy_n(p.begin() + static_cast<std::ptrdiff_t>(offset), N, result.begin());
    return result;
}
void minimum(std::span<const Byte> p, std::size_t size, Byte code) {
    if (p.size() < size || p[0] != code) throw ProtocolError("unexpected or truncated response");
}
Block<4> little(std::uint32_t value) {
    return {static_cast<Byte>(value), static_cast<Byte>(value >> 8), static_cast<Byte>(value >> 16), static_cast<Byte>(value >> 24)};
}
Bytes digestInput(Byte code, Block<4> salt, const std::string& password) {
    Bytes data{code, 1}; data.insert(data.end(), salt.begin(), salt.end());
    data.insert(data.end(), password.begin(), password.end()); return data;
}
}
Block<16> md5(std::span<const Byte> data) {
    if (data.size() > std::numeric_limits<ULONG>::max()) throw ProtocolError("digest input too large");
    Block<16> result{};
    const auto status = BCryptHash(BCRYPT_MD5_ALG_HANDLE, nullptr, 0,
        const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), result.data(), static_cast<ULONG>(result.size()));
    if (status < 0) throw ProtocolError("Windows digest operation failed");
    return result;
}
Block<4> checksum(std::span<const Byte> data) {
    std::uint32_t value = 1234;
    for (std::size_t i = 0; i < data.size(); i += 4) {
        std::uint32_t word = 0;
        for (std::size_t j = 0; j < 4 && i + j < data.size(); ++j) word |= std::uint32_t(data[i + j]) << (j * 8);
        value ^= word;
    }
    return little(value * 1968u);
}
Block<4> heartbeatCrc(std::span<const Byte> data) {
    if (data.size() % 2 != 0) throw ProtocolError("heartbeat CRC requires even byte count");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < data.size(); i += 2) value ^= std::uint32_t(data[i]) | (std::uint32_t(data[i + 1]) << 8);
    return little(value * 711u);
}
void validate(const Credentials& c, const Host& h) {
    if (c.username.empty() || c.username.size() > 36 || c.username.find('\0') != std::string::npos) throw ProtocolError("username must contain 1..36 bytes");
    // Reject unsupported input explicitly rather than truncate a secret or index past its digest.
    if (c.password.empty() || c.password.size() > 16 || c.password.find('\0') != std::string::npos) throw ProtocolError("this profile supports passwords of 1..16 bytes");
    if (h.hostname.empty() || h.hostname.size() > 32) throw ProtocolError("hostname must contain 1..32 bytes");
}
Bytes challenge(Byte counter, Block<2> nonce) {
    Bytes p(20); p[0] = 1; p[1] = counter; put(p, 2, nonce); p[4] = 0x6a; return p;
}
ChallengeReply parseChallenge(std::span<const Byte> p, Byte counter, Block<2> nonce) {
    minimum(p, 24, 2);
    if (p[1] != counter || slice<2>(p, 2) != nonce) throw ProtocolError("challenge echo mismatch");
    return {slice<4>(p, 4), slice<4>(p, 20)};
}
LoginPacket login(const Credentials& c, const Host& h, const Profile& profile, Block<4> salt, Block<2> nonce) {
    validate(c, h);
    if (profile.fingerprint.size() > 40) throw ProtocolError("fingerprint exceeds profile field");
    const auto n = c.password.size();
    Bytes p(330 + ((n + 3) / 4) * 4);
    p[0] = 3; p[1] = 1; p[3] = static_cast<Byte>(c.username.size() + 20);
    const auto a = md5(digestInput(3, salt, c.password)); put(p, 4, a); text(p, 20, c.username);
    p[56] = 0x20; p[57] = 5;
    for (std::size_t i = 0; i < h.mac.size(); ++i) p[58 + i] = a[i] ^ h.mac[i];
    Bytes b{1}; b.insert(b.end(), c.password.begin(), c.password.end()); b.insert(b.end(), salt.begin(), salt.end()); b.resize(b.size() + 4);
    put(p, 64, md5(b)); p[80] = 1; put(p, 81, h.ip);
    Bytes md5c(p.begin(), p.begin() + 97); md5c.insert(md5c.end(), {0x14, 0, 7, 0x0b});
    const auto digestC = md5(md5c); put(p, 97, std::span(digestC).first(8));
    p[105] = 1; text(p, 110, h.hostname); put(p, 142, profile.dns);
    p[162] = 0x94; p[166] = 6; p[170] = 2; p[174] = 0xf0; p[175] = 0x23; p[178] = 2;
    const Block<9> signature{0x44, 0x72, 0x43, 0x4f, 0x4d, 0, 0xcf, 7, profile.authVersion[0]};
    put(p, 182, signature); text(p, 246, profile.fingerprint); put(p, 310, profile.authVersion); p[313] = static_cast<Byte>(n);
    for (std::size_t i = 0; i < n; ++i) { const auto x = static_cast<Byte>(a[i] ^ static_cast<Byte>(c.password[i])); p[314 + i] = static_cast<Byte>((x << 3) | (x >> 5)); }
    p[314 + n] = 2; p[315 + n] = 0x0c;
    Bytes sum(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(316 + n));
    sum.insert(sum.end(), {1, 0x26, 7, 0x11, 0, 0}); sum.insert(sum.end(), h.mac.begin(), h.mac.end());
    put(p, 316 + n, checksum(sum)); put(p, 322 + n, h.mac); put(p, p.size() - 2, nonce);
    return {std::move(p), a};
}
Block<16> parseLogin(std::span<const Byte> p) { minimum(p, 39, 4); return slice<16>(p, 23); }
Bytes keep38(Block<16> digest, Block<16> tail, Block<2> nonce) {
    Bytes p(38); p[0] = 0xff; put(p, 1, digest); put(p, 20, tail); put(p, 36, nonce); return p;
}
Block<2> parseKeep38(std::span<const Byte> p, Block<2> nonce) {
    minimum(p, 30, 7);
    if (slice<2>(p, 6) != nonce) throw ProtocolError("keep38 echo mismatch");
    return slice<2>(p, 28);
}
Bytes keep40(Byte seq, Byte type, bool extra, Block<2> version, Block<2> nonce, Block<4> tail, Block<4> ip) {
    if (type != 1 && type != 3) throw ProtocolError("invalid keep40 type");
    Bytes p(40); p[0] = 7; p[1] = seq; p[2] = 40; p[4] = 0x0b; p[5] = type;
    put(p, 6, extra ? Block<2>{0x0f, 0x27} : version); put(p, 8, nonce); put(p, 16, tail);
    if (type == 3) { put(p, 24, ip); put(p, 24, heartbeatCrc(std::span(p).first(28))); put(p, 28, ip); }
    return p;
}
Block<4> parseKeep40(std::span<const Byte> p, Byte seq, Byte type, Block<2> nonce) {
    minimum(p, type == 6 ? 16 : 40, 7);
    if (p[1] != seq || p[4] != 0x0b || p[5] != type || slice<2>(p, 8) != nonce) throw ProtocolError("keep40 sequence, type or echo mismatch");
    const auto length = std::size_t(p[2]) | (std::size_t(p[3]) << 8);
    if (length < (type == 6 ? 16u : 40u) || length != p.size()) throw ProtocolError("keep40 declared length mismatch");
    return type == 6 ? Block<4>{} : slice<4>(p, 16);
}
Bytes logout(const Credentials& c, const Host& h, Block<4> salt, Block<16> tail) {
    validate(c, h); Bytes p(80); p[0] = 6; p[1] = 1; p[3] = static_cast<Byte>(c.username.size() + 20);
    const auto a = md5(digestInput(6, salt, c.password)); put(p, 4, a); text(p, 20, c.username); p[56] = 0x20; p[57] = 5;
    for (std::size_t i = 0; i < h.mac.size(); ++i) p[58 + i] = a[i] ^ h.mac[i];
    put(p, 64, tail); return p;
}
void parseLogout(std::span<const Byte> p) { minimum(p, 25, 4); }
}
