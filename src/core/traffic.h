#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace jlu {
struct TrafficCounters { std::uint64_t received = 0, sent = 0; };
// Measures adapter-wide traffic observed during this process, never billing usage.
class TrafficMeter {
public:
    void sample(const std::string& identity, std::optional<TrafficCounters> counters, std::uint64_t milliseconds) {
        available_ = counters.has_value(); receivedRate_ = sentRate_ = 0;
        if (!counters) { baseline_.reset(); identity_.clear(); return; }
        if (baseline_ && identity == identity_ && milliseconds == time_ &&
            counters->received >= baseline_->received && counters->sent >= baseline_->sent) return;
        if (baseline_ && identity == identity_ && milliseconds > time_ &&
            counters->received >= baseline_->received && counters->sent >= baseline_->sent) {
            const auto received = counters->received - baseline_->received;
            const auto sent = counters->sent - baseline_->sent;
            receivedTotal_ += received; sentTotal_ += sent;
            // Do not present an average across sleep as a current transfer rate.
            const auto elapsed = milliseconds - time_;
            if (elapsed <= 5000) {
                receivedRate_ = static_cast<double>(received) * 1000.0 / static_cast<double>(elapsed);
                sentRate_ = static_cast<double>(sent) * 1000.0 / static_cast<double>(elapsed);
            }
        }
        baseline_ = counters; identity_ = identity; time_ = milliseconds;
    }
    bool available() const { return available_; }
    double receivedRate() const { return receivedRate_; }
    double sentRate() const { return sentRate_; }
    std::uint64_t receivedTotal() const { return receivedTotal_; }
    std::uint64_t sentTotal() const { return sentTotal_; }
private:
    std::optional<TrafficCounters> baseline_;
    std::string identity_;
    std::uint64_t time_ = 0, receivedTotal_ = 0, sentTotal_ = 0;
    bool available_ = false;
    double receivedRate_ = 0, sentRate_ = 0;
};
}
