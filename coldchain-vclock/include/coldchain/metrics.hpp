#pragma once
/// @file metrics.hpp
/// @brief Metrics pipeline for comparing vector-clock vs. naive-baseline pipelines.
///
/// Provides:
///   - R_err  (causal misordering rate)
///   - F_liab (false liability attribution rate)
///   - S_prune (storage reduction ratio from matrix-clock pruning)
///
/// All metrics are designed so the simulator's ground-truth is the reference.

#include <cstddef>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/config.hpp"

namespace coldchain {

// Forward-declare
class LedgerNode;
class NetworkSimulator;

// ═══════════════════════════════════════════════════════════════
//  RunMetrics — single-run summary
// ═══════════════════════════════════════════════════════════════

struct RunMetrics {
    double causal_misordering_rate = 0.0;   ///< R_err: fraction of inverted pairs
    double false_liability_rate    = 0.0;   ///< F_liab: fraction of misattributed excursions
    double storage_reduction_ratio = 0.0;   ///< S_prune: 1 - (pruned_bytes / unbounded_bytes)
    double avg_blackout_hours      = 0.0;   ///< independent variable
    size_t naive_inversions        = 0;     ///< raw count of causal inversions (naive)
    size_t vclock_inversions       = 0;     ///< raw count (vclock — should be 0)
    size_t total_events            = 0;     ///< events generated
    size_t total_excursions        = 0;     ///< temperature excursions observed
    size_t false_liability_count   = 0;     ///< excursions misattributed by naive pipeline
    size_t unpruned_store_size     = 0;     ///< records without pruning
    size_t pruned_store_size       = 0;     ///< records after pruning
};

// ═══════════════════════════════════════════════════════════════
//  Temperature excursion ground truth (for F_liab)
// ═══════════════════════════════════════════════════════════════

struct ExcursionRecord {
    uint32_t shipment_id;
    uint8_t  true_custodian;          ///< entity that actually had custody
    float    temperature_celsius;     ///< the out-of-range reading
    VectorClock vc;                   ///< event's causal timestamp
    uint64_t physical_timestamp_ns;   ///< drifted physical timestamp
};

// ═══════════════════════════════════════════════════════════════
//  Metric computation functions
// ═══════════════════════════════════════════════════════════════

/// Compute R_err: fraction of pairs (i,j) with i<j where event_vc(j) strictly
/// precedes event_vc(i) — a causal inversion.
///
/// Returns inverted_pairs / total_comparable_pairs, or 0 if no comparable pairs.
double compute_causal_misordering_rate(
    const std::vector<LedgerEvent>& ordered_events,
    size_t& out_inversion_count);

/// Compute F_liab: fraction of temperature excursions where the naive pipeline
/// attributes custody to the wrong entity.
///
/// @param excursions  List of excursion records with true custodian known.
/// @param naive_events  Events as ordered by the naive physical-timestamp pipeline.
/// @param cfg  Config for temperature thresholds.
/// @return (misattributed / total_excursions), or 0 if no excursions.
double compute_false_liability_rate(
    const std::vector<ExcursionRecord>& excursions,
    const std::vector<LedgerEvent>& naive_events,
    const SimConfig& cfg);

/// Compute S_prune: 1 - (pruned_size / unbounded_size).
/// Both sizes measured in number of active state store records.
double compute_storage_reduction(size_t pruned_size, size_t unbounded_size);

/// Extract temperature excursions from the ground-truth event log,
/// using the simulation config's temperature thresholds.
std::vector<ExcursionRecord> extract_excursions(
    const std::vector<LedgerEvent>& ground_truth,
    const SimConfig& cfg);

/// Run a complete single simulation and compute all metrics.
/// This is the main entry point for sweep_runner.
RunMetrics run_single_experiment(const SimConfig& cfg);

/// Format RunMetrics as a single CSV row (no header).
std::string metrics_to_csv_row(const RunMetrics& m, uint64_t run_id);

/// CSV header line for metrics output.
std::string metrics_csv_header();

} // namespace coldchain
