/// @file metrics.cpp
/// @brief Metrics pipeline implementation — R_err, F_liab, S_prune computation.

#include "coldchain/metrics.hpp"
#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/pruning_engine.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace coldchain {

// ══════════════════════════════════════════════════════════════
//  R_err — causal misordering rate
// ══════════════════════════════════════════════════════════════

double compute_causal_misordering_rate(
    const std::vector<LedgerEvent>& ordered_events,
    size_t& out_inversion_count) {

    size_t inversions = 0;
    size_t comparable_pairs = 0;
    const size_t n = ordered_events.size();

    std::vector<const VectorClock*> vcs(n);
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(ordered_events[i]);
        sums[i] = vcs[i]->sum();
    }

    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = i + 1; j < n; ++j) {
            uint32_t sum_j = sums[j];

            if (sum_j < sum_i) {
                // Potential causal inversion
                if (vcs[j]->precedes(vc_i)) {
                    ++inversions;
                    ++comparable_pairs;
                }
            } else if (sum_i < sum_j) {
                // Correct causal ordering
                if (vc_i.precedes(*vcs[j])) {
                    ++comparable_pairs;
                }
            }
        }
    }

    out_inversion_count = inversions;
    return (comparable_pairs > 0)
        ? static_cast<double>(inversions) / static_cast<double>(comparable_pairs)
        : 0.0;
}

// ══════════════════════════════════════════════════════════════
//  F_liab — false liability attribution
// ══════════════════════════════════════════════════════════════

double compute_false_liability_rate(
    const std::vector<ExcursionRecord>& excursions,
    const std::vector<LedgerEvent>& naive_events,
    const SimConfig& /*cfg*/) {

    if (excursions.empty()) return 0.0;

    size_t misattributed = 0;

    for (const auto& exc : excursions) {
        // In the naive pipeline's ordering, find the closest SensorEvent
        // matching this excursion's shipment and temperature.
        // The custodian assigned by the naive pipeline is the origin of
        // the nearest SensorEvent in the naive ordering.
        uint8_t naive_custodian = exc.true_custodian;  // default: correct
        bool found = false;

        for (const auto& ev : naive_events) {
            const auto* se = std::get_if<SensorEvent>(&ev);
            if (!se) continue;
            if (se->shipment_id != exc.shipment_id) continue;

            // Match by physical timestamp proximity
            int64_t diff = static_cast<int64_t>(se->physical_timestamp_ns)
                         - static_cast<int64_t>(exc.physical_timestamp_ns);
            if (std::abs(diff) < 500'000'000LL) {  // within 500ms window
                // Check if temperature matches (same excursion event)
                if (std::abs(se->temperature_celsius - exc.temperature_celsius) < 0.01f) {
                    naive_custodian = se->origin_entity_id;
                    found = true;
                    break;
                }
            }
        }

        // If the naive pipeline attributes custody to a different entity
        // than the true custodian, it's a false liability.
        if (found && naive_custodian != exc.true_custodian) {
            ++misattributed;
        }
    }

    return static_cast<double>(misattributed)
         / static_cast<double>(excursions.size());
}

// ══════════════════════════════════════════════════════════════
//  S_prune — storage reduction ratio
// ══════════════════════════════════════════════════════════════

double compute_storage_reduction(size_t pruned_size, size_t unbounded_size) {
    if (unbounded_size == 0) return 0.0;
    return 1.0 - static_cast<double>(pruned_size)
               / static_cast<double>(unbounded_size);
}

// ══════════════════════════════════════════════════════════════
//  Excursion extraction from ground-truth log
// ══════════════════════════════════════════════════════════════

std::vector<ExcursionRecord> extract_excursions(
    const std::vector<LedgerEvent>& ground_truth,
    const SimConfig& cfg) {

    std::vector<ExcursionRecord> excursions;

    for (const auto& ev : ground_truth) {
        const auto* se = std::get_if<SensorEvent>(&ev);
        if (!se) continue;

        // Check if temperature is outside safe range [temp_low_c, temp_high_c]
        if (se->temperature_celsius < cfg.temp_low_c ||
            se->temperature_celsius > cfg.temp_high_c) {

            ExcursionRecord rec;
            rec.shipment_id          = se->shipment_id;
            rec.true_custodian       = se->origin_entity_id;
            rec.temperature_celsius  = se->temperature_celsius;
            rec.vc                   = se->vc;
            rec.physical_timestamp_ns = se->physical_timestamp_ns;
            excursions.push_back(rec);
        }
    }

    return excursions;
}

