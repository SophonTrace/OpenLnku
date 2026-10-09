// Offline tests for adapter traffic counter sampling. No network APIs are used.
#include "core/traffic.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using jlu::TrafficCounters;
using jlu::TrafficMeter;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void checkNear(double actual, double expected, std::string_view message) {
    if (std::abs(actual - expected) > 1e-9) throw std::runtime_error(std::string(message));
}

void sample(TrafficMeter& meter, std::string identity, std::uint64_t received,
            std::uint64_t sent, std::uint64_t milliseconds) {
    meter.sample(identity, TrafficCounters{received, sent}, milliseconds);
}

void initialSampleIsBaseline() {
    TrafficMeter meter;
    check(!meter.available(), "meter starts unavailable");
    sample(meter, "ethernet-A", 1000, 2000, 100);
    check(meter.available(), "valid counters make meter available");
    checkNear(meter.receivedRate(), 0, "first sample has zero receive rate");
    checkNear(meter.sentRate(), 0, "first sample has zero send rate");
    check(meter.receivedTotal() == 0 && meter.sentTotal() == 0,
          "first sample establishes a baseline without counting existing bytes");
}

void intervalRatesAndTotals() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 10000, 20000, 1000);
    sample(meter, "ethernet-A", 12000, 21000, 2000);
    checkNear(meter.receivedRate(), 2000, "receive rate is bytes per second");
    checkNear(meter.sentRate(), 1000, "send rate is bytes per second");
    check(meter.receivedTotal() == 2000 && meter.sentTotal() == 1000,
          "first interval contributes its deltas to totals");

    sample(meter, "ethernet-A", 13000, 23000, 2500);
    checkNear(meter.receivedRate(), 2000, "fractional-second receive interval is scaled");
    checkNear(meter.sentRate(), 4000, "fractional-second send interval is scaled");
    check(meter.receivedTotal() == 3000 && meter.sentTotal() == 3000,
          "totals accumulate successive deltas");
}

void adapterSwitchEstablishesNewBaseline() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 100, 200, 100);
    sample(meter, "ethernet-A", 500, 600, 1100);
    check(meter.receivedTotal() == 400 && meter.sentTotal() == 400,
          "first adapter interval is counted");

    sample(meter, "wifi-B", 50000, 90000, 2100);
    checkNear(meter.receivedRate(), 0, "adapter switch has zero receive rate");
    checkNear(meter.sentRate(), 0, "adapter switch has zero send rate");
    check(meter.receivedTotal() == 400 && meter.sentTotal() == 400,
          "adapter switch does not count the new adapter's accumulated counters");

    sample(meter, "wifi-B", 50200, 90100, 2600);
    check(meter.receivedTotal() == 600 && meter.sentTotal() == 500,
          "subsequent samples count deltas on the new adapter");
}

void missingCountersResetBaselineButKeepTotals() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 1000, 2000, 100);
    sample(meter, "ethernet-A", 1100, 2200, 200);
    check(meter.receivedTotal() == 100 && meter.sentTotal() == 200,
          "traffic before a missing sample is retained");

    meter.sample("ethernet-A", std::nullopt, 300);
    check(!meter.available(), "missing counters mark meter unavailable");
    checkNear(meter.receivedRate(), 0, "missing counters clear receive rate");
    checkNear(meter.sentRate(), 0, "missing counters clear send rate");
    check(meter.receivedTotal() == 100 && meter.sentTotal() == 200,
          "missing counters preserve accumulated totals");

    sample(meter, "ethernet-A", 5000, 6000, 400);
    check(meter.available(), "valid counters restore availability");
    check(meter.receivedTotal() == 100 && meter.sentTotal() == 200,
          "first sample after a gap only establishes a new baseline");
    sample(meter, "ethernet-A", 5500, 6200, 900);
    check(meter.receivedTotal() == 600 && meter.sentTotal() == 400,
          "traffic after the gap is counted from the restored baseline");
}

