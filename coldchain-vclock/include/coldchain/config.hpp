#pragma once
/// @file config.hpp
/// @brief Central simulation configuration — every tunable knob lives here so
///        sweep_runner can vary them programmatically.

#include <cstdint>
#include <cstddef>

namespace coldchain {

/// Number of entities in the permissioned network (M, D, C, P).
inline constexpr size_t kDefaultNumNodes = 4;

/// Role indices — kept as named constants for readability.
inline constexpr uint8_t kManufacturer = 0;
inline constexpr uint8_t kDistributor  = 1;
inline constexpr uint8_t kColdStorage  = 2;
inline constexpr uint8_t kPharmacy     = 3;

/// Holds every tunable knob for a single simulation run.
struct SimConfig {
    // --- topology ---
    size_t   num_nodes             = kDefaultNumNodes;
    uint32_t num_shipments         = 5;

    // --- timing ---
    double   duration_hours        = 72.0;      ///< total simulated time
    double   sampling_interval_s   = 60.0;      ///< telemetry every N sim-seconds

    // --- blackout distribution (log-normal) ---
    double   blackout_mean_hours   = 4.0;
    double   blackout_stddev_hours = 2.0;
    double   blackout_rate_per_hour= 0.25;      ///< Poisson arrival rate

    // --- clock drift for the naive baseline ---
    double   drift_ppm_mean        = 20.0;      ///< parts-per-million
    double   drift_ppm_stddev      = 10.0;

    // --- transmission jitter (log-normal, milliseconds) ---
    double   tx_jitter_mean_ms     = 50.0;
    double   tx_jitter_stddev_ms   = 30.0;

    // --- temperature thresholds ---
    float    temp_low_c            =  2.0f;
    float    temp_high_c           =  8.0f;

    // --- reproducibility ---
    uint64_t seed                  = 42;

    // --- block sealing ---
    size_t   events_per_block      = 32;        ///< seal a block every N events
};

} // namespace coldchain
