#pragma once
#include "protocol.h"
#include <optional>

namespace jlu {
enum class State { Idle, WaitingNetwork, Challenge, Login, Keep38, Extra, Keep1, Keep2,
                   Online, Backoff, Paused, Suspended, LogoutChallenge, Logout, Error };
// Single-threaded deterministic state machine. Time is injected in monotonic milliseconds.
// Transport must reject foreign sources and close/reopen whenever generation changes.
class Session {
public:
    explicit Session(Profile profile = {});
    ~Session() { forgetCredentials(); }
    void start(Credentials credentials, std::uint64_t now);
    void stop(std::uint64_t now);
    void suspend();
    void resume(std::uint64_t now);
    void network(std::optional<Host> host, std::uint64_t now);
    void tick(std::uint64_t now);
    void receive(std::span<const Byte> packet, std::uint64_t now);
    void transportFailed(std::uint64_t now);
    void setAutoReconnect(bool enabled);
    void forgetCredentials();
    bool verified() const { return authenticated_ && heartbeatCount_ > 0; }
    std::optional<Bytes> takePacket();
    State state() const { return state_; }
    std::uint64_t generation() const { return generation_; }
    bool wantsConnection() const { return desired_; }
    const std::string& message() const { return message_; }
    std::uint64_t heartbeatCount() const { return heartbeatCount_; }
    std::uint64_t retryAt() const { return retryAt_; }
    const std::optional<Host>& host() const { return host_; }
private:
    void clearSession();
    void send(State state, Bytes packet, std::uint64_t now);
    void beginChallenge(std::uint64_t now, bool loggingOut = false);
    void sendKeep40(State state, std::uint64_t now);
    void fail(std::uint64_t now, std::string message);
    Block<2> nonce();
    Profile profile_;
    Credentials credentials_;
    State state_ = State::Idle;
    std::optional<Host> host_;
    std::optional<Bytes> outgoing_;
    std::string message_ = "ready";
    bool desired_ = false;
    bool authenticated_ = false;
    bool autoReconnect_ = true;
    std::uint64_t generation_ = 0, deadline_ = 0, retryAt_ = 0, heartbeatAt_ = 0, heartbeatCount_ = 0;
    unsigned failures_ = 0;
    Byte challengeCounter_ = 1, sequence_ = 0, pendingSequence_ = 0;
    Block<2> pendingNonce_{}, version_{};
    Block<4> salt_{}, tail2_{}, advertisedIp_{};
    Block<16> digest_{}, tail1_{};
};
}
