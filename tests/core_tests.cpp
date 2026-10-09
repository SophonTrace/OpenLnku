// Offline-only tests for the protocol helpers and deterministic session core.
// All replies are constructed in memory from the nonce/sequence in each request.
#include "core/protocol.h"
#include "core/session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using jlu::Block;
using jlu::Byte;
using jlu::Bytes;
using jlu::Credentials;
using jlu::Host;
using jlu::Profile;
using jlu::Session;
using jlu::State;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

template<class Function>
void checkProtocolError(Function&& function, std::string_view message) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const jlu::ProtocolError&) {
        threw = true;
    }
    check(threw, message);
}

Credentials credentials() {
    return {"synthetic-user", "synthetic-pass"};
}

Host syntheticHost() {
    Host host;
    host.ip = Block<4>{192, 0, 2, 25};
    host.mac = Block<6>{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    host.hostname = "OfflineTest";
    host.interfaceIndex = 7;
    return host;
}

Profile testProfile() {
    Profile profile;
    profile.timeoutMs = 1000;
    profile.heartbeatMs = 10;
    return profile;
}

Bytes takePacket(Session& session) {
    auto packet = session.takePacket();
    check(packet.has_value(), "expected an outgoing packet");
    return std::move(*packet);
}

Bytes makeChallengeReply(const Bytes& request) {
    check(request.size() == 20 && request[0] == 1, "challenge request shape");
    Bytes reply(24);
    reply[0] = 2;
    reply[1] = request[1];
    std::copy_n(request.begin() + 2, 2, reply.begin() + 2);
    const Block<4> salt{0x10, 0x20, 0x30, 0x40};
    const Block<4> advertisedIp{198, 51, 100, 7};
    std::copy(salt.begin(), salt.end(), reply.begin() + 4);
    std::copy(advertisedIp.begin(), advertisedIp.end(), reply.begin() + 20);
    return reply;
}

Bytes makeLoginReply() {
    Bytes reply(39);
    reply[0] = 4;
    const Block<16> tail{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                         0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    std::copy(tail.begin(), tail.end(), reply.begin() + 23);
    return reply;
}

Bytes makeKeep38Reply(const Bytes& request) {
    check(request.size() == 38 && request[0] == 0xff, "keep38 request shape");
    Bytes reply(30);
    reply[0] = 7;
    std::copy_n(request.begin() + 36, 2, reply.begin() + 6);
    reply[28] = 0xdc;
    reply[29] = 0x02;
    return reply;
}

Bytes makeKeep40Reply(const Bytes& request, Byte responseType) {
    check(request.size() == 40 && request[0] == 7, "keep40 request shape");
    const std::size_t responseSize = responseType == 6 ? 16u : 40u;
    Bytes reply(responseSize);
    reply[0] = 7;
    reply[1] = request[1];
    reply[2] = static_cast<Byte>(responseSize);
    reply[4] = 0x0b;
    reply[5] = responseType;
    std::copy_n(request.begin() + 8, 2, reply.begin() + 8);
    if (responseSize >= 20) {
        const Block<4> tail{0xa1, 0xb2, 0xc3, 0xd4};
        std::copy(tail.begin(), tail.end(), reply.begin() + 16);
    }
    return reply;
}

void startAtChallenge(Session& session, std::uint64_t startTime = 0) {
    session.start(credentials(), startTime);
    session.network(syntheticHost(), startTime);
    check(session.state() == State::WaitingNetwork, "start waits for adapter settle");
    session.tick(startTime + 1000);
    check(session.state() == State::Challenge, "adapter settle begins challenge");
}

void acceptChallenge(Session& session, std::uint64_t now) {
    const auto request = takePacket(session);
    check(request[0] == 1, "outgoing challenge opcode");
    const auto reply = makeChallengeReply(request);
    session.receive(reply, now);
    check(session.state() == State::Login, "challenge advances to login");
}

void initialHandshakeToOnline(Session& session, std::uint64_t& now) {
    startAtChallenge(session);
    acceptChallenge(session, ++now);

    const auto loginRequest = takePacket(session);
    check(loginRequest[0] == 3, "outgoing login opcode");
    const auto loginReply = makeLoginReply();
    session.receive(loginReply, ++now);
    check(session.state() == State::Keep38, "login success starts keep38");
    check(session.heartbeatCount() == 0, "login alone does not count as online");

    const auto keep38Request = takePacket(session);
    const auto keep38Reply = makeKeep38Reply(keep38Request);
    session.receive(keep38Reply, ++now);
    check(session.state() == State::Extra, "first heartbeat includes extra exchange");
    check(session.heartbeatCount() == 0, "keep38 alone does not count as online");

    const auto extraRequest = takePacket(session);
    const auto extraReply = makeKeep40Reply(extraRequest, 6);
    session.receive(extraReply, ++now);
    check(session.state() == State::Keep1, "extra exchange advances to keep1");
    check(session.heartbeatCount() == 0, "extra exchange alone does not count as online");

    const auto keep1Request = takePacket(session);
    const auto keep1Reply = makeKeep40Reply(keep1Request, 2);
    session.receive(keep1Reply, ++now);
    check(session.state() == State::Keep2, "keep1 advances to keep2");
    check(session.heartbeatCount() == 0, "keep1 alone does not count as online");

    const auto keep2Request = takePacket(session);
    const auto keep2Reply = makeKeep40Reply(keep2Request, 4);
    session.receive(keep2Reply, ++now);
    check(session.state() == State::Online, "complete first heartbeat reaches online");
    check(session.heartbeatCount() == 1, "first complete heartbeat increments count");
}

void protocolVectors() {
    const Bytes empty;
    const Bytes abc{'a', 'b', 'c'};
    check(jlu::md5(empty) == Block<16>{0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
                                      0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e},
          "MD5 empty standard vector");
    check(jlu::md5(abc) == Block<16>{0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
                                    0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72},
          "MD5 abc standard vector");

    // Expected checksums were independently calculated using Python
    // int.from_bytes(..., "little") with explicit four-byte zero padding.
    const Bytes checksumInput{0x01, 0x02, 0x0a, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    check(jlu::checksum(checksumInput) == Block<4>{0x50, 0xb2, 0xf6, 0xa3},
          "checksum vector including LF and partial final word");
    check(jlu::checksum(empty) == Block<4>{0x60, 0x0e, 0x25, 0x00}, "checksum empty vector");

    // Expected CRC was independently calculated by XOR-folding little-endian
    // 16-bit words (0x0201 ^ 0x0403 ^ 0x0605 = 7), then 7 * 711.
    const Bytes crcInput{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    check(jlu::heartbeatCrc(crcInput) == Block<4>{0x71, 0x13, 0x00, 0x00},
          "heartbeat CRC vector");
    checkProtocolError([] { (void)jlu::heartbeatCrc(Bytes{0x01}); },
                       "odd-byte heartbeat CRC is rejected");
}

void credentialBoundariesAndLoginLayout() {
    const Host host = syntheticHost();
    checkProtocolError([&] { jlu::validate({std::string(37, 'u'), "p"}, host); },
                       "37-byte username is rejected");
    checkProtocolError([&] { jlu::validate({"u", std::string(17, 'p')}, host); },
                       "17-byte password is rejected");
    Host longHostname = host;
    longHostname.hostname = std::string(33, 'h');
    checkProtocolError([&] { jlu::validate({"u", "p"}, longHostname); },
                       "33-byte hostname is rejected");

    const std::array<std::pair<std::size_t, std::size_t>, 5> cases{{
        {1, 334}, {5, 338}, {9, 342}, {13, 346}, {16, 346}
    }};
    const Block<4> salt{0x10, 0x20, 0x30, 0x40};
    const Block<2> nonce{0x5a, 0xa5};
    for (const auto& [passwordLength, expectedPacketLength] : cases) {
        const Credentials c{"synthetic-user", std::string(passwordLength, 'p')};
        const auto packet = jlu::login(c, host, testProfile(), salt, nonce);
        check(packet.bytes.size() == expectedPacketLength, "login packet length follows password padding");
        const auto macOffset = 322u + passwordLength;
        check(macOffset + host.mac.size() <= packet.bytes.size(), "six-byte MAC fits login packet");
        check(std::equal(host.mac.begin(), host.mac.end(),
                         packet.bytes.begin() + static_cast<std::ptrdiff_t>(macOffset)),
              "login packet contains the complete MAC at the expected offset");
        check(packet.bytes[packet.bytes.size() - 2] == nonce[0] &&
              packet.bytes[packet.bytes.size() - 1] == nonce[1],
              "login packet ends with its synthetic nonce");
    }
}

void handshakeAndSequenceWrap() {
    Session session(testProfile());
    std::uint64_t now = 1000;
    initialHandshakeToOnline(session, now);
    check(!session.takePacket().has_value(), "online transition leaves no queued packet");

    bool saw255 = false;
    bool sawWrapToZero = false;
    for (unsigned cycle = 0; cycle < 127; ++cycle) {
        now += 20;
        session.tick(now);
        check(session.state() == State::Keep38, "heartbeat timer starts keep38");
        const auto keep38Request = takePacket(session);
        session.receive(makeKeep38Reply(keep38Request), ++now);

        if (session.state() == State::Extra) {
            const auto extraRequest = takePacket(session);
            session.receive(makeKeep40Reply(extraRequest, 6), ++now);
            check(session.state() == State::Keep1, "periodic extra exchange advances to keep1");
        }

        check(session.state() == State::Keep1, "keep38 response starts keep1");
        const auto keep1Request = takePacket(session);
        const Byte keep1Sequence = keep1Request[1];
        session.receive(makeKeep40Reply(keep1Request, 2), ++now);

        check(session.state() == State::Keep2, "keep1 response starts keep2");
        const auto keep2Request = takePacket(session);
        const Byte keep2Sequence = keep2Request[1];
        session.receive(makeKeep40Reply(keep2Request, 4), ++now);

        check(session.state() == State::Online, "valid keep2 response returns online");
        if (keep1Sequence == 0xff) {
            saw255 = true;
            check(keep2Sequence == 0, "sequence wraps from 255 to 0");
            sawWrapToZero = true;
        }
    }
    check(saw255 && sawWrapToZero, "heartbeat sequence crosses 255 and wraps");
}

void verifiedSessionRemainsOnlineDuringHeartbeat() {
    Session session(testProfile());
    std::uint64_t now = 1000;
    initialHandshakeToOnline(session, now);
    check(session.verified(), "first complete heartbeat verifies the session");

    session.tick(now + 9);
    check(session.state() == State::Online && session.verified(),
          "session stays online before the next heartbeat deadline");
    const auto heartbeatAt = now + 10;
    session.tick(heartbeatAt);
    check(session.state() == State::Keep38 && session.verified(),
          "verified state remains true while keep38 is pending");
    const auto keep38Request = takePacket(session);
    session.receive(makeKeep38Reply(keep38Request), heartbeatAt + 1);
    check(session.state() == State::Keep1 && session.verified(),
          "verified state remains true while keep1 is pending");

    const auto keep1Request = takePacket(session);
    session.receive(makeKeep40Reply(keep1Request, 2), heartbeatAt + 2);
    check(session.state() == State::Keep2 && session.verified(),
          "verified state remains true while keep2 is pending");
    const auto keep2Request = takePacket(session);
    session.receive(makeKeep40Reply(keep2Request, 4), heartbeatAt + 3);
    check(session.state() == State::Online && session.verified(),
          "completed periodic heartbeat returns to verified online");
    check(session.heartbeatCount() == 2, "periodic heartbeat increments the count");
}

void malformedResponsesDoNotAdvanceOrExtendDeadline() {
    {
        Session session(testProfile());
        startAtChallenge(session);
        const auto request = takePacket(session);
        const Bytes shortReply{2};
        session.receive(shortReply, 1500);
        check(session.state() == State::Challenge, "short challenge reply is discarded");
        check(!session.takePacket().has_value(), "short reply does not queue another packet");

        auto wrongEcho = makeChallengeReply(request);
        wrongEcho[2] ^= 1;
        session.receive(wrongEcho, 1999);
        check(session.state() == State::Challenge, "challenge nonce mismatch is discarded");
        session.tick(2000);
        check(session.state() == State::Backoff, "malformed replies do not extend challenge deadline");
        check(session.retryAt() == 3000, "challenge timeout enters expected backoff");
    }

    {
        Session session(testProfile());
        startAtChallenge(session);
        acceptChallenge(session, 1001);
        (void)takePacket(session);
        const Bytes shortReply{4};
        session.receive(shortReply, 2000);
        check(session.state() == State::Login, "short login reply is discarded");
        session.tick(2001);
        check(session.state() == State::Backoff, "short login reply does not extend deadline");
    }

    {
        Session session(testProfile());
        startAtChallenge(session);
        acceptChallenge(session, 1001);
        (void)takePacket(session);
        session.receive(makeLoginReply(), 1002);
        const auto keep38Request = takePacket(session);
        session.receive(makeKeep38Reply(keep38Request), 1003);
        const auto extraRequest = takePacket(session);
        session.receive(makeKeep40Reply(extraRequest, 6), 1004);
        check(session.state() == State::Keep1, "test reaches keep1 state");

        const auto keep1Request = takePacket(session);
        auto wrongSequence = makeKeep40Reply(keep1Request, 2);
        wrongSequence[1] ^= 1;
        session.receive(wrongSequence, 2003);
        check(session.state() == State::Keep1, "keep40 sequence mismatch is discarded");
        check(!session.takePacket().has_value(), "sequence mismatch does not queue another packet");
        session.tick(2004);
        check(session.state() == State::Backoff, "sequence mismatch does not extend keep1 deadline");
    }
}

void timeoutStartsANewGeneration() {
    Session session(testProfile());
    startAtChallenge(session);
    const auto firstChallenge = takePacket(session);
    const auto generationAtChallenge = session.generation();
    session.tick(2000);
    check(session.state() == State::Backoff, "challenge timeout enters backoff");
    check(session.generation() == generationAtChallenge + 1,
          "timeout invalidates the current transport generation");

    session.tick(session.retryAt());
    check(session.state() == State::Challenge, "backoff starts a fresh challenge");
    const auto secondChallenge = takePacket(session);
    check(session.generation() == generationAtChallenge + 2,
          "retry starts a new transport generation");
    check(secondChallenge[1] != firstChallenge[1], "retry advances challenge counter");
}

void rejectedPasswordStopsAutomaticRetry() {
    Session session(testProfile());
    startAtChallenge(session);
    acceptChallenge(session, 1001);
    (void)takePacket(session);
    const Bytes rejected{5, 0, 0, 0, 3};
    session.receive(rejected, 1002);
    check(session.state() == State::Error, "password rejection enters error state");
    check(!session.wantsConnection(), "password rejection disables automatic reconnect");
    session.tick(100000);
    check(session.state() == State::Error, "password rejection stays paused without retry");
    check(!session.takePacket().has_value(), "password rejection emits no retry packet");
}

void disabledAutoReconnectStopsFailureAndWakeRetriesButAllowsManualStart() {
    {
        Session session(testProfile());
        session.setAutoReconnect(false);
        startAtChallenge(session);
        (void)takePacket(session);
        session.transportFailed(1001);
        check(session.state() == State::Error, "transport failure pauses when reconnect is disabled");
        check(!session.wantsConnection(), "failed session no longer requests automatic reconnect");
        session.tick(100000);
        check(session.state() == State::Error, "disabled reconnect does not retry after a failure");
        check(!session.takePacket().has_value(), "disabled reconnect emits no retry packet");

        session.start(credentials(), 100001);
        session.tick(100001);
        check(session.state() == State::Challenge, "manual start works after a disabled-reconnect failure");
        check(takePacket(session)[0] == 1, "manual restart begins with a new challenge");
    }

    {
        Session session(testProfile());
        session.setAutoReconnect(false);
        session.start(credentials(), 0);
        session.network(syntheticHost(), 0);
        session.tick(1000);
        check(session.state() == State::Challenge, "wake test begins with an active session");
        (void)takePacket(session);

        session.suspend();
        session.resume(5000);
        check(session.state() == State::Paused && !session.wantsConnection(),
              "wake pauses when automatic reconnect is disabled");
        session.network(syntheticHost(), 5001);
        session.tick(100000);
        check(session.state() == State::Paused, "network events do not restart a disabled session");
        check(!session.takePacket().has_value(), "wake does not emit a reconnect packet");

        session.start(credentials(), 100001);
        session.tick(100001);
        check(session.state() == State::Challenge, "manual start works after a disabled-reconnect wake");
        check(takePacket(session)[0] == 1, "manual wake restart begins with a new challenge");
    }
}

void manualPauseSurvivesSuspendAndResume() {
    Session session(testProfile());
    session.start(credentials(), 0);
    session.stop(1);
    check(session.state() == State::Paused && !session.wantsConnection(),
          "manual stop pauses automatic reconnect");

    session.suspend();
    check(session.state() == State::Paused, "suspend preserves a manual pause");
    session.resume(5000);
    check(session.state() == State::Paused && !session.wantsConnection(),
          "resume preserves a manual pause");
    session.network(syntheticHost(), 5001);
    session.tick(100000);
    check(session.state() == State::Paused, "network changes do not undo manual pause");
    check(!session.takePacket().has_value(), "paused session emits no packet");
}

void resumeWaitsForANewNetworkEvent() {
    Session session(testProfile());
    session.start(credentials(), 0);
    session.network(syntheticHost(), 0);
    session.tick(1000);
    check(session.state() == State::Challenge, "initial adapter starts challenge");
    (void)takePacket(session);

    session.suspend();
    check(session.state() == State::Suspended, "desired session records suspend");
    session.resume(5000);
    check(session.state() == State::WaitingNetwork, "resume waits for network");
    check(!session.host().has_value(), "resume clears stale adapter snapshot");
    session.tick(6000);
    check(session.state() == State::WaitingNetwork, "elapsed time alone cannot start login");
    check(!session.takePacket().has_value(), "no challenge before a fresh network event");

    session.network(syntheticHost(), 6500);
    session.tick(7499);
    check(session.state() == State::WaitingNetwork, "adapter settle delay is honored");
    session.tick(7500);
    check(session.state() == State::Challenge, "fresh network event permits a new challenge");
    check(takePacket(session)[0] == 1, "fresh session starts with challenge packet");
}

void authenticatedManualStopUsesLogoutAndStaysPaused() {
    {
        Session session(testProfile());
        std::uint64_t now = 1000;
        initialHandshakeToOnline(session, now);
        session.stop(++now);
        check(session.state() == State::LogoutChallenge, "online manual stop starts logout challenge");
        check(!session.wantsConnection(), "online manual stop disables automatic reconnect");

        const auto request = takePacket(session);
        check(request[0] == 1, "logout flow begins with challenge packet");
        session.receive(makeChallengeReply(request), ++now);
        check(session.state() == State::Logout, "logout challenge creates logout packet");
        const auto logoutRequest = takePacket(session);
        check(logoutRequest.size() == 80 && logoutRequest[0] == 6,
              "manual stop emits the 80-byte logout packet");

        Bytes reply(25);
        reply[0] = 4;
        session.receive(reply, ++now);
        check(session.state() == State::Paused, "valid logout reply enters paused state");
        check(!session.wantsConnection(), "confirmed logout stays paused");
    }

    {
        Session session(testProfile());
        std::uint64_t now = 1000;
        initialHandshakeToOnline(session, now);
        session.stop(++now);
        const auto challengeRequest = takePacket(session);
        session.receive(makeChallengeReply(challengeRequest), ++now);
        const auto logoutSentAt = now;
        const auto logoutRequest = takePacket(session);
        check(logoutRequest.size() == 80 && logoutRequest[0] == 6,
              "short-logout test reaches logout request");

        const Bytes shortReply{4};
        session.receive(shortReply, ++now);
        check(session.state() == State::Logout, "short logout reply is rejected");
        check(!session.takePacket().has_value(), "short logout reply does not advance the flow");
        session.tick(logoutSentAt + 999);
        check(session.state() == State::Logout, "logout waits until its original deadline");
        session.tick(logoutSentAt + 1000);
        check(session.state() == State::Paused, "logout timeout leaves manual session paused");
        check(!session.wantsConnection(), "logout timeout does not resume automatic reconnect");
    }
}
}

int main() {
    try {
        protocolVectors();
        credentialBoundariesAndLoginLayout();
        handshakeAndSequenceWrap();
        verifiedSessionRemainsOnlineDuringHeartbeat();
        malformedResponsesDoNotAdvanceOrExtendDeadline();
        timeoutStartsANewGeneration();
        rejectedPasswordStopsAutomaticRetry();
        disabledAutoReconnectStopsFailureAndWakeRetriesButAllowsManualStart();
        manualPauseSurvivesSuspendAndResume();
        resumeWaitsForANewNetworkEvent();
        authenticatedManualStopUsesLogoutAndStaysPaused();
        std::cout << "All offline core assertions passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Core test failed: " << error.what() << '\n';
        return 1;
    }
}
