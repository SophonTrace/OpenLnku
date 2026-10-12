#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
// The four transfer lanes share this deadline. Only the sampling callback
// captures the baseline; a slow warm-up never consumes the measurement window.
class SpeedWindow {
public:
 using Clock=std::chrono::steady_clock; using Time=Clock::time_point;
 explicit SpeedWindow(std::int64_t durationMs,Time start=Clock::now()):start_(start),baseline_(start),duration_(durationMs),end_(start+std::chrono::seconds(8)) {}
 bool begin(Time now,bool allReady) {
  if (measuring_ || !allReady || now-start_<std::chrono::seconds(3) || now>=end_.load()) return false;
  baseline_=now; end_.store(now+std::chrono::milliseconds(duration_)); measuring_=true; return true;
 }
 std::int64_t measuredMs(Time now=Clock::now()) const { return std::chrono::duration_cast<std::chrono::milliseconds>(now-baseline_).count(); }
 const std::atomic<Time>* deadlineSignal() const { return &end_; }
 Time deadline() const { return end_.load(); }
private:
 Time start_,baseline_; std::int64_t duration_; bool measuring_=false; std::atomic<Time> end_;
};