// ══════════════════════════════════════════════════════════════
//  Single experiment runner
// ══════════════════════════════════════════════════════════════

RunMetrics run_single_experiment(const SimConfig& cfg) {
    RunMetrics metrics;
    metrics.avg_blackout_hours = cfg.blackout_mean_hours;

    // 1. Run the simulation
    NetworkSimulator sim(cfg);

    // Add shipments with default M→D→C→P route
    for (uint32_t s = 0; s < cfg.num_shipments; ++s) {
        std::vector<uint8_t> route;
        for (size_t i = 0; i < cfg.num_nodes; ++i)
            route.push_back(static_cast<uint8_t>(i));
        sim.add_shipment(s + 1, route);
    }

    sim.run_full();
    metrics.total_events = sim.total_events_generated();

    // 2. Collect all events from both pipelines
    std::vector<LedgerEvent> vclock_all, naive_all;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& ve = sim.vclock_node(i).all_events();
        vclock_all.insert(vclock_all.end(), ve.begin(), ve.end());
        const auto& ne = sim.naive_node(i).all_events();
        naive_all.insert(naive_all.end(), ne.begin(), ne.end());
    }

    // 3. Sort both pipelines
    auto vclock_sorted = CausalMergeEngine::topo_sort(vclock_all);
    auto naive_sorted  = naive_physical_order(naive_all);

    // 4. R_err for both pipelines
    size_t vc_inv = 0, naive_inv = 0;
    compute_causal_misordering_rate(vclock_sorted, vc_inv);
    metrics.causal_misordering_rate = compute_causal_misordering_rate(naive_sorted, naive_inv);
    metrics.vclock_inversions = vc_inv;
    metrics.naive_inversions  = naive_inv;

    // 5. F_liab
    auto excursions = extract_excursions(sim.ground_truth_events(), cfg);
    metrics.total_excursions   = excursions.size();
    metrics.false_liability_rate = compute_false_liability_rate(
        excursions, naive_sorted, cfg);
    metrics.false_liability_count = static_cast<size_t>(
        std::round(metrics.false_liability_rate * excursions.size()));

    // 6. S_prune — compute store sizes before and after pruning
    if (cfg.gossip_rounds > 0) {
        sim.gossip_rounds(cfg.gossip_rounds);
    }

    //    Sum active_state_store sizes across all vclock nodes BEFORE pruning
    //    (this is the "unbounded" count), then prune and measure the result.
    size_t unbounded_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        unbounded_total += sim.vclock_node(i).active_state_store().size();
    }

    // Prune all nodes
    size_t pruned_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        PruningEngine::prune_stable_records(sim.vclock_node(i));
        pruned_total += sim.vclock_node(i).active_state_store().size();
    }

    metrics.unpruned_store_size = unbounded_total;
    metrics.pruned_store_size   = pruned_total;
    metrics.storage_reduction_ratio = compute_storage_reduction(pruned_total, unbounded_total);

    return metrics;
}

// ══════════════════════════════════════════════════════════════
//  CSV formatting
// ══════════════════════════════════════════════════════════════

std::string metrics_csv_header() {
    return "blackout_hours,run_id,r_err,f_liab,s_prune,"
           "naive_inversions,vclock_inversions,total_events,"
           "total_excursions,false_liability_count,"
           "unpruned_store,pruned_store";
}

std::string metrics_to_csv_row(const RunMetrics& m, uint64_t run_id) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4)
        << m.avg_blackout_hours << ","
        << run_id << ","
        << m.causal_misordering_rate << ","
        << m.false_liability_rate << ","
        << m.storage_reduction_ratio << ","
        << m.naive_inversions << ","
        << m.vclock_inversions << ","
        << m.total_events << ","
        << m.total_excursions << ","
        << m.false_liability_count << ","
        << m.unpruned_store_size << ","
        << m.pruned_store_size;
    return oss.str();
}

} // namespace coldchain