void counterRollbackStartsANewBaseline() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 1000, 2000, 100);
    sample(meter, "ethernet-A", 900, 2500, 1100);
    checkNear(meter.receivedRate(), 0, "counter rollback clears receive rate");
    checkNear(meter.sentRate(), 0, "counter rollback clears send rate");
    check(meter.receivedTotal() == 0 && meter.sentTotal() == 0,
          "a reset counter does not create a large unsigned delta or partial interval");

    sample(meter, "ethernet-A", 950, 2600, 1200);
    check(meter.receivedTotal() == 50 && meter.sentTotal() == 100,
          "sampling resumes from the post-rollback baseline");
    checkNear(meter.receivedRate(), 500, "receive rate uses post-rollback interval");
    checkNear(meter.sentRate(), 1000, "send rate uses post-rollback interval");
}

void longSleepCountsTotalsButSuppressesRate() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 1000, 2000, 1000);
    sample(meter, "ethernet-A", 8000, 6000, 7001);
    check(meter.receivedTotal() == 7000 && meter.sentTotal() == 4000,
          "long sleep interval still contributes counter deltas to totals");
    checkNear(meter.receivedRate(), 0, "long sleep receive rate is suppressed");
    checkNear(meter.sentRate(), 0, "long sleep send rate is suppressed");

    sample(meter, "ethernet-A", 8500, 7000, 7501);
    checkNear(meter.receivedRate(), 1000, "rate resumes after a fresh short interval");
    checkNear(meter.sentRate(), 2000, "send rate resumes after a fresh short interval");
    check(meter.receivedTotal() == 7500 && meter.sentTotal() == 5000,
          "post-sleep traffic is accumulated once");
}

void repeatedTimestampDoesNotCountTwice() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 1000, 2000, 1000);
    sample(meter, "ethernet-A", 1500, 2500, 1000);
    check(meter.receivedTotal() == 0 && meter.sentTotal() == 0,
          "sample at the same timestamp contributes no delta");
    checkNear(meter.receivedRate(), 0, "same timestamp has zero receive rate");
    checkNear(meter.sentRate(), 0, "same timestamp has zero send rate");

    sample(meter, "ethernet-A", 1700, 2700, 1500);
    check(meter.receivedTotal() == 700 && meter.sentTotal() == 700,
          "later interval retains all bytes since the last positive-time baseline");
    checkNear(meter.receivedRate(), 1400, "later receive rate uses positive elapsed time");
    checkNear(meter.sentRate(), 1400, "later send rate uses positive elapsed time");
}

void clockRollbackReanchorsWithoutCounting() {
    TrafficMeter meter;
    sample(meter, "ethernet-A", 1000, 2000, 3000);
    sample(meter, "ethernet-A", 2000, 3000, 2000);
    check(meter.receivedTotal() == 0 && meter.sentTotal() == 0,
          "backward timestamp does not count bytes");
    checkNear(meter.receivedRate(), 0, "backward timestamp has zero receive rate");
    checkNear(meter.sentRate(), 0, "backward timestamp has zero send rate");

    sample(meter, "ethernet-A", 2200, 3400, 2500);
    check(meter.receivedTotal() == 200 && meter.sentTotal() == 400,
          "sampling reanchors at the backward timestamp");
    checkNear(meter.receivedRate(), 400, "reanchored receive rate is valid");
    checkNear(meter.sentRate(), 800, "reanchored send rate is valid");
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

} // namespace

int main() {
    constexpr std::array tests{
        TestCase{"initial sample baseline", initialSampleIsBaseline},
        TestCase{"interval rates and totals", intervalRatesAndTotals},
        TestCase{"adapter switch baseline", adapterSwitchEstablishesNewBaseline},
        TestCase{"missing counter sample", missingCountersResetBaselineButKeepTotals},
        TestCase{"counter rollback", counterRollbackStartsANewBaseline},
        TestCase{"long sleep interval", longSleepCountsTotalsButSuppressesRate},
        TestCase{"repeated timestamp", repeatedTimestampDoesNotCountTwice},
        TestCase{"clock rollback", clockRollbackReanchorsWithoutCounting},
    };

    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << tests.size() << " offline traffic tests passed.\n";
    return 0;
}
