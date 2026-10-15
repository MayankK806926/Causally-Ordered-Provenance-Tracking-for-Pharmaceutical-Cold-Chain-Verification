/// @file main_sim.cpp
/// @brief CLI simulation runner — Week 5: full pipeline with pruning, metrics,
///        and dual-pipeline comparison (vector clock vs naive baseline).

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/metrics.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/pruning_engine.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

void print_banner() {
    std::cout << "========================================================================\n"
              << " Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks\n"
              << " CS6666 Distributed Ledger Technologies — Simulation Harness (Week 5)\n"
              << "========================================================================\n\n";
}

int main(int argc, char* argv[]) {
    SimConfig cfg;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shipments" && i + 1 < argc) {
            cfg.num_shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-hours" && i + 1 < argc) {
            cfg.duration_hours = std::stod(argv[++i]);
        } else if (arg == "--blackout-mean-hours" && i + 1 < argc) {
            cfg.blackout_mean_hours = std::stod(argv[++i]);
        } else if (arg == "--sampling-interval-s" && i + 1 < argc) {
            cfg.sampling_interval_s = std::stod(argv[++i]);
        } else if (arg == "--gossip-rounds" && i + 1 < argc) {
            cfg.gossip_rounds = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./sim [options]\n"
                      << "Options:\n"
                      << "  --shipments N              Number of shipments (default: 5)\n"
                      << "  --duration-hours H         Simulation duration in hours (default: 72)\n"
                      << "  --blackout-mean-hours H    Mean blackout duration (default: 4)\n"
                      << "  --sampling-interval-s S    Telemetry interval in seconds (default: 60)\n"
                      << "  --gossip-rounds N          Matrix clock gossip rounds (default: 3)\n"
                      << "  --seed S                   Random seed (default: 42)\n"
                      << "  --verbose, -v              Print detailed logs\n"
                      << "  --help, -h                 Show this message\n";
            return 0;
        }
    }

    print_banner();
    std::cout << "[Config]\n"
              << "  Shipments:          " << cfg.num_shipments << "\n"
              << "  Duration:           " << cfg.duration_hours << " hours\n"
              << "  Sampling interval:  " << cfg.sampling_interval_s << " seconds\n"
              << "  Gossip rounds:      " << cfg.gossip_rounds << "\n"
              << "  Blackout mean:      " << cfg.blackout_mean_hours << " hours\n"
              << "  Blackout rate:      " << cfg.blackout_rate_per_hour << " /hour/node\n"
              << "  Clock drift:        ±" << cfg.drift_ppm_mean << " PPM\n"
              << "  TX jitter mean:     " << cfg.tx_jitter_mean_ms << " ms\n"
              << "  Seed:               " << cfg.seed << "\n"
              << "  Entities:           " << cfg.num_nodes
              << " (Manufacturer, Distributor, Cold Storage, Pharmacy)\n\n";

    // ── Run discrete-event simulation ────────────────────────────
    std::cout << "[Simulation] Running discrete-event network simulation...\n";
    NetworkSimulator sim(cfg);
    sim.run_full();

    std::cout << "  Generated:   " << sim.total_events_generated() << " events\n"
              << "  Blackouts:   " << sim.total_blackouts_injected() << " injected\n"
              << "  Sim clock:   " << std::fixed << std::setprecision(1)
              << sim.sim_clock_ms() / (3600.0 * 1000.0) << " hours\n\n";

    // ── Pipeline 1: Vector Clock — collect all events from all nodes ─────
    std::cout << "--- Pipeline 1: Vector Clock + Causal Merge Engine ---\n";

    std::vector<LedgerEvent> vclock_all_events;
    size_t vclock_total_blocks = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& events = sim.vclock_node(i).all_events();
        vclock_all_events.insert(vclock_all_events.end(), events.begin(), events.end());
        vclock_total_blocks += sim.vclock_node(i).chain().size();
    }

    // Topo-sort all collected events
    auto vclock_sorted = CausalMergeEngine::topo_sort(vclock_all_events);
    size_t vclock_inversions = count_causal_inversions(vclock_sorted);
    bool vclock_causal_ok = CausalMergeEngine::is_causally_valid(vclock_sorted);

    std::cout << "  Events received: " << vclock_all_events.size() << "\n"
              << "  Blocks sealed:   " << vclock_total_blocks << "\n"
              << "  Causal inversions (R_err): " << vclock_inversions << " (must be 0)\n"
              << "  Causal invariant:          " << (vclock_causal_ok ? "PASS" : "FAIL") << "\n";

    // Verify all chains
    bool vclock_chains_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.vclock_node(i).chain())) {
            vclock_chains_valid = false;
            break;
        }
    }
    std::cout << "  Chain integrity:           " << (vclock_chains_valid ? "VALID" : "CORRUPT") << "\n";

    // ── Pruning (Week 4) ──────────────────────────────────────────
    std::cout << "\n--- Matrix-Clock Pruning (Week 4) ---\n";

    if (cfg.gossip_rounds > 0) {
        std::cout << "  Propagating matrix clocks via " << cfg.gossip_rounds
                  << " gossip round(s)...\n";
        sim.gossip_rounds(cfg.gossip_rounds);
    } else {
        std::cout << "  (0 gossip rounds configured — matrix clocks will not advance)\n";
    }

    size_t store_before = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i)
        store_before += sim.vclock_node(i).active_state_store().size();

    size_t total_pruned = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        size_t pruned = PruningEngine::prune_stable_records(sim.vclock_node(i));
        total_pruned += pruned;
        if (verbose) {
            VectorClock horizon = PruningEngine::pruning_horizon(sim.vclock_node(i));
            std::cout << "  Node " << i << ": pruned " << pruned
                      << " records, horizon = " << horizon.to_string() << "\n";
        }
    }

    size_t store_after = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i)
        store_after += sim.vclock_node(i).active_state_store().size();

    double s_prune = compute_storage_reduction(store_after, store_before);
    std::cout << "  Store before pruning: " << store_before << " records\n"
              << "  Store after pruning:  " << store_after << " records\n"
              << "  Records pruned:       " << total_pruned << "\n"
              << "  S_prune:              " << std::fixed << std::setprecision(2)
              << (s_prune * 100.0) << "%\n";

    // Verify chain integrity after pruning (pruning must NOT corrupt chain)
    bool post_prune_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.vclock_node(i).chain())) {
            post_prune_valid = false;
            break;
        }
    }
    std::cout << "  Chain integrity post-prune: "
              << (post_prune_valid ? "VALID" : "CORRUPT") << "\n";

    // Matrix clock diagnostics
    if (verbose) {
        std::cout << "\n  Matrix clock states:\n";
        for (size_t i = 0; i < sim.num_nodes(); ++i) {
            const auto& mc = sim.vclock_node(i).matrix_clock();
            std::cout << "    Node " << i << ":\n";
            for (size_t j = 0; j < mc.n(); ++j) {
                std::cout << "      [";
                for (size_t k = 0; k < mc.n(); ++k) {
                    if (k) std::cout << ", ";
                    std::cout << mc.at(j, k);
                }
                std::cout << "]\n";
            }
            VectorClock cmin = mc.column_min();
            std::cout << "      column_min = " << cmin.to_string() << "\n";
        }
    }
    std::cout << "\n";

    // ── Pipeline 2: Naive Physical-Timestamp Baseline ────────────
    std::cout << "--- Pipeline 2: Naive Physical Timestamp Baseline ---\n";

    std::vector<LedgerEvent> naive_all_events;
    size_t naive_total_blocks = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& events = sim.naive_node(i).all_events();
        naive_all_events.insert(naive_all_events.end(), events.begin(), events.end());
        naive_total_blocks += sim.naive_node(i).chain().size();
    }

    auto naive_sorted = naive_physical_order(naive_all_events);
    size_t naive_inversions = count_causal_inversions(naive_sorted);
    bool naive_causal_ok = CausalMergeEngine::is_causally_valid(naive_sorted);

    std::cout << "  Events received: " << naive_all_events.size() << "\n"
              << "  Blocks sealed:   " << naive_total_blocks << "\n"
              << "  Causal inversions (R_err): " << naive_inversions << " (violations due to drift!)\n"
              << "  Causal invariant:          " << (naive_causal_ok ? "PASS" : "VIOLATION DETECTED") << "\n";

    bool naive_chains_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.naive_node(i).chain())) {
            naive_chains_valid = false;
            break;
        }
    }
    std::cout << "  Chain integrity:           " << (naive_chains_valid ? "VALID" : "CORRUPT") << "\n\n";

    // ── Metrics (Week 5) ─────────────────────────────────────────
    std::cout << "--- Computed Metrics (Week 5) ---\n";

    size_t vc_inv_count = 0, naive_inv_count = 0;
    double vc_rerr    = compute_causal_misordering_rate(vclock_sorted, vc_inv_count);
    double naive_rerr = compute_causal_misordering_rate(naive_sorted, naive_inv_count);

    auto excursions = extract_excursions(sim.ground_truth_events(), cfg);
    double f_liab = compute_false_liability_rate(excursions, naive_sorted, cfg);

    std::cout << "  R_err (vector clock): " << std::setprecision(6) << vc_rerr
              << " (" << vc_inv_count << " inversions)\n"
              << "  R_err (naive):        " << naive_rerr
              << " (" << naive_inv_count << " inversions)\n"
              << "  F_liab (naive):       " << f_liab
              << " (" << excursions.size() << " excursions total)\n"
              << "  S_prune:              " << std::setprecision(4) << s_prune << "\n\n";

    // ── Summary Comparison ──────────────────────────────────────
    std::cout << "========================================================================\n"
              << " COMPARISON RESULTS\n"
              << "========================================================================\n"
              << " Metric                       | Vector Clock Pipeline | Naive Baseline\n"
              << "------------------------------+-----------------------+-----------------\n"
              << " Causal Inversions (R_err)    | " << std::setw(21) << vclock_inversions << " | "
              << std::setw(15) << naive_inversions << "\n"
              << " R_err Rate                   | " << std::setw(21) << std::setprecision(6) << vc_rerr << " | "
              << std::setw(15) << naive_rerr << "\n"
              << " Causal Invariant Preserved?  | " << std::setw(21) << (vclock_causal_ok ? "YES (Guaranteed)" : "NO") << " | "
              << std::setw(15) << (naive_causal_ok ? "YES" : "NO (Violated)") << "\n"
              << " Chain Integrity Verified?    | " << std::setw(21) << (vclock_chains_valid ? "YES" : "NO") << " | "
              << std::setw(15) << (naive_chains_valid ? "YES" : "NO") << "\n"
              << " Total Events Generated       | " << std::setw(21) << vclock_all_events.size() << " | "
              << std::setw(15) << naive_all_events.size() << "\n"
              << " Blocks Sealed                | " << std::setw(21) << vclock_total_blocks << " | "
              << std::setw(15) << naive_total_blocks << "\n"
              << " F_liab                       | " << std::setw(21) << "N/A (zero by def)" << " | "
              << std::setw(15) << std::setprecision(4) << f_liab << "\n"
              << " S_prune                      | " << std::setw(21) << std::setprecision(2) << (s_prune * 100) << "%" << " | "
              << std::setw(15) << "N/A" << "\n"
              << " Store (before → after prune) | " << std::setw(11) << store_before
              << " → " << std::setw(5) << store_after << " | "
              << std::setw(15) << "N/A" << "\n"
              << "========================================================================\n";

    if (vclock_inversions == 0 && vclock_causal_ok) {
        std::cout << "\n[SUCCESS] Vector clock pipeline guarantees zero causal inversions.\n";
        if (naive_inversions > 0) {
            std::cout << "          Naive baseline produced " << naive_inversions
                      << " causal violation(s) — project thesis validated.\n";
        }
        if (s_prune > 0) {
            std::cout << "          Matrix-clock pruning achieved " << std::setprecision(1)
                      << (s_prune * 100.0) << "% storage reduction.\n";
        }
    }

    return 0;
}
