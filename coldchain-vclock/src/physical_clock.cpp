/// @file physical_clock.cpp
/// @brief Implementation of DriftingPhysicalClock and naive ordering baseline.

#include "coldchain/physical_clock.hpp"

#include <algorithm>
#include <random>

namespace coldchain {

DriftingPhysicalClock::DriftingPhysicalClock(double drift_ppm_mean,
                                             double drift_ppm_stddev,
                                             uint64_t seed) {
    if (drift_ppm_stddev <= 0.0) {
        drift_rate_ppm_ = drift_ppm_mean;
    } else {
        std::mt19937_64 rng(seed);
        std::normal_distribution<double> dist(drift_ppm_mean, drift_ppm_stddev);
        drift_rate_ppm_ = dist(rng);
    }
}

uint64_t DriftingPhysicalClock::now_ns(uint64_t sim_time_ns) const {
    double drift_ratio = drift_rate_ppm_ * 1e-6;
    double delta = static_cast<double>(sim_time_ns) * drift_ratio;
    double drifted = static_cast<double>(sim_time_ns) + delta;
    return drifted < 0.0 ? 0ULL : static_cast<uint64_t>(drifted);
}

double DriftingPhysicalClock::drift_rate_ppm() const {
    return drift_rate_ppm_;
}

std::vector<LedgerEvent> naive_physical_order(std::vector<LedgerEvent> incoming) {
    std::stable_sort(incoming.begin(), incoming.end(), [](const LedgerEvent& a, const LedgerEvent& b) {
        uint64_t ta = event_physical_time(a);
        uint64_t tb = event_physical_time(b);
        if (ta != tb) return ta < tb;
        return event_origin(a) < event_origin(b);
    });
    return incoming;
}

size_t count_causal_inversions(const std::vector<LedgerEvent>& events) {
    size_t inversions = 0;
    const size_t n = events.size();
    std::vector<const VectorClock*> vcs(n);
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(events[i]);
        sums[i] = vcs[i]->sum();
    }
    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = i + 1; j < n; ++j) {
            if (sums[j] < sum_i && vcs[j]->precedes(vc_i)) {
                ++inversions;
            }
        }
    }
    return inversions;
}

bool has_causal_inversion(const std::vector<LedgerEvent>& events) {
    const size_t n = events.size();
    std::vector<const VectorClock*> vcs(n);
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(events[i]);
        sums[i] = vcs[i]->sum();
    }
    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = i + 1; j < n; ++j) {
            if (sums[j] < sum_i && vcs[j]->precedes(vc_i)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace coldchain
