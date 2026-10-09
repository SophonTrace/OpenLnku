#include "session.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>

namespace jlu {
Session::Session(Profile profile) : profile_(std::move(profile)), version_(profile_.keepVersion) {}
Block<2> Session::nonce() {
    Block<2> result{};
    if (BCryptGenRandom(nullptr, result.data(), 2, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw ProtocolError("random source unavailable");
    return result;
}
void Session::clearSession() {
    outgoing_.reset(); authenticated_ = false; digest_.fill(0); tail1_.fill(0); tail2_.fill(0); salt_.fill(0);
    sequence_ = 0; heartbeatCount_ = 0; version_ = profile_.keepVersion; ++generation_;
}
void Session::start(Credentials c, std::uint64_t now) {
    validate(c, Host{}); forgetCredentials(); credentials_ = std::move(c); desired_ = true; failures_ = 0;
    clearSession(); state_ = State::WaitingNetwork; retryAt_ = now; message_ = "waiting for adapter";
}
void Session::stop(std::uint64_t now) {
    desired_ = false;
    if (authenticated_ && host_ && state_ != State::Suspended) { outgoing_.reset(); beginChallenge(now, true); }
    else { clearSession(); forgetCredentials(); state_ = State::Paused; message_ = "automatic reconnect paused"; }
}
void Session::suspend() { clearSession(); host_.reset(); state_ = desired_ ? State::Suspended : State::Paused; message_ = "suspended"; }
void Session::resume(std::uint64_t now) {
    if (!autoReconnect_) { desired_ = false; forgetCredentials(); }
    clearSession(); host_.reset(); state_ = desired_ ? State::WaitingNetwork : State::Paused;
    retryAt_ = now + 2500; message_ = desired_ ? "waiting for network after resume" : "automatic reconnect paused";
}
void Session::network(std::optional<Host> h, std::uint64_t now) {
    if (state_ == State::Suspended) return;
    if (h != host_) {
        if (host_ && desired_ && !autoReconnect_) { desired_ = false; forgetCredentials(); }
        host_ = std::move(h); clearSession();
        if (desired_) { state_ = State::WaitingNetwork; retryAt_ = std::max(retryAt_, now + 1000); message_ = "adapter changed; rebuilding session"; }
        else if (state_ != State::Idle && state_ != State::Error) { state_ = State::Paused; message_ = "adapter changed; automatic reconnect paused"; }
    }
}
void Session::send(State state, Bytes p, std::uint64_t now) { state_ = state; outgoing_ = std::move(p); deadline_ = now + profile_.timeoutMs; }
void Session::beginChallenge(std::uint64_t now, bool loggingOut) {
    pendingNonce_ = nonce(); ++challengeCounter_;
    send(loggingOut ? State::LogoutChallenge : State::Challenge, challenge(challengeCounter_, pendingNonce_), now);
    message_ = loggingOut ? "requesting logout" : "requesting challenge";
}
void Session::sendKeep40(State state, std::uint64_t now) {
    pendingSequence_ = sequence_++; pendingNonce_ = nonce();
    send(state, keep40(pendingSequence_, state == State::Keep2 ? 3 : 1, state == State::Extra,
        version_, pendingNonce_, tail2_, advertisedIp_), now);
}
void Session::fail(std::uint64_t now, std::string message) {
    clearSession(); message_ = std::move(message);
    if (!desired_) { forgetCredentials(); state_ = State::Paused; return; }
    if (!autoReconnect_) { desired_ = false; forgetCredentials(); state_ = State::Error; return; }
    state_ = State::Backoff;
    retryAt_ = now + std::min<std::uint64_t>(30000, 1000ull << std::min(failures_++, 5u));
}
void Session::transportFailed(std::uint64_t now) { fail(now, "transport unavailable; retrying with a new session"); }
void Session::setAutoReconnect(bool enabled) {
    autoReconnect_ = enabled;
    if (!enabled && state_ == State::Backoff) { desired_ = false; clearSession(); forgetCredentials(); state_ = State::Paused; message_ = "automatic reconnect paused"; }
}
void Session::forgetCredentials() {
    if (!credentials_.username.empty()) SecureZeroMemory(credentials_.username.data(), credentials_.username.size());
    if (!credentials_.password.empty()) SecureZeroMemory(credentials_.password.data(), credentials_.password.size());
    credentials_.username.clear(); credentials_.password.clear();
}
void Session::tick(std::uint64_t now) {
    if ((state_ == State::WaitingNetwork || state_ == State::Backoff) && desired_ && host_ && now >= retryAt_) {
        clearSession(); beginChallenge(now); return;
    }
    if (state_ == State::Online && now >= heartbeatAt_) {
        pendingNonce_ = nonce(); send(State::Keep38, keep38(digest_, tail1_, pendingNonce_), now); return;
    }
    if (state_ == State::Challenge || state_ == State::Login || state_ == State::Keep38 || state_ == State::Extra ||
        state_ == State::Keep1 || state_ == State::Keep2 || state_ == State::LogoutChallenge || state_ == State::Logout) {
        if (now >= deadline_) fail(now, desired_ ? "response timeout; starting a fresh session" : "logout timed out; automatic reconnect paused");
    }
}
void Session::receive(std::span<const Byte> p, std::uint64_t now) {
    try {
        switch (state_) {
        case State::Challenge:
        case State::LogoutChallenge: {
            const bool loggingOut = state_ == State::LogoutChallenge;
            const auto reply = parseChallenge(p, challengeCounter_, pendingNonce_); salt_ = reply.salt; advertisedIp_ = reply.ip;
            // Do not replace the physical bind address with an unverified server-supplied IP.
            Host packetHost = *host_; packetHost.ip = reply.ip;
            if (loggingOut) send(State::Logout, logout(credentials_, packetHost, salt_, tail1_), now);
            else { const auto request = login(credentials_, packetHost, profile_, salt_, nonce()); digest_ = request.digest; send(State::Login, request.bytes, now); }
            break;
        }
        case State::Login:
            if (p.size() >= 5 && p[0] == 5) {
                const auto code = p[4];
                if (code == 3 || code == 0x0b) { clearSession(); desired_ = false; forgetCredentials(); state_ = State::Error; message_ = code == 3 ? "account or password rejected" : "MAC rejected; check selected adapter"; }
                else fail(now, "server rejected login, code " + std::to_string(code) + "; retrying");
                break;
            }
            tail1_ = parseLogin(p); authenticated_ = true;
            pendingNonce_ = nonce(); send(State::Keep38, keep38(digest_, tail1_, pendingNonce_), now); break;
        case State::Keep38:
            version_ = parseKeep38(p, pendingNonce_);
            sendKeep40(heartbeatCount_ % 10 == 0 ? State::Extra : State::Keep1, now); break;
        case State::Extra:
            (void)parseKeep40(p, pendingSequence_, 6, pendingNonce_); sendKeep40(State::Keep1, now); break;
        case State::Keep1:
            tail2_ = parseKeep40(p, pendingSequence_, 2, pendingNonce_); sendKeep40(State::Keep2, now); break;
        case State::Keep2:
            tail2_ = parseKeep40(p, pendingSequence_, 4, pendingNonce_);
            state_ = State::Online; ++heartbeatCount_; failures_ = 0; heartbeatAt_ = now + profile_.heartbeatMs;
            message_ = "authenticated; heartbeat confirmed"; break;
        case State::Logout:
            parseLogout(p); clearSession(); forgetCredentials(); state_ = State::Paused; message_ = "logout confirmed; automatic reconnect paused"; break;
        default: break;
        }
    } catch (const ProtocolError& error) {
        // Ignore malformed/late packets without extending the original response deadline.
        message_ = std::string("discarded response: ") + error.what();
    }
}
std::optional<Bytes> Session::takePacket() { auto result = std::move(outgoing_); outgoing_.reset(); return result; }
}
