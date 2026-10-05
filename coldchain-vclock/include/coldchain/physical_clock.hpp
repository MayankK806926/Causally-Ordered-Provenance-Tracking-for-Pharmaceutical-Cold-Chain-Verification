#pragma once
/// @file physical_clock.hpp
/// @brief Naive physical-clock baseline with hardware oscillator drift.
///
/// Used ONLY to demonstrate causal violations (R_err) and false liability
/// attributions (F_liab) when a naive physical-timestamp ordering is used
/// instead of logical clocks.

#include <cstdint>
#include <vector>

#include "coldchain/events.hpp"

namespace coldchain {

/// @brief Simulates an edge hardware clock with oscillator drift.
///
/// Hardware drift rate is sampled once per device (PPM — parts per million).
/// Drift can be positive (clock runs fast) or negative (clock runs slow).
class DriftingPhysicalClock {
public:
    /// @param drift_ppm_mean Mean clock drift in parts per million (PPM).
    /// @param drift_ppm_stddev Standard deviation of clock drift in PPM.
    /// @param seed Random seed for sampling device drift.
    DriftingPhysicalClock(double drift_ppm_mean = 20.0,
                          double drift_ppm_stddev = 10.0,
                          uint64_t seed = 42);

    /// Convert true simulation time (in nanoseconds) to drifted local physical time.
    uint64_t now_ns(uint64_t sim_time_ns) const;

    /// Return the sampled drift rate in PPM.
    double drift_rate_ppm() const;

private:
    double drift_rate_ppm_;
};

/// @brief Order events strictly by physical timestamp / local counter,
///        ignoring causal vector clocks.
///
/// Re-orders incoming events purely by event_physical_time(ev).
/// Out-of-order delivery and clock drift will produce causal inversions.
std::vector<LedgerEvent> naive_physical_order(std::vector<LedgerEvent> incoming);

/// @brief Count causal inversions in an event sequence.
///
/// A pair (i, j) with i < j is an inversion if event j strictly preceded
/// event i in the ground-truth causal order (i.e. event_vc(j).precedes(event_vc(i))).
size_t count_causal_inversions(const std::vector<LedgerEvent>& events);

/// @brief Check whether any causal inversions exist in an event sequence.
bool has_causal_inversion(const std::vector<LedgerEvent>& events);

} // namespace coldchain
